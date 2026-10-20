// Transport-agnostic JSON protocol (see README "Protocol").
#include "services.h"
#include "config.h"

void api_broadcast(const String& json) { ble_notify(json); wifi_broadcast(json); }

void api_handle(const String& in, ReplyFn r) {
#ifdef VM_MODE_BRIDGE
  hub_forward(in, r);   // the Pi hub validates the token and runs the command
#else
  JsonDocument d;
  if (deserializeJson(d, in)) return r("{\"ok\":false,\"error\":\"bad json\"}");
  int id = d["id"] | 0;
  String cmd = d["cmd"] | "";
  ReplyFn wrap = [r, id](const String& body) {
    r("{\"type\":\"response\",\"id\":" + String(id) + ",\"result\":" + body + "}"); };
  if (String(d["token"] | "") != API_TOKEN) return wrap("{\"ok\":false,\"error\":\"unauthorized\"}");
  if      (cmd == "dial")     voice_dial(d["number"] | "", wrap);
  else if (cmd == "answer")   voice_answer(wrap);
  else if (cmd == "hangup")   voice_hangup(wrap);
  else if (cmd == "sms_send") sms_send(d["number"] | "", d["text"] | "", wrap);
  else if (cmd == "sms_read") sms_read(d["index"] | 0, wrap);
  else if (cmd == "sms_list") sms_list(d["filter"] | "ALL", wrap);
  else if (cmd == "data_up")  data_connect(d["apn"] | "", wrap);
  else if (cmd == "data_down")data_disconnect(wrap);
  else if (cmd == "http_get") data_tcp_get(d["host"] | "", d["port"] | 80, d["path"] | "/", wrap);
  else if (cmd == "status")   status_get(wrap);
  else wrap("{\"ok\":false,\"error\":\"unknown cmd\"}");
#endif
}
