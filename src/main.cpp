// VictorModem — ESP32-S3 + cellular modem as a second phone line.
// Legal use only: no IMEI changes, carrier unlocking, SIM cloning, or interception.
#include <Arduino.h>
#include "config.h"
#ifndef VM_MODE_BRIDGE
#include "modem.h"
#endif
#include "services.h"

void setup() {
  Serial.begin(115200);
#ifdef VM_MODE_BRIDGE
  hub_begin();
  Serial.println("VictorModem bridge mode -> Pi 5 hub");
#else
  modem.begin([](const String& type, const String& data){
    String ev = "{\"type\":\"event\",\"event\":\"" + type + "\",\"data\":" + data + "}";
    Serial.println(ev);
    api_broadcast(ev);
    if (type == "sms_received") {          // auto-fetch body for the phone
      JsonDocument d; deserializeJson(d, data);
      sms_read(d["index"] | 0, [](const String& r){ api_broadcast("{\"type\":\"event\",\"event\":\"sms\",\"data\":" + r + "}"); });
    }
  });
  modem.powerOn();
  modem.detect();
  Serial.printf("Modem: %s\n", modelName(modem.model()));
#endif
  ble_begin();
  wifi_begin();
}

void loop() {
#ifdef VM_MODE_BRIDGE
  hub_loop();
#else
  modem.loop();
#endif
  wifi_loop();
  delay(1);
}
