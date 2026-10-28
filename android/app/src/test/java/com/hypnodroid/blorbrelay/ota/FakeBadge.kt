// The badge's side of docs/ota.md, faithful enough to test the updater against:
// a port of web/public/ota-mock.js's MockDish, with its faults (drop, gaps,
// lostAck, corrupt, rollback). Timers run on the test's virtual clock.
package com.hypnodroid.blorbrelay.ota

import com.hypnodroid.blorbrelay.proto.BadgeError
import com.hypnodroid.blorbrelay.proto.LinkError
import com.hypnodroid.blorbrelay.proto.Reply
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.yield
import java.nio.ByteBuffer
import java.nio.ByteOrder

const val SLOT = 0x1e0000
const val CHUNK = 240
const val AT_EVERY = 4096

/** A deterministic stand-in image with a real SHA-256 (ota-mock.js mockRelease). The fake never checks the signature. */
fun mockRelease(version: String = "0.4.0", size: Int = 256 * 1024, board: String = "lcd128"): Release {
    val image = ByteArray(size)
    var x = 0x9e3779b9.toInt()
    for (i in 0 until size) {
        x = x xor (x shl 13); x = x xor (x ushr 17); x = x xor (x shl 5); image[i] = (x and 255).toByte()
    }
    val m = Manifest(board, version, 0, 1, size, sha256B64(image), "A".repeat(86), "mock.bin")
    return Release(m) { image.copyOf() }
}

class FakeBadge(
    private val scope: CoroutineScope,
    private val now: () -> Long,
    var fw: String = "0.3.0",
    val board: String = "lcd128",
    val confirmMs: Long = 30000,
    val bootMs: Long = 200,
    val trialMs: Long = 300,
    val faults: Set<String> = emptySet(),
) {
    class Session(val id: String, val version: String, val token: String, val size: Int, val sha: String) {
        var at = 0
        val buf = ByteArray(size)
    }

    class Stats {
        var drops = 0; var lostWrites = 0; var gaps = 0; var lostAcks = 0; var polls = 0; var reboots = 0
        var confirms = 0; var refusedConfirms = 0; var ignored = 0; var flashed: String? = null
    }

    /** Who hears this badge's events and drops: the updater running, as the app's connection forwards them. */
    var listener: Updater? = null
    val events = ArrayList<String>()
    var state = "idle"
    var last = "none"
    var rolledback = "none"
    var trial = false
    var trialToken: String? = null
    var confirmed = false
    var s: Session? = null
    var connected = true
    var booting: Job? = null
    val stats = Stats()
    private val timers = HashMap<String, Job>()
    private var lastGap = Long.MIN_VALUE / 2
    private var dropAt = if ("drop" in faults) 0.4 else Double.POSITIVE_INFINITY
    private val loseWritesAt = if ("gaps" in faults) mutableListOf(0.15, 0.55, 0.85) else mutableListOf()
    private var loseAckAt = if ("lostAck" in faults) 0.7 else Double.POSITIVE_INFINITY
    private var corruptAt = if ("corrupt" in faults) 0.5 else Double.POSITIVE_INFINITY

    private fun say(text: String) {
        if (!connected) return
        events += "! $text"
        scope.launch { delay(5); listener?.event(text) }
    }

    private fun timer(name: String, ms: Long? = null, fn: () -> Unit = {}) {
        timers.remove(name)?.cancel()
        if (ms != null) timers[name] = scope.launch { delay(ms); fn() }
    }

    private fun to(state: String, last: String = this.last) {
        this.state = state
        if (state == "idle") {
            this.last = last; s = null; timer("confirm"); timer("idle"); timer("ready")
        }
        say(when (state) {
            "idle" -> "OTA state=idle last=$last"
            "receiving" -> "OTA state=receiving at=${s!!.at}"
            else -> "OTA state=$state"
        })
    }

    private fun where(): String = s?.let { "state=$state at=${it.at} size=${it.size} id=${it.id} chunk=$CHUNK" }
        ?: "state=idle board=$board fw=$fw chunk=$CHUNK trial=${if (trial) 1 else 0} last=$last rolledback=$rolledback"

    /** One OTA request's arguments ("BEGIN ...", "STATUS"); the reply after "#<id> ". */
    fun command(args: String): String {
        val a = args.trim().split(Regex("\\s+"))
        val sub = a[0]
        val rest = a.drop(1)
        when (sub) {
            "STATUS" -> {
                if (state == "receiving" && (s?.at ?: 0) > 0) stats.polls++
                return "OK ${where()}"
            }
            "ABORT" -> {
                val cur = s ?: return "OK state=idle"
                if (rest.getOrNull(0) != cur.token) return "ERR 403 NOT_YOURS"
                to("idle", "aborted")
                return "OK state=idle"
            }
            "REBOOT" -> {
                val cur = s
                if (cur != null && rest.getOrNull(0) != cur.token) return "ERR 403 NOT_YOURS"
                if (state != "ready") return "ERR 409 NOT_READY"
                scope.launch { delay(300); restart() }
                return "OK rebooting"
            }
            "CONFIRM" -> {
                if (!trial) return "OK trial=0"
                if (rest.getOrNull(0) != trialToken) {
                    stats.refusedConfirms++; return "ERR 403 NOT_YOURS"
                }
                confirmed = true
                stats.confirms++
                return "OK trial=1"
            }
            "BEGIN" -> return begin(rest)
        }
        return "ERR 400 BAD_ARGS"
    }

    // In the badge's order: an open session first, then the cheap refusals.
    private fun begin(a: List<String>): String {
        if (a.size != 8) return "ERR 400 BAD_ARGS"
        val (board, version, flags, fmt) = a
        val size = a[4]; val sha = a[5]; val sig = a[6]; val token = a[7]
        if (semver(version) == null || !flags.all { it.isDigit() } || !fmt.all { it.isDigit() } || !size.all { it.isDigit() } ||
            !Regex("^[A-Za-z0-9_-]{43}$").matches(sha) || !Regex("^[A-Za-z0-9_-]{86}$").matches(sig) ||
            !Regex("^[A-Za-z0-9_-]{22}$").matches(token)
        ) return "ERR 400 BAD_ARGS"
        val id = imageId(sha)
        val cur = s
        if (cur != null && cur.id != id) return "ERR 409 OTA_ACTIVE"
        if (cur != null && cur.token != token) return "ERR 403 NOT_YOURS"
        if (cur != null) return "OK ${where()}"
        if (board != this.board) return "ERR 403 WRONG_BOARD"
        if (flags.toInt() and 1 == 0 && !isNewer(version, fw)) return "ERR 409 NOT_NEWER"
        if (size.toInt() == 0 || size.toInt() > SLOT) return "ERR 413 TOO_BIG"
        if (trial) return "ERR 409 TRIAL"
        s = Session(id, version, token, size.toInt(), sha)
        to("confirm")
        timer("confirm", confirmMs) { to("idle", "no_confirm") }
        return "OK ${where()}"
    }

    /** A hand on the badge: the press (or the turn) that says yes. */
    fun press() {
        if (state != "confirm") return
        timer("confirm")
        to("receiving")
        timer("idle", 300000) { to("idle", "timeout") }
    }

    /** One write on the data characteristic, as it reached the badge. */
    fun data(bytes: ByteArray) {
        val cur = s
        val off = ByteBuffer.wrap(bytes, 0, 4).order(ByteOrder.LITTLE_ENDIAN).int
        if (cur != null && loseWritesAt.isNotEmpty() && off >= loseWritesAt[0] * cur.size) {
            loseWritesAt.removeAt(0); stats.lostWrites++; return
        }
        if (state != "receiving" || cur == null || off != cur.at) {
            stats.ignored++
            if (state == "receiving" && cur != null && off > cur.at && now() - lastGap >= 200) {
                lastGap = now()
                stats.gaps++
                say("OTA at=${cur.at} gap")
            }
            return
        }
        val n = bytes.size - 4
        if (cur.at + n > cur.size) return to("idle", "overflow")
        bytes.copyInto(cur.buf, cur.at, 4, bytes.size)
        if (cur.at >= corruptAt * cur.size) {
            cur.buf[cur.at] = (cur.buf[cur.at].toInt() xor 0xff).toByte(); corruptAt = Double.POSITIVE_INFINITY
        }
        val before = cur.at
        cur.at += n
        timer("idle", 300000) { to("idle", "timeout") }
        if (cur.at / AT_EVERY > before / AT_EVERY && cur.at < cur.size) {
            if (cur.at >= loseAckAt * cur.size) {
                loseAckAt = Double.POSITIVE_INFINITY; stats.lostAcks++
            } else {
                say("OTA at=${cur.at}")
            }
        }
        if (cur.at >= dropAt * cur.size) {
            dropAt = Double.POSITIVE_INFINITY; stats.drops++; scope.launch { drop() }
        }
        if (cur.at == cur.size) check(cur)
    }

    private fun check(cur: Session) {
        if (sha256B64(cur.buf) != cur.sha) return to("idle", "bad_hash")
        stats.flashed = cur.sha
        timer("idle")
        to("ready")
        timer("ready", 20000) { restart() }
    }

    fun drop() {
        if (!connected) return
        connected = false
        listener?.dropped()
    }

    private fun restart() {
        val old = fw
        val cur = s ?: return
        for (name in timers.keys.toList()) timer(name)
        boot(cur.version, true) { startTrial(old, cur.token) }
    }

    // On trial in the running image. It keeps itself once the phone holding the token
    // that switched it in has sent CONFIRM; with no CONFIRM, or the rollback fault, it
    // restarts into `old`, as the bootloader would.
    fun startTrial(old: String, token: String) {
        confirmed = false
        trialToken = token
        timer("trial", trialMs) {
            if ("rollback" in faults || !confirmed) {
                rolledback = fw; boot(old, false)
            } else {
                trial = false
            }
        }
    }

    private fun boot(fw: String, trial: Boolean, then: () -> Unit = {}) {
        stats.reboots++
        drop()
        booting = scope.launch {
            delay(bootMs)
            this@FakeBadge.fw = fw
            this@FakeBadge.trial = trial
            state = "idle"; last = "none"; s = null; booting = null
            then()
        }
    }

    /** The phone coming back to the same device. */
    suspend fun reconnect() {
        delay(300)
        booting?.join()
        connected = true
    }
}

/**
 * The phone's side, shaped like the app's BadgeConnection: request lines with
 * ids, replies, and a transcript in the protocol's own form ("#1 OTA STATUS",
 * "#1 OK state=idle ..."). closeAfter: the app is killed once that command is answered.
 */
class FakePort(private val dish: FakeBadge, private val closeAfter: String? = null) : OtaPort {
    val sent = ArrayList<String>()
    val transcript = ArrayList<String>()
    var closed = false
    private var id = 1

    private fun live() {
        if (closed) throw LinkError("app closed")
        if (!dish.connected) throw LinkError("disconnected")
    }

    override suspend fun send(cmd: String, timeoutMs: Long): Reply {
        live()
        val tag = "#${id.toString(16)}"
        id++
        val line = "$tag $cmd"
        check(line.length <= 200) { "line over 200 bytes: ${line.length}" }
        sent += cmd
        transcript += line
        delay(2)
        live()
        val verb = cmd.substringBefore(' ')
        val reply = when (verb) {
            "OTA" -> dish.command(cmd.substringAfter(' '))
            "HELLO" -> "OK fw=${dish.fw}"
            else -> "ERR 404 UNKNOWN_VERB"
        }
        transcript += "$tag $reply"
        if (closeAfter != null && cmd.startsWith(closeAfter)) closed = true
        if (reply.startsWith("ERR ")) throw BadgeError.parse(reply.removePrefix("ERR "))
        return Reply(reply.substring(minOf(3, reply.length)), emptyList())
    }

    override suspend fun writeData(bytes: ByteArray) {
        live()
        yield()
        dish.data(bytes)
    }

    override suspend fun reconnect() {
        if (closed) throw LinkError("app closed")
        dish.reconnect()
    }
}
