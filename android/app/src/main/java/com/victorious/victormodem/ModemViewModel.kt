package com.victorious.victormodem

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import androidx.core.app.NotificationCompat
import androidx.compose.runtime.*
import androidx.lifecycle.AndroidViewModel
import org.json.JSONObject

enum class Mode { BLE, ESP_WS, HUB_WS }
data class Sms(val index: Int, val from: String, val text: String, val time: String = "")

/** Speaks the exact JSON protocol in README "Protocol": {id, token, cmd, ...} → {type:response,id,result} / {type:event,event,data}. */
class ModemViewModel(app: Application) : AndroidViewModel(app) {
    private val prefs = app.getSharedPreferences("vm", Context.MODE_PRIVATE)
    var token by mutableStateOf(prefs.getString("token", "") ?: "")
    var host by mutableStateOf(prefs.getString("host", "192.168.4.1") ?: "192.168.4.1")
    var mode by mutableStateOf(Mode.BLE)
    var connected by mutableStateOf(false)
    var statusLine by mutableStateOf("Disconnected")
    var callState by mutableStateOf("idle")
    var status by mutableStateOf<JSONObject?>(null)
    val inbox = mutableStateListOf<Sms>()
    val log = mutableStateListOf<String>()
    private var transport: Transport? = null
    private var nextId = 1
    private val pending = mutableMapOf<Int, String>()

    init { createChannels() }

    fun saveSettings() { prefs.edit().putString("token", token).putString("host", host).apply() }

    fun connect() {
        saveSettings(); disconnect()
        val onLine: (String) -> Unit = { l -> android.os.Handler(android.os.Looper.getMainLooper()).post { handle(l) } }
        val onState: (Boolean, String) -> Unit = { c, s -> android.os.Handler(android.os.Looper.getMainLooper()).post {
            connected = c; statusLine = s; if (c) request("status") } }
        transport = when (mode) {
            Mode.BLE -> BleTransport(getApplication(), onLine, onState).also { it.start() }
            Mode.ESP_WS -> WsTransport("ws://$host:81/", onLine, onState)
            Mode.HUB_WS -> WsTransport("ws://$host:8080/ws?token=${java.net.URLEncoder.encode(token, "UTF-8")}", onLine, onState)
        }
    }
    fun disconnect() { transport?.close(); transport = null; connected = false }

    fun request(cmd: String, params: Map<String, Any> = emptyMap()) {
        val id = nextId++
        val o = JSONObject().put("id", id).put("token", token).put("cmd", cmd)
        params.forEach { (k, v) -> o.put(k, v) }
        pending[id] = cmd
        if (transport?.send(o.toString()) != true) log.add(0, "send failed: $cmd")
    }
    fun dial(n: String) { request("dial", mapOf("number" to n)); callState = "dialing $n" }
    fun answer() = request("answer")
    fun hangup() { request("hangup"); callState = "idle" }
    fun sendSms(n: String, t: String) = request("sms_send", mapOf("number" to n, "text" to t))
    fun refreshInbox() = request("sms_list", mapOf("filter" to "ALL"))

    private fun handle(line: String) {
        log.add(0, line); if (log.size > 200) log.removeAt(log.lastIndex)
        val o = runCatching { JSONObject(line) }.getOrNull() ?: return
        when (o.optString("type")) {
            "response" -> {
                val cmd = pending.remove(o.optInt("id")); val r = o.optJSONObject("result") ?: return
                if (!r.optBoolean("ok", true)) { statusLine = "Error: ${r.optString("error")}"; return }
                when (cmd) { "status" -> status = r; "sms_list" -> parseInbox(r) }
            }
            "event" -> {
                val d = o.optJSONObject("data") ?: JSONObject()
                when (o.optString("event")) {
                    "ring", "incoming_call" -> { val n = d.optString("number", "Unknown")
                        callState = "incoming $n"; notify(1, "Incoming call", n) }
                    "call_ended" -> callState = "idle"
                    "sms" -> { val s = Sms(d.optInt("index", -1), d.optString("number", d.optString("from")), d.optString("text"), d.optString("time"))
                        inbox.add(0, s); notify(2 + s.index.coerceAtLeast(0), "SMS from ${s.from}", s.text) }
                }
            }
        }
    }

    private fun parseInbox(r: JSONObject) {
        val arr = r.optJSONArray("messages") ?: r.optJSONArray("sms") ?: return
        inbox.clear()
        for (i in 0 until arr.length()) { val m = arr.getJSONObject(i)
            inbox.add(Sms(m.optInt("index"), m.optString("number", m.optString("from")), m.optString("text"), m.optString("time"))) }
    }

    private fun createChannels() {
        val nm = getApplication<Application>().getSystemService(NotificationManager::class.java)
        if (android.os.Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(NotificationChannel("calls", "Calls", NotificationManager.IMPORTANCE_HIGH))
            nm.createNotificationChannel(NotificationChannel("sms", "SMS", NotificationManager.IMPORTANCE_HIGH))
        }
    }
    private fun notify(id: Int, title: String, text: String) {
        val ctx = getApplication<Application>()
        val n = NotificationCompat.Builder(ctx, if (id == 1) "calls" else "sms")
            .setSmallIcon(android.R.drawable.stat_notify_chat).setContentTitle(title).setContentText(text)
            .setPriority(NotificationCompat.PRIORITY_HIGH).setAutoCancel(true).build()
        runCatching { ctx.getSystemService(NotificationManager::class.java).notify(id, n) }
    }
}
