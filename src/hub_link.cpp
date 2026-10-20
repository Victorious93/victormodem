// Bridge mode: forward phone JSON requests to the Pi 5 hub over UART; route replies by id,
// broadcast hub events to BLE + WebSocket clients. Frames are newline-delimited JSON.
#ifdef VM_MODE_BRIDGE
#include <map>
#include "services.h"
#include "config.h"
#include "fleet_config.h"

static HardwareSerial hub(HUB_UART_NUM);
struct Pending { long origId; ReplyFn reply; uint32_t t0; };
static std::map<long, Pending> pending;
static long nextId = 1;
static String rx;
static uint32_t lastPong = 0, lastPing = 0;

void hub_begin() {
  hub.setRxBufferSize(8192);
  hub.begin(cfg_hub_baud(), SERIAL_8N1, HUB_RX_PIN, HUB_TX_PIN);
  hub.print("{\"type\":\"hello\"}\n");
}
bool hub_online() { return lastPong && millis() - lastPong < 15000; }

void hub_forward(const String& json, ReplyFn r) {
  JsonDocument d;
  if (deserializeJson(d, json)) return r("{\"type\":\"response\",\"id\":0,\"result\":{\"ok\":false,\"error\":\"bad json\"}}");
  long orig = d["id"] | 0, lid = nextId++;
  if (!hub_online()) return r("{\"type\":\"response\",\"id\":" + String(orig) + ",\"result\":{\"ok\":false,\"error\":\"hub offline\"}}");
  d["id"] = lid;                       // token passes through; the hub authenticates
  pending[lid] = {orig, r, millis()};
  String out; serializeJson(d, out); hub.print(out); hub.print('\n');
}

static void handleFrame(const String& line) {
  JsonDocument d;
  if (deserializeJson(d, line)) return;
  String type = d["type"] | "";
  if (type == "pong") { lastPong = millis(); return; }
  if (type == "event") { api_broadcast(line); return; }
  if (type == "response") {
    long lid = d["id"] | 0;
    auto it = pending.find(lid);
    if (it == pending.end()) return;
    d["id"] = it->second.origId;
    String out; serializeJson(d, out);
    ReplyFn r = it->second.reply; pending.erase(it); r(out);
  }
}

void hub_loop() {
  while (hub.available()) {
    char c = hub.read();
    if (c == '\n') { if (rx.length()) handleFrame(rx); rx = ""; }
    else if (c != '\r' && rx.length() < 4096) rx += c;
  }
  uint32_t now = millis();
  if (now - lastPing > 5000) { lastPing = now; hub.print("{\"type\":\"ping\"}\n"); }
  for (auto it = pending.begin(); it != pending.end();) {   // 90 s covers ATD / data_up
    if (now - it->second.t0 > 90000) {
      it->second.reply("{\"type\":\"response\",\"id\":" + String(it->second.origId) + ",\"result\":{\"ok\":false,\"error\":\"hub timeout\"}}");
      it = pending.erase(it);
    } else ++it;
  }
}
#endif
