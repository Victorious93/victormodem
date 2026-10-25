#pragma once
// Runtime configuration: NVS (Preferences namespace "fleet") overrides config.h defaults.
// Written by the USB serial provisioning contract v1 (see README "Fleet USB provisioning").
#include <Arduino.h>

#ifndef VM_FW_VERSION
#define VM_FW_VERSION "0.0.0-dev"
#endif
#ifndef VM_PIO_ENV
#define VM_PIO_ENV "unknown"
#endif

void        cfg_begin();              // load NVS; call before anything reads config
const char* cfg_ap_ssid();
const char* cfg_ap_pass();
const char* cfg_api_token();
const char* cfg_apn();
const char* cfg_apn_user();
const char* cfg_apn_pass();
uint32_t    cfg_hub_baud();
bool        cfg_provisioned();        // api_token present in NVS

void prov_begin();                    // record boot time
void prov_loop();                     // poll Serial for provisioning commands
