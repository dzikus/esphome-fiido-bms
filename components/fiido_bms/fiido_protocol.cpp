#include "fiido_protocol.h"

namespace esphome {
namespace fiido_bms {

// CRC is the XOR of all preceding bytes, confirmed against a live BMS.
const PollDef POLL_TABLE[POLL_TABLE_SIZE] = {
    {0x7B, 13, "BATTERY"},
    {0xAF, 12, "CTRL"},
    {0x96, 12, "MOTOR"},
    {0xC8, 12, "ENERGY"},
    {0x05, 53, "STATS"},
    {0x60, 13, "METER"},
    // Speed limit value (read-only baseline, log-only). Frame: 46 64 55 01 3C 4A.
    {0x3C, 1, "SPEEDLIM"},
    // Boost level read-back (1 byte). Frame: 46 64 55 01 52 24.
    {0x52, 1, "BOOST"},
    // Display block read-back: 0x57 brightness + 0x58 guard time (2 bytes).
    // Frame: 46 64 55 02 57 22.
    {0x57, 2, "DISPLAY"},
};
static_assert(sizeof(POLL_TABLE) / sizeof(POLL_TABLE[0]) == POLL_TABLE_SIZE,
              "POLL_TABLE_SIZE must match the table; the burst cursor walks by it");

PollCursor skip_disabled_polls(PollCursor cursor, const bool *enabled) {
  size_t safety = POLL_TABLE_SIZE;
  while (safety-- > 0 && cursor.remaining > 0) {
    if (enabled[cursor.index]) break;
    cursor.index = (cursor.index + 1) % POLL_TABLE_SIZE;
    cursor.remaining--;
  }
  return cursor;
}

BurstGate evaluate_burst_gate(uint32_t now, uint32_t interval, uint32_t phase,
                              BurstState state, bool forced) {
  if (interval == 0) interval = 1;
  const uint32_t slot = (now - (phase % interval)) / interval;
  if (forced || !state.started) return {true, slot};
  if (slot == state.last_slot) return {false, slot};
  // Half an interval, not a whole one. A full interval here would pace every hub
  // to the same period from whenever it last fired, which re-synchronises two
  // hubs that once fired together and holds them there, masking the phase. Half
  // is still wide enough to swallow a forced burst landing next to a boundary.
  if ((now - state.last_ms) < interval / 2) return {false, slot};
  return {true, slot};
}

uint8_t compute_crc(const uint8_t *buf, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) crc ^= buf[i];
  return crc;
}

void build_poll_frame(uint8_t addr, uint8_t len, uint8_t *out) {
  out[0] = FIIDO_SIG_0;
  out[1] = FIIDO_SIG_1;
  out[2] = FRAME_TYPE_POLL;
  out[3] = len;
  out[4] = addr;
  out[5] = compute_crc(out, 5);
}

size_t build_write_frame(FrameType type, uint8_t addr, const uint8_t *payload,
                         uint8_t payload_len, uint8_t *out) {
  out[0] = FIIDO_SIG_0;
  out[1] = FIIDO_SIG_1;
  out[2] = static_cast<uint8_t>(type);
  out[3] = payload_len;
  out[4] = addr;
  for (uint8_t i = 0; i < payload_len; i++) out[5 + i] = payload[i];
  size_t crc_off = 5 + payload_len;
  out[crc_off] = compute_crc(out, crc_off);
  return crc_off + 1;
}

MaskedWrite compute_masked_write(uint8_t addr, uint8_t cache, uint8_t mask,
                                 uint8_t new_bits) {
  uint8_t value = static_cast<uint8_t>((cache & ~mask) | (new_bits & mask));
  if (addr == 0x39) {
    return {FrameType::WriteJ0, static_cast<uint8_t>(value & 0x1F)};
  }
  return {FrameType::WriteL0, value};
}

StatsView decode_stats(const uint8_t *payload, size_t len) {
  StatsView v{};
  if (len != stats::PAYLOAD_LEN) return v;
  v.valid = true;
  v.total_km = u32be(payload, stats::TOTAL_KM_OFFSET) / 10.0f;
  v.total_km_ok = v.total_km <= stats::MAX_TOTAL_KM;
  v.trip_km = u16be(payload, stats::TRIP_KM_OFFSET) / 10.0f;
  v.trip_km_ok = v.trip_km <= stats::MAX_TRIP_KM;
  v.speed_kmh = u16be(payload, stats::SPEED_OFFSET) / 10.0f;
  v.speed_ok = v.speed_kmh <= stats::MAX_SPEED_KMH;
  v.soc_pct = payload[stats::ADDR_24_OFFSET];
  v.soc_ok = v.soc_pct <= stats::MAX_SOC_PCT;
  v.gear = payload[stats::ADDR_26_OFFSET];
  v.gear_start = payload[stats::ADDR_25_OFFSET];
  // ADDR 0x25 packs the gear count into a nibble pair, not a plain number.
  const uint8_t raw_25 = payload[stats::ADDR_25_OFFSET];
  const uint8_t upper = (raw_25 >> 4) & 0x0F;
  const uint8_t lower = raw_25 & 0x0F;
  const uint8_t max_gear = upper > lower ? upper : lower;
  v.max_gear = (max_gear == 3 || max_gear == 5) ? max_gear : 0;
  v.brake = (payload[stats::ADDR_2A_OFFSET] & 0x20) != 0;
  v.b25 = payload[stats::ADDR_25_OFFSET];
  v.b27 = payload[stats::ADDR_27_OFFSET];
  v.b28 = payload[stats::ADDR_28_OFFSET];
  v.b2B = payload[stats::ADDR_2B_OFFSET];
  v.b2C = payload[stats::ADDR_2C_OFFSET];
  v.b38 = payload[stats::ADDR_38_OFFSET];
  // Only bits 4..0 of 0x39 are defined; a write must send the rest as zero.
  v.b39 = payload[stats::ADDR_39_OFFSET] & 0x1F;
  return v;
}

FlagView decode_flags(uint8_t b27, uint8_t b28, uint8_t b2B, uint8_t b2C, uint8_t b38,
                      uint8_t b39) {
  FlagView f{};
  f.motor_on = (b27 & 0x80) != 0;
  // With the controller off the lamp cannot be lit whatever the bit says.
  f.light_on = f.motor_on && (b27 & 0x08) != 0;
  f.speed_limit_on = (b27 & 0x20) != 0;
  f.cruise_on = (b27 & 0x40) != 0;
  f.start_mode_on = (b27 & 0x02) != 0;
  f.insensitivity_on = (b27 & 0x01) != 0;
  f.speed_unit_mph = (b28 & 0x80) != 0;
  f.show_total_km_on = (b28 & 0x40) != 0;
  f.throttle_on = (b2B & 0x02) == 0;      // inverted
  f.double_speed_on = (b2B & 0x20) != 0;
  f.bike_guard_on = (b2B & 0x40) != 0;
  f.key_sound_on = (b2C & 0x10) == 0;     // inverted
  f.slow_mode_on = (b2C & 0x40) != 0;
  f.pas_limit_on = (b2C & 0x80) != 0;
  f.speaker_audible = (b38 & 0x0C) == 0;  // any non-zero pattern silences it
  f.auto_screen_off_on = (b39 & 0x08) != 0;
  f.ring_on = (b39 & 0x02) != 0;
  return f;
}

bool validate_notify(const uint8_t *buf, size_t len, uint8_t *out_addr, size_t *out_payload_len) {
  if (len < NOTIFY_OVERHEAD) return false;
  if (buf[0] != FIIDO_SIG_0 || buf[1] != FIIDO_SIG_1) return false;
  if (buf[2] != FRAME_TYPE_NOTIFY) return false;
  uint8_t declared_len = buf[3];
  // Frame total = 5 header bytes + declared_len payload + 1 CRC
  if (size_t(declared_len) + NOTIFY_OVERHEAD != len) return false;
  uint8_t expected_crc = compute_crc(buf, len - 1);
  if (buf[len - 1] != expected_crc) return false;
  *out_addr = buf[4];
  *out_payload_len = declared_len;
  return true;
}

}  // namespace fiido_bms
}  // namespace esphome
