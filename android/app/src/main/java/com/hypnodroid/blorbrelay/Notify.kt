package com.hypnodroid.blorbrelay

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat

object Notify {
    const val CH_RELAY = "relay"
    const val CH_ASK = "ask"
    const val ID_SERVICE = 1
    private const val ID_PROGRESS = 2

    fun channels(ctx: Context) {
        val nm = ctx.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CH_RELAY, "Badge relay", NotificationManager.IMPORTANCE_LOW).apply {
                description = "Shown while the app looks after the badges"
            },
        )
        nm.createNotificationChannel(
            NotificationChannel(CH_ASK, "Badge asks for a yes", NotificationManager.IMPORTANCE_HIGH).apply {
                description = "A badge wants its update: turn it around to say yes"
            },
        )
    }

    private fun openApp(ctx: Context): PendingIntent = PendingIntent.getActivity(
        ctx, 0, Intent(ctx, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
    )

    fun service(ctx: Context, text: String): Notification =
        NotificationCompat.Builder(ctx, CH_RELAY)
            .setSmallIcon(R.drawable.ic_badge)
            .setContentTitle("Blorb relay")
            .setContentText(text)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(openApp(ctx))
            .build()

    private fun post(ctx: Context, id: Int, n: Notification) {
        try {
            NotificationManagerCompat.from(ctx).notify(id, n)
        } catch (_: SecurityException) {
            // No notification permission: the work goes on without it.
        }
    }

    /** While a badge asks for its yes (30 s): whoever has the phone can tell the child. */
    fun ask(ctx: Context, address: String, name: String, version: String) {
        post(
            ctx, address.hashCode(),
            NotificationCompat.Builder(ctx, CH_ASK)
                .setSmallIcon(R.drawable.ic_badge)
                .setContentTitle("$name has a new version ($version)")
                .setContentText("Turn the badge around (or press its button) to say yes.")
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setTimeoutAfter(35_000)
                .setAutoCancel(true)
                .setContentIntent(openApp(ctx))
                .build(),
        )
    }

    fun clearAsk(ctx: Context, address: String) = NotificationManagerCompat.from(ctx).cancel(address.hashCode())

    /** The update's progress, or null to clear it. */
    fun progress(ctx: Context, text: String?) {
        if (text == null) return NotificationManagerCompat.from(ctx).cancel(ID_PROGRESS)
        post(
            ctx, ID_PROGRESS,
            NotificationCompat.Builder(ctx, CH_RELAY)
                .setSmallIcon(R.drawable.ic_badge)
                .setContentTitle("Updating a badge")
                .setContentText(text)
                .setOnlyAlertOnce(true)
                .setOngoing(true)
                .setContentIntent(openApp(ctx))
                .build(),
        )
    }
}
