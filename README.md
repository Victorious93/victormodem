<p align="center"><img src="assets/logo.jpg" width="200" alt="VictorModem neon ring logo"></p>

# VictorModem

Don Victor's second phone line in a box: an **ESP32-S3** drives a **cellular modem** (SIMCom SIM7600, SIMCom A7670, or Quectel EC25) with **its own SIM**. Your phone connects over **BLE** or **Wi-Fi** to place and answer calls, send and receive SMS, and use the modem's data connection.

Two first-class builds share **one phone protocol** (BLE GATT + Wi-Fi HTTP/WebSocket JSON, see *Protocol*), so the Android app doesn't change between them:

| Build | Hardware | PlatformIO env | Who speaks AT |
|---|---|---|---|
| **5G** | Raspberry Pi 5 + Waveshare PCIe to 5G HAT+ (M.2 Key B) + SIMCom SIM8262A-M2, with an ESP32-S3 as the BLE/Wi-Fi bridge | `esp32s3_bridge5g` + `hub/` (Python daemon on the Pi) | Pi hub over USB `/dev/ttyUSB*` |
| **4G** | LilyGO T-SIM7600G-H (SIM7600G-H, Verizon-certified VoLTE) | `tsim7600_4g` (runs on the board's own ESP32) or `esp32s3_direct4g` (separate ESP32-S3 wired to the modem UART) | ESP32 directly |

Brand: neon ring, cyan `#00F0FF` to magenta `#FF00E5`, on near-black `#0A0A12`.

## Legal use only
Use it only with a SIM and service plan you own. The firmware does not and will not change or spoof the IMEI, carrier-unlock the modem, clone SIMs, or intercept anyone else's traffic. Follow local rules for call recording and automated SMS.

## Wiring (4G direct build)

| ESP32-S3 (DevKitC-1) | Modem | Notes |
|---|---|---|
| GPIO17 (TX) | RXD | Check the logic level. SIM7600/EC25 UART is 1.8 V on many bare modules, so use a level shifter (e.g. TXS0108E). Breakout boards often already shift to 3.3 V |
| GPIO18 (RX) | TXD | Same as above |
| GPIO4 | PWRKEY | Through an NPN/N-MOSFET (HIGH on the GPIO pulls PWRKEY low for about 600 ms). Many boards also have a PWRKEY jumper or auto-on |
| GPIO5 | RESET (optional) | Open-drain via transistor |
| GPIO6 | RI (optional) | Ring indicator, for waking from sleep |
| GND | GND | Common ground is required |

**Power:** the modem needs **3.8–4.2 V (VBAT) at 2 A peak**, because GSM/LTE TX bursts are large. Use a 1S Li-ion cell or a buck converter rated 3 A, plus at least 1000 µF of low-ESR bulk capacitance close to the module. **Do not power the modem from the ESP32's 3.3 V pin or from USB alone.** Brown-outs show up as random resets or `+CPIN: NOT READY`. Connect an LTE antenna (main, plus diversity if available) before powering on.

Pins are set in `include/config.h`.

## 5G architecture (Pi 5 hub)
```mermaid
flowchart LR
  Phone["Moto G Play (Android app)"] -- "BLE GATT JSON" --> ESP["ESP32-S3<br/>env esp32s3_bridge5g"]
  Phone -- "Wi-Fi WS :81 / HTTP :80" --> ESP
  Phone -. "optional: LAN REST/WS :8080 (Bearer token)" .-> HUB
  ESP -- "UART 921600, 3.3 V<br/>NDJSON (GPIO14/15)" --> HUB["Pi 5: victormodem-hub<br/>FastAPI + AT engine"]
  HUB -- "USB AT /dev/ttyUSBx<br/>ATD/ATA/CHUP, CMGS/CMGL, +CMTI" --> MOD["SIM8262A-M2<br/>(5G HAT+, M.2 Key B)"]
  MM["ModemManager / QMI<br/>or RNDIS"] -- data --> MOD
  HUB -- "mmcli simple-connect" --> MM
  MOD -- "PCM/I2S audio (needs codec bridge)" -.-> AUD["I2S codec -> Pi or ESP32 (TODO)"]
  MOD --- SIM["Visible / Total Wireless SIM (Verizon)"]
```
The ESP32 in bridge mode re-numbers each request id, forwards it to the hub unchanged (the **hub checks the token**), maps the response back to the BLE/WS client that asked, and broadcasts hub events (`ring`, `incoming_call`, `sms`, `call_ended`, ...). It pings the hub every 5 s and answers `hub offline` if the hub hasn't replied for 15 s.

UART wiring (both sides are 3.3 V, so no level shifter): ESP GPIO17 TX -> Pi **GPIO15 RXD (pin 10)**, ESP GPIO18 RX <- Pi **GPIO14 TXD (pin 8)**, GND <-> GND (pin 6). Don't connect the 5 V rails. On the Pi, add `dtparam=uart0=on` to `/boot/firmware/config.txt` and disable the serial console (`raspi-config` > Interface > Serial: login shell **No**, hardware **Yes**). The port is `/dev/ttyAMA0`.

### Hub (`hub/`)
Python 3.11+, FastAPI, pyserial. Config is at `/etc/victormodem/config.toml` (see `hub/deploy/config.toml.example`), state (SMS log `sms.jsonl`) at `/var/lib/victormodem`, systemd unit `hub/deploy/victormodem-hub.service`, installer `hub/deploy/install.sh`.
- AT engine: one command in flight, URC dispatch (RING, +CLIP, +CMTI, NO CARRIER, VOICE CALL), `> ` prompt for CMGS, timeouts with recovery. `at_port = "auto"` probes `/dev/ttyUSB*` for a port that answers `AT`.
- REST (header `Authorization: Bearer <token>`): `GET /api/status`, `POST /api/call/dial|answer|hangup`, `GET /api/calls`, `POST /api/sms`, `GET /api/sms[?filter=]`, `GET|DELETE /api/sms/{i}`, `POST /api/data/up|down`, `POST /api` (generic JSON protocol). WebSocket: `/ws?token=...` (requests + pushed events). `GET /healthz` needs no auth.
- Data: `data_backend = "modemmanager"` (`mmcli --simple-connect`, QMI/MBIM) or `"rndis"` (`AT$QCRMCALL`).
- Tests: `cd hub && pip install -e .[test] && pytest`. These use a scripted fake SIM8262A (`tests/fake_modem.py`).
- If ModemManager is running, it may also probe the AT port. Pin the hub's port with a udev rule plus `ID_MM_PORT_IGNORE=1` on that one tty (keep the QMI/cdc-wdm port for MM).

## 4G architecture (direct)
```
 Phone (Android app / any BLE or WS client)
     │ BLE GATT (JSON lines)      │ Wi-Fi AP 192.168.4.1: HTTP :80 /api, WebSocket :81
     ▼                            ▼
 ┌───────────── api.cpp: JSON protocol router + token auth ─────────────┐
 │ services.cpp: voice (ATD/ATA/ATH|CHUP) · SMS (CMGF=1, CMGS/CMGR/CMGL)│
 │               data (CGDCONT, NETOPEN/CIPOPEN or QIACT/QIOPEN) · status│
 ├──────────────── modem.cpp: UART AT driver ───────────────────────────┤
 │ FIFO command queue (one command in flight, per-command timeout,      │
 │ "> " prompt + payload + Ctrl-Z) · URC parser (RING, +CLIP, +CMTI,    │
 │ NO CARRIER…) → events · auto-detect via ATI (SIM7600 / A7670 / EC25) │
 └──────────────────────────────────────────────────────────────────────┘
     │ UART1 115200 8N1 + PWRKEY
     ▼
  Cellular modem + SIM + antenna
```
Model differences: hangup uses `AT+CHUP` on SIMCom and `ATH` on EC25. TCP uses `NETOPEN/CIPOPEN/CIPSEND` on SIMCom and `QICSGP/QIACT/QIOPEN/QISEND` on Quectel.

## Protocol (v0)
The same UTF-8 JSON is used on every transport. On BLE, each message ends with `\n` and can arrive in several notify chunks.

- BLE service `7a1c0001-5e5f-4d1b-9c3a-56c7f0a0b001`. RX (write) is `…0002…`, TX (notify) is `…0003…`.
- WebSocket `ws://192.168.4.1:81/`. HTTP `POST /api` returns `202`, and the result is pushed over WebSocket.

Request: `{"id":1,"token":"…","cmd":"dial","number":"+15551234567"}`

| cmd | params |
|---|---|
| `dial` | `number` |
| `answer` / `hangup` | none |
| `sms_send` | `number`, `text` (1–160 GSM chars) |
| `sms_read` | `index` |
| `sms_list` | `filter` (`ALL`, `REC UNREAD`, …) |
| `data_up` / `data_down` | `apn` (optional) |
| `http_get` | `host`, `port`, `path` (demonstrates the modem's TCP stack) |
| `status` | returns model, CSQ, CREG, COPS |

Response: `{"type":"response","id":1,"result":{"ok":true,...}}`

Events: `{"type":"event","event":"incoming_call","data":{"number":"+1555…"}}`. Event names: `ring`, `incoming_call`, `call_ended`, `sms_received` (index), `sms` (auto-fetched body), `urc` (raw line).

## Build
```
pip install platformio
pio run                                   # builds all three envs
pio run -e esp32s3_bridge5g -t upload     # 5G bridge ESP32-S3
pio run -e tsim7600_4g -t upload          # 4G on LilyGO T-SIM7600G-H
pio run -e esp32s3_direct4g -t upload     # 4G, S3 wired to a SIM7600 module
```
`tsim7600_4g` uses the LilyGO pin map (TX27/RX26/PWRKEY4/RI33/FLIGHT25). Check it against your board revision.
Before use, change `WIFI_AP_PASS`, `API_TOKEN` and `DEFAULT_APN` in `include/config.h`.

## Roadmap
1. **Audio path.** Today, call audio goes to the modem's own analog/PCM audio pins (handset or speaker). Next is the modem PCM/I2S feeding the ESP32-S3 I2S, then streaming to the phone (BLE has too little bandwidth for good audio, so use Wi-Fi with Opus or G.711 over WS/RTP). The alternative is a Bluetooth Classic HFP module, since the S3 has no BR/EDR.
2. **PPPoS.** Move to ESP-IDF `esp_modem` with CMUX so data (PPP) and AT control share the UART. Then the ESP32 can route phone traffic over LTE (Wi-Fi AP + NAPT).
3. SMS: PDU mode, UCS2/emoji, concatenated messages, a store on NVS/SD, and delivery reports.
4. Security: BLE pairing with a passkey (LE Secure Connections), a per-device token in NVS, and an optional WPA3 AP.
5. Reliability: modem watchdog and re-init, network registration events, low-power mode with RI wake, OTA updates.
6. **Companion Android app** (Kotlin + Jetpack Compose, neon-ring theme): BLE/WS client, dialer, in-call screen, SMS threads, notifications via a foreground service, signal and data dashboard, and later audio streaming and Android `ConnectionService` integration.

## BOM (prices as of Oct 2026; **(unverified)** = not checked against a live listing)

**5G build**
| Part | Price | Source |
|---|---|---|
| Waveshare PCIe to 5G HAT+ with SIM8262A-M2 (antennas included) | $349.99 | https://www.amazon.com/dp/B0D4DRD5H2 |
| Raspberry Pi 5, 4 GB (8 GB is about $80) | ~$60 **(unverified)** | raspberrypi.com resellers |
| Official Raspberry Pi 27 W USB-C PSU | ~$12 **(unverified)** | |
| Official Pi 5 Active Cooler | ~$5 **(unverified)** | |
| microSD 32 GB A2 (NVMe boot isn't an option because the HAT uses the Pi 5 PCIe port) | ~$10 **(unverified)** | |
| ESP32-S3-DevKitC-1 (BLE/Wi-Fi bridge) | ~$15 **(unverified)** | |
| 5 V/5 A buck or 5 V 5 A supply for the HAT's separate input (see Power) | ~$15 **(unverified)** | |
| I2S/PCM codec bridge for call audio (e.g. WM8960 HAT or MAX98357+INMP441 on ESP32) | ~$10–25 **(unverified)** | |
| Visible / Total Wireless physical SIM | plan | |
| **Total** | **~$480–500** | |

**4G build**
| Part | Price | Source |
|---|---|---|
| LilyGO T-SIM7600G-H (ESP32 + SIM7600G-H, Verizon VoLTE) | $63 | https://www.amazon.com/dp/B0C6PPR3V1 |
| 18650 cell (board has a holder/charger) or 5 V 2 A+ USB supply | ~$8 **(unverified)** | |
| LTE antenna (often included) / GPS antenna | included / ~$5 **(unverified)** | |
| **Total** | **~$70–80** | |

## Power (5G)
- Pi 5 needs the **27 W (5 V 5 A)** PSU. Otherwise it caps USB current and warns about undervoltage.
- The SIM8262A draws **2–3 A bursts** at its 3.3–4.2 V rail during 5G/LTE TX. Power the 5G HAT+ from its **own 5 V regulator/supply** (rated ≥3 A continuous, ideally 5 A) rather than leeching from the Pi's 5 V through the header or PCIe FFC. Share GND only. Keep bulk capacitance near the module, use short, thick leads, and expect brownout symptoms (`+CPIN: NOT READY`, USB re-enumeration) if the supply sags.
- Power the ESP32-S3 from its USB port or from the Pi's 3.3 V (pin 1, <500 mA total budget), not both at once.

## Thermals (5G)
SIM8262A modules get hot under sustained 5G data (heat-pad the module to the HAT's heatsink/case). Use the Pi 5 Active Cooler, a ventilated case, and spacing between the Pi SoC and the HAT. Monitor with `vcgencmd measure_temp` and `AT+CPMUTEMP`. Expect throttling in closed enclosures, and keep it out of direct sun or a hot car.

## Open risks
1. **Verizon voice certification.** The SIM8262A-M2 is not (as far as we can find) on Verizon's approved/certified device list for VoLTE voice. Data may work, but **calls may be refused or VoLTE may not provision**. The 4G build (SIM7600G-H, Verizon-certified) is the safe voice path.
2. **Call audio is PCM/I2S only** on the SIM8262A-M2 (no analog). Calls connect, but you'll hear nothing until a codec bridge exists: PCM into a Pi I2S codec (WM8960-class) or into the ESP32-S3 I2S, then streamed to the phone over Wi-Fi (Opus/G.711). The M.2 PCM pins must also be routed out on the HAT. Verify against the Waveshare schematic.
3. **Visible / Total Wireless may refuse the device IMEI** (non-phone/module IMEI not in their compatibility database, or an IMEI unlock check). We will **not** change or spoof the IMEI. If activation fails, ask the carrier, or use a plan that supports IoT/hotspot modules.
4. Untested on hardware: SIM8262A USB port layout (which ttyUSB is AT), `AT$QCRMCALL` RNDIS usage, and `AT+C5GREG` availability depend on firmware. The hub probes and fails soft.
5. SMS is GSM 7-bit text mode only (no UCS2/emoji or concatenation yet), and the bridge's BLE notify throughput is limited (~180 B chunks).
6. Security: one shared token, Wi-Fi AP with WPA2 PSK, and a plain-HTTP hub API. Put the hub on a trusted LAN or behind TLS, and change every default in `config.h` and `config.toml`.

## Android companion app (`android/`)

Kotlin + Jetpack Compose, minSdk 26 / targetSdk 35. It's built for the Moto G Play 2025/2026. It connects over BLE GATT (service `7a1c0001-…`, RX write `…0002…`, TX notify `…0003…`, newline-reassembled), the ESP32 WebSocket `ws://192.168.4.1:81/`, or the Pi hub `ws://<hub>:8080/ws?token=…`. Screens: Connect (token entry), Dialer, SMS, Status. It posts notifications for incoming calls and SMS.

Build: `cd android && ./gradlew assembleDebug` → `app/build/outputs/apk/debug/app-debug.apk`.
