package com.victorious.victormodem

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.Build
import android.os.ParcelUuid
import okhttp3.*
import java.util.UUID

/** A transport carries newline-delimited JSON lines of the VictorModem protocol. */
interface Transport {
    fun send(line: String): Boolean
    fun close()
}

class WsTransport(url: String, onLine: (String) -> Unit, onState: (Boolean, String) -> Unit) : Transport {
    private val client = OkHttpClient()
    private val ws: WebSocket = client.newWebSocket(Request.Builder().url(url).build(), object : WebSocketListener() {
        override fun onOpen(webSocket: WebSocket, response: Response) = onState(true, "WebSocket connected")
        override fun onMessage(webSocket: WebSocket, text: String) =
            text.split('\n').filter { it.isNotBlank() }.forEach(onLine)
        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) = onState(false, "Closed: $reason")
        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) = onState(false, "Error: ${t.message}")
    })
    override fun send(line: String) = ws.send(line)
    override fun close() { ws.close(1000, "bye"); client.dispatcher.executorService.shutdown() }
}

/** BLE GATT client for the firmware service in src/ble.cpp. TX notifications are chunked; reassemble until '\n'. */
@SuppressLint("MissingPermission")
class BleTransport(private val ctx: Context, private val onLine: (String) -> Unit,
                   private val onState: (Boolean, String) -> Unit) : Transport {
    companion object {
        val SVC: UUID = UUID.fromString("7a1c0001-5e5f-4d1b-9c3a-56c7f0a0b001")
        val RX: UUID = UUID.fromString("7a1c0002-5e5f-4d1b-9c3a-56c7f0a0b001")
        val TX: UUID = UUID.fromString("7a1c0003-5e5f-4d1b-9c3a-56c7f0a0b001")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
    private val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private val buf = StringBuilder()
    private var mtu = 23

    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            adapter.bluetoothLeScanner?.stopScan(this)
            onState(false, "Found ${result.device.address}, connecting…")
            gatt = result.device.connectGatt(ctx, false, gattCb, BluetoothDevice.TRANSPORT_LE)
        }
        override fun onScanFailed(errorCode: Int) = onState(false, "Scan failed $errorCode")
    }

    private val gattCb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) g.requestMtu(247)
            else onState(false, "BLE disconnected")
        }
        override fun onMtuChanged(g: BluetoothGatt, m: Int, status: Int) { mtu = m; g.discoverServices() }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(SVC) ?: return onState(false, "VictorModem service not found")
            rx = svc.getCharacteristic(RX)
            val tx = svc.getCharacteristic(TX)
            g.setCharacteristicNotification(tx, true)
            val d = tx.getDescriptor(CCCD)
            if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            else { @Suppress("DEPRECATION") run { d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; g.writeDescriptor(d) } }
        }
        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) =
            onState(true, "BLE connected (MTU $mtu)")
        @Deprecated("pre-33")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION") feed(c.value)
        }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) = feed(value)
    }

    private fun feed(bytes: ByteArray) = synchronized(buf) {
        buf.append(String(bytes, Charsets.UTF_8))
        var i = buf.indexOf("\n")
        while (i >= 0) { val l = buf.substring(0, i); buf.delete(0, i + 1); if (l.isNotBlank()) onLine(l); i = buf.indexOf("\n") }
    }

    fun start() {
        val sc = adapter?.bluetoothLeScanner ?: return onState(false, "Bluetooth off/unavailable")
        onState(false, "Scanning for VictorModem…")
        sc.startScan(listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(SVC)).build()),
            ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCb)
    }

    override fun send(line: String): Boolean {
        val g = gatt ?: return false; val c = rx ?: return false
        val data = line.toByteArray()
        if (data.size > mtu - 3) return false // firmware expects one JSON object per write
        return if (Build.VERSION.SDK_INT >= 33)
            g.writeCharacteristic(c, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS
        else @Suppress("DEPRECATION") run { c.value = data; g.writeCharacteristic(c) }
    }
    override fun close() { adapter?.bluetoothLeScanner?.stopScan(scanCb); gatt?.close(); gatt = null }
}
