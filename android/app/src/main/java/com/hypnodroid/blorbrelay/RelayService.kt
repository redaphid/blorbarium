// The foreground service: a small persistent notification, and a low-power
// scan filtered on the badges' UART service that keeps running while the
// screen is off. Each sighting may queue a sync (Policy.dueOnSight).
package com.hypnodroid.blorbrelay

import android.annotation.SuppressLint
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import com.hypnodroid.blorbrelay.sync.RelayLog
import com.hypnodroid.blorbrelay.sync.SyncEngine
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

class RelayService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var scanning = false

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(type: Int, r: ScanResult) {
            @SuppressLint("MissingPermission")
            val name = r.scanRecord?.deviceName ?: try { r.device.name } catch (_: SecurityException) { null }
            SyncEngine.seen(r.device.address, name, r.rssi)
        }

        override fun onBatchScanResults(results: MutableList<ScanResult>) = results.forEach { onScanResult(0, it) }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            RelayLog.say("background scan failed ($errorCode)")
        }
    }

    // Bluetooth turned off and on again: the scan has to be started again.
    private val btState = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            when (i.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1)) {
                BluetoothAdapter.STATE_ON -> startScan()
                BluetoothAdapter.STATE_OFF -> scanning = false
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        SyncEngine.init(this)
        Notify.channels(this)
        try {
            ServiceCompat.startForeground(
                this, Notify.ID_SERVICE, Notify.service(this, "Looking after the badges nearby"),
                if (Build.VERSION.SDK_INT >= 29) ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE else 0,
            )
        } catch (e: Exception) {
            // Android 14 refuses a connectedDevice service without the Bluetooth permission.
            RelayLog.say("can't run in the foreground: ${e.message}")
            stopSelf()
            return
        }
        ContextCompat.registerReceiver(this, btState, IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED),
            ContextCompat.RECEIVER_NOT_EXPORTED)
        running = true
        startScan()
        RelayLog.say("relay service started")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_SYNC_ALL -> scope.launch { SyncEngine.scanAndSync(20_000, force = intent.getBooleanExtra(EXTRA_FORCE, false)) }
        }
        if (!scanning) startScan()
        return START_STICKY
    }

    @SuppressLint("MissingPermission")
    private fun startScan() {
        if (scanning) return
        val scanner = (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter
            ?.takeIf { it.isEnabled }?.bluetoothLeScanner ?: return RelayLog.say("background scan waits for Bluetooth")
        try {
            scanner.startScan(
                listOf(SyncEngine.nusFilter()),
                ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_POWER).build(),
                scanCallback,
            )
            scanning = true
            RelayLog.say("background scan on")
        } catch (e: SecurityException) {
            RelayLog.say("background scan: no Bluetooth permission")
        }
    }

    @SuppressLint("MissingPermission")
    private fun stopScan() {
        if (!scanning) return
        scanning = false
        try {
            (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (_: Exception) {
        }
    }

    override fun onDestroy() {
        running = false
        stopScan()
        try {
            unregisterReceiver(btState)
        } catch (_: Exception) {
        }
        scope.cancel()
        super.onDestroy()
    }

    companion object {
        const val ACTION_SYNC_ALL = "com.hypnodroid.blorbrelay.SYNC_ALL"
        const val EXTRA_FORCE = "force"

        @Volatile var running = false
            private set

        /** Start (or poke) the service. False when Android refused a foreground start from here. */
        fun start(ctx: Context, syncAll: Boolean = false, force: Boolean = false): Boolean = try {
            // Never ask for a foreground start that would then be refused (and crash the app).
            if (!Perms.bluetoothReady(ctx)) throw IllegalStateException("no Bluetooth permission yet")
            val i = Intent(ctx, RelayService::class.java)
            if (syncAll) i.action = ACTION_SYNC_ALL
            i.putExtra(EXTRA_FORCE, force)
            ContextCompat.startForegroundService(ctx, i)
            true
        } catch (e: Exception) {
            RelayLog.say("couldn't start the relay service: ${e.message}")
            false
        }
    }
}
