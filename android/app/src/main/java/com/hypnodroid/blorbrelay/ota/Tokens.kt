package com.hypnodroid.blorbrelay.ota

/** Somewhere tokens outlive the app's process (SharedPreferences on the phone, a map in tests). */
interface TokenStorage {
    fun get(key: String): String?
    fun put(key: String, value: String)
}

/**
 * A session belongs to whoever holds its token. One per (badge, version), kept
 * so that a sync after the app was killed still owns its update and can CONFIRM
 * it (ota.js tokenStore). Keyed by the Bluetooth address, which an update never
 * changes (the advertised name does). Without storage, a token lasts as long as this store.
 */
fun tokenStore(device: String, storage: TokenStorage?): (String) -> String {
    val held = HashMap<String, String>()
    return { version ->
        val key = "blorb-ota-token:$device:$version"
        var token = held[key]
        try {
            storage?.get(key)?.let { token = it }
        } catch (_: Exception) {
        }
        val t = token ?: newToken()
        held[key] = t
        try {
            storage?.put(key, t)
        } catch (_: Exception) {
        }
        t
    }
}
