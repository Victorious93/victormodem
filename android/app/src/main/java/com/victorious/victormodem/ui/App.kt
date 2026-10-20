package com.victorious.victormodem.ui

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.*
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.victorious.victormodem.*

val Cyan = Color(0xFF00F0FF); val Magenta = Color(0xFFFF2BD6); val Bg = Color(0xFF07070F); val Panel = Color(0xFF12121F)
val Ring = Brush.sweepGradient(listOf(Cyan, Magenta, Cyan))
private val Scheme = darkColorScheme(primary = Cyan, secondary = Magenta, background = Bg, surface = Panel,
    onPrimary = Color.Black, onBackground = Color.White, onSurface = Color.White)

@Composable
fun App(vm: ModemViewModel) = MaterialTheme(colorScheme = Scheme) {
    var tab by remember { mutableIntStateOf(0) }
    val tabs = listOf("Connect" to Icons.Default.Link, "Dialer" to Icons.Default.Call,
        "SMS" to Icons.Default.Sms, "Status" to Icons.Default.SignalCellularAlt)
    Scaffold(containerColor = Bg, bottomBar = {
        NavigationBar(containerColor = Panel) { tabs.forEachIndexed { i, (l, ic) ->
            NavigationBarItem(selected = tab == i, onClick = { tab = i }, icon = { Icon(ic, l) }, label = { Text(l) }) } }
    }) { pad -> Column(Modifier.padding(pad).fillMaxSize().padding(16.dp)) {
        Header(vm)
        when (tab) { 0 -> ConnectScreen(vm); 1 -> DialerScreen(vm); 2 -> SmsScreen(vm); else -> StatusScreen(vm) }
    } }
}

@Composable
fun Header(vm: ModemViewModel) = Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(bottom = 12.dp)) {
    Image(painterResource(com.victorious.victormodem.R.drawable.logo), "logo", Modifier.size(56.dp).clip(CircleShape).border(3.dp, Ring, CircleShape))
    Spacer(Modifier.width(12.dp))
    Column { Text("VictorModem", style = MaterialTheme.typography.titleLarge, color = Cyan)
        Text(vm.statusLine, color = if (vm.connected) Cyan else Magenta, style = MaterialTheme.typography.bodySmall) }
}

@Composable
fun NeonButton(text: String, modifier: Modifier = Modifier, onClick: () -> Unit) =
    OutlinedButton(onClick, modifier, border = BorderStroke(2.dp, Ring), shape = RoundedCornerShape(24.dp)) { Text(text, color = Color.White) }

@Composable
fun ConnectScreen(vm: ModemViewModel) = Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
    Row { Mode.entries.forEach { m -> FilterChip(vm.mode == m, { vm.mode = m },
        { Text(when (m) { Mode.BLE -> "BLE"; Mode.ESP_WS -> "ESP32 Wi-Fi"; Mode.HUB_WS -> "Pi hub" }) }, Modifier.padding(end = 6.dp)) } }
    if (vm.mode != Mode.BLE) OutlinedTextField(vm.host, { vm.host = it }, label = { Text("Host (ESP AP 192.168.4.1 / hub IP)") }, singleLine = true, modifier = Modifier.fillMaxWidth())
    OutlinedTextField(vm.token, { vm.token = it }, label = { Text("API token") }, singleLine = true,
        visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        NeonButton("Connect", Modifier.weight(1f)) { vm.connect() }
        NeonButton("Disconnect", Modifier.weight(1f)) { vm.disconnect() } }
    Text("Log", color = Magenta)
    LazyColumn(Modifier.fillMaxSize().background(Panel, RoundedCornerShape(8.dp)).padding(8.dp)) {
        items(vm.log) { Text(it, style = MaterialTheme.typography.bodySmall) } }
}

@Composable
fun DialerScreen(vm: ModemViewModel) {
    var num by remember { mutableStateOf("") }
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
        Text(vm.callState, color = Magenta)
        OutlinedTextField(num, { num = it.filter { c -> c.isDigit() || c in "+*#" } }, label = { Text("Number") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        listOf("123", "456", "789", "*0#").forEach { row -> Row { row.forEach { k ->
            Box(Modifier.padding(6.dp).size(68.dp).clip(CircleShape).border(2.dp, Ring, CircleShape).clickable { num += k },
                contentAlignment = Alignment.Center) { Text("$k", style = MaterialTheme.typography.headlineSmall) } } } }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 8.dp)) {
            NeonButton("Dial") { if (num.isNotBlank()) vm.dial(num) }
            NeonButton("Answer") { vm.answer() }
            NeonButton("Hang up") { vm.hangup() }
            IconButton({ num = num.dropLast(1) }) { Icon(Icons.Default.Backspace, "del", tint = Cyan) } }
    }
}

@Composable
fun SmsScreen(vm: ModemViewModel) {
    var to by remember { mutableStateOf("") }; var body by remember { mutableStateOf("") }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedTextField(to, { to = it }, label = { Text("To") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(body, { if (it.length <= 160) body = it }, label = { Text("Message (${body.length}/160)") }, modifier = Modifier.fillMaxWidth())
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            NeonButton("Send") { if (to.isNotBlank() && body.isNotBlank()) { vm.sendSms(to, body); body = "" } }
            NeonButton("Refresh inbox") { vm.refreshInbox() } }
        LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) { items(vm.inbox) { s ->
            Column(Modifier.fillMaxWidth().border(1.dp, Ring, RoundedCornerShape(10.dp)).padding(10.dp).clickable { to = s.from }) {
                Text("${s.from}  ${s.time}", color = Cyan, style = MaterialTheme.typography.labelMedium); Text(s.text) } } }
    }
}

@Composable
fun StatusScreen(vm: ModemViewModel) = Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
    NeonButton("Refresh status") { vm.request("status") }
    val s = vm.status
    if (s == null) Text("No status yet") else s.keys().forEach { k ->
        Row(Modifier.fillMaxWidth().background(Panel, RoundedCornerShape(8.dp)).padding(10.dp)) {
            Text(k, color = Magenta, modifier = Modifier.width(110.dp)); Text(s.opt(k).toString()) } }
}
