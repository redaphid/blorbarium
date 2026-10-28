// When to sync, and the bodies the site's endpoints take. Pure, so the JVM tests cover it.
package com.hypnodroid.blorbrelay.sync

import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put

object Policy {
    const val MINUTE = 60_000L
    const val HOUR = 60 * MINUTE

    const val OK_EVERY = 3 * HOUR          // a badge in range is synced again this often
    const val RETRY_EVERY = 20 * MINUTE    // ... or this soon after a sync that failed
    const val BACK_IN_RANGE = 15 * MINUTE  // a badge back after this long away is synced at once (if not synced this recently)
    const val NO_CONFIRM_BACKOFF = 3 * HOUR // nobody turned it: don't ask again before this (Sync now always asks)
    const val BUSY_BACKOFF = 20 * MINUTE
    const val LINES_EVERY = 12 * HOUR
    const val STATS_EVERY = 6 * HOUR
    const val DEBUG_EVERY = 1 * HOUR

    /** Sync a badge the scanner just saw? prevSeen: when it was seen before this sighting. */
    fun dueOnSight(r: BadgeRecord, prevSeen: Long, now: Long): Boolean {
        if (r.lastAttempt == 0L) return true
        val since = now - r.lastAttempt
        if (since >= (if (r.lastOk) OK_EVERY else RETRY_EVERY)) return true
        return prevSeen != 0L && now - prevSeen >= BACK_IN_RANGE && since >= BACK_IN_RANGE
    }

    /**
     * An unattended update makes the badge's screen ask a child to turn it, so the
     * phone never starts one in the night (21:00 to 07:00, the phone's time). A
     * transfer already under way carries on, and Sync now asks whatever the hour.
     */
    fun quietHour(hour: Int): Boolean = hour >= 21 || hour < 7
}

private val STAGES = listOf("baby", "child", "adult", "elder")

/** STATE's stage name as the number the badge itself posts (blorb::Stage). */
fun stageIndex(name: String?): Int? = STAGES.indexOf(name ?: "").takeIf { it >= 0 }

/** "d4:05:92:77:1b:44" as the site's 12 hex digits, or null. */
fun macHex(mac: String?): String? = mac?.lowercase()?.replace(Regex("[^0-9a-f]"), "")?.takeIf { it.length == 12 }

/**
 * POST /api/pet-lines as the badge's own Wi-Fi window sends it (src/main.cpp
 * petLinesBody): mac, genome hex, stage, generation, fw, wait:false.
 */
fun petLinesBody(mac: String, genomeHex: String, stage: Int, generation: Int, fw: String): JsonObject = buildJsonObject {
    put("mac", mac)
    put("genome", genomeHex)
    put("stage", stage)
    put("generation", generation)
    put("fw", fw)
    put("wait", false)
}

/**
 * POST /api/stats from a `STATS json` document (docs/life-stats.md "Upload paths",
 * Bluetooth): the life as stats, parent and grandparent as parents, the analytics
 * as they are, with the badge's identity the phone read.
 */
fun statsBody(mac: String, genomeHash: String?, fw: String, doc: JsonObject, nowMs: Long): JsonObject = buildJsonObject {
    put("mac", mac)
    if (genomeHash != null) put("genome_hash", genomeHash)
    put("fw", (doc["fw"] as? JsonPrimitive)?.content ?: fw)
    put("stats_version", (doc["v"] as? JsonPrimitive)?.content?.toIntOrNull() ?: 1)
    doc["life"]?.takeIf { it is JsonObject }?.let { put("stats", it) }
    val parents = buildJsonObject {
        for (k in listOf("parent", "grandparent")) doc[k]?.takeIf { it !is JsonNull }?.let { put(k, it) }
    }
    if (parents.isNotEmpty()) put("parents", parents)
    doc["analytics"]?.takeIf { it is JsonObject }?.let { put("analytics", it) }
    put("period_end", nowMs)
}

/** The site's `ack` (the closed buckets it stored), if its reply names one; only then may the badge drop them. */
fun statsAck(reply: JsonElement?): Int? =
    ((reply as? JsonObject)?.get("ack") as? JsonPrimitive)?.takeIf { !it.isString }?.content?.toIntOrNull()
