package com.hypnodroid.blorbrelay.ota

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class ReleaseTest {
    // The site's manifest for 0.7.30 as it was published, signed by the owner's key.
    private val live = """
        {
         "board": "lcd128",
         "version": "0.7.30",
         "flags": 0,
         "size": 2899360,
         "sha256": "77kbNEpRekw7ltcMXztc9JsUg9sVawhFhkk7_aUSLoQ",
         "sig": "BbVwfjGjhOnRCjo1dACa6GwyUr6AVzQr65J-wazF-mMJLP_cW5oQe45q7linS7SBn5X9kv20XIOD6dq6NM4Z_A",
         "image": "blorbarium-lcd128-0.7.30.bin",
         "fmt": 1
        }
    """.trimIndent()

    @Test
    fun theLiveManifestParsesAndItsSignatureVerifiesAgainstTheBadgesKey() {
        val m = parseManifest(live, "lcd128")
        assertEquals("0.7.30", m.version)
        assertEquals(2899360, m.size)
        assertEquals("blorbota2 lcd128 0.7.30 0 1 2899360 77kbNEpRekw7ltcMXztc9JsUg9sVawhFhkk7_aUSLoQ", signedText(m))
        assertTrue(verifySignature(m))
    }

    @Test
    fun anyChangedFieldBreaksTheSignature() {
        val m = parseManifest(live, "lcd128")
        assertFalse(verifySignature(m.copy(version = "0.7.31")))
        assertFalse(verifySignature(m.copy(size = m.size + 1)))
        assertFalse(verifySignature(m.copy(flags = 1)))
        assertFalse(verifySignature(m.copy(fmt = 2)))
        assertFalse(verifySignature(m.copy(sha256 = "A" + m.sha256.drop(1))))
        assertFalse(verifySignature(m.copy(sig = "A".repeat(86))))
        // The image name is not signed: the hash binds the bytes whatever they are called.
        assertTrue(verifySignature(m.copy(image = "renamed.bin")))
    }

    @Test
    fun aBadManifestIsRefused() {
        val ok = parseManifest(live, "lcd128")
        assertThrows(BadManifest::class.java) { parseManifest(live, "badge128") }
        assertThrows(BadManifest::class.java) { parseManifest("not json", "lcd128") }
        assertThrows(BadManifest::class.java) { parseManifest(live.replace("\"0.7.30\"", "\"0.7\""), "lcd128") }
        assertThrows(BadManifest::class.java) { parseManifest(live.replace("2899360", "\"2899360\""), "lcd128") }
        assertThrows(BadManifest::class.java) { parseManifest(live.replace("2899360", "0"), "lcd128") }
        assertThrows(BadManifest::class.java) { parseManifest(live.replace("\"flags\": 0", "\"flags\": -1"), "lcd128") }
        assertThrows(BadManifest::class.java) { parseManifest(live.replace(ok.sha256, ok.sha256.drop(1)), "lcd128") }
        assertThrows(BadManifest::class.java) { parseManifest(live.replace(ok.sig, ok.sig + "A"), "lcd128") }
        assertThrows(BadManifest::class.java) { parseManifest(live.replace("blorbarium-lcd128-0.7.30.bin", ""), "lcd128") }
    }

    @Test
    fun versionsCompareAsNumbers() {
        assertTrue(isNewer("0.7.30", "0.7.29"))
        assertTrue(isNewer("0.7.30", "0.7.9"))
        assertTrue(isNewer("1.0.0", "0.99.99"))
        assertFalse(isNewer("0.7.30", "0.7.30"))
        assertFalse(isNewer("0.7.29", "0.7.30"))
        assertFalse("not a version: never newer", isNewer("0.8", "0.7.30"))
        assertFalse(isNewer("0.8.0", null))
        assertFalse(isNewer("0.8.0-rc1", "0.7.0"))
    }

    @Test
    fun theImageMustMatchItsSizeAndHash() {
        val bytes = ByteArray(1000) { (it * 7).toByte() }
        val m = Manifest("lcd128", "0.4.0", 0, 1, 1000, sha256B64(bytes), "A".repeat(86), "x.bin")
        checkImage(m, bytes)
        assertThrows(ImageMismatch::class.java) { checkImage(m, bytes.copyOf(999)) }
        assertThrows(ImageMismatch::class.java) { checkImage(m, bytes.copyOf().also { it[5] = 0 }) }
    }

    @Test
    fun theBadgeNamesAnImageByItsHashsFirstFourBytes() {
        assertEquals("efb91b34", imageId("77kbNEpRekw7ltcMXztc9JsUg9sVawhFhkk7_aUSLoQ"))
    }

    @Test
    fun aPacketIsALittleEndianOffsetThenTheBytes() {
        val image = ByteArray(300_000) { it.toByte() }
        val p = packet(image, 0x012345, 240)
        assertEquals(244, p.size)
        assertArrayEquals(byteArrayOf(0x45, 0x23, 0x01, 0x00), p.copyOf(4))
        assertArrayEquals(image.copyOfRange(0x012345, 0x012345 + 240), p.copyOfRange(4, 244))
    }

    @Test
    fun chunksCoverTheImageExactlyOnceInOrder() {
        // As the sender cuts it: chunk-sized packets from 0, the last one short.
        val size = 10_000
        val image = ByteArray(size) { (it * 31).toByte() }
        val out = ByteArray(size)
        var off = 0
        var writes = 0
        while (off < size) {
            val n = minOf(CHUNK, size - off)
            val p = packet(image, off, n)
            val at = (p[0].toInt() and 255) or ((p[1].toInt() and 255) shl 8) or ((p[2].toInt() and 255) shl 16)
            assertEquals(off, at)
            p.copyInto(out, at, 4, p.size)
            off += n
            writes++
        }
        assertArrayEquals(image, out)
        assertEquals((size + CHUNK - 1) / CHUNK, writes)
    }

    @Test
    fun beginCarriesTheManifestAndTheTokenInUnder200Bytes() {
        val m = parseManifest(live, "lcd128")
        val token = newToken()
        assertEquals(22, token.length)
        val line = "#ffffff ${beginLine(m, token)}"
        assertTrue("${line.length} bytes", line.length <= 200)
        assertEquals("OTA BEGIN lcd128 0.7.30 0 1 2899360 ${m.sha256} ${m.sig} $token", beginLine(m, token))
    }

    @Test
    fun rawSignaturesBecomeDer() {
        // A high bit in r gets a leading zero; leading zeros in s are dropped.
        val raw = ByteArray(64).also { it[0] = 0x80.toByte(); it[33] = 0x01 }
        val der = rawSigToDer(raw)
        assertEquals(0x30, der[0].toInt())
        assertEquals(der.size - 2, der[1].toInt())
        assertEquals(0x02, der[2].toInt())
        assertEquals(33, der[3].toInt())
        assertEquals(0, der[4].toInt())
    }
}
