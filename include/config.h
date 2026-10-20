#pragma once
// Values can be overridden per build env with -D flags in platformio.ini.
// ---- Pin map (ESP32-S3 DevKitC-1 defaults; change to match your board) ----
#ifndef MODEM_UART_NUM
#define MODEM_UART_NUM   1
#endif
#ifndef MODEM_TX_PIN
#define MODEM_TX_PIN     17   // ESP TX -> modem RXD
#endif
#ifndef MODEM_RX_PIN
#define MODEM_RX_PIN     18   // ESP RX <- modem TXD
#endif
#ifndef MODEM_PWRKEY_PIN
#define MODEM_PWRKEY_PIN 4    // via NPN/MOSFET to modem PWRKEY (active pulse)
#endif
#ifndef MODEM_RESET_PIN
#define MODEM_RESET_PIN  5    // optional, -1 if unused
#endif
#ifndef MODEM_RI_PIN
#define MODEM_RI_PIN     6    // ring indicator (optional wake)
#endif
#ifndef MODEM_BAUD
#define MODEM_BAUD       115200
#endif
#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID     "VictorModem"
#endif
#ifndef WIFI_AP_PASS
#define WIFI_AP_PASS     "changeme123"   // >= 8 chars; change before use
#endif
#ifndef API_TOKEN
#define API_TOKEN        "change-this-token"
#endif
#ifndef DEFAULT_APN
#define DEFAULT_APN      ""              // set to your carrier APN
#endif
#ifndef MODEM_FLIGHT_PIN
#define MODEM_FLIGHT_PIN -1   // LilyGO T-SIM7600: GPIO25, driven HIGH = RF on
#endif
// ---- Bridge mode (VM_MODE_BRIDGE): UART link to the Raspberry Pi 5 hub (3.3 V both sides) ----
#ifndef HUB_UART_NUM
#define HUB_UART_NUM 1
#endif
#ifndef HUB_TX_PIN
#define HUB_TX_PIN 17         // ESP TX -> Pi GPIO15 (RXD, header pin 10)
#endif
#ifndef HUB_RX_PIN
#define HUB_RX_PIN 18         // ESP RX <- Pi GPIO14 (TXD, header pin 8)
#endif
#ifndef HUB_BAUD
#define HUB_BAUD 921600
#endif
