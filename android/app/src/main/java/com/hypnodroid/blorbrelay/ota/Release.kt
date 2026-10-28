// A release on the site, checked before a byte of it goes near a badge: the
// manifest's shape (ota.js parseManifest), the owner's signature over it
// (docs/ota.md "Signing", tools/ble_ota.py verify_signature) and the image's
// size and SHA-256 (ota.js checkImage).
package com.hypnodroid.blorbrelay.ota

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonObject
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.SecureRandom
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.Base64

data class Manifest(
    val board: String,
    val version: String,
    val flags: Int,
    val fmt: Int,
    val size: Int,
    val sha256: String,
    val sig: String,
    val image: String,
)

class BadManifest(why: String) : Exception("bad manifest: $why")

private val SEMVER = Regex("^(\\d+)\\.(\\d+)\\.(\\d+)$")
private val SHA = Regex("^[A-Za-z0-9_-]{43}$")
private val SIG = Regex("^[A-Za-z0-9_-]{86}$")

fun semver(v: String?): List<Int>? =
    SEMVER.matchEntire(v ?: "")?.groupValues?.drop(1)?.map { it.toIntOrNull() ?: return null }

/** a is newer than b; false when either is not major.minor.patch (ota.js isNewer). */
fun isNewer(a: String?, b: String?): Boolean {
    val x = semver(a) ?: return false
    val y = semver(b) ?: return false
    for (i in 0 until 3) if (x[i] != y[i]) return x[i] > y[i]
    return false
}

/** The manifest comes off the network, so it is checked here and trusted after. */
fun parseManifest(text: String, board: String): Manifest {
    val m = try {
        Json.parseToJsonElement(text).jsonObject
    } catch (e: Exception) {
        throw BadManifest("not JSON")
    }
    return parseManifest(m, board)
}

fun parseManifest(m: JsonObject, board: String): Manifest {
    fun str(k: String): String? = (m[k] as? JsonPrimitive)?.takeIf { it.isString }?.content
    fun int(k: String): Int? = (m[k] as? JsonPrimitive)?.takeIf { !it.isString }?.intOrNull
    val b = str("board")
    val version = str("version")
    val flags = int("flags")
    val fmt = int("fmt")
    val size = int("size")
    val sha = str("sha256") ?: ""
    val sig = str("sig") ?: ""
    val image = str("image")
    if (b != board) throw BadManifest("board $b, not $board")
    if (semver(version) == null) throw BadManifest("version $version")
    if (flags == null || flags < 0) throw BadManifest("flags")
    if (fmt == null || fmt < 0) throw BadManifest("fmt")
    if (size == null || size <= 0) throw BadManifest("size")
    if (!SHA.matches(sha)) throw BadManifest("sha256")
    if (!SIG.matches(sig)) throw BadManifest("sig")
    if (image.isNullOrEmpty()) throw BadManifest("image")
    return Manifest(b, version!!, flags, fmt, size, sha, sig, image)
}

fun b64url(bytes: ByteArray): String = Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
fun unb64url(s: String): ByteArray = Base64.getUrlDecoder().decode(s)
fun sha256B64(bytes: ByteArray): String = b64url(MessageDigest.getInstance("SHA-256").digest(bytes))

/** The badge names an image by the first four bytes of its hash, in hex (STATUS `id=`). */
fun imageId(sha: String): String = unb64url(sha).copyOf(4).joinToString("") { "%02x".format(it) }

class ImageMismatch(why: String) : Exception(why)

/** Never push an image whose size or hash doesn't match the manifest. */
fun checkImage(m: Manifest, bytes: ByteArray) {
    if (bytes.size != m.size) throw ImageMismatch("size ${bytes.size}, want ${m.size}")
    if (sha256B64(bytes) != m.sha256) throw ImageMismatch("hash mismatch")
}

/** What the owner's key signs (docs/ota.md "Signing"). */
fun signedText(m: Manifest): String =
    "blorbota2 ${m.board} ${m.version} ${m.flags} ${m.fmt} ${m.size} ${m.sha256}"

/**
 * The public half of the owner's update key, as src/hw/ota_pubkey.h builds it
 * into every badge: an uncompressed P-256 point. Public by design; the badge
 * checks the same signature again at BEGIN.
 */
val OTA_PUBLIC_KEY: ByteArray = intArrayOf(
    0x04, 0xe4, 0x9c, 0xcb, 0x60, 0xc6, 0xe6, 0xbf, 0xc5, 0xec, 0x45, 0xb7, 0x89,
    0x5c, 0xa3, 0xf5, 0x58, 0xf4, 0x12, 0x2b, 0x32, 0x75, 0x80, 0xbd, 0x62, 0x1f,
    0x7a, 0x0a, 0x50, 0x56, 0x93, 0x69, 0xb7, 0x00, 0x94, 0x66, 0x36, 0x68, 0x39,
    0x7d, 0xa3, 0xdc, 0x61, 0x52, 0xad, 0xbb, 0x52, 0xb9, 0xa9, 0xc3, 0x67, 0xb3,
    0xb2, 0x20, 0x78, 0xe5, 0x92, 0xf8, 0x69, 0x6c, 0x4f, 0xf2, 0x46, 0xa2, 0x2f,
).map { it.toByte() }.toByteArray()

// SubjectPublicKeyInfo for an id-ecPublicKey on prime256v1, before the 65-byte point.
private val P256_SPKI_PREFIX = byteArrayOf(
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86.toByte(), 0x48, 0xce.toByte(), 0x3d, 0x02, 0x01,
    0x06, 0x08, 0x2a, 0x86.toByte(), 0x48, 0xce.toByte(), 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00,
)

private fun derInt(raw: ByteArray): ByteArray {
    var v = raw.dropWhile { it == 0.toByte() }.toByteArray()
    if (v.isEmpty()) v = byteArrayOf(0)
    if (v[0] < 0) v = byteArrayOf(0) + v
    return byteArrayOf(0x02, v.size.toByte()) + v
}

/** raw r || s (64 bytes) as the DER SEQUENCE java.security.Signature wants. */
fun rawSigToDer(raw: ByteArray): ByteArray {
    require(raw.size == 64) { "signature is ${raw.size} bytes" }
    val body = derInt(raw.copyOfRange(0, 32)) + derInt(raw.copyOfRange(32, 64))
    return byteArrayOf(0x30, body.size.toByte()) + body
}

/** True when the manifest's signature verifies against [point] (the badge's built-in key by default). */
fun verifySignature(m: Manifest, point: ByteArray = OTA_PUBLIC_KEY): Boolean = try {
    val key = KeyFactory.getInstance("EC").generatePublic(X509EncodedKeySpec(P256_SPKI_PREFIX + point))
    Signature.getInstance("SHA256withECDSA").run {
        initVerify(key)
        update(signedText(m).toByteArray(Charsets.US_ASCII))
        verify(rawSigToDer(unb64url(m.sig)))
    }
} catch (e: Exception) {
    false
}

/** One data write: the offset, 4 bytes little-endian, then n image bytes from it (docs/ota.md "Data writes"). */
fun packet(image: ByteArray, off: Int, n: Int): ByteArray {
    val p = ByteArray(4 + n)
    ByteBuffer.wrap(p).order(ByteOrder.LITTLE_ENDIAN).putInt(off)
    image.copyInto(p, 4, off, off + n)
    return p
}

/** BEGIN's line: the manifest's first seven fields and this phone's token. */
fun beginLine(m: Manifest, token: String): String =
    "OTA BEGIN ${m.board} ${m.version} ${m.flags} ${m.fmt} ${m.size} ${m.sha256} ${m.sig} $token"

/** A session token: 16 random bytes, base64url (22 characters). */
fun newToken(random: SecureRandom = SecureRandom()): String = b64url(ByteArray(16).also { random.nextBytes(it) })
