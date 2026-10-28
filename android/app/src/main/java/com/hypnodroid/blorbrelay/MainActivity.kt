// The one screen: the badges seen, each with its version, last sync and
// result, a Sync now button, and what is still missing for background work.
package com.hypnodroid.blorbrelay

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Typeface
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.text.format.DateUtils
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import com.hypnodroid.blorbrelay.sync.BadgeRecord
import com.hypnodroid.blorbrelay.sync.RelayLog
import com.hypnodroid.blorbrelay.sync.SyncEngine

class MainActivity : Activity() {
    private val main = Handler(Looper.getMainLooper())
    private lateinit var status: TextView
    private lateinit var badges: LinearLayout
    private lateinit var logView: TextView
    private lateinit var permsButton: Button
    private lateinit var batteryButton: Button
    private var refreshQueued = false

    private val prefsChanged = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> refreshSoon() }
    private val tick = object : Runnable {
        override fun run() {
            refresh(); main.postDelayed(this, 5000)
        }
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        SyncEngine.init(this)
        Notify.channels(this)
        val pad = dp(16)
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        col.addView(TextView(this).apply {
            text = "Blorb relay"
            textSize = 22f
            setTypeface(typeface, Typeface.BOLD)
        })
        col.addView(TextView(this).apply {
            text = "Updates the badges over Bluetooth and passes their news to the site, by itself, every few hours " +
                "and whenever a badge comes into range."
            setPadding(0, dp(4), 0, dp(8))
        })
        status = TextView(this).also { col.addView(it) }
        val buttons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        buttons.addView(Button(this).apply {
            text = "Sync now"
            setOnClickListener {
                if (!Perms.bluetoothReady(this@MainActivity)) return@setOnClickListener askPermissions()
                Relay.ensure(this@MainActivity)
                SyncEngine.syncNow()
            }
        })
        permsButton = Button(this).apply {
            text = "Permissions"
            setOnClickListener { askPermissions() }
        }
        buttons.addView(permsButton)
        batteryButton = Button(this).apply {
            text = "Battery"
            setOnClickListener { askBatteryExemption() }
        }
        buttons.addView(batteryButton)
        col.addView(buttons)
        col.addView(header("Badges"))
        badges = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        col.addView(badges)
        col.addView(header("Log"))
        logView = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 11f
            setTextIsSelectable(true)
        }
        col.addView(logView)
        setContentView(ScrollView(this).apply { addView(col) })

        if (!Perms.bluetoothReady(this)) askPermissions() else Relay.ensure(this)
    }

    private fun header(t: String) = TextView(this).apply {
        text = t
        textSize = 17f
        setTypeface(typeface, Typeface.BOLD)
        setPadding(0, dp(16), 0, dp(4))
    }

    override fun onResume() {
        super.onResume()
        SyncEngine.store.listen(prefsChanged)
        RelayLog.listener = { refreshSoon() }
        main.post(tick)
    }

    override fun onPause() {
        SyncEngine.store.unlisten(prefsChanged)
        RelayLog.listener = null
        main.removeCallbacks(tick)
        super.onPause()
    }

    private fun refreshSoon() {
        main.post {
            if (refreshQueued) return@post
            refreshQueued = true
            main.postDelayed({ refreshQueued = false; refresh() }, 300)
        }
    }

    private fun ago(t: Long) = if (t == 0L) "never" else DateUtils.getRelativeTimeSpanString(t).toString()

    private fun refresh() {
        val missing = ArrayList<String>()
        if (!Perms.bluetoothReady(this)) missing += "Bluetooth permission"
        if (Build.VERSION.SDK_INT >= 33 && !Perms.granted(this, Manifest.permission.POST_NOTIFICATIONS)) missing += "notifications"
        if (Perms.needsBackgroundLocation(this)) missing += "location \"all the time\""
        if (!Perms.batteryExempt(this)) missing += "battery exemption"
        val busy = SyncEngine.busy.value
        status.text = buildString {
            append(if (RelayService.running) "Running in the background." else "Background service not running.")
            if (busy != null) append(" Syncing $busy now.")
            if (missing.isNotEmpty()) append("\nStill needed: ${missing.joinToString(", ")}.")
        }
        permsButton.visibility = if (Perms.bluetoothReady(this) && !Perms.needsBackgroundLocation(this)) View.GONE else View.VISIBLE
        batteryButton.visibility = if (Perms.batteryExempt(this)) View.GONE else View.VISIBLE

        badges.removeAllViews()
        val all = SyncEngine.store.all()
        if (all.isEmpty()) badges.addView(TextView(this).apply { text = "No badge seen yet." })
        for (b in all) badges.addView(row(b))
        logView.text = RelayLog.tail(80).reversed().joinToString("\n")
    }

    private fun row(b: BadgeRecord) = TextView(this).apply {
        val title = b.name.ifEmpty { "badge" } + if (b.petName.isNotEmpty() && !b.petName.equals(b.name, true)) " (${b.petName})" else ""
        text = buildString {
            append(title).append("   ").append(b.address).append('\n')
            append("version ${b.fw.ifEmpty { "?" }} · seen ${ago(b.lastSeen)} · signal ${b.rssi} dBm\n")
            append("last sync ${ago(b.lastSync)}")
            if (b.lastAttempt != 0L) append(if (b.lastOk) " · OK" else " · PROBLEM")
            if (b.lastResult.isNotEmpty()) append('\n').append(b.lastResult)
        }
        setPadding(0, dp(6), 0, dp(10))
    }

    // ---- permissions ----------------------------------------------------------------------

    private fun askPermissions() {
        val missing = Perms.wanted().filterNot { Perms.granted(this, it) }
        if (missing.isNotEmpty()) return requestPermissions(missing.toTypedArray(), REQ_PERMS)
        // Android 10/11: background scanning needs location "all the time", asked on its own.
        if (Perms.needsBackgroundLocation(this)) {
            return requestPermissions(arrayOf(Manifest.permission.ACCESS_BACKGROUND_LOCATION), REQ_BG)
        }
        if (!Perms.batteryExempt(this)) askBatteryExemption()
    }

    @SuppressLint("BatteryLife")
    private fun askBatteryExemption() {
        // Doze would otherwise hold the periodic check and the scan's syncs for hours.
        try {
            startActivity(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName")))
        } catch (e: Exception) {
            startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (Perms.bluetoothReady(this)) Relay.ensure(this)
        if (requestCode == REQ_PERMS && Perms.bluetoothReady(this)) {
            if (Perms.needsBackgroundLocation(this)) askPermissions()
            else if (!Perms.batteryExempt(this)) askBatteryExemption()
        }
        refresh()
    }

    private companion object {
        const val REQ_PERMS = 1
        const val REQ_BG = 2
    }
}
