#include "modem.h"
#include "config.h"
#include "services.h"

Modem modem;
const char* modelName(ModemModel m) {
  switch (m) { case ModemModel::SIM7600: return "SIM7600"; case ModemModel::A7670: return "A7670";
    case ModemModel::EC25: return "EC25"; default: return "unknown"; }
}

void Modem::begin(EventSink sink) {
  sink_ = sink;
  ser_ = new HardwareSerial(MODEM_UART_NUM);
  ser_->setRxBufferSize(4096);
  ser_->begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  if (MODEM_FLIGHT_PIN >= 0) { pinMode(MODEM_FLIGHT_PIN, OUTPUT); digitalWrite(MODEM_FLIGHT_PIN, HIGH); }
  pinMode(MODEM_PWRKEY_PIN, OUTPUT); digitalWrite(MODEM_PWRKEY_PIN, LOW);
}

void Modem::powerOn() {
  if (sendSync("AT", 500).ok) return;          // already on
  // PWRKEY pulse (through inverting transistor: HIGH here = PWRKEY pulled low).
  digitalWrite(MODEM_PWRKEY_PIN, HIGH); delay(600); digitalWrite(MODEM_PWRKEY_PIN, LOW);
  for (int i = 0; i < 30; i++) { if (sendSync("AT", 1000).ok) return; }
}

AtResult Modem::sendSync(const String& cmd, uint32_t timeoutMs) {
  while (ser_->available()) ser_->read();
  ser_->print(cmd); ser_->print("\r");
  String all, line; uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (ser_->available()) {
      char c = ser_->read();
      if (c == '\n') { line.trim();
        if (line == "OK") return {true, all};
        if (line.startsWith("ERROR") || line.startsWith("+CME ERROR") || line.startsWith("+CMS ERROR")) return {false, all + line};
        if (line.length() && line != cmd) all += line + "\n";
        line = "";
      } else if (c != '\r') line += c;
    }
    delay(2);
  }
  return {false, all + "TIMEOUT"};
}

void Modem::detect() {
  AtResult r = sendSync("ATI", 2000);
  String s = r.lines; s.toUpperCase();
  if (s.indexOf("EC25") >= 0 || s.indexOf("QUECTEL") >= 0) model_ = ModemModel::EC25;
  else if (s.indexOf("A7670") >= 0 || s.indexOf("A76") >= 0) model_ = ModemModel::A7670;
  else if (s.indexOf("SIM7600") >= 0 || s.indexOf("SIMCOM") >= 0) model_ = ModemModel::SIM7600;
  else model_ = ModemModel::Unknown;
  sendSync("ATE0"); sendSync("AT+CMEE=2"); sendSync("AT+CLIP=1"); sendSync("AT+CMGF=1");
  sendSync("AT+CNMI=2,1,0,0,0");   // new SMS -> +CMTI URC
  sendSync("AT+CSCS=\"GSM\"");
}

void Modem::enqueue(const String& cmd, AtCallback cb, uint32_t timeoutMs, const String& prompt, const String& payload) {
  std::lock_guard<std::recursive_mutex> g(mx_);
  q_.push_back({cmd, cb, timeoutMs, prompt, payload});
}

bool Modem::isUrc(const String& l) {
  static const char* p[] = {"RING", "+CLIP:", "+CMTI:", "NO CARRIER", "BUSY", "NO ANSWER", "+CMT:",
                            "VOICE CALL:", "+CIPOPEN:", "+QIOPEN:", "+QIURC:", "+CREG:", "+CEREG:", "+CPIN:"};
  for (auto s : p) if (l.startsWith(s)) return true;
  return false;
}

void Modem::handleUrc(const String& l) {
  if (l == "RING") sink_("ring", "{}");
  else if (l.startsWith("+CLIP:")) {
    int a = l.indexOf('"'), b = l.indexOf('"', a + 1);
    sink_("incoming_call", "{\"number\":\"" + jesc(l.substring(a + 1, b)) + "\"}");
  } else if (l.startsWith("+CMTI:")) {
    int idx = l.substring(l.lastIndexOf(',') + 1).toInt();
    sink_("sms_received", "{\"index\":" + String(idx) + "}");
  } else if (l == "NO CARRIER" || l == "BUSY" || l == "NO ANSWER" || l.startsWith("VOICE CALL: END")) {
    sink_("call_ended", "{\"reason\":\"" + jesc(l) + "\"}");
  } else sink_("urc", "{\"line\":\"" + jesc(l) + "\"}");
}

void Modem::finish(bool ok) {
  Job j = q_.front(); q_.erase(q_.begin()); busy_ = false;
  if (j.cb) j.cb({ok, acc_});
  acc_ = "";
}

void Modem::handleLine(const String& raw) {
  String l = raw; l.trim();
  if (!l.length()) return;
  if (isUrc(l) && !(busy_ && (l == "NO CARRIER" || l == "BUSY") && q_.front().cmd.startsWith("ATD"))) {
    handleUrc(l); return;
  }
  if (!busy_) { sink_("urc", "{\"line\":\"" + jesc(l) + "\"}"); return; }
  if (l == "OK") return finish(true);
  if (l.startsWith("ERROR") || l.startsWith("+CME ERROR") || l.startsWith("+CMS ERROR") ||
      l == "NO CARRIER" || l == "BUSY") { acc_ += l; return finish(false); }
  acc_ += l + "\n";
}

void Modem::loop() {
  std::lock_guard<std::recursive_mutex> g(mx_);
  while (ser_->available()) {
    char c = ser_->read();
    if (c == '\n') { handleLine(rx_); rx_ = ""; }
    else if (c != '\r') {
      rx_ += c;
      // Prompt handling (e.g. "> " for CMGS): send payload + Ctrl-Z.
      if (busy_ && q_.front().prompt.length() && rx_.endsWith(q_.front().prompt)) {
        ser_->print(q_.front().payload); ser_->write(0x1A); rx_ = "";
        q_.front().prompt = "";
      }
    }
  }
  if (busy_ && millis() - started_ > q_.front().timeout) { acc_ += "TIMEOUT"; finish(false); }
  if (!busy_ && !q_.empty()) {
    busy_ = true; started_ = millis(); acc_ = "";
    ser_->print(q_.front().cmd); ser_->print("\r");
  }
}
