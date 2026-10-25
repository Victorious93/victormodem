// Wi-Fi SoftAP + HTTP (GET /api/status, POST /api) + WebSocket :81 for events.
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "services.h"
#include "config.h"
#include "fleet_config.h"

static WebServer http(80);
static WebSocketsServer ws(81);

void wifi_begin() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(cfg_ap_ssid(), cfg_ap_pass());
  http.on("/", [](){ http.send(200, "text/plain", "VictorModem API. POST JSON to /api, events on ws://192.168.4.1:81/"); });
  http.on("/api", HTTP_POST, [](){
    // Response is async; HTTP returns "queued" and the result arrives on WebSocket.
    api_handle(http.arg("plain"), [](const String& resp){ wifi_broadcast(resp); });
    http.send(202, "application/json", "{\"queued\":true}");
  });
  http.begin();
  ws.begin();
  ws.onEvent([](uint8_t num, WStype_t t, uint8_t* p, size_t len){
    if (t == WStype_TEXT) {
      String in((const char*)p, len);
      api_handle(in, [num](const String& resp){ String s = resp; ws.sendTXT(num, s); });
    }
  });
}
void wifi_loop() { http.handleClient(); ws.loop(); }
void wifi_broadcast(const String& s) { String m = s; ws.broadcastTXT(m); }
