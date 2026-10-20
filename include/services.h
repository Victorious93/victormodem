#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
// Command router: JSON in -> modem ops; reply/events via sink.
using ReplyFn = std::function<void(const String& json)>;
void voice_dial(const String& num, ReplyFn r);
void voice_answer(ReplyFn r);
void voice_hangup(ReplyFn r);
void sms_init();
void sms_send(const String& num, const String& text, ReplyFn r);
void sms_read(int idx, ReplyFn r);
void sms_list(const String& filter, ReplyFn r);
void data_connect(const String& apn, ReplyFn r);
void data_disconnect(ReplyFn r);
void data_tcp_get(const String& host, int port, const String& path, ReplyFn r);
void status_get(ReplyFn r);
void api_handle(const String& json, ReplyFn r);   // protocol entry point
void api_broadcast(const String& json);           // to BLE + WS clients
void ble_begin(); void ble_notify(const String& s);
void wifi_begin(); void wifi_loop(); void wifi_broadcast(const String& s);
String jesc(const String& s);
// Bridge mode (Pi 5 hub over UART)
void hub_begin(); void hub_loop(); bool hub_online();
void hub_forward(const String& json, ReplyFn r);
