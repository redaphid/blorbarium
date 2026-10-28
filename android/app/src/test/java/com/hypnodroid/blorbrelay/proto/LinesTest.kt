package com.hypnodroid.blorbrelay.proto

import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.util.Base64

class LinesTest {
    @Test
    fun notificationsAreReassembledIntoLines() {
        val r = LineReader()
        // The board's MTU-3 pieces, then "\n" on its own (docs/ble.md "Framing").
        assertEquals(emptyList<String>(), r.feed("#2b + phase=creature gen=0 st".toByteArray()))
        assertEquals(emptyList<String>(), r.feed("age=baby".toByteArray()))
        assertEquals(listOf("#2b + phase=creature gen=0 stage=baby"), r.feed("\n".toByteArray()))
        // Several lines in one notification, a "\r" ignored, empty lines skipped.
        assertEquals(listOf("#2b OK", "! OTA at=4096"), r.feed("#2b OK\r\n\n! OTA at=4096\n#3".toByteArray()))
        assertEquals(listOf("#3 OK"), r.feed(" OK\n".toByteArray()))
    }

    @Test
    fun aCharacterSplitAcrossNotificationsSurvives() {
        val r = LineReader()
        val bytes = "#1 OK name=Grüngo\n".toByteArray()
        val cut = bytes.indexOf(0xc3.toByte()) + 1
        assertEquals(emptyList<String>(), r.feed(bytes.copyOfRange(0, cut)))
        assertEquals(listOf("#1 OK name=Grüngo"), r.feed(bytes.copyOfRange(cut, bytes.size)))
    }

    @Test
    fun linesParse() {
        assertEquals(Incoming.Reply(0x1a, "OK", "fw=0.4.0 fmt=1"), parseLine("#1a OK fw=0.4.0 fmt=1"))
        assertEquals(Incoming.Reply(0x2b, "+", "drives hunger=301"), parseLine("#2b + drives hunger=301"))
        assertEquals(Incoming.Reply(5, "ERR", "404 UNKNOWN_VERB"), parseLine("#5 ERR 404 UNKNOWN_VERB"))
        assertEquals(Incoming.Reply(7, "OK", ""), parseLine("#7 OK"))
        assertEquals(Incoming.Event("OTA at=4096 gap"), parseLine("! OTA at=4096 gap"))
        assertEquals(null, parseLine("[hw] ble conn h=1"))
        val k = kv("state=idle board=lcd128 fw=0.4.0 chunk=240 trial=0 last=none rolledback=none")
        assertEquals("lcd128", k["board"])
        assertEquals("240", k["chunk"])
        assertEquals("none", k["rolledback"])
        assertEquals(emptyMap<String, String>(), kv(null))
    }

    @Test
    fun badgeErrorsKeepTheCodeAndTheText() {
        val e = BadgeError.parse("409 TRIAL")
        assertEquals(409, e.code)
        assertEquals("409 TRIAL", e.message)
        assertEquals("TRIAL", e.reason)
    }

    /** A pretend badge on the far side of the link: it answers each request line written. */
    private class Wire(val answer: (String) -> List<String>) {
        lateinit var link: Link
        val written = ArrayList<String>()
        suspend fun write(text: String) {
            written += text
            val line = text.trimEnd('\n')
            for (reply in answer(line)) link.receive(reply)
        }
    }

    @Test
    fun aRequestGetsItsContinuationLinesAndItsOk() = runTest {
        val events = ArrayList<String>()
        val wire = Wire { line ->
            val id = line.substringBefore(' ')
            listOf("$id + phase=creature", "! STATE hunger=1", "#99 OK not mine", "$id + drives x=1", "$id OK")
        }
        wire.link = Link(wire::write, { events += it })
        val r = wire.link.send("STATE")
        assertEquals(listOf("phase=creature", "drives x=1"), r.lines)
        assertEquals("", r.ok)
        assertEquals(listOf("STATE hunger=1"), events)
        assertEquals(listOf("#1 STATE\n"), wire.written)
        wire.link.send("HELLO")
        assertEquals("#2 HELLO\n", wire.written[1])
    }

    @Test
    fun anErrorIsABadgeErrorAndHashZeroAnswersTheRequestInFlight() = runTest {
        val wire = Wire { line -> if (line.contains("BAD")) listOf("#0 ERR 400 BAD_ID") else listOf("${line.substringBefore(' ')} ERR 409 NOT_NOW") }
        wire.link = Link(wire::write)
        val e1 = try {
            wire.link.send("ADOPT"); null
        } catch (e: BadgeError) {
            e
        }
        assertEquals(409, e1!!.code)
        val e2 = try {
            wire.link.send("BAD"); null
        } catch (e: BadgeError) {
            e
        }
        assertEquals(400, e2!!.code)
    }

    @Test
    fun aSilentBadgeTimesOutAndALongLineIsRefused() = runTest {
        val wire = Wire { emptyList() }
        wire.link = Link(wire::write)
        try {
            wire.link.send("HELLO", 1000); fail()
        } catch (e: LinkError) {
            assertEquals("TIMEOUT", e.message)
        }
        try {
            wire.link.send("TWIST say " + "A".repeat(200)); fail()
        } catch (e: LinkError) {
            assertEquals("TOO_LONG", e.message)
        }
    }

    @Test
    fun requestsGoOneAtATimeAndADropFailsTheOneInFlight() = runTest {
        val wire = Wire { emptyList() }
        val link = Link(wire::write)
        wire.link = link
        val a = async { runCatching { link.send("HELLO") } }
        val b = async { runCatching { link.send("STATE") } }
        delay(10)
        assertEquals("the second waits for the first", 1, wire.written.size)
        link.failAll("disconnected")
        assertEquals("disconnected", a.await().exceptionOrNull()!!.message)
        delay(10)
        link.receive("#2 OK")
        assertTrue(b.await().isSuccess)
    }

    @Test
    fun chunkedRepliesAssembleAndCheckTheirCrc() {
        val bytes = ByteArray(300) { (it * 13).toByte() }
        val lines = (0 until 300 step 120).map { at ->
            "$at " + Base64.getEncoder().encodeToString(bytes.copyOfRange(at, minOf(300, at + 120)))
        }
        val ok = "300 ${crc32Hex(bytes)}"
        assertArrayEquals(bytes, assembleChunked(Reply(ok, lines)))
        assertThrows(LinkError::class.java) { assembleChunked(Reply(ok, lines.drop(1))) }
        assertThrows(LinkError::class.java) { assembleChunked(Reply("300 00000000", lines)) }
        assertEquals(64, sha256Hex(bytes).length)
    }

    @Test
    fun eventsArriveWhileWaiting() = runTest {
        val events = ArrayList<String>()
        lateinit var link: Link
        link = Link({ line ->
            val id = line.substringBefore(' ')
            launch { delay(5); link.receive("! OTA at=4096"); link.receive("$id OK state=receiving") }
        }, { events += it })
        assertEquals("state=receiving", link.send("OTA STATUS").ok)
        assertEquals(listOf("OTA at=4096"), events)
    }
}
