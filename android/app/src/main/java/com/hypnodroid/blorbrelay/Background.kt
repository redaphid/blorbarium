// Everything that keeps the relay going with nobody opening it: the periodic
// worker, the boot receiver, and the permissions they need.
package com.hypnodroid.blorbrelay

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.PowerManager
import androidx.core.content.ContextCompat
import androidx.work.CoroutineWorker
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import com.hypnodroid.blorbrelay.sync.RelayLog
import com.hypnodroid.blorbrelay.sync.SyncEngine
import java.util.concurrent.TimeUnit

object Perms {
    /** The runtime permissions Bluetooth needs on this Android version. */
    fun bluetooth(): Array<String> = if (Build.VERSION.SDK_INT >= 31) {
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    } else {
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }

    fun wanted(): Array<String> = bluetooth() +
        (if (Build.VERSION.SDK_INT >= 33) arrayOf(Manifest.permission.POST_NOTIFICATIONS) else emptyArray())

    fun granted(ctx: Context, p: String) = ContextCompat.checkSelfPermission(ctx, p) == PackageManager.PERMISSION_GRANTED

    fun bluetoothReady(ctx: Context) = bluetooth().all { granted(ctx, it) }

    /** Android 10 and 11 scan in the background only with location "all the time". */
    fun needsBackgroundLocation(ctx: Context) = Build.VERSION.SDK_INT in 29..30 &&
        !granted(ctx, Manifest.permission.ACCESS_BACKGROUND_LOCATION)

    fun batteryExempt(ctx: Context) =
        (ctx.getSystemService(Context.POWER_SERVICE) as PowerManager).isIgnoringBatteryOptimizations(ctx.packageName)
}

object Relay {
    private const val PERIODIC = "blorb-relay-periodic"

    /** The service and the periodic worker, both, whenever the app can run them. Safe to call often. */
    fun ensure(ctx: Context) {
        SyncEngine.init(ctx)
        WorkManager.getInstance(ctx).enqueueUniquePeriodicWork(
            PERIODIC, ExistingPeriodicWorkPolicy.KEEP,
            PeriodicWorkRequestBuilder<SyncWorker>(3, TimeUnit.HOURS, 30, TimeUnit.MINUTES).build(),
        )
        if (Perms.bluetoothReady(ctx) && !RelayService.running) RelayService.start(ctx)
    }
}

/** Every few hours: make sure the service runs, then sync every badge in range that is due. */
class SyncWorker(ctx: Context, params: WorkerParameters) : CoroutineWorker(ctx, params) {
    override suspend fun doWork(): Result {
        SyncEngine.init(applicationContext)
        if (!Perms.bluetoothReady(applicationContext)) {
            RelayLog.say("periodic: no Bluetooth permission yet")
            return Result.success()
        }
        RelayLog.say("periodic check")
        if (!RelayService.running) RelayService.start(applicationContext)
        // The sync runs here either way, so a refused foreground start still syncs.
        SyncEngine.scanAndSync(25_000, force = false)
        return Result.success()
    }
}

/** After a reboot or an app update, start again. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        when (intent.action) {
            Intent.ACTION_BOOT_COMPLETED, Intent.ACTION_MY_PACKAGE_REPLACED -> {
                RelayLog.say("starting after ${intent.action?.substringAfterLast('.')}")
                Relay.ensure(ctx)
            }
        }
    }
}
