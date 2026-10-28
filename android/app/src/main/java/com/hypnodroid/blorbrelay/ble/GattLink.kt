// The Nordic UART Service over Android's BluetoothGatt (docs/ble.md): request
// lines on RX, replies and events notified on TX, image bytes on the OTA data
// characteristic (docs/ota.md). One GATT operation at a time, each waiting for
// its callback, retried with backoff when the stack says busy (index.html gattRetry).
package com.hypnodroid.blorbrelay.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.os.Build
import com.hypnodroid.blorbrelay.proto.LineReader
import com.hypnodroid.blorbrelay.proto.LinkError
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID

object Uuids {
    val NUS: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    val RX: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
    val TX: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
    val OTA_DATA: UUID = UUID.fromString("6e400004-b5a3-f393-e0a9-e50e24dcca9e")
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}

@SuppressLint("MissingPermission")
class GattLink(
    private val ctx: Context,
    val device: BluetoothDevice,
    private val log: (String) -> Unit = {},
) {
    /** Every whole line the badge notifies. Called on a binder thread. */
    var onLine: (String) -> Unit = {}

    /** The link went (after it was up). Called on a binder thread. */
    var onDrop: () -> Unit = {}

    @Volatile var connected = false
        private set
    @Volatile var mtu = 23
        private set

    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var tx: BluetoothGattCharacteristic? = null
    private var data: BluetoothGattCharacteristic? = null
    private val reader = LineReader()
    private val ops = Mutex()

    @Volatile private var op: CompletableDeferred<Int>? = null
    @Volatile private var connecting: CompletableDeferred<Boolean>? = null
    @Volatile private var opened = false

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (gatt !== g) {
                // An earlier connection's late word: it is closed already, and says nothing about this one.
                if (newState == BluetoothProfile.STATE_DISCONNECTED) try { g.close() } catch (_: Exception) {}
                return
            }
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                connected = true
                connecting?.complete(true)
                return
            }
            if (newState == BluetoothProfile.STATE_DISCONNECTED || status != BluetoothGatt.GATT_SUCCESS) {
                val wasOpen = opened
                connected = false
                opened = false
                if (gatt === g) gatt = null
                log("ble: link down (status $status)")
                // The owner hears of the drop before the operation in flight fails, so the
                // updater reads that failure as a drop (ON_DROP) and not as an error.
                if (wasOpen) onDrop()
                connecting?.complete(false)
                op?.complete(DROPPED)
                try {
                    g.close()
                } catch (_: Exception) {
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) this@GattLink.mtu = mtu
            op?.complete(status)
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            op?.complete(status)
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            op?.complete(status)
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            op?.complete(status)
        }

        // API 33+ delivers the value here; its default would call the old one too, so it is not called.
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            heard(c, value)
        }

        @Deprecated("Before API 33")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < 33) heard(c, c.value ?: return)
        }
    }

    private fun heard(c: BluetoothGattCharacteristic, value: ByteArray) {
        if (c.uuid != Uuids.TX) return
        for (line in reader.feed(value)) onLine(line)
    }

    /** Connect, ask for MTU 247, find the UART service and subscribe to TX. Never leaves a half-open link. */
    suspend fun open(timeoutMs: Long = 20000, fresh: Boolean = false) {
        close()
        reader.reset()
        val wait = CompletableDeferred<Boolean>()
        connecting = wait
        val g = if (Build.VERSION.SDK_INT >= 26) {
            device.connectGatt(ctx, false, callback, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(ctx, false, callback)
        } ?: throw LinkError("connectGatt refused")
        gatt = g
        val ok = withTimeoutOrNull(timeoutMs) { wait.await() } ?: false
        if (!ok) {
            close()
            throw LinkError("connect failed")
        }
        try {
            // A restart into another firmware may change the services: never trust a cached table.
            if (fresh) refreshCache(g)
            try {
                gattOp(5000, tries = 2) { it.requestMtu(247) }
            } catch (e: LinkError) {
                if (!connected) throw e   // a refusal leaves MTU 23, and lines go in pieces
            }
            val st = gattOp(15000) { it.discoverServices() }
            if (st != BluetoothGatt.GATT_SUCCESS) throw LinkError("discovery failed ($st)")
            val svc = g.getService(Uuids.NUS) ?: throw LinkError("no UART service")
            rx = svc.getCharacteristic(Uuids.RX) ?: throw LinkError("no RX")
            val t = svc.getCharacteristic(Uuids.TX) ?: throw LinkError("no TX")
            tx = t
            data = svc.getCharacteristic(Uuids.OTA_DATA)
            if (!g.setCharacteristicNotification(t, true)) throw LinkError("notify refused")
            val cccd = t.getDescriptor(Uuids.CCCD) ?: throw LinkError("no CCCD")
            val ds = gattOp { writeDescriptor(it, cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) }
            if (ds != BluetoothGatt.GATT_SUCCESS) throw LinkError("subscribe failed ($ds)")
            opened = true
            log("ble: open ${device.address} mtu=$mtu ota=${data != null}")
        } catch (e: Exception) {
            close()
            throw e
        }
    }

    /** Back to the same device after a drop, for about two minutes (index.html reopen). */
    suspend fun reopen(ms: Long = 120000) {
        val until = System.currentTimeMillis() + ms
        var wait = 500L
        while (true) {
            try {
                return open(fresh = true)
            } catch (e: LinkError) {
                if (System.currentTimeMillis() + wait > until) throw e
                delay(wait)
                wait = minOf(wait * 2, 5000)
            }
        }
    }

    fun close() {
        val g = gatt ?: return
        gatt = null
        opened = false
        connected = false
        op?.complete(DROPPED)
        try {
            g.disconnect()
        } catch (_: Exception) {
        }
        try {
            g.close()
        } catch (_: Exception) {
        }
    }

    val hasOtaData: Boolean get() = data != null

    /** A request line, written with response in pieces the MTU fits (a whole line at MTU 247). */
    suspend fun writeLine(text: String) {
        val c = rx ?: throw LinkError("not open")
        val bytes = text.toByteArray(Charsets.UTF_8)
        val piece = maxOf(20, mtu - 3)
        var i = 0
        while (i < bytes.size) {
            val part = bytes.copyOfRange(i, minOf(bytes.size, i + piece))
            val st = gattOp { writeChar(it, c, part, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) }
            if (st != BluetoothGatt.GATT_SUCCESS) throw LinkError("write failed ($st)")
            i += piece
        }
    }

    /** One image packet on the data characteristic, without response. */
    suspend fun writeData(bytes: ByteArray) {
        val c = data ?: throw LinkError("no OTA data characteristic")
        // Android reports a write without response once the stack has taken it, which paces
        // the sender; a stack that never reports it is given a second and assumed to have.
        gattOp(1000, lenient = true) { writeChar(it, c, bytes, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE) }
    }

    private suspend fun gattOp(
        timeoutMs: Long = 5000,
        tries: Int = 8,
        lenient: Boolean = false,
        start: (BluetoothGatt) -> Boolean,
    ): Int = ops.withLock {
        for (i in 1..tries) {
            val g = gatt
            if (g == null || !connected) throw LinkError("disconnected")
            val d = CompletableDeferred<Int>()
            op = d
            val started = try {
                start(g)
            } catch (e: SecurityException) {
                op = null
                throw LinkError("no Bluetooth permission")
            }
            if (!started) {
                op = null
                delay(120L * i)   // busy: another operation in flight in the stack
                continue
            }
            val st = withTimeoutOrNull(timeoutMs) { d.await() }
            op = null
            if (st == null) {
                if (lenient) return@withLock BluetoothGatt.GATT_SUCCESS
                throw LinkError("gatt timeout")
            }
            if (st == DROPPED) throw LinkError("disconnected")
            return@withLock st
        }
        throw LinkError("gatt busy")
    }

    @Suppress("DEPRECATION")
    private fun writeChar(g: BluetoothGatt, c: BluetoothGattCharacteristic, bytes: ByteArray, type: Int): Boolean =
        if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(c, bytes, type) == BluetoothStatusCodes.SUCCESS
        } else {
            c.writeType = type
            c.value = bytes
            g.writeCharacteristic(c)
        }

    @Suppress("DEPRECATION")
    private fun writeDescriptor(g: BluetoothGatt, d: BluetoothGattDescriptor, value: ByteArray): Boolean =
        if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(d, value) == BluetoothStatusCodes.SUCCESS
        } else {
            d.value = value
            g.writeDescriptor(d)
        }

    private fun refreshCache(g: BluetoothGatt) {
        try {
            g.javaClass.getMethod("refresh").invoke(g)
        } catch (_: Exception) {
        }
    }

    private companion object {
        const val DROPPED = -1
    }
}
