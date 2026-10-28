// One badge at a time (each badge serves one phone at a time, and one GATT
// link at a time is what Android does best): a queue of badges to sync, fed by
// the scanner's sightings, the periodic worker and the Sync now button.
package com.hypnodroid.blorbrelay.sync

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import android.os.PowerManager
import com.hypnodroid.blorbrelay.ble.Uuids
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

object SyncEngine {
    private data class Job(val address: String, val force: Boolean)

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val queue = Channel<Job>(Channel.UNLIMITED)
    private val queued = HashSet<String>()
    private val forced = HashSet<String>()   // queued without force, then Sync now asked for it
    private var started = false
    private lateinit var app: Context
    lateinit var store: Store
        private set

    private val _busy = MutableStateFlow<String?>(null)
    /** The address being synced now, or null. */
    val busy: StateFlow<String?> = _busy
    private val _pending = MutableStateFlow(0)

    @Synchronized
    fun init(ctx: Context) {
        if (started) return
        app = ctx.applicationContext
        store = Store(app)
        started = true
        scope.launch { drain() }
    }

    /** Sync this badge when its turn comes; a badge already waiting is not queued twice. */
    fun request(address: String, force: Boolean = false) {
        synchronized(queued) {
            if (!queued.add(address)) {
                if (force) forced.add(address)
                return
            }
            _pending.value = queued.size
        }
        RelayLog.say("[$address] queued${if (force) " (Sync now)" else ""}")
        queue.trySend(Job(address, force))
    }

    /** Suspends until nothing is queued or running. */
    suspend fun idle() {
        _pending.first { it == 0 }
    }

    private suspend fun drain() {
        for (job in queue) {
            _busy.value = job.address
            val wake = (app.getSystemService(Context.POWER_SERVICE) as PowerManager)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "blorbrelay:sync")
            try {
                wake.acquire(15 * Policy.MINUTE)
                val force = job.force || synchronized(queued) { forced.remove(job.address) }
                BadgeSync(app, store, job.address, force).run()
            } catch (e: kotlinx.coroutines.CancellationException) {
                throw e
            } catch (e: Exception) {
                RelayLog.say("[${job.address}] sync crashed: $e")
            } finally {
                if (wake.isHeld) wake.release()
                _busy.value = null
                synchronized(queued) {
                    queued.remove(job.address)
                    forced.remove(job.address)
                    _pending.value = queued.size
                }
            }
        }
    }

    /** The scanner saw a badge advertising the UART service. */
    fun seen(address: String, name: String?, rssi: Int, autoSync: Boolean = true) {
        val now = System.currentTimeMillis()
        val before = store.get(address)
        // A badge advertises every few seconds: note it at most every 30 s unless its name changed.
        val recent = now - before.lastSeen < 30_000 && (name.isNullOrBlank() || name == before.name)
        val r = if (recent) before else store.update(address) {
            it.copy(name = name?.takeIf { n -> n.isNotBlank() } ?: it.name, rssi = rssi, lastSeen = now)
        }
        if (autoSync && Policy.dueOnSight(r, before.lastSeen, now)) request(address)
    }

    /** Badges advertising the UART service, heard for ms: address to (name, rssi). */
    @SuppressLint("MissingPermission")
    suspend fun scan(ms: Long): Map<String, Pair<String?, Int>> {
        val scanner = (app.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
            ?.takeIf { it.isEnabled }?.bluetoothLeScanner ?: run {
            RelayLog.say("scan: Bluetooth is off")
            return emptyMap()
        }
        val found = HashMap<String, Pair<String?, Int>>()
        val cb = object : ScanCallback() {
            override fun onScanResult(type: Int, r: ScanResult) {
                synchronized(found) { found[r.device.address] = (r.scanRecord?.deviceName ?: r.device.name) to r.rssi }
            }

            override fun onBatchScanResults(results: MutableList<ScanResult>) = results.forEach { onScanResult(0, it) }
            override fun onScanFailed(errorCode: Int) = RelayLog.say("scan failed ($errorCode)")
        }
        try {
            scanner.startScan(listOf(nusFilter()), ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), cb)
            delay(ms)
        } catch (e: SecurityException) {
            RelayLog.say("scan: no Bluetooth permission")
        } finally {
            try {
                scanner.stopScan(cb)
            } catch (_: Exception) {
            }
        }
        return synchronized(found) { HashMap(found) }
    }

    /** Scan, then sync every badge heard (force: whatever the schedule says), and wait for them all. */
    suspend fun scanAndSync(ms: Long, force: Boolean) {
        val heard = scan(ms)
        RelayLog.say("scan: ${heard.size} badge(s) in range")
        for ((address, v) in heard.entries.sortedByDescending { it.value.second }) {
            seen(address, v.first, v.second, autoSync = !force)
            if (force) request(address, force = true)
        }
        idle()
    }

    /** The Sync now button: scan, then sync every badge heard whatever the schedule says. Outlives the screen. */
    fun syncNow() {
        scope.launch { scanAndSync(15_000, force = true) }
    }

    fun nusFilter(): ScanFilter = ScanFilter.Builder().setServiceUuid(ParcelUuid(Uuids.NUS)).build()
}
