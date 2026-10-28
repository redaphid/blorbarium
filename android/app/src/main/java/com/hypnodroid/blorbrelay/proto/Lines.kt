// The badge's line protocol (docs/ble.md): framing, reply parsing, and one
// request at a time over any line transport. A port of web/public/index.html's
// lineReader and Link.
package com.hypnodroid.blorbrelay.proto

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import java.io.ByteArrayOutputStream

/** `key=value` words of a reply or event line, as ota.js's kv() reads them. */
fun kv(text: String?): Map<String, String> {
    val out = LinkedHashMap<String, String>()
    if (text == null) return out
    for (m in KV.findAll(text)) out[m.groupValues[1]] = m.groupValues[2]
    return out
}

private val KV = Regex("([A-Za-z_]+)=(\\S*)")

/**
 * The board sends each line as MTU-3 byte notifications and then "\n" on its
 * own. Buffer bytes until "\n"; never treat one notification as a line. A "\r"
 * is ignored and an empty line is skipped. Bytes, not text, are buffered, so a
 * character split across two notifications survives.
 */
class LineReader(private val maxLine: Int = 64 * 1024) {
    private val buf = ByteArrayOutputStream()

    @Synchronized
    fun feed(bytes: ByteArray): List<String> {
        val out = ArrayList<String>()
        for (b in bytes) {
            if (b == NL) {
                val line = String(buf.toByteArray(), Charsets.UTF_8).replace("\r", "")
                buf.reset()
                if (line.isNotEmpty()) out += line
            } else if (buf.size() < maxLine) {
                buf.write(b.toInt())
            }
        }
        return out
    }

    @Synchronized
    fun reset() = buf.reset()

    private companion object {
        const val NL = '\n'.code.toByte()
    }
}

/** One parsed incoming line. */
sealed interface Incoming {
    /** `! <text>`: an unsolicited event (`! OTA at=4096`, `! STATE ...`). */
    data class Event(val text: String) : Incoming

    /** `#<id> + body`, `#<id> OK body` or `#<id> ERR body`. */
    data class Reply(val id: Int, val kind: String, val body: String) : Incoming
}

private val REPLY = Regex("^#([0-9a-fA-F]+) (\\+|OK|ERR)(?: (.*))?$")

fun parseLine(line: String): Incoming? {
    if (line.startsWith("!")) return Incoming.Event(line.substring(1).trim())
    val m = REPLY.matchEntire(line) ?: return null
    val id = m.groupValues[1].toIntOrNull(16) ?: return null
    return Incoming.Reply(id, m.groupValues[2], m.groupValues[3])
}

/** `#<id> ERR <code> <TEXT>`: the badge said no. [message] is "<code> <TEXT>", as ota.js's e.message. */
class BadgeError(val code: Int, message: String) : Exception(message) {
    /** The text after the code ("BUSY" of "409 BUSY"): ota.js's `e.message.replace(/^\d+ /, "")`. */
    val reason: String get() = message!!.replace(Regex("^\\d+ "), "")

    companion object {
        fun parse(body: String): BadgeError = BadgeError(body.takeWhile { it.isDigit() }.toIntOrNull() ?: 0, body)
    }
}

/** The link failed, not the badge: a timeout, a drop, a write the radio refused. */
class LinkError(message: String) : Exception(message)

/** What `#<id> OK ...` closed: its continuation lines and the OK line's body. */
data class Reply(val ok: String, val lines: List<String>)

/**
 * One request/response channel over a line transport. Requests go one at a
 * time: the board answers each line before reading the next. "#0 ERR" answers
 * a line whose id the board couldn't parse, so it goes to the one in flight.
 */
class Link(
    private val writeLine: suspend (String) -> Unit,
    private val onEvent: (String) -> Unit = {},
    private val log: (dir: String, text: String) -> Unit = { _, _ -> },
) {
    private class Pending(val id: Int) {
        val lines = ArrayList<String>()
        val done = CompletableDeferred<Reply>()
    }

    private val mutex = Mutex()
    private var nextId = 1

    @Volatile
    private var pending: Pending? = null

    /** Feed every whole line the transport hears. Safe from any thread. */
    fun receive(line: String) {
        log("in", line)
        when (val p = parseLine(line)) {
            null -> return
            is Incoming.Event -> onEvent(p.text)
            is Incoming.Reply -> {
                val inFlight = pending ?: return
                val id = if (p.id == 0 && p.kind == "ERR") inFlight.id else p.id
                if (id != inFlight.id) return
                when (p.kind) {
                    "+" -> synchronized(inFlight) { inFlight.lines += p.body }
                    "OK" -> inFlight.done.complete(Reply(p.body, synchronized(inFlight) { inFlight.lines.toList() }))
                    else -> inFlight.done.completeExceptionally(BadgeError.parse(p.body))
                }
            }
        }
    }

    suspend fun send(cmd: String, timeoutMs: Long = 8000): Reply = mutex.withLock {
        val id = nextId
        val line = "#${id.toString(16)} $cmd"
        if (line.length > MAX_LINE) throw LinkError("TOO_LONG")
        nextId = if (nextId >= 0xffffff) 1 else nextId + 1
        val p = Pending(id)
        pending = p
        try {
            log("out", line)
            writeLine(line + "\n")
            withTimeoutOrNull(timeoutMs) { p.done.await() } ?: run {
                log("err", "timeout after $timeoutMs ms: $line")
                throw LinkError("TIMEOUT")
            }
        } finally {
            if (pending === p) pending = null
        }
    }

    /** The link is gone: whatever waits for a reply fails now. */
    fun failAll(reason: String) {
        pending?.done?.completeExceptionally(LinkError(reason))
    }

    companion object {
        /** docs/ble.md: a request line is at most 200 bytes. */
        const val MAX_LINE = 200
    }
}
