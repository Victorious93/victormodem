// Voice, SMS, data and status modules built on the AT queue.
#include "services.h"
#include "modem.h"
#include "config.h"
#include "fleet_config.h"
#include <memory>

String jesc(const String& s) {
  String o; for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n"; else if ((uint8_t)c >= 0x20) o += c; } return o;
}
static String reply(bool ok, const String& extra = "") {
  return String("{\"ok\":") + (ok ? "true" : "false") + (extra.length() ? "," + extra : "") + "}";
}
static bool validNumber(const String& n) {
  if (n.length() < 3 || n.length() > 20) return false;
  for (size_t i = 0; i < n.length(); i++) { char c = n[i];
    if (!(isdigit(c) || (c == '+' && i == 0) || c == '*' || c == '#')) return false; }
  return true;
}

// ---- Voice ----
void voice_dial(const String& num, ReplyFn r) {
  if (!validNumber(num)) return r(reply(false, "\"error\":\"bad number\""));
  modem.enqueue("ATD" + num + ";", [r](const AtResult& a){ r(reply(a.ok, "\"raw\":\"" + jesc(a.lines) + "\"")); }, 30000);
}
void voice_answer(ReplyFn r) { modem.enqueue("ATA", [r](const AtResult& a){ r(reply(a.ok)); }, 20000); }
void voice_hangup(ReplyFn r) {
  const char* cmd = modem.model() == ModemModel::EC25 ? "ATH" : "AT+CHUP";
  modem.enqueue(cmd, [r](const AtResult& a){ r(reply(a.ok)); });
}

// ---- SMS (text mode) ----
void sms_init() {}
void sms_send(const String& num, const String& text, ReplyFn r) {
  if (!validNumber(num) || text.length() == 0 || text.length() > 160)
    return r(reply(false, "\"error\":\"bad number or text (1-160 chars GSM)\""));
  modem.enqueue("AT+CMGS=\"" + num + "\"", [r](const AtResult& a){
    r(reply(a.ok, "\"raw\":\"" + jesc(a.lines) + "\"")); }, 60000, "> ", text);
}
void sms_read(int idx, ReplyFn r) {
  modem.enqueue("AT+CMGR=" + String(idx), [r, idx](const AtResult& a){
    // +CMGR: "REC UNREAD","+1555...",,"26/10/20,00:10:00-16"\n<body>
    int nl = a.lines.indexOf('\n'); String hdr = a.lines.substring(0, nl), body = a.lines.substring(nl + 1);
    body.trim();
    int q1 = hdr.indexOf("\",\"") + 3, q2 = hdr.indexOf('"', q1);
    r(reply(a.ok, "\"index\":" + String(idx) + ",\"from\":\"" + jesc(hdr.substring(q1, q2)) +
                  "\",\"header\":\"" + jesc(hdr) + "\",\"text\":\"" + jesc(body) + "\""));
  });
}
void sms_list(const String& filter, ReplyFn r) {
  String f = filter.length() ? filter : "ALL";
  modem.enqueue("AT+CMGL=\"" + f + "\"", [r](const AtResult& a){ r(reply(a.ok, "\"raw\":\"" + jesc(a.lines) + "\"")); }, 15000);
}

// ---- Data: PDP context + modem-internal TCP (PPPoS on roadmap) ----
void data_connect(const String& apn, ReplyFn r) {
  String a = apn.length() ? apn : String(cfg_apn());
  String u = cfg_apn_user(), pw = cfg_apn_pass();
  int auth = (u.length() || pw.length()) ? 3 : 0;   // 3 = PAP or CHAP
  modem.enqueue("AT+CGDCONT=1,\"IP\",\"" + a + "\"");
  if (modem.model() == ModemModel::EC25) {
    modem.enqueue("AT+QICSGP=1,1,\"" + a + "\",\"" + u + "\",\"" + pw + "\"," + String(auth ? 3 : 1));
    modem.enqueue("AT+QIACT=1", [r](const AtResult& x){ r(reply(x.ok)); }, 150000);
  } else {
    if (auth) modem.enqueue("AT+CGAUTH=1," + String(auth) + ",\"" + pw + "\",\"" + u + "\"");
    modem.enqueue("AT+CGACT=1,1", nullptr, 30000);
    modem.enqueue("AT+NETOPEN", [r](const AtResult& x){ r(reply(x.ok, "\"raw\":\"" + jesc(x.lines) + "\"")); }, 30000);
  }
}
void data_disconnect(ReplyFn r) {
  modem.enqueue(modem.model() == ModemModel::EC25 ? "AT+QIDEACT=1" : "AT+NETCLOSE",
                [r](const AtResult& x){ r(reply(x.ok)); }, 40000);
}
void data_tcp_get(const String& host, int port, const String& path, ReplyFn r) {
  String req = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\n\r\n";
  if (modem.model() == ModemModel::EC25) {
    modem.enqueue("AT+QIOPEN=1,0,\"TCP\",\"" + host + "\"," + String(port) + ",0,1", nullptr, 30000);
    modem.enqueue("AT+QISEND=0," + String(req.length()), nullptr, 10000, "> ", req);
    modem.enqueue("AT+QIRD=0,1500", [r](const AtResult& x){ r(reply(x.ok, "\"raw\":\"" + jesc(x.lines) + "\"")); }, 10000);
    modem.enqueue("AT+QICLOSE=0");
  } else {
    modem.enqueue("AT+CIPOPEN=0,\"TCP\",\"" + host + "\"," + String(port), nullptr, 30000);
    modem.enqueue("AT+CIPSEND=0," + String(req.length()), nullptr, 10000, ">", req);
    modem.enqueue("AT+CIPRXGET=2,0,1500", [r](const AtResult& x){ r(reply(x.ok, "\"raw\":\"" + jesc(x.lines) + "\"")); }, 10000);
    modem.enqueue("AT+CIPCLOSE=0");
  }
}

// ---- Status ----
void status_get(ReplyFn r) {
  auto acc = std::make_shared<String>();
  modem.enqueue("AT+CSQ", [acc](const AtResult& a){ *acc += a.lines; });
  modem.enqueue("AT+CREG?", [acc](const AtResult& a){ *acc += a.lines; });
  modem.enqueue("AT+COPS?", [acc, r](const AtResult& a){ *acc += a.lines;
    r(reply(true, String("\"model\":\"") + modelName(modem.model()) + "\",\"raw\":\"" + jesc(*acc) + "\"")); });
}
