// One update over Bluetooth as an explicit state machine: a faithful port of
// web/public/ota.js's Updater (docs/ota.md "The page's side"). Same states,
// same drop table, same windows and waits; coroutines in place of promises.
package com.hypnodroid.blorbrelay.ota

import com.hypnodroid.blorbrelay.proto.BadgeError
import com.hypnodroid.blorbrelay.proto.LinkError
import com.hypnodroid.blorbrelay.proto.Reply
import com.hypnodroid.blorbrelay.proto.kv
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.withTimeoutOrNull

/** What the updater needs of a connected badge (ota.js's port). */
interface OtaPort {
    /** A request line; its reply, or throws BadgeError (the badge said no) / LinkError (the link failed). */
    suspend fun send(cmd: String, timeoutMs: Long = 8000): Reply

    /** One write on the OTA data characteristic, without response. */
    suspend fun writeData(bytes: ByteArray)

    /** Back to the same badge after a drop; gives up (throws) after about two minutes. */
    suspend fun reconnect()
}

/** A release: its checked manifest, and the image (downloaded or cached) on demand. */
class Release(val manifest: Manifest, val image: suspend () -> ByteArray)

/** ota.js's outcome: {ok:true, kind, fw} or {ok:false, reason}. */
data class Outcome(val ok: Boolean, val kind: String? = null, val fw: String? = null, val reason: String? = null) {
    companion object {
        fun done(kind: String, fw: String?) = Outcome(true, kind = kind, fw = fw)
        fun failed(reason: String) = Outcome(false, reason = reason)
    }
}

data class Progress(val state: String, val at: Int, val size: Int, val outcome: Outcome?)

/** "confirmed", "not_yours" or null (not on trial). ota.js confirmTrial. */
suspend fun confirmTrial(port: OtaPort, tokens: (String) -> String, status: Map<String, String>): String? {
    if (status["trial"] != "1") return null
    return try {
        port.send("OTA CONFIRM ${tokens(status["fw"] ?: "")}")
        "confirmed"
    } catch (e: BadgeError) {
        if (e.code == 403) "not_yours" else throw e
    }
}

data class TrialCheck(val status: Map<String, String>, val confirm: String?)

/** STATUS, with the CONFIRM a trial needs from the phone whose update it is. ota.js checkTrial. */
suspend fun checkTrial(port: OtaPort, tokens: (String) -> String): TrialCheck {
    val status = kv(port.send("OTA STATUS").ok)
    return TrialCheck(status, confirmTrial(port, tokens, status))
}

class Updater(
    private val port: OtaPort,
    private val release: Release,
    private val tokens: (String) -> String,
    private val onChange: (Progress) -> Unit = {},
    private val trialWaitMs: Long = TRIAL_WAIT_MS,
    private val now: () -> Long = System::currentTimeMillis,
) {
    private class Fail(val reason: String) : Exception(reason)

    private class Dish {
        @Volatile var state = "idle"
        @Volatile var at = 0
        @Volatile var last = "none"
    }

    private val m = release.manifest
    private val token = tokens(m.version)
    private val dish = Dish()
    private val wake = Channel<Unit>(Channel.CONFLATED)
    private lateinit var image: ByteArray

    @Volatile var state = "idle"
        private set
    @Volatile private var lost = false
    @Volatile private var rewind: Int? = null
    @Volatile private var chunk = 0
    private var detour = false
    private var aborted = false
    private var waited = false
    private var outcome: Outcome? = null

    /** The owner forwards every "OTA ..." event here (the text after "! "). */
    fun event(text: String) {
        if (!text.startsWith("OTA")) return
        val k = absorb(text)
        if ("at" in k && Regex("\\bgap\\b").containsMatchIn(text)) rewind = dish.at
        wake.trySend(Unit)
    }

    /** ... and a lost link here, before failing the requests in flight. */
    fun dropped() {
        lost = true
        wake.trySend(Unit)
    }

    @Synchronized
    private fun absorb(text: String): Map<String, String> {
        val k = kv(text)
        k["state"]?.let { dish.state = it }
        k["at"]?.toIntOrNull()?.let { dish.at = it }
        k["last"]?.let { dish.last = it }
        k["chunk"]?.toIntOrNull()?.let { chunk = it }
        return k
    }

    /** Returns after ms, or sooner on any event or drop. */
    private suspend fun sleep(ms: Long) {
        if (ms <= 0) return
        withTimeoutOrNull(ms) { wake.receive() }
    }

    private fun set(s: String) {
        state = s
        onChange(Progress(s, dish.at, m.size, outcome))
    }

    private fun finish(kind: String, fw: String?): String {
        outcome = Outcome.done(kind, fw); return "done"
    }

    private fun fail(reason: String): String {
        outcome = Outcome.failed(reason); return "failed"
    }

    private suspend fun status() = absorb(port.send("OTA STATUS").ok)

    private suspend fun reconnect() {
        try {
            port.reconnect()
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            throw Fail("unreachable")
        }
        lost = false
    }

    suspend fun run(): Outcome {
        var s = "checking"
        while (true) {
            set(s)
            if (s == "done" || s == "failed") return outcome!!
            s = try {
                step(s)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Fail) {
                fail(e.reason)
            } catch (e: Exception) {
                val onDrop = ON_DROP[s]
                if (lost && onDrop != null) onDrop
                else fail(if (e is BadgeError) e.reason else "error")
            }
        }
    }

    private suspend fun step(s: String): String = when (s) {
        "checking" -> checking()
        "begin" -> begin()
        "confirm" -> confirm()
        "sending" -> sending()
        "finishing" -> finishing()
        "rebooting" -> rebooting()
        "verifying" -> verifying()
        "trial" -> trial()
        "resuming" -> resuming()
        else -> fail("error")
    }

    private suspend fun checking(): String {
        try {
            image = release.image()
            checkImage(m, image)
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            throw Fail("download")
        }
        return "begin"
    }

    private suspend fun begin(): String {
        val r = try {
            port.send(beginLine(m, token))
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            if (lost || e !is BadgeError) throw e
            val msg = e.message ?: ""
            // A different image holds the session. If it is this phone's own (an older
            // manifest of this version), end it.
            if ("OTA_ACTIVE" in msg && !aborted) {
                aborted = true
                port.send("OTA ABORT $token")
                return "begin"
            }
            if ("NOT_NEWER" in msg) return finish("up_to_date", null)
            // An earlier update is still on trial: confirm it if it is this phone's, wait it out, then begin.
            if ("TRIAL" in msg && !detour) {
                detour = true
                return "trial"
            }
            // The dish checks at most one signature every 2 s.
            if ("TOO_SOON" in msg && !waited) {
                waited = true
                sleep(2100)
                return "begin"
            }
            throw Fail(e.reason)
        }
        detour = false
        absorb(r.ok)
        return next() ?: "confirm"
    }

    /** The state the dish's own state calls for, or null while it says confirm. */
    private fun next(): String? = when (dish.state) {
        "receiving" -> "sending"
        "ready" -> "finishing"
        "idle" -> fail(if (dish.last == "none") "lost_session" else dish.last)
        else -> null
    }

    private suspend fun confirm(): String {
        while (true) {
            status()
            next()?.let { return it }
            sleep(1000)
            if (lost) throw LinkError("disconnected")
        }
    }

    private suspend fun sending(): String {
        val size = m.size
        var off = dish.at
        var heard = dish.at
        var moved = now()
        rewind = null
        while (true) {
            if (lost) throw LinkError("disconnected")
            if (dish.state != "receiving") return next() ?: "confirm"
            rewind?.let { off = it; rewind = null }
            if (dish.at != heard) {
                heard = dish.at
                moved = now()
                set("sending")
            }
            if (off < size && off - dish.at < WINDOW) {
                val n = minOf(if (chunk > 0) chunk else MIN_CHUNK, size - off)
                port.writeData(packet(image, off, n))
                off += n
                continue
            }
            val quiet = now() - moved
            if (quiet < STALL_MS) {
                sleep(STALL_MS - quiet); continue
            }
            status()
            off = dish.at
            moved = now()
        }
    }

    private suspend fun finishing(): String {
        try {
            port.send("OTA REBOOT $token")
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            if (lost || e !is BadgeError || e.code != 409) throw e
            status()
            return next() ?: "confirm"
        }
        return "rebooting"
    }

    private suspend fun rebooting(): String {
        val until = now() + REBOOT_WAIT_MS
        while (!lost && now() < until) sleep(until - now())
        if (lost) reconnect()
        return "verifying"
    }

    private suspend fun verifying(): String {
        val fw = kv(port.send("HELLO").ok)["fw"]
        if (fw == m.version) return "trial"
        if (detour) return "begin"
        // Still waiting to restart (busy, or the drop was not the restart): ask again.
        status()
        if (dish.state == "ready") return "finishing"
        return finish("rolled_back", fw)
    }

    // The new image keeps itself only after its own checks and a CONFIRM from
    // the phone that switched it in (docs/ota.md "The trial"). Also the detour
    // for an earlier update found on trial at BEGIN.
    private suspend fun trial(): String {
        val until = now() + trialWaitMs
        var asked = false
        while (true) {
            val k = status()
            if (k["trial"] == "1" && !asked) {
                asked = true
                if (confirmTrial(port, tokens, k) == "not_yours") throw Fail("TRIAL")
            }
            if (k["trial"] != "1") {
                if (k["fw"] == m.version) return finish("updated", k["fw"])
                return if (detour) "begin" else "verifying"
            }
            if (now() > until) return finish("unconfirmed", k["fw"])
            sleep(TRIAL_POLL_MS)
            if (lost) throw LinkError("dropped")
        }
    }

    private suspend fun resuming(): String {
        reconnect()
        return "begin"
    }

    companion object {
        const val WINDOW = 6144             // bytes in flight past the last at= heard
        const val STALL_MS = 2000L          // no progress for this long: ask STATUS
        const val REBOOT_WAIT_MS = 15000L   // how long to wait for the restart's drop
        const val TRIAL_WAIT_MS = 90000L    // the dish keeps itself 30 s after CONFIRM; past this, it is unconfirmed
        const val TRIAL_POLL_MS = 2000L
        private const val MIN_CHUNK = 16    // only if a reply never said chunk= (it always does)

        /** Where a dropped connection sends each state: before the restart, resume; after REBOOT, it is the restart. */
        val ON_DROP = mapOf(
            "begin" to "resuming", "confirm" to "resuming", "sending" to "resuming",
            "finishing" to "rebooting", "rebooting" to "rebooting", "verifying" to "rebooting", "trial" to "rebooting",
        )
    }
}
