# Design proposal: Victorious Mesh relay nodes

Status: Draft · Author: Don Victor · Date: 2026-10-20

Goal: cheap ESP32 relay nodes that move **end-to-end encrypted** messages and small files between phones running Victorious Mesh (`com.victorious.mesh`, BSD) with no Wi-Fi or cell service. Radios: LoRa (SX1262, US 915 MHz) and/or ESP-NOW. Optional uplink through a VictorModem (ESP32-S3 + cellular modem, firmware in this repo). The phone talks to a node over BLE, or over Wi-Fi like the existing VictorModem API (BLE GATT service `7a1c0001-5e5f-4d1b-9c3a-56c7f0a0b001`, WebSocket on port 81, JSON).

Labels: **[V]** checked against the linked source on 2026-10-20. **[U]** unverified, an estimate, or needs a lab or legal check. Prices change all the time. No purchases were made.

---

## 1. Hardware recommendation

| Board | MCU / radio | Price (USD) | Where | Notes |
|---|---|---|---|---|
| **Heltec WiFi LoRa 32 V3** (902–928 MHz) | ESP32-S3FN8 + SX1262, 0.96" OLED, LiPo charger, U.FL | $17.90–19.90 from Heltec [V]; $29.97 Rokland [V]; $34.99 Amazon [V] | https://heltec.org/project/wifi-lora-32-v3/ · https://store.rokland.com/products/heltec-wifi-lora-32v3 · https://www.amazon.com/dp/B0D1H1FN9Y | **Default dev and relay board.** Cheap, and it is the same MCU family as VictorModem. No PSRAM (8 MB flash), so the store-and-forward queue goes in flash. V4 is listed at $17.90–27.50 [V]. |
| **LilyGO T3-S3** (SX1262 915) | ESP32-S3 + SX1262, OLED, SD slot | from $16.55 at LilyGO [V]; $16.45 Tindie (V1.3) [V] | https://lilygo.cc/en-us/collections/lora-or-gps · https://www.tindie.com/products/lilygo/t3-s3-v13-esp32-s3-sx1262-lora-dev-board/ | The SD slot is handy for a large store-and-forward spool. Has a PSRAM variant [U]. |
| **LilyGO T-Beam Supreme** | ESP32-S3 + SX1262, GNSS, 8 MB PSRAM, 18650 holder | $37.12 at LilyGO [V]; $47–65 at resellers (rfindex) [V] | https://lilygo.cc/en-us/products/t-beam-supreme | **Best fixed or solar relay** (18650 battery, GPS time and position, PSRAM). |
| **Seeed XIAO ESP32S3 + Wio-SX1262 kit** | ESP32-S3 (8 MB PSRAM) + SX1262 | $10.99 at Seeed [V]; $15.19 Amazon [V] | https://www.seeedstudio.com/Wio-SX1262-with-XIAO-ESP32S3-p-5982.html · https://www.amazon.com/dp/B0F8TSW1WZ | **Cheapest per node.** No screen or case. Good for volume deployments. |
| **RAK WisBlock RAK4631** | nRF52840 + SX1262 (**no ESP32, no Wi-Fi/ESP-NOW**) | Starter kit $19.50–25.70 [V]; Meshtastic kit $24.99–60.99 [V] | https://store.rakwireless.com/products/wisblock-starter-kit · https://store.rakwireless.com/products/wisblock-meshtastic-starter-kit | Very low power. A poor fit for our ESP32 firmware, but a strong choice for the separate Meshtastic device (§4). |

**Recommendation:** Heltec V3 for development and portable nodes. XIAO kit for cheap fleet nodes. T-Beam Supreme for solar or rooftop relays. Order only **902–928 MHz** SKUs, and add a properly tuned 915 MHz antenna. Never transmit without an antenna attached.

### LoRa vs ESP-NOW

| | LoRa SX1262 @ 915 MHz | ESP-NOW (2.4 GHz Wi-Fi vendor action frames) |
|---|---|---|
| Range | Roughly 1–10+ km line of sight, a few hundred m in town [U, depends on terrain and antenna] | Roughly 100–200 m line of sight with PCB antennas; longer with Long-Range mode [U] |
| Throughput | About 0.3–22 kbps depending on SF/BW [U]; one frame takes hundreds of ms | About 1 Mbps or more. Payload is 250 B (v1.0), or up to 1470 B with ESP-NOW v2.0 (ESP-IDF ≥ 5.4) [V: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html] |
| Payload per frame | Up to 255 B (SX1262 FIFO) [V: Semtech SX1262 datasheet] | 250 / 1470 B |
| Power | TX up to +22 dBm, about 100+ mA while transmitting | Wi-Fi radio. Higher idle power unless duty-cycled |
| Co-existence | Separate radio | Shares the 2.4 GHz radio with the BLE/Wi-Fi link to the phone (time-sliced) |
| Use | Long-haul backbone | Dense local clusters, fast bulk file hops between nearby nodes |

Plan: **LoRa is the backbone. ESP-NOW is an opportunistic fast path** between nodes that hear each other well (good RSSI, or a node discovered over ESP-NOW broadcast).

### US regulatory requirement (HARD): FCC Part 15.247, 902–928 MHz

**This is a hard requirement, not a guideline.** Every LoRa transmission from our firmware MUST meet 47 CFR §15.247 *and* stay inside the conditions of the FCC grant of the radio it runs on. Firmware that cannot prove this at compile time must not build.

Rule text, from eCFR 47 CFR 15.247 [V: https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/subject-group-ECFR2f2e5828339709e/section-15.247]:
- (a)(1)(i) **FHSS**: if the 20 dB BW is < 250 kHz, use **≥ 50 hop channels** with average occupancy **≤ 0.4 s per channel in any 20 s**. If it is ≥ 250 kHz (max 500 kHz), use ≥ 25 channels with ≤ 0.4 s per 10 s. Hops follow a pseudo-random list, every channel is used equally, and receivers hop in sync.
- (a)(2) **Digital modulation (DTS)**: **6 dB BW ≥ 500 kHz**.
- (b)(2)/(b)(3): **1 W** max conducted (FHSS ≥ 50 ch, or DTS). 0.25 W for FHSS with 25–49 channels.
- (b)(4): if antenna gain is **> 6 dBi**, reduce conducted power by the excess dB.
- (e) DTS **PSD ≤ 8 dBm in any 3 kHz**.
- (g) An FHSS system using short bursts must still spread across the minimum channel count. **So single-channel LoRa at 125 or 250 kHz is neither valid FHSS nor DTS.** Our old default (SF7/8 at BW250 on one channel) was **non-compliant and is removed.**

Practice elsewhere: Meshtastic ≥ 2.8 now moves US nodes off LongFast (BW250) to LongTurbo (BW500) because "Long Fast's bandwidth is not US-compliant" [V: https://meshtastic.org/docs/software/apple/user/settings/]. LoRaWAN US915 uses 64 × 125 kHz hopping uplinks (902.3–914.9 MHz, FHSS/hybrid) plus 8 × 500 kHz DTS uplinks (903.0–914.2 MHz, 1.6 MHz spacing) and 8 × 500 kHz downlinks (923.3–927.5 MHz) [U: LoRaWAN Regional Parameters RP002]. The module grants below were certified against exactly these ranges.

### Chosen radio profile: **(b) DTS, LoRa BW 500 kHz, US915 region lock**

We chose DTS over FHSS. It needs no hop synchronisation, which an ad-hoc flood mesh cannot easily provide (FHSS also needs ≥ 50 channels used equally and receivers that hop in sync, per (a)(1)). The cost is about 3 dB less sensitivity than BW250 at the same SF.

| Parameter | Value | Enforced in code (`include/mesh_radio_limits.h`) |
|---|---|---|
| Region | **US915 only**. Compile-time lock (`MESH_REGION_US915`); any other region causes `#error` | #error |
| Modulation | LoRa, **BW = 500 kHz only** [U: a lab must measure that the SX1262 BW500 6 dB BW is ≥ 500 kHz; the RAK and Seeed grants list the 903–914.2 BW500 range] | `static_assert(MESH_LORA_BW_HZ == 500000)`; there is no API to set another BW |
| Channel plan | The 8 LoRaWAN US915 BW500 uplink centres: **903.0 + 1.6·n MHz, n = 0…7 (903.0–914.2)**. Default **n = 4 → 909.4 MHz**. Single channel (DTS has no hopping requirement) | index range-checked; frequency comes from a table |
| Spreading factor | **SF7 default (data)**, SF8 and SF9 allowed. SF10–12 are rejected | `MESH_SF_MIN=7`, `MESH_SF_MAX=9` |
| Coding / preamble | CR 4/5, 8-symbol preamble, explicit header, CRC on | constants |
| Max frame | 222 B PHY payload | `MESH_MAX_PHY_PAYLOAD` |
| Max airtime per frame | **≤ 400 ms hard cap** (not required for DTS; we keep it for fairness and for headroom). Airtime is computed with the Semtech formula before each TX, and the frame is refused if over the cap. SF7/222 B ≈ 87 ms; SF8/222 B ≈ 154 ms; SF9/222 B ≈ 277 ms [math, Semtech formula] | `mesh_tx_allowed()` |
| Channel utilisation | ≤ 10% per node over a sliding 60 s window (an etiquette budget, not an FCC rule) | `MESH_DUTY_PERMILLE=100` |
| TX power | `min(board grant cap, 30 dBm − max(0, G_ant − 6))`. The SX1262 maximum is +22 dBm, so §15.247's 1 W never binds; the grant does | `MESH_TX_DBM_MAX` + `mesh_clamp_tx_dbm()` |
| PSD | At +22 dBm over 500 kHz, ≈ 22 − 10·log10(500/3) ≈ −0.2 dBm/3 kHz, well under 8 dBm [math; U: the LoRa chirp spectrum is not perfectly flat, so a lab must measure the peak 3 kHz] | implied by the power cap |
| Antenna | **≤ 6 dBi hard limit** in firmware config, AND no higher than the gain/type listed in the board's grant (typically a 2–3 dBi dipole [U]). The power cap is reduced if G > 6 | `static_assert(MESH_ANT_GAIN_DBI <= 6)` |
| RF exposure | Grants require ≥ 20 cm separation from people (mobile, not portable). Handheld or body-worn use is outside the grants [V: grant notes below] | documentation only |

**Operating within the module's FCC grant is required.** The firmware must not use frequencies, power, modes or antennas beyond what the grant lists. A modular grant (§15.212) lets a host product reuse it if the integration follows the grant's conditions and the label says "Contains FCC ID: …". A board-level (non-modular) grant covers only that exact board as sold. Firmware that changes RF behaviour outside the tested modes may fall outside either kind of grant [U, confirm with a TCB/test lab].

### Candidate boards: FCC status (checked 2026-10-20 on fccid.io, which mirrors FCC EAS)

| Board | FCC ID | Grant type | 900 MHz ranges / class / power in grant | Our BW500 plan (903–914.2) inside grant? | Per-board TX cap (`MESH_BOARD_*`) |
|---|---|---|---|---|---|
| **RAK4631** (RAK4630 module) | **2AF6B-RAK4630** https://fccid.io/2AF6B-RAK4630 | **Single Modular Approval** [V]. RAK4631 is a declared series model of RAK4630 [V: similarity letter, https://fccid.co/fcc/2AF6B-RAK4630] | 902.3–914.9 (hybrid/FHSS, 0.036 W) and **903–914.2 DTS, 0.038 W (15.8 dBm)** [V] | **Yes** [V] | **15 dBm** |
| **Seeed Wio-SX1262** (XIAO ESP32S3 kit) | **Z4T-WIO-SX1262** https://fccid.io/Z4T-WIO-SX1262 | **Single Modular Approval** [V]. OEM-integrator only; 20 cm; no co-location | 902.3–914.9 and **903–914.2**, 0.092 W (19.6 dBm), listed as DSS [V] | **Yes**, the 903–914.2 range is listed [V]. The XIAO ESP32S3 host has its own FCC ID [U, not checked] | **19 dBm** |
| **Heltec WiFi LoRa 32 V3** | **2A2GJ-HTIT-WBR2H** https://fccid.io/2A2GJ-HTIT-WBR2H (granted 2026-06-03; the older 2A2GJ-HTIT is the 2022 "WiFi Kit LoRa Node") | **Not modular** ("Does not apply"; composite device) [V] | 902.3–914.9 **DSS** 0.0063 W; **923.3–927.5 DTS** 0.0155 W (11.9 dBm) [V] | **No.** Its DTS grant covers only 923.3–927.5 (the LoRaWAN downlink band) [V]. Whether this ID is the exact V3 SKU is [U] | build refused for this plan (would be 11 dBm, 923.3–927.5 only) |
| **LilyGO T3-S3** | **2ASYE-T3-S3** https://fccid.io/2ASYE-T3-S3 (V1.3: 2ASYE-T3-S3-V13 https://fccid.io/2ASYE-T3-S3-V13) | **Not modular** [V] | "915 MHz" only, DTS, 0.0059 W (7.7 dBm); V1.3 0.005 W [V] | **No / unverified.** The grant lists a single 915 MHz frequency [V] | build refused |
| **LilyGO T-Beam Supreme** | **2ASYE-T-BEAM-S3** https://fccid.io/2ASYE-T-BEAM-S3 [U: that this ID is the Supreme and not another T-Beam S3] | **Not modular** [V] | "915 MHz", DTS, 0.157 W (21.9 dBm) [V: https://fccid.co/fcc/2ASYE-T-BEAM-S3] | **No / unverified.** Only 915 MHz is listed | build refused |

**Consequence for hardware:** to meet the HARD requirement, US-deployed relays use **RAK4631** or the **Seeed Wio-SX1262 / XIAO ESP32S3** kit. They are the only candidates whose grants list 903–914.2 MHz. Heltec V3 and the LilyGO boards stay non-transmitting or dev-only (into a dummy load / shielded) for the US until their grant test reports confirm BW500 on our channels, or until we add a per-board channel plan (open question 1). This supersedes the "default dev board" recommendation in §1 for any over-the-air US use.

---

## 2. Firmware architecture (ESP32-S3, Arduino + PlatformIO)

- Toolchain: PlatformIO `espressif32`, Arduino core 3.x (ESP-IDF 5.x, which brings ESP-NOW v2). Radio driver: **RadioLib** (MIT, https://github.com/jgromes/RadioLib) [V license]. BLE: NimBLE-Arduino (Apache-2.0) [U, check version]. Crypto on nodes: only for node identity and signing beacons (libsodium/monocypher). **Nodes never get message keys.**
- New env in `platformio.ini`: `[env:relay-heltec-v3]`, `[env:relay-xiao-s3]`, `[env:relay-tbeam-s3]`. The code shares `include/` with the existing VictorModem firmware so the uplink bridge can be compiled into the modem build (`-DMESH_BRIDGE`).

```
src/mesh/
  radio_lora.cpp     RadioLib SX1262, IRQ RX -> queue, airtime accounting, CAD/listen-before-talk
  radio_espnow.cpp   ESP-NOW broadcast + unicast, peer table by RSSI
  link_phone_ble.cpp GATT service (new UUID 7a1c0101-...), framing in §3
  link_phone_ws.cpp  Optional: binary WS frames on the existing :81 server
  router.cpp         Flood/TTL, dedupe, neighbour table, next-hop hints
  store.cpp          Store-and-forward spool (LittleFS ring, or SD on T3-S3)
  bridge.cpp         Optional uplink via VictorModem cellular (MQTT/HTTPS to relay server)
  sched.cpp          Duty-cycle/airtime budget, jittered rebroadcast
```

**Routing:** managed flooding.
1. When a frame arrives, check the **dedupe cache**: key = (src_id, msg_id, frag_idx), held in a 512-entry LRU with a 10-minute expiry. If the frame was already seen, drop it. If the copy we heard came from a neighbour with a higher hop count, cancel our own pending rebroadcast (a Meshtastic-style "someone else covered it" rule).
2. If `dst` is one of our attached phones, deliver it over BLE/WS. Otherwise, if TTL > 0, decrement TTL, increment the hop count, and **rebroadcast after a random delay**. The delay is weighted by SNR, so weaker receivers (farther away) wait less.
3. Unicast hint: the neighbour table learns `src → last-heard via` and puts that in the next-hop field. Every relay still accepts flooded copies.
4. Default TTL is 5, and the maximum is 7.

**Store-and-forward:** if `dst` is unknown or offline, the frame goes into the spool (TTL-limited, 24 h maximum by default, size quota such as 1 MB). A phone or node announces itself with a signed `BEACON` and drains its messages with a `PULL` request. Bridge nodes copy the spool upstream.

**Uplink bridge (VictorModem):** frames addressed to `dst=0xFFFFFFFE` (uplink), or frames not delivered after N minutes, get forwarded as opaque ciphertext. They go over MQTT-over-TLS or HTTPS to a relay server (to be decided), which gives them to the recipient's phone when it is online, or to another bridge. Downlink is the reverse. The bridge never sees plaintext.

---

## 3. Phone ↔ node protocol (draft v1)

### 3.1 Transport framing (BLE / WS)
- BLE GATT service `7a1c0101-5e5f-4d1b-9c3a-56c7f0a0b001` (a new service, kept separate from the modem's JSON service `7a1c0001-…`):
  - `…0102` **RX** (phone→node, Write Without Response)
  - `…0103` **TX** (node→phone, Notify)
  - `…0104` **INFO** (Read: node id, fw version, MTU, profile)
- Request ATT MTU 247 (ATT payload 244 B). The link layer must still handle 20 B (MTU 23).
- Wi-Fi: the same link frames inside binary WebSocket messages on `ws://192.168.4.1:81/mesh`, one link frame per WS message, with no BLE fragmentation.

**Link frame** (little-endian):
```
| SOF 0xA5 (1) | LEN (2) | SEQ (1) | FLAGS (1) | PAYLOAD (LEN) | CRC16-CCITT (2) |
```
- LEN = payload length, up to 4096. CRC covers LEN..PAYLOAD. CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
- FLAGS: bit0 ACK, bit1 NAK, bit2 MORE (the BLE segment continues in the next write).
- A link frame is split into BLE writes of up to (MTU − 3) bytes. The receiver reassembles by SOF/LEN. If a frame is incomplete after a 500 ms timeout, the partial data is discarded and a NAK is sent.
- Link ACK: the node ACKs each SEQ. The phone retransmits up to 3 times with backoff (200, 400, 800 ms). The window is 4 frames.

### 3.2 Mesh packet (the PAYLOAD; this is what goes over LoRa and ESP-NOW unchanged)
```
off size field
0   1    ver(4b)=1 | type(4b)
1   1    flags: b0 ack_req, b1 encrypted, b2 signed, b3 fragmented, b4 store_ok, b5 uplink_ok
2   4    src_id  = first 4 B of SHA-256(Ed25519 pubkey of sender phone)
6   4    dst_id  = same for recipient; 0xFFFFFFFF broadcast, 0xFFFFFFFE uplink
10  4    msg_id  (random 32-bit, per message; same for all fragments)
14  1    ttl(4b) | hop_count(4b)
15  1    frag_idx
16  1    frag_count (1 = unfragmented)
17  1    next_hop hint (1 B of relay node id, 0 = none)
18  ..   body
```
**18-byte header.** Types: 0 DATA, 1 ACK (end-to-end), 2 BEACON, 3 PULL, 4 NODE_CTRL, 5 KEY_HELLO (optional, signed public-key announcement).

The routing fields (ttl/hop_count/next_hop) are **not** covered by the AEAD, because relays have to change them. Everything else is passed in as AEAD associated data: ver, type, flags (minus relay-mutable bits), src, dst, msg_id, frag_idx, frag_count.

### 3.3 Sizes
- LoRa frame ≤ **222 B** (US915 DTS profile, BW500: about 87 ms at SF7 and 277 ms at SF9, both under our 400 ms hard cap). That leaves 204 B of body.
- Encrypted DATA body = `nonce-suffix(8) ‖ ciphertext ‖ tag(16)`. The full 24 B XChaCha20 nonce comes from `msg_id(4) ‖ src_id(4) ‖ 8 random bytes` sent in fragment 0, plus `frag_idx` for the others. The per-fragment nonce is HKDF(base_nonce, frag_idx) [design choice]. **About 180 B of plaintext per LoRa frame.**
- ESP-NOW v2: up to 1470 B per frame. The same packet format is used with a bigger body, but frames are re-fragmented to LoRa size at LoRa hops.
- **Practical small-file limit: 32 KB** (≤ 182 fragments, so frag_count fits in 1 byte with a cap of 255 → about 45 KB hard max). At about 0.09 s per frame (SF7/BW500) plus a 10% duty budget, a 32 KB file takes roughly **2–3 minutes per hop** over LoRa [U]. Recommended: images ≤ 16 KB (compress to WebP on the phone), voice notes ≤ 8 KB (Codec2 or Opus at 6 kbps).
- A signature (64 B) goes **inside** the ciphertext, so relays learn nothing. Ed25519 signs the header AD and the plaintext.

### 3.4 Ack / retry
- End-to-end: the recipient sends a `ACK` packet (type 1) containing `msg_id` plus a received-fragment bitmap (32 B covers 255 frags), encrypted the same way. The sender retransmits only the missing fragments, with exponential backoff (30 s, 60 s, 120 s, max 5 tries over LoRa), then falls back to store-and-forward or uplink.
- Hop-by-hop: there are no per-hop ACKs on a flood. Overhearing a rebroadcast counts as an implicit ACK. For unicast next-hop, a single retry happens if no overheard copy arrives within 2× the expected airtime.

### 3.5 End-to-end crypto (phone only; nodes never hold message keys)
- Identity: Ed25519 keypair per user/device. A static X25519 key is derived (`crypto_sign_ed25519_pk_to_curve25519`), or a separate X25519 key is published in the QR.
- **Key exchange by QR:** `vmesh://k?v=1&ed=<b64url 32B>&x=<b64url 32B>&n=<name>&sig=<ed25519 sig over fields>`. Users scan each other in person, which verifies them out of band. Safety number = SHA-256(sorted pubkeys) shown as 12 digits.
- Per-message key: `k = HKDF-SHA256(X25519(eph_sk, peer_x_pk) ‖ X25519(my_x_sk, peer_x_pk), salt=msg_id‖src‖dst, info="vmesh v1")`. The ephemeral pubkey (32 B) is sent in fragment 0 when `flags.b6=eph`. Without it, static-static mode saves 32 B but loses forward secrecy. [Open question: Double Ratchet when there are round trips.]
- AEAD: **XChaCha20-Poly1305** (libsodium on Android via lazysodium or Tink) [U, pick a library]. ChaCha20-Poly1305 (IETF, 12 B nonce) is the fallback if the platform library lacks XChaCha.
- Broadcast/group channels: a symmetric group key is shared by QR. The same AEAD is used, and the sender is signed inside the ciphertext.
- Metadata note: src_id and dst_id are visible to relays. Possible fix: rotate them with a daily HMAC-derived pseudonym (open question).

---

## 4. Meshtastic-compatible option (separate device)
- Meshtastic firmware is **GPL-3.0** (https://github.com/meshtastic/firmware) [V]. Its protobufs (https://github.com/meshtastic/protobufs) are also GPL-3.0 [U, check the LICENSE file before generating any code from them]. Victorious Mesh is BSD.
- Plan: a **separate physical device** running unmodified, stock Meshtastic firmware (for example a RAK4631 kit or a Heltec V3 flashed with Meshtastic). Our relay firmware contains **no Meshtastic code**.
- The Android app talks to it through Meshtastic's documented public client API (BLE/serial protobufs). There are two ways to handle licensing:
  (a) put the integration in a separate GPL-3 plugin APK or module that talks to our BSD app over an IPC boundary (Intent/AIDL), or
  (b) write a clean-room implementation of the wire format from the published docs.
  Whether (b) avoids GPL obligations depends on whether a protocol/schema counts as copyrightable expression. Have counsel review before choosing [U]. Do not copy `.proto` files or generated code into the BSD repo.
- Interop limits: Meshtastic uses its own AES-CTR/PKI crypto and its own packet format. Our E2E messages can go across Meshtastic as opaque payloads on a private app portnum (PRIVATE_APP = 256) [U, check the size limit of about 233 B], so Meshtastic nodes can carry our ciphertext with no change to their firmware.
- Bridging Meshtastic and our mesh **on one device** is not allowed (that would mix GPL with BSD). Keep the bridge on the phone, or in a separately licensed GPL bridge device.

---

## 5. Open questions
1. ~~FCC: single channel BW250, or hopping?~~ **Resolved:** single-channel BW250 is non-compliant and has been removed. We use DTS BW500 on the US915 903.0–914.2 MHz plan, region-locked at compile time, with power capped per board grant (§2). Still to do: (a) a test lab confirms the RAK4630 and Wio-SX1262 test reports cover BW500 at our SFs, including 6 dB BW ≥ 500 kHz and PSD; (b) decide whether to support Heltec V3 on its 923.3–927.5 MHz DTS range; (c) confirm what the LilyGO “915 MHz” grants actually cover; (d) a sold product needs its own Part 15 review and “Contains FCC ID” labelling. [U]
2. Forward secrecy: ephemeral per message (+32 B) versus a ratchet once two phones have exchanged messages.
3. Metadata privacy: rotating pseudonymous node IDs, and the collision rate of 4-byte IDs (birthday bound about 1% at ~9,300 users) [math].
4. Spool abuse and DoS: rate limits per src_id. Should relays require a signed proof-of-membership token?
5. Uplink relay server: self-host or Cloudflare? How does a phone retrieve its messages (push vs poll), and how long is data retained?
6. Time sync for expiry without GPS (beacon-carried time vs phone time).
7. Should the BLE relay service ship inside VictorModem firmware as well (`-DMESH_BRIDGE`), sharing NimBLE with the existing `7a1c0001` service?
8. Battery and solar budget per board (measure Heltec V3 vs XIAO RX current). [U]
9. Antennas and enclosures (IP rating) for the outdoor relay SKU.
