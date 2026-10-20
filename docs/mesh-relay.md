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

### US regulatory notes (FCC Part 15.247, 902–928 MHz)
Source: 47 CFR §15.247, https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/subject-group-ECFR2f2e5828339709e/section-15.247 [V for the rule text summarized here]
- **Frequency hopping** systems with a 20 dB bandwidth under 250 kHz must use **≥ 50 channels**, with an average dwell of **≤ 0.4 s per channel in any 20 s period**, at up to 1 W conducted. (Systems with 25–50 channels need ≥ 250 kHz bandwidth and are limited to 0.25 W.)
- **Digital modulation (DTS)** systems need a 6 dB bandwidth of ≥ 500 kHz and PSD ≤ 8 dBm/3 kHz. There is no dwell limit. Max 1 W conducted, and up to 6 dBi antenna gain before the power must be reduced.
- Whether LoRa at BW 500 kHz qualifies as DTS, and how single-channel LoRa at 125/250 kHz (common practice in hobby mesh firmware) is treated, depends on the **module's FCC grant and how it was certified**. [U] Check the FCC ID of each board or module, and the conditions in its grant, before shipping a product.
- Design rules we adopt anyway: **every LoRa frame takes ≤ 400 ms of airtime**, there is a per-node airtime budget (default ≤ 10% channel utilisation, adaptive backoff), and TX power is capped at the module's certified level. A consumer product sold with these boards still needs its own Part 15 compliance review. [U, get legal advice]
- ESP-NOW runs under the ESP32-S3 module's existing 2.4 GHz grant [U per board].

Approximate airtime (Semtech formula, CR 4/5, 8-symbol preamble, explicit header, CRC on) [U, check with the Semtech LoRa calculator]: SF7/BW125 ≈ 0.37 s for 222 B. SF9/BW125 ≈ 0.4 s for about 60 B. SF7/BW250 ≈ 0.19 s for 222 B. **Default profile: SF7 or SF8 at BW 250 kHz, 915 MHz band plan, max frame 222 B.** Use a slower long-range profile only for short control frames.

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
- LoRa frame ≤ **222 B** (stays under the 400 ms dwell limit at SF7/BW125, with margin at BW250). That leaves 204 B of body.
- Encrypted DATA body = `nonce-suffix(8) ‖ ciphertext ‖ tag(16)`. The full 24 B XChaCha20 nonce comes from `msg_id(4) ‖ src_id(4) ‖ 8 random bytes` sent in fragment 0, plus `frag_idx` for the others. The per-fragment nonce is HKDF(base_nonce, frag_idx) [design choice]. **About 180 B of plaintext per LoRa frame.**
- ESP-NOW v2: up to 1470 B per frame. The same packet format is used with a bigger body, but frames are re-fragmented to LoRa size at LoRa hops.
- **Practical small-file limit: 32 KB** (≤ 182 fragments, so frag_count fits in 1 byte with a cap of 255 → about 45 KB hard max). At about 0.2 s per frame plus a 10% duty budget, a 32 KB file takes roughly **6–7 minutes per hop** over LoRa [U]. Recommended: images ≤ 16 KB (compress to WebP on the phone), voice notes ≤ 8 KB (Codec2 or Opus at 6 kbps).
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
1. FCC: does our chosen LoRa config (single channel, BW 250 kHz) fit each module's grant? Should we implement 50+-channel hopping instead (it makes discovery and sync harder)? Get a legal or test-lab opinion. [U]
2. Forward secrecy: ephemeral per message (+32 B) versus a ratchet once two phones have exchanged messages.
3. Metadata privacy: rotating pseudonymous node IDs, and the collision rate of 4-byte IDs (birthday bound about 1% at ~9,300 users) [math].
4. Spool abuse and DoS: rate limits per src_id. Should relays require a signed proof-of-membership token?
5. Uplink relay server: self-host or Cloudflare? How does a phone retrieve its messages (push vs poll), and how long is data retained?
6. Time sync for expiry without GPS (beacon-carried time vs phone time).
7. Should the BLE relay service ship inside VictorModem firmware as well (`-DMESH_BRIDGE`), sharing NimBLE with the existing `7a1c0001` service?
8. Battery and solar budget per board (measure Heltec V3 vs XIAO RX current). [U]
9. Antennas and enclosures (IP rating) for the outdoor relay SKU.
