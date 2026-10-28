// One sync of one badge: what the website does on connect (index.html session()
// and offerUpdate()), then what the badge's own Wi-Fi window does with the site
// (pet lines, stats, a debug note), then the update if the site has a newer one.
package com.hypnodroid.blorbrelay.sync

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.content.Context
import com.hypnodroid.blorbrelay.BuildConfig
import com.hypnodroid.blorbrelay.Notify
import com.hypnodroid.blorbrelay.ble.BadgeConnection
import com.hypnodroid.blorbrelay.net.Api
import com.hypnodroid.blorbrelay.ota.Manifest
import com.hypnodroid.blorbrelay.ota.Outcome
import com.hypnodroid.blorbrelay.ota.Release
import com.hypnodroid.blorbrelay.ota.TrialCheck
import com.hypnodroid.blorbrelay.ota.Updater
import com.hypnodroid.blorbrelay.ota.checkImage
import com.hypnodroid.blorbrelay.ota.checkTrial
import com.hypnodroid.blorbrelay.ota.imageId
import com.hypnodroid.blorbrelay.ota.isNewer
import com.hypnodroid.blorbrelay.ota.parseManifest
import com.hypnodroid.blorbrelay.ota.tokenStore
import com.hypnodroid.blorbrelay.ota.verifySignature
import com.hypnodroid.blorbrelay.proto.BadgeError
import com.hypnodroid.blorbrelay.proto.assembleChunked
import com.hypnodroid.blorbrelay.proto.hex
import com.hypnodroid.blorbrelay.proto.kv
import com.hypnodroid.blorbrelay.proto.sha256Hex
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.put
import java.io.File
import java.net.URL
import java.util.Calendar
import java.util.TimeZone

class BadgeSync(
    private val ctx: Context,
    private val store: Store,
    private val address: String,
    private val force: Boolean,
) {
    private val notes = ArrayList<String>()
    private val transcript = ArrayList<String>()
    private var ok = true
    private val log: (String) -> Unit = { line ->
        RelayLog.say("[$address] $line")
        synchronized(transcript) { if (transcript.size < 200) transcript += line }
    }

    private fun note(text: String, bad: Boolean = false) {
        notes += text
        if (bad) ok = false
        log(text)
    }

    @SuppressLint("MissingPermission")
    suspend fun run() {
        val now = System.currentTimeMillis()
        store.update(address) { it.copy(lastAttempt = now) }
        val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        if (adapter == null || !adapter.isEnabled) {
            finishWith(false, "Bluetooth is off")
            return
        }
        val conn = BadgeConnection(ctx, adapter.getRemoteDevice(address), log)
        var reached = false
        try {
            conn.open()
            reached = true
            session(conn)
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            note(if (reached) "lost the badge: ${e.message}" else "couldn't connect: ${e.message}", bad = true)
        } finally {
            conn.close()
            Notify.clearAsk(ctx, address)
        }
        if (reached) store.update(address) { it.copy(lastSync = System.currentTimeMillis()) }
        finishWith(ok, notes.joinToString("; "))
    }

    private fun finishWith(good: Boolean, text: String) {
        store.update(address) { it.copy(lastOk = good, lastResult = text.ifEmpty { if (good) "ok" else "failed" }) }
        RelayLog.say("[$address] ${if (good) "done" else "FAILED"}: $text")
    }

    private suspend fun sendTime(conn: BadgeConnection) {
        val nowMs = System.currentTimeMillis()
        val tz = TimeZone.getDefault().getOffset(nowMs) / 60000
        try {
            conn.send("TIME ${nowMs / 1000} $tz")
            note("time set")
        } catch (e: BadgeError) {
            note("TIME refused (${e.reason})")
        }
    }

    private suspend fun session(conn: BadgeConnection) {
        // TIME at once: it sets a clock the badge may have lost, and a badge drops a phone
        // that says nothing it knows within 10 s (docs/ota.md, the squatters rule).
        sendTime(conn)
        val hello = kv(conn.send("HELLO").ok)
        val stateLines = try {
            conn.send("STATE").lines
        } catch (e: BadgeError) {
            emptyList()
        }
        val st = stateLines.firstOrNull { it.startsWith("phase=") }?.let { kv(it) } ?: emptyMap()
        val rec = store.update(address) {
            it.copy(fw = hello["fw"] ?: it.fw, petName = hello["name"] ?: it.petName,
                mac = st["mac"]?.takeIf { m -> m != "none" } ?: it.mac)
        }
        note("fw ${hello["fw"] ?: "?"}, ${hello["phase"] ?: "?"}${st["stage"]?.let { " $it" } ?: ""}")

        // OTA STATUS, and the CONFIRM a trial this phone began is waiting for (ota.js checkTrial).
        val tokens = tokenStore(address, store.tokens())
        val ota: TrialCheck? = try {
            checkTrial(conn, tokens)
        } catch (e: BadgeError) {
            if (e.code == 404) note("this badge needs one cable flash before it can update by Bluetooth")
            else note("OTA STATUS refused (${e.reason})", bad = true)
            null
        }
        when (ota?.confirm) {
            "confirmed" -> note("confirmed the new ${ota.status["fw"]} on trial")
            "not_yours" -> note("${ota.status["fw"]} is on trial for another phone")
        }

        val mac = macHex(rec.mac)
        val sealed = st["sealed"] != null
        var genome: ByteArray? = null
        if (hello["phase"] == "creature" && !sealed && mac != null) {
            genome = readGenome(conn)
            if (genome != null) postLines(mac, genome, hello, st)
        }
        if (mac != null && !sealed) uploadStats(conn, mac, genome, hello["fw"] ?: "")

        if (ota != null) update(conn, ota, hello)

        if (mac != null) postDebug(mac, hello, st, ota)
    }

    // ---- the site: lines, stats, a debug note ---------------------------------------------

    private suspend fun readGenome(conn: BadgeConnection): ByteArray? {
        for (i in 1..2) {
            try {
                return assembleChunked(conn.send("GENOME", 30000))
            } catch (e: BadgeError) {
                if (e.code != 404) note("GENOME refused (${e.reason})")
                return null
            } catch (e: com.hypnodroid.blorbrelay.proto.LinkError) {
                if (i == 2 || e.message == "disconnected") throw e
                log("GENOME: ${e.message}, retrying")
            }
        }
        return null
    }

    private suspend fun postLines(mac: String, genome: ByteArray, hello: Map<String, String>, st: Map<String, String>) {
        val rec = store.get(address)
        if (!force && System.currentTimeMillis() - rec.linesPosted < Policy.LINES_EVERY) return
        val body = petLinesBody(mac, hex(genome), stageIndex(st["stage"]) ?: 2, hello["gen"]?.toIntOrNull() ?: 0,
            hello["fw"] ?: "")
        try {
            val r = withContext(Dispatchers.IO) { Api.postJson("api/pet-lines", body) }
            when {
                r.status == 202 -> note("site: his lines are being written")
                r.ok -> {
                    val v = runCatching { Json.parseToJsonElement(r.text).jsonObject["version"]?.toString() }.getOrNull()
                    note("site: lines v${v ?: "?"} (they reach the badge over its Wi-Fi)")
                }
                else -> note("site: pet-lines ${r.status}")
            }
            if (r.ok || r.status == 429) store.update(address) { it.copy(linesPosted = System.currentTimeMillis()) }
        } catch (e: Exception) {
            note("site unreachable (pet-lines: ${e.message})")
        }
    }

    private suspend fun uploadStats(conn: BadgeConnection, mac: String, genome: ByteArray?, fw: String) {
        val rec = store.get(address)
        if (!force && System.currentTimeMillis() - rec.statsPosted < Policy.STATS_EVERY) return
        val doc = try {
            Json.parseToJsonElement(String(assembleChunked(conn.send("STATS json", 30000)), Charsets.UTF_8)).jsonObject
        } catch (e: BadgeError) {
            if (e.code != 404) note("STATS refused (${e.reason})")
            return
        } catch (e: com.hypnodroid.blorbrelay.proto.LinkError) {
            if (e.message == "disconnected") throw e
            note("STATS unreadable (${e.message})")
            return
        } catch (e: Exception) {
            note("STATS unreadable (${e.message})")
            return
        }
        val body = statsBody(mac, genome?.let { sha256Hex(it) }, fw, doc, System.currentTimeMillis())
        try {
            val r = withContext(Dispatchers.IO) { Api.postJson("api/stats", body) }
            if (!r.ok) {
                note("site: stats ${r.status}")
                return
            }
            store.update(address) { it.copy(statsPosted = System.currentTimeMillis()) }
            val ack = statsAck(runCatching { Json.parseToJsonElement(r.text) }.getOrNull())
            if (ack != null) {
                // Only an ack the site gave lets the badge drop its closed buckets.
                val a = kv(conn.send("STATS ack $ack").ok)
                note("stats uploaded, acked $ack (dropped=${a["dropped"] ?: "?"})")
            } else {
                note("stats uploaded")
            }
        } catch (e: BadgeError) {
            note("STATS ack refused (${e.reason})")
        } catch (e: java.io.IOException) {
            note("site unreachable (stats: ${e.message})")
        }
    }

    private suspend fun postDebug(mac: String, hello: Map<String, String>, st: Map<String, String>, ota: TrialCheck?) {
        val rec = store.get(address)
        if (!force && System.currentTimeMillis() - rec.debugPosted < Policy.DEBUG_EVERY) return
        val body = buildJsonObject {
            put("mac", mac)
            hello["fw"]?.let { put("fw", it) }
            put("via", "phone")
            put("items", buildJsonArray {
                add(buildJsonObject {
                    put("kind", "phone")
                    put("summary", "relay sync (${rec.name.ifEmpty { "badge" }}): ${notes.joinToString("; ")}".take(500))
                    put("data", buildJsonObject {
                        put("app", BuildConfig.VERSION_NAME)
                        put("address", address)
                        put("advertised", rec.name)
                        put("rssi", rec.rssi)
                        put("hello", JsonObject(hello.mapValues { JsonPrimitive(it.value) }))
                        put("state", JsonObject(st.mapValues { JsonPrimitive(it.value) }))
                        ota?.let { put("ota", JsonObject(it.status.mapValues { e -> JsonPrimitive(e.value) })) }
                        put("transcript", JsonArray(synchronized(transcript) { transcript.toList() }.map { JsonPrimitive(it) }))
                    })
                })
            })
        }
        try {
            val r = withContext(Dispatchers.IO) { Api.postJson("api/debug", body) }
            if (r.ok) store.update(address) { it.copy(debugPosted = System.currentTimeMillis()) }
            else log("site: debug ${r.status}")
        } catch (e: Exception) {
            log("site unreachable (debug: ${e.message})")
        }
    }

    // ---- the update (index.html offerUpdate + runUpdate) ----------------------------------

    private suspend fun update(conn: BadgeConnection, ota: TrialCheck, hello: Map<String, String>) {
        val st = ota.status
        if (st["trial"] == "1") {
            note("an update is still checking itself; nothing more until it keeps")
            return
        }
        val board = st["board"] ?: "lcd128"
        val m = try {
            fetchManifest(board)
        } catch (e: Exception) {
            note("couldn't check for updates (${e.message})")
            return
        }
        // The badge checks the signature at BEGIN too; this phone never asks it to look at one that fails here.
        if (!verifySignature(m)) {
            note("the site's manifest for ${m.version} is not signed by the owner's key: not offered", bad = true)
            return
        }
        val fw = st["fw"] ?: hello["fw"]
        val halfway = st["state"] != "idle" && st["id"] == imageId(m.sha256)
        if (!halfway && !isNewer(m.version, fw)) {
            note("up to date ($fw)")
            return
        }
        val failedBefore = !halfway && st["rolledback"] == m.version
        if (!force) {
            // A version that went back is never retried in a silent loop (index.html offerUpdate).
            if (failedBefore) return note("${m.version} went back on this badge before: only Sync now retries it")
            if (!halfway && Policy.quietHour(Calendar.getInstance().get(Calendar.HOUR_OF_DAY))) {
                return note("${m.version} is ready; the phone asks the badge in the daytime")
            }
            val wait = store.get(address).otaNotBefore - System.currentTimeMillis()
            if (!halfway && wait > 0) return note("${m.version} is ready; asking again in ${wait / Policy.MINUTE} min")
        }
        if (!conn.hasOtaData) return note("no OTA data characteristic on this badge", bad = true)

        val name = store.get(address).name.ifEmpty { "The badge" }
        note(if (halfway) "carrying on the update to ${m.version}" else "updating $fw -> ${m.version}")
        val release = Release(m) { cachedImage(m, URL(Api.manifestUrl(board), m.image)) }
        var lastPct = -1
        val updater = Updater(conn, release, tokenStore(address, store.tokens()), onChange = { p ->
            if (p.state == "confirm") Notify.ask(ctx, address, name, m.version) else Notify.clearAsk(ctx, address)
            if (p.state == "sending") {
                val pct = (p.at.toLong() * 100 / p.size).toInt()
                if (pct / 10 != lastPct / 10) log("sending $pct%")
                if (pct != lastPct) Notify.progress(ctx, "$name: sending ${m.version}, $pct%")
                lastPct = pct
            } else if (p.state != "done" && p.state != "failed") {
                log("update: ${p.state}")
                Notify.progress(ctx, "$name: update ${p.state}")
            }
        })
        conn.updater = updater
        val outcome = try {
            updater.run()
        } finally {
            conn.updater = null
            Notify.clearAsk(ctx, address)
            Notify.progress(ctx, null)
        }
        note(describe(outcome, fw))
        val backoff = when (outcome.reason) {
            "no_confirm" -> Policy.NO_CONFIRM_BACKOFF
            "BUSY", "busy", "TRIAL" -> Policy.BUSY_BACKOFF
            else -> 0L
        }
        if (backoff > 0) store.update(address) { it.copy(otaNotBefore = System.currentTimeMillis() + backoff) }
        if (!outcome.ok && outcome.reason !in setOf("no_confirm", "BUSY", "busy", "TRIAL", "NOT_YOURS", "OTA_ACTIVE")) ok = false
        if (outcome.reason != "unreachable") {
            // As the page does after an update: hello again, and the time a restart forgot.
            try {
                sendTime(conn)
                kv(conn.send("HELLO").ok)["fw"]?.let { f -> store.update(address) { it.copy(fw = f) } }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                log("after the update: ${e.message}")
            }
        }
    }

    private fun describe(o: Outcome, fw: String?): String = when {
        o.ok && o.kind == "updated" -> "updated to ${o.fw}"
        o.ok && o.kind == "unconfirmed" -> "${o.fw} is still checking itself (the next sync confirms it)"
        o.ok && o.kind == "rolled_back" -> "the update didn't take; back on ${o.fw}"
        o.ok -> "already up to date"
        else -> "update stopped: ${WHY[o.reason] ?: o.reason} (nothing changed, ${fw ?: "old"} still runs)"
    }

    private suspend fun fetchManifest(board: String): Manifest = withContext(Dispatchers.IO) {
        val r = Api.get(Api.manifestUrl(board), maxBytes = 64 * 1024)
        if (!r.ok) throw java.io.IOException("manifest ${r.status}")
        parseManifest(r.text, board)
    }

    /** The image, from the phone's cache when it is there and still checks, else downloaded (and checked) once. */
    private suspend fun cachedImage(m: Manifest, url: URL): ByteArray = withContext(Dispatchers.IO) {
        val dir = File(ctx.filesDir, "fw").apply { mkdirs() }
        val name = "${m.board}-${m.version}-${m.sha256}.bin"
        val file = File(dir, name)
        if (file.exists()) {
            val bytes = file.readBytes()
            try {
                checkImage(m, bytes)
                return@withContext bytes
            } catch (e: Exception) {
                file.delete()
            }
        }
        log("downloading ${m.image} (${m.size} bytes)")
        val r = Api.get(url, maxBytes = m.size + 1, timeoutMs = 120000)
        if (!r.ok) throw java.io.IOException("image ${r.status}")
        checkImage(m, r.body)
        dir.listFiles()?.forEach { if (it.name != name) it.delete() }   // keep one image
        File(dir, "$name.part").apply { writeBytes(r.body); renameTo(file) }
        r.body
    }

    companion object {
        /** Why an update stopped, by the badge's last= or ERR text (index.html UPDATE_FAILED, ble_ota.py WHY). */
        val WHY = mapOf(
            "no_confirm" to "nobody turned the badge or pressed its button within 30 s",
            "download" to "couldn't download the update",
            "unreachable" to "couldn't reach the badge again",
            "BUSY" to "the pet is busy",
            "busy" to "the pet became busy",
            "BAD_SIGNATURE" to "the badge didn't trust the update",
            "WRONG_BOARD" to "the update is for another board",
            "TRIAL" to "the badge is still checking its last update",
            "NOT_YOURS" to "another phone is updating this badge",
            "bad_hash" to "the bytes didn't hash right on the badge",
            "timeout" to "5 minutes with no image byte",
            "lost_session" to "the badge forgot the session",
        )
    }
}
