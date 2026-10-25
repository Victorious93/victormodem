// mesh_radio_limits.h - HARD FCC Part 15.247 (902-928 MHz) limits for the
// Victorious Mesh LoRa relay. See docs/mesh-relay.md section 2.
// Profile: DTS (15.247(a)(2)), LoRa BW 500 kHz, US915 BW500 channel plan
// 903.0 + 1.6*n MHz (n=0..7), inside the RAK4630 / Wio-SX1262 modular grants.
// Every TX MUST go through mesh_tx_allowed() + mesh_clamp_tx_dbm().
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifndef __cplusplus
#include <assert.h>  // static_assert in C11
#endif

// ---- Compile-time region lock ----
#ifndef MESH_REGION_US915
#define MESH_REGION_US915 1
#endif
#if !MESH_REGION_US915
#error "Only the US915 region is supported (FCC Part 15.247 hard requirement)."
#endif
#if defined(MESH_REGION_EU868) || defined(MESH_REGION_AS923) || defined(MESH_REGION_AU915)
#error "Non-US regions are not implemented; region is locked to US915."
#endif

// ---- Board: TX cap = FCC grant conducted power, rounded down ----
#if defined(MESH_BOARD_RAK4631)        // FCC ID 2AF6B-RAK4630, 903-914.2 DTS 0.038 W
#define MESH_BOARD_GRANT_DBM 15
#elif defined(MESH_BOARD_WIO_SX1262)   // FCC ID Z4T-WIO-SX1262, 903-914.2, 0.092 W
#define MESH_BOARD_GRANT_DBM 19
#elif defined(MESH_BOARD_HELTEC_V3) || defined(MESH_BOARD_LILYGO_T3S3) || defined(MESH_BOARD_TBEAM_SUPREME)
#error "Board grant does not list the 903.0-914.2 MHz BW500 plan; TX not allowed (see docs/mesh-relay.md)."
#else
#error "Define one MESH_BOARD_* (RAK4631 or WIO_SX1262) for LoRa TX."
#endif

// ---- Antenna: 15.247(b)(4) + grant antenna ----
#ifndef MESH_ANT_GAIN_DBI
#define MESH_ANT_GAIN_DBI 2   // must not exceed the antenna listed in the grant
#endif
static_assert(MESH_ANT_GAIN_DBI >= 0 && MESH_ANT_GAIN_DBI <= 6,
              "Antenna gain >6 dBi is not permitted by this firmware");

#define MESH_FCC_MAX_DBM 30   // 1 W conducted, 15.247(b)(3)
#define MESH_ANT_PENALTY_DB ((MESH_ANT_GAIN_DBI > 6) ? (MESH_ANT_GAIN_DBI - 6) : 0)
#define MESH_MIN_(a, b) ((a) < (b) ? (a) : (b))
#define MESH_TX_DBM_MAX MESH_MIN_(MESH_MIN_(MESH_BOARD_GRANT_DBM, MESH_FCC_MAX_DBM - MESH_ANT_PENALTY_DB), 22)
static_assert(MESH_TX_DBM_MAX <= MESH_BOARD_GRANT_DBM, "TX cap above grant");

// ---- Modulation: DTS needs >= 500 kHz 6 dB BW ----
#define MESH_LORA_BW_HZ 500000UL
static_assert(MESH_LORA_BW_HZ == 500000UL, "Only LoRa BW500 is allowed");
#define MESH_SF_DEFAULT 7
#define MESH_SF_MIN 7
#define MESH_SF_MAX 9
#define MESH_CR_DENOM 5          // 4/5
#define MESH_PREAMBLE_SYMS 8
#define MESH_MAX_PHY_PAYLOAD 222
#define MESH_MAX_AIRTIME_US 400000UL   // per-frame hard cap
#define MESH_DUTY_PERMILLE 100         // 10 % per 60 s window (etiquette)
#define MESH_DUTY_WINDOW_MS 60000UL

// ---- Channel plan: LoRaWAN US915 BW500 uplink centres ----
#define MESH_NUM_CHANNELS 8
#define MESH_DEFAULT_CHANNEL 4   // 909.4 MHz
static inline uint32_t mesh_channel_hz(uint8_t ch) {
  return (ch < MESH_NUM_CHANNELS) ? 903000000UL + 1600000UL * ch : 0;  // 0 = invalid
}

// Semtech LoRa time-on-air (explicit header, CRC on), microseconds.
static inline uint32_t mesh_airtime_us(uint8_t sf, uint16_t len) {
  uint32_t tsym_us = (1UL << sf) * 1000000UL / MESH_LORA_BW_HZ;
  int ldro = (tsym_us > 16000) ? 1 : 0;
  int32_t num = 8 * (int32_t)len - 4 * sf + 28 + 16;
  int32_t den = 4 * (sf - 2 * ldro);
  int32_t n = num > 0 ? (num + den - 1) / den : 0;
  uint32_t syms_x4 = (MESH_PREAMBLE_SYMS * 4 + 17) + 4 * (8 + n * MESH_CR_DENOM);
  return syms_x4 * tsym_us / 4;
}

static inline int8_t mesh_clamp_tx_dbm(int8_t req) {
  return req > MESH_TX_DBM_MAX ? (int8_t)MESH_TX_DBM_MAX : req;
}

// True only if this exact frame is within every hard limit.
static inline bool mesh_tx_allowed(uint8_t ch, uint8_t sf, uint32_t bw_hz, uint16_t len) {
  if (mesh_channel_hz(ch) == 0) return false;
  if (bw_hz != MESH_LORA_BW_HZ) return false;
  if (sf < MESH_SF_MIN || sf > MESH_SF_MAX) return false;
  if (len == 0 || len > MESH_MAX_PHY_PAYLOAD) return false;
  return mesh_airtime_us(sf, len) <= MESH_MAX_AIRTIME_US;
}
