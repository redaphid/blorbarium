package com.hypnodroid.blorbrelay.ble

import android.bluetooth.BluetoothDevice
import android.content.Context
import com.hypnodroid.blorbrelay.ota.OtaPort
import com.hypnodroid.blorbrelay.ota.Updater
import com.hypnodroid.blorbrelay.proto.Link
import com.hypnodroid.blorbrelay.proto.LinkError
import com.hypnodroid.blorbrelay.proto.Reply
import kotlinx.coroutines.delay

/** One badge over Bluetooth: the GATT link, the line protocol on it, and the port an update drives. */
class BadgeConnection(ctx: Context, device: BluetoothDevice, private val log: (String) -> Unit) : OtaPort {
    private val gatt = GattLink(ctx, device, log)

    /** The update in progress, which hears "OTA ..." events and drops first. */
    @Volatile var updater: Updater? = null

    @Volatile private var link = newLink()

    init {
        gatt.onLine = { link.receive(it) }
        gatt.onDrop = {
            // The updater hears first, so the requests failing next read as a drop and not an error.
            updater?.dropped()
            link.failAll("disconnected")
        }
    }

    private fun newLink() = Link(
        writeLine = { gatt.writeLine(it) },
        onEvent = { text -> if (text.startsWith("OTA")) updater?.event(text) },
        log = { dir, text -> if (dir != "in" || !text.startsWith("! OTA at=")) log("$dir $text") },
    )

    val hasOtaData get() = gatt.hasOtaData

    suspend fun open(tries: Int = 3) {
        for (i in 1..tries) {
            try {
                gatt.open()
                link = newLink()
                return
            } catch (e: LinkError) {
                log("ble: open try $i: ${e.message}")
                if (i == tries) throw e
                delay(1500L * i)
            }
        }
    }

    override suspend fun send(cmd: String, timeoutMs: Long): Reply = link.send(cmd, timeoutMs)

    override suspend fun writeData(bytes: ByteArray) = gatt.writeData(bytes)

    override suspend fun reconnect() {
        gatt.reopen()
        link = newLink()
    }

    fun close() = gatt.close()
}
