package com.hypnodroid.blorbrelay.sync

import com.hypnodroid.blorbrelay.sync.Policy.HOUR
import com.hypnodroid.blorbrelay.sync.Policy.MINUTE
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.jsonObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PolicyTest {
    private val now = 1_800_000_000_000L

    @Test
    fun aBadgeInRangeIsSyncedEveryFewHoursAndSoonerAfterAProblem() {
        val never = BadgeRecord("A")
        assertTrue(Policy.dueOnSight(never, 0, now))
        val fine = BadgeRecord("A", lastAttempt = now - 2 * HOUR, lastOk = true)
        assertFalse(Policy.dueOnSight(fine, now - MINUTE, now))
        assertTrue(Policy.dueOnSight(fine.copy(lastAttempt = now - 3 * HOUR), now - MINUTE, now))
        val failed = BadgeRecord("A", lastAttempt = now - 21 * MINUTE, lastOk = false)
        assertTrue(Policy.dueOnSight(failed, now - MINUTE, now))
        assertFalse(Policy.dueOnSight(failed.copy(lastAttempt = now - 5 * MINUTE), now - MINUTE, now))
    }

    @Test
    fun aBadgeBackInRangeIsSyncedAtOnce() {
        val r = BadgeRecord("A", lastAttempt = now - 40 * MINUTE, lastOk = true)
        assertTrue("away an hour", Policy.dueOnSight(r, now - HOUR, now))
        assertFalse("seen a moment ago", Policy.dueOnSight(r, now - 10_000, now))
        assertFalse("synced just now", Policy.dueOnSight(r.copy(lastAttempt = now - MINUTE), now - HOUR, now))
    }

    @Test
    fun noUnattendedAskInTheNight() {
        assertTrue(Policy.quietHour(23))
        assertTrue(Policy.quietHour(3))
        assertFalse(Policy.quietHour(7))
        assertFalse(Policy.quietHour(16))
        assertTrue(Policy.quietHour(21))
    }

    @Test
    fun petLinesBodyIsWhatTheBadgeSends() {
        val b = petLinesBody("d40592771b44", "424c5247", 2, 3, "0.7.30")
        assertEquals("""{"mac":"d40592771b44","genome":"424c5247","stage":2,"generation":3,"fw":"0.7.30","wait":false}""", b.toString())
        assertEquals("d40592771b44", macHex("D4:05:92:77:1B:44"))
        assertNull(macHex("none"))
        assertEquals(1, stageIndex("child"))
        assertNull(stageIndex("-"))
    }

    @Test
    fun statsJsonBecomesTheSitesUpload() {
        val doc = Json.parseToJsonElement(
            """{"schema":"blorb.life-stats","v":1,"fw":"0.7.12","lineage":"00d15400000b","gen":3,"seq":118,
               "life":{"presses":40,"death":null},"parent":{"gen":2,"life":{"presses":9}},
               "analytics":{"powered_s":86532,"closed_upto":1,"buckets":[]}}""",
        ).jsonObject
        val b = statsBody("d40592771b44", "ab".repeat(32), "0.7.30", doc, now)
        assertEquals("d40592771b44", (b["mac"] as JsonPrimitive).content)
        assertEquals("0.7.12", (b["fw"] as JsonPrimitive).content)
        assertEquals(1, (b["stats_version"] as JsonPrimitive).content.toInt())
        assertEquals(doc["life"], b["stats"])
        assertEquals(doc["parent"], (b["parents"] as JsonObject)["parent"])
        assertNull("absent for a founding line", (b["parents"] as JsonObject)["grandparent"])
        assertEquals(doc["analytics"], b["analytics"])
        assertEquals(now.toString(), (b["period_end"] as JsonPrimitive).content)
    }

    @Test
    fun onlyTheSitesAckLetsTheBadgeDropItsBuckets() {
        assertEquals(3, statsAck(Json.parseToJsonElement("""{"ack":3}""")))
        assertNull("today's site answers without one", statsAck(Json.parseToJsonElement("""{"ok":true,"stats_id":5}""")))
        assertNull(statsAck(Json.parseToJsonElement("""{"ack":"3"}""")))
        assertNull(statsAck(null))
    }
}
