// Fleet USB serial provisioning contract v1: JSON lines over Serial @115200.
#include "fleet_config.h"
#include "config.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define VM_CHIP "esp32s3"
#else
#define VM_CHIP "esp32"
#endif

static const char* NS = "fleet";
static const uint32_t WINDOW_MS = 10000;

enum KType { K_STR, K_U32 };
struct KeyDef { const char* name; KType type; int minv; long maxv; bool secret; const char* defS; uint32_t defU; };

#ifndef VM_MODE_BRIDGE
#define VM_HAS_APN 1
#endif

static const KeyDef KEYS[] = {
  {"ap_ssid",   K_STR, 1, 32,  false, WIFI_AP_SSID, 0},
  {"ap_pass",   K_STR, 8, 63,  true,  WIFI_AP_PASS, 0},
  {"api_token", K_STR, 16, 128, true, API_TOKEN, 0},
#ifdef VM_HAS_APN
  {"apn",       K_STR, 0, 63,  false, DEFAULT_APN, 0},
  {"apn_user",  K_STR, 0, 63,  false, "", 0},
  {"apn_pass",  K_STR, 0, 63,  true,  "", 0},
#endif
#ifdef VM_MODE_BRIDGE
  {"hub_baud",  K_U32, 9600, 3000000, false, "", HUB_BAUD},
#endif
};
static const size_t NKEYS = sizeof(KEYS) / sizeof(KEYS[0]);
static String   sVal[NKEYS];
static uint32_t uVal[NKEYS];
static bool     provisioned = false;
static uint32_t bootMs = 0;
static String   line;

static int idx(const char* n) { for (size_t i = 0; i < NKEYS; i++) if (!strcmp(KEYS[i].name, n)) return i; return -1; }
static const char* s(const char* n) { int i = idx(n); return i < 0 ? "" : sVal[i].c_str(); }

void cfg_begin() {
  Preferences p; p.begin(NS, true);
  for (size_t i = 0; i < NKEYS; i++) {
    if (KEYS[i].type == K_STR) sVal[i] = p.isKey(KEYS[i].name) ? p.getString(KEYS[i].name) : String(KEYS[i].defS);
    else uVal[i] = p.isKey(KEYS[i].name) ? p.getUInt(KEYS[i].name) : KEYS[i].defU;
  }
  provisioned = p.isKey("api_token");
  p.end();
}
const char* cfg_ap_ssid()   { return s("ap_ssid"); }
const char* cfg_ap_pass()   { return s("ap_pass"); }
const char* cfg_api_token() { return s("api_token"); }
const char* cfg_apn()       { return s("apn"); }
const char* cfg_apn_user()  { return s("apn_user"); }
const char* cfg_apn_pass()  { return s("apn_pass"); }
uint32_t cfg_hub_baud()     { int i = idx("hub_baud"); return i < 0 ? HUB_BAUD : uVal[i]; }
bool cfg_provisioned()      { return provisioned; }

static void send(JsonDocument& d) { serializeJson(d, Serial); Serial.print('\n'); }
static void fail(const String& e) { JsonDocument d; d["v"] = 1; d["ok"] = false; d["err"] = e; send(d); }
static void okv() { JsonDocument d; d["v"] = 1; d["ok"] = true; send(d); }

static void doProvision(JsonVariantConst cfg) {
  if (!cfg.is<JsonObjectConst>()) return fail("cfg must be object");
  JsonObjectConst o = cfg.as<JsonObjectConst>();
  if (o.size() == 0) return fail("cfg empty");
  for (JsonPairConst kv : o) {             // validate everything before writing anything
    int i = idx(kv.key().c_str());
    if (i < 0) return fail(String("unknown key: ") + kv.key().c_str());
    const KeyDef& k = KEYS[i];
    if (k.type == K_STR) {
      if (!kv.value().is<const char*>()) return fail(String(k.name) + ": must be string");
      size_t n = strlen(kv.value().as<const char*>());
      if ((int)n < k.minv || (long)n > k.maxv) return fail(String(k.name) + ": length must be " + k.minv + "-" + k.maxv);
    } else {
      if (!kv.value().is<uint32_t>()) return fail(String(k.name) + ": must be integer");
      uint32_t v = kv.value().as<uint32_t>();
      if ((long)v < k.minv || (long)v > k.maxv) return fail(String(k.name) + ": must be " + k.minv + "-" + k.maxv);
    }
  }
  Preferences p;
  if (!p.begin(NS, false)) return fail("nvs open failed");
  for (JsonPairConst kv : o) {
    const KeyDef& k = KEYS[idx(kv.key().c_str())];
    bool w = k.type == K_STR ? p.putString(k.name, kv.value().as<const char*>()) > 0 || !strlen(kv.value().as<const char*>())
                             : p.putUInt(k.name, kv.value().as<uint32_t>()) > 0;
    if (!w) { p.end(); return fail(String("nvs write failed: ") + k.name); }
  }
  p.end();
  cfg_begin();
  okv();
}

static void handle(const String& l) {
  JsonDocument in;
  if (deserializeJson(in, l)) return fail("bad json");
  if ((in["v"] | 0) != 1) return fail("unsupported version");
  String cmd = in["cmd"] | "";
  if (cmd == "hello") {
    JsonDocument d; d["v"] = 1; d["ok"] = true; d["project"] = "victormodem";
    d["fw"] = VM_FW_VERSION; d["env"] = VM_PIO_ENV; d["chip"] = VM_CHIP;
    JsonArray k = d["keys"].to<JsonArray>();
    for (size_t i = 0; i < NKEYS; i++) k.add(KEYS[i].name);
    send(d);
  } else if (cmd == "get") {
    JsonDocument d; d["v"] = 1; d["ok"] = true; d["provisioned"] = provisioned;
    JsonObject c = d["cfg"].to<JsonObject>();
    for (size_t i = 0; i < NKEYS; i++) {
      if (KEYS[i].secret) c[KEYS[i].name] = "***";
      else if (KEYS[i].type == K_STR) c[KEYS[i].name] = sVal[i];
      else c[KEYS[i].name] = uVal[i];
    }
    send(d);
  } else if (cmd == "provision") {
    doProvision(in["cfg"]);
  } else if (cmd == "reboot") {
    okv(); Serial.flush(); delay(100); ESP.restart();
  } else if (cmd == "factory_reset") {
    Preferences p; p.begin(NS, false); bool c = p.clear(); p.end();
    if (!c) return fail("nvs clear failed");
    cfg_begin(); okv();
  } else fail("unknown cmd");
}

void prov_begin() { bootMs = millis(); }

void prov_loop() {
  bool open = !provisioned || millis() - bootMs < WINDOW_MS;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (open && line.length() && line[0] == '{') handle(line);
      line = "";
    } else if (line.length() < 1024) line += c;
  }
}
