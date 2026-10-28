// The site's public endpoints, the same ones the website and the badges use.
// No accounts and no secrets: nothing here needs a token.
package com.hypnodroid.blorbrelay.net

import com.hypnodroid.blorbrelay.BuildConfig
import kotlinx.serialization.json.JsonObject
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

class HttpResult(val status: Int, val body: ByteArray, val headers: Map<String, String>) {
    val text: String get() = String(body, Charsets.UTF_8)
    val ok: Boolean get() = status in 200..299
}

object Api {
    const val SITE = "https://blorb.hypnodroid.com/"
    private const val UA = "blorb-relay/${BuildConfig.VERSION_NAME} (Android)"

    fun manifestUrl(board: String): URL {
        require(Regex("^[a-z0-9_]{1,32}$").matches(board)) { "board $board" }
        return URL("${SITE}fw/$board/manifest.json")
    }

    fun get(url: URL, maxBytes: Int = 8 * 1024 * 1024, timeoutMs: Int = 30000): HttpResult =
        request(url, "GET", null, maxBytes, timeoutMs)

    fun postJson(path: String, body: JsonObject, timeoutMs: Int = 30000): HttpResult =
        request(URL(SITE + path.removePrefix("/")), "POST", body.toString().toByteArray(Charsets.UTF_8), 1 shl 20, timeoutMs)

    private fun request(url: URL, method: String, body: ByteArray?, maxBytes: Int, timeoutMs: Int): HttpResult {
        if (url.protocol != "https") throw IOException("https only: $url")
        val c = url.openConnection() as HttpURLConnection
        try {
            c.requestMethod = method
            c.connectTimeout = 15000
            c.readTimeout = timeoutMs
            c.useCaches = false
            c.setRequestProperty("User-Agent", UA)
            c.setRequestProperty("Cache-Control", "no-cache")
            if (body != null) {
                c.doOutput = true
                c.setRequestProperty("Content-Type", "application/json")
                c.setFixedLengthStreamingMode(body.size)
                c.outputStream.use { it.write(body) }
            }
            val status = c.responseCode
            val stream = if (status >= 400) c.errorStream else c.inputStream
            val out = ByteArrayOutputStream()
            stream?.use { s ->
                val buf = ByteArray(16384)
                while (true) {
                    val n = s.read(buf)
                    if (n < 0) break
                    out.write(buf, 0, n)
                    if (out.size() > maxBytes) throw IOException("reply over $maxBytes bytes")
                }
            }
            val headers = c.headerFields.filterKeys { it != null }.mapKeys { it.key.lowercase() }
                .mapValues { it.value.joinToString(",") }
            return HttpResult(status, out.toByteArray(), headers)
        } finally {
            c.disconnect()
        }
    }
}
