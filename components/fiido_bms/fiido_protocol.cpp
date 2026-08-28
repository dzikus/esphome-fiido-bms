#include "fiido_protocol.h"

namespace esphome::fiido_bms {

PollCursor skip_disabled_polls(PollCursor cursor, std::span<const bool, POLL_TABLE_SIZE> enabled) {
  size_t safety = POLL_TABLE_SIZE;
  while (safety-- > 0 && cursor.remaining > 0) {
    if (enabled[cursor.index])
      break;
    cursor.index = (cursor.index + 1) % POLL_TABLE_SIZE;
    cursor.remaining--;
  }
  return cursor;
}

BurstGate evaluate_burst_gate(uint32_t now, uint32_t interval, uint32_t phase, BurstState state, bool forced) {
  if (interval == 0)
    interval = 1;
  const uint32_t slot = (now - (phase % interval)) / interval;
  if (forced || !state.started)
    return {true, slot};
  if (slot == state.last_slot)
    return {false, slot};
  // Half an interval, not a whole one. A full interval here would pace every hub
  // to the same period from whenever it last fired, which re-synchronises two
  // hubs that once fired together and holds them there, masking the phase. Half
  // is still wide enough to swallow a forced burst landing next to a boundary.
  if ((now - state.last_ms) < interval / 2)
    return {false, slot};
  return {true, slot};
}

// CRC is the XOR of all preceding bytes, confirmed against a live BMS.
uint8_t compute_crc(std::span<const uint8_t> data) {
  uint8_t crc = 0;
  for (uint8_t byte : data)
    crc ^= byte;
  return crc;
}

std::array<uint8_t, POLL_FRAME_LEN> build_poll_frame(Addr addr, uint8_t len) {
  std::array<uint8_t, POLL_FRAME_LEN> frame{
      FIIDO_SIG_0, FIIDO_SIG_1, FRAME_TYPE_POLL, len, static_cast<uint8_t>(addr), 0};
  frame.back() = compute_crc(std::span<const uint8_t>(frame).first(POLL_FRAME_LEN - 1));
  return frame;
}

std::vector<uint8_t> build_write_frame(FrameType type, Addr addr, std::span<const uint8_t> payload) {
  if (payload.size() > MAX_WRITE_PAYLOAD)
    return {};
  std::vector<uint8_t> frame;
  frame.reserve(payload.size() + WRITE_FRAME_OVERHEAD);
  frame.push_back(FIIDO_SIG_0);
  frame.push_back(FIIDO_SIG_1);
  frame.push_back(static_cast<uint8_t>(type));
  frame.push_back(static_cast<uint8_t>(payload.size()));
  frame.push_back(static_cast<uint8_t>(addr));
  frame.insert(frame.end(), payload.begin(), payload.end());
  frame.push_back(compute_crc(frame));
  return frame;
}

MaskedWrite compute_masked_write(Addr addr, uint8_t cache, uint8_t mask, uint8_t new_bits) {
  uint8_t value = static_cast<uint8_t>((cache & ~mask) | (new_bits & mask));
  if (addr == Addr::FLAGS_39) {
    return {FrameType::WRITE_J0, static_cast<uint8_t>(value & 0x1F)};
  }
  return {FrameType::WRITE_L0, value};
}

StatsView decode_stats(std::span<const uint8_t> payload) {
  StatsView v{};
  if (payload.size() != stats::PAYLOAD_LEN)
    return v;
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
  v.b2b = payload[stats::ADDR_2B_OFFSET];
  v.b2c = payload[stats::ADDR_2C_OFFSET];
  v.b38 = payload[stats::ADDR_38_OFFSET];
  // Only bits 4..0 of 0x39 are defined; a write must send the rest as zero.
  v.b39 = payload[stats::ADDR_39_OFFSET] & 0x1F;
  return v;
}

FlagView decode_flags(uint8_t b27, uint8_t b28, uint8_t b2b, uint8_t b2c, uint8_t b38, uint8_t b39) {
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
  f.throttle_on = (b2b & 0x02) == 0;  // inverted
  f.double_speed_on = (b2b & 0x20) != 0;
  f.bike_guard_on = (b2b & 0x40) != 0;
  f.key_sound_on = (b2c & 0x10) == 0;  // inverted
  f.slow_mode_on = (b2c & 0x40) != 0;
  f.pas_limit_on = (b2c & 0x80) != 0;
  f.speaker_audible = (b38 & 0x0C) == 0;  // any non-zero pattern silences it
  f.auto_screen_off_on = (b39 & 0x08) != 0;
  f.ring_on = (b39 & 0x02) != 0;
  return f;
}

NotifyView validate_notify(std::span<const uint8_t> frame) {
  if (frame.size() < NOTIFY_OVERHEAD)
    return {};
  if (frame[0] != FIIDO_SIG_0 || frame[1] != FIIDO_SIG_1)
    return {};
  if (frame[2] != FRAME_TYPE_NOTIFY)
    return {};
  const size_t declared_len = frame[3];
  // Frame total = 5 header bytes + declared_len payload + 1 CRC
  if (declared_len + NOTIFY_OVERHEAD != frame.size())
    return {};
  if (frame.back() != compute_crc(frame.first(frame.size() - 1)))
    return {};
  return {true, static_cast<Addr>(frame[4]), frame.subspan(NOTIFY_HDR_LEN, declared_len)};
}

}  // namespace esphome::fiido_bms
