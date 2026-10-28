// The updater against the fake badge, the cases of web/test/ota.test.mjs, on a
// virtual clock (a whole update with its waits runs in milliseconds).
package com.hypnodroid.blorbrelay.ota

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class UpdaterTest {
    /** Token storage as one phone has it: a killed and restarted app finds its tokens. */
    class Profile : TokenStorage {
        val m = HashMap<String, String>()
        override fun get(key: String) = m[key]
        override fun put(key: String, value: String) {
            m[key] = value
        }
    }

    private class Run(val outcome: Outcome, val states: List<String>, val port: FakePort, val tokens: (String) -> String)

    private suspend fun TestScope.run(
        dish: FakeBadge,
        release: Release,
        storage: Profile = Profile(),
        press: Boolean = true,
        closeAfter: String? = null,
        trialWaitMs: Long = Updater.TRIAL_WAIT_MS,
        hear: Boolean = true,
        onState: (String) -> Unit = {},
    ): Run {
        val port = FakePort(dish, closeAfter)
        val tokens = tokenStore(DEVICE, storage)
        val states = ArrayList<String>()
        val bg: CoroutineScope = backgroundScope
        val updater = Updater(port, release, tokens, onChange = { p ->
            if (states.lastOrNull() != p.state) states += p.state
            if (p.state == "confirm" && press) bg.launch { delay(50); dish.press() }
            onState(p.state)
        }, trialWaitMs = trialWaitMs, now = { testScheduler.currentTime })
        if (hear) dish.listener = updater
        val outcome = updater.run()
        return Run(outcome, states, port, tokens)
    }

    private fun TestScope.badge(trialMs: Long = 300, confirmMs: Long = 30000, faults: Set<String> = emptySet(), fw: String = "0.3.0") =
        FakeBadge(backgroundScope, { testScheduler.currentTime }, fw = fw, trialMs = trialMs, confirmMs = confirmMs, faults = faults)

    private fun why(r: Run) = "states ${r.states}\n" + r.port.transcript.joinToString("\n").take(4000)

    /** Waits out the badge's restart and its trial verdict. */
    private suspend fun settled(dish: FakeBadge) {
        dish.booting?.join()
        delay(dish.trialMs + 100)
        dish.booting?.join()
    }

    @Test
    fun anUpdateSurvivesADropLostWritesAndALostAckThenRestartsIntoTheNewVersion() = runTest {
        val release = mockRelease()
        val dish = badge(faults = setOf("drop", "gaps", "lostAck"))
        val r = run(dish, release)
        assertEquals(why(r), Outcome.done("updated", "0.4.0"), r.outcome)
        assertEquals("the badge hashed the bytes it wrote", release.manifest.sha256, dish.stats.flashed)
        assertEquals(1, dish.stats.drops)
        assertEquals(3, dish.stats.lostWrites)
        assertTrue("a lost write raised a gap event", dish.stats.gaps >= 1)
        assertEquals(1, dish.stats.lostAcks)
        assertEquals("only the lost at= needed a STATUS poll", 1, dish.stats.polls)
        assertEquals(1, dish.stats.reboots)
        assertEquals("the phone told the new image it got through", 1, dish.stats.confirms)
        assertTrue("resumed after the drop: ${r.states}", "resuming" in r.states)
        assertEquals(listOf("finishing", "rebooting", "verifying", "trial", "done"), r.states.takeLast(5))
        val token = r.tokens("0.4.0")
        assertTrue(Regex("^[A-Za-z0-9_-]{22}$").matches(token))
        val m = release.manifest
        val begin = r.port.sent.first { it.startsWith("OTA BEGIN ") }
        assertEquals("OTA BEGIN lcd128 0.4.0 0 1 ${m.size} ${m.sha256} ${m.sig} $token", begin)
        assertTrue("BEGIN fits the 200-byte line", "#fff $begin".length <= 200)
        assertTrue("OTA REBOOT $token" in r.port.sent && "OTA CONFIRM $token" in r.port.sent)
        // The transcript reads like the protocol: ids, the request, then its reply.
        assertTrue(r.port.transcript[0].matches(Regex("^#1 OTA BEGIN .*")))
        assertTrue(r.port.transcript[1].startsWith("#1 OK state=confirm at=0 size=${m.size} id=${imageId(m.sha256)} chunk=240"))
    }

    @Test
    fun nobodyPressesTheButton() = runTest {
        val dish = badge(confirmMs = 300)
        val r = run(dish, mockRelease(), press = false)
        assertEquals(why(r), Outcome.failed("no_confirm"), r.outcome)
        assertEquals("0.3.0", dish.fw)
        assertEquals(0, dish.stats.reboots)
    }

    @Test
    fun aByteCorruptedOnTheWayInEndsInBadHashAndNoRestart() = runTest {
        val dish = badge(faults = setOf("corrupt"))
        val r = run(dish, mockRelease())
        assertEquals(why(r), Outcome.failed("bad_hash"), r.outcome)
        assertEquals(null, dish.stats.flashed)
        assertEquals(0, dish.stats.reboots)
        assertEquals("0.3.0", dish.fw)
    }

    @Test
    fun aNewImageThatDoesNotKeepItselfReportsTheRollback() = runTest {
        val dish = badge(faults = setOf("rollback"))
        val r = run(dish, mockRelease())
        assertEquals(why(r), Outcome.done("rolled_back", "0.3.0"), r.outcome)
        assertEquals("into the new image on trial, then back to the old one", 2, dish.stats.reboots)
        val (status, _) = checkTrial(r.port, r.tokens)
        assertEquals("STATUS names the version that went back", "0.4.0", status["rolledback"])
    }

    @Test
    fun anAppKilledAfterRebootConfirmsWithItsStoredTokenAtTheNextSync() = runTest {
        val release = mockRelease()
        val dish = badge(trialMs = 1500)
        val storage = Profile()
        val first = run(dish, release, storage = storage, closeAfter = "OTA REBOOT")
        assertEquals("the first sync ended before the trial", Outcome.failed("unreachable"), first.outcome)
        assertEquals(0, dish.stats.confirms)
        dish.reconnect()
        val again = FakePort(dish)
        val check = checkTrial(again, tokenStore(DEVICE, storage))
        assertEquals("1", check.status["trial"])
        assertEquals("0.4.0", check.status["fw"])
        assertEquals("confirmed", check.confirm)
        settled(dish)
        assertEquals("kept the new version", "0.4.0", dish.fw)
        assertFalse(dish.trial)
        assertEquals(1, dish.stats.reboots)
    }

    @Test
    fun anotherPhoneCannotConfirmAndTheImageRollsBack() = runTest {
        val dish = badge(trialMs = 1500)
        run(dish, mockRelease(), closeAfter = "OTA REBOOT")
        dish.reconnect()
        val stranger = FakePort(dish)
        val strangerTokens = tokenStore(DEVICE, Profile())
        val check = checkTrial(stranger, strangerTokens)
        assertEquals("not_yours", check.confirm)
        settled(dish)
        assertEquals("rolled back without its own phone's CONFIRM", "0.3.0", dish.fw)
        dish.reconnect()
        assertEquals("0.4.0", checkTrial(stranger, strangerTokens).status["rolledback"])
    }

    @Test
    fun beginWhileAnEarlierUpdateIsOnTrialConfirmsItWaitsItOutThenUpdates() = runTest {
        val dish = badge(trialMs = 1500)
        val storage = Profile()
        run(dish, mockRelease(version = "0.3.5", size = 128 * 1024), storage = storage, closeAfter = "OTA REBOOT")
        dish.reconnect()
        assertEquals("0.3.5", dish.fw)
        assertTrue(dish.trial)
        val r = run(dish, mockRelease(), storage = storage)
        assertEquals(why(r), Outcome.done("updated", "0.4.0"), r.outcome)
        assertEquals(listOf("checking", "begin", "trial", "begin", "confirm"), r.states.take(5))
        assertTrue("OTA CONFIRM ${r.tokens("0.3.5")}" in r.port.sent)
        assertEquals(2, dish.stats.confirms)
        assertEquals(2, dish.stats.reboots)
        assertEquals("none", dish.rolledback)
    }

    @Test
    fun anUpdateStillOnTrialAtTheWaitsEndIsUnconfirmedNeverUpdated() = runTest {
        val dish = badge(trialMs = 4000)
        val r = run(dish, mockRelease(), trialWaitMs = 500)
        assertEquals(why(r), Outcome.done("unconfirmed", "0.4.0"), r.outcome)
        assertTrue("still on trial when the phone gave up waiting", dish.trial)
        assertEquals(1, dish.stats.confirms)
    }

    @Test
    fun anImageThatDoesNotMatchItsManifestNeverReachesTheBadge() = runTest {
        val good = mockRelease()
        val bad = Release(good.manifest) { good.image().also { it[1000] = (it[1000].toInt() xor 1).toByte() } }
        val dish = badge()
        val r = run(dish, bad)
        assertEquals(Outcome.failed("download"), r.outcome)
        assertTrue("not even a BEGIN: ${r.port.sent}", r.port.sent.isEmpty())
        val short = Release(good.manifest) { good.image().copyOf(good.manifest.size - 1) }
        assertEquals(Outcome.failed("download"), run(dish, short).outcome)
    }

    @Test
    fun anUpToDateBadgeEndsAtBegin() = runTest {
        val dish = badge(fw = "0.4.0")
        val r = run(dish, mockRelease())
        assertEquals(Outcome.done("up_to_date", null), r.outcome)
        assertEquals(0, dish.stats.reboots)
    }

    @Test
    fun anotherPhonesBeginOrAbortDuringASessionGetsNotYoursAndTheTransferCarriesOn() = runTest {
        val release = mockRelease()
        val other = mockRelease(version = "0.4.1", size = 128 * 1024)
        val dish = badge()
        val ownerStorage = Profile()
        FakePort(dish).send(beginLine(release.manifest, tokenStore(DEVICE, ownerStorage)("0.4.0")))
        dish.press()
        val same = run(dish, release, press = false, hear = false)
        assertEquals("the same image with another token", Outcome.failed("NOT_YOURS"), same.outcome)
        val diff = run(dish, other, press = false, hear = false)
        assertEquals("another image: its ABORT was refused", Outcome.failed("NOT_YOURS"), diff.outcome)
        assertTrue("it asked once, with its own token", "OTA ABORT ${diff.tokens("0.4.1")}" in diff.port.sent)
        assertEquals("receiving", dish.state)
        // The owner comes back with its token: the same BEGIN resumes the session, and it finishes.
        val r = run(dish, release, storage = ownerStorage)
        assertEquals(why(r), Outcome.done("updated", "0.4.0"), r.outcome)
        assertEquals(release.manifest.sha256, dish.stats.flashed)
        assertEquals(1, dish.stats.reboots)
    }

    @Test
    fun aTokenIsSixteenRandomBytesOnePerBadgeAndVersionKept() {
        val storage = Profile()
        val a = tokenStore(DEVICE, storage)
        val t = a("0.4.0")
        assertTrue(Regex("^[A-Za-z0-9_-]{22}$").matches(t))
        assertEquals("a restart reuses it", t, tokenStore(DEVICE, storage)("0.4.0"))
        assertTrue(a("0.4.1") != t)
        assertTrue(tokenStore("AA:BB:CC:DD:EE:FF", storage)("0.4.0") != t)
        val broken = object : TokenStorage {
            override fun get(key: String): String = throw IllegalStateException("denied")
            override fun put(key: String, value: String) = throw IllegalStateException("denied")
        }
        val b = tokenStore("x", broken)
        assertEquals("without storage it lasts as long as the store", b("0.4.0"), b("0.4.0"))
    }

    private companion object {
        const val DEVICE = "D4:05:92:77:1B:45"
    }
}
