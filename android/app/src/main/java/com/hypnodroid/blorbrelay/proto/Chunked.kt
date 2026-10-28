// Replies that carry bytes: GENOME, SNAPSHOT and STATS json answer
// "+ <offset> <base64>" lines closed by "OK <len> <crc32>" (index.html fetchGenome,
// docs/life-stats.md).
package com.hypnodroid.blorbrelay.proto

import java.security.MessageDigest
import java.util.Base64
import java.util.zip.CRC32

private val CHUNK = Regex("^(\\d+) ([A-Za-z0-9+/]+=*)$")
private val END = Regex("^(\\d+) ([0-9a-f]{8})$")

fun crc32Hex(bytes: ByteArray): String = CRC32().run { update(bytes); "%08x".format(value) }

/** The bytes a chunked reply carried, or LinkError("garbled ...") when any is missing or the CRC is wrong. */
fun assembleChunked(reply: Reply): ByteArray {
    val end = END.matchEntire(reply.ok.trim()) ?: throw LinkError("odd ending: ${reply.ok}")
    val len = end.groupValues[1].toInt()
    if (len > 1 shl 20) throw LinkError("too big: $len")
    val bytes = ByteArray(len)
    var filled = 0
    for (line in reply.lines) {
        val c = CHUNK.matchEntire(line.trim()) ?: continue
        val at = c.groupValues[1].toInt()
        val part = Base64.getDecoder().decode(c.groupValues[2])
        if (at < 0 || at + part.size > len) throw LinkError("garbled: chunk at $at past $len")
        part.copyInto(bytes, at)
        filled += part.size
    }
    val got = crc32Hex(bytes)
    if (filled != len || got != end.groupValues[2]) {
        throw LinkError("garbled: $filled/$len bytes, crc $got vs ${end.groupValues[2]}")
    }
    return bytes
}

fun hex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it) }

/** The site's genome_hash: SHA-256 over the genome's bytes, as hex. */
fun sha256Hex(bytes: ByteArray): String = hex(MessageDigest.getInstance("SHA-256").digest(bytes))
