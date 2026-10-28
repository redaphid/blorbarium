package com.hypnodroid.blorbrelay.sync

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import com.hypnodroid.blorbrelay.ota.TokenStorage
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.longOrNull
import kotlinx.serialization.json.put
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** What the app knows about one badge, keyed by its Bluetooth address (an update renames it; the address stays). */
data class BadgeRecord(
    val address: String,
    val name: String = "",          // what it advertises: its species, or blorb-xxxx during a trial
    val petName: String = "",
    val fw: String = "",
    val mac: String = "",           // STATE mac=: the Wi-Fi MAC the site knows it by
    val rssi: Int = 0,
    val lastSeen: Long = 0,
    val lastAttempt: Long = 0,
    val lastSync: Long = 0,         // the last sync that reached it and finished
    val lastOk: Boolean = false,
    val lastResult: String = "",
    val otaNotBefore: Long = 0,     // an unattended update ask waits until then (after nobody said yes)
    val linesPosted: Long = 0,
    val statsPosted: Long = 0,
    val debugPosted: Long = 0,
) {
    fun toJson(): JsonObject = buildJsonObject {
        put("address", address); put("name", name); put("petName", petName); put("fw", fw); put("mac", mac)
        put("rssi", rssi); put("lastSeen", lastSeen); put("lastAttempt", lastAttempt); put("lastSync", lastSync)
        put("lastOk", lastOk); put("lastResult", lastResult); put("otaNotBefore", otaNotBefore)
        put("linesPosted", linesPosted); put("statsPosted", statsPosted); put("debugPosted", debugPosted)
    }

    companion object {
        fun fromJson(o: JsonObject): BadgeRecord {
            fun s(k: String) = (o[k] as? JsonPrimitive)?.content ?: ""
            fun l(k: String) = (o[k] as? JsonPrimitive)?.longOrNull ?: 0L
            return BadgeRecord(
                address = s("address"), name = s("name"), petName = s("petName"), fw = s("fw"), mac = s("mac"),
                rssi = l("rssi").toInt(), lastSeen = l("lastSeen"), lastAttempt = l("lastAttempt"), lastSync = l("lastSync"),
                lastOk = (o["lastOk"] as? JsonPrimitive)?.booleanOrNull ?: false, lastResult = s("lastResult"),
                otaNotBefore = l("otaNotBefore"), linesPosted = l("linesPosted"), statsPosted = l("statsPosted"),
                debugPosted = l("debugPosted"),
            )
        }
    }
}

class Store(ctx: Context) {
    private val prefs: SharedPreferences = ctx.getSharedPreferences("badges", Context.MODE_PRIVATE)
    val tokenPrefs: SharedPreferences = ctx.getSharedPreferences("ota-tokens", Context.MODE_PRIVATE)

    @Synchronized
    fun all(): List<BadgeRecord> = prefs.all.values.mapNotNull { v ->
        try {
            BadgeRecord.fromJson(Json.parseToJsonElement(v as String).jsonObject)
        } catch (e: Exception) {
            null
        }
    }.sortedByDescending { it.lastSeen }

    @Synchronized
    fun get(address: String): BadgeRecord = prefs.getString(address, null)?.let {
        try {
            BadgeRecord.fromJson(Json.parseToJsonElement(it).jsonObject)
        } catch (e: Exception) {
            null
        }
    } ?: BadgeRecord(address)

    @Synchronized
    fun update(address: String, change: (BadgeRecord) -> BadgeRecord): BadgeRecord {
        val r = change(get(address))
        prefs.edit().putString(address, r.toJson().toString()).apply()
        return r
    }

    fun listen(l: SharedPreferences.OnSharedPreferenceChangeListener) = prefs.registerOnSharedPreferenceChangeListener(l)
    fun unlisten(l: SharedPreferences.OnSharedPreferenceChangeListener) = prefs.unregisterOnSharedPreferenceChangeListener(l)

    fun tokens(): TokenStorage = object : TokenStorage {
        override fun get(key: String): String? = tokenPrefs.getString(key, null)
        override fun put(key: String, value: String) {
            tokenPrefs.edit().putString(key, value).commit()   // commit: the token must exist before BEGIN carries it
        }
    }
}

/** A short in-memory log the screen shows, mirrored to logcat. */
object RelayLog {
    private const val MAX = 400
    private val lines = ArrayDeque<String>()
    private val fmt = SimpleDateFormat("HH:mm:ss", Locale.US)
    @Volatile var listener: (() -> Unit)? = null

    fun say(text: String) {
        Log.i("BlorbRelay", text)
        synchronized(lines) {
            lines.addLast("${fmt.format(Date())} $text")   // fmt is used only under this lock
            while (lines.size > MAX) lines.removeFirst()
        }
        listener?.invoke()
    }

    fun tail(n: Int): List<String> = synchronized(lines) { lines.toList().takeLast(n) }
}
