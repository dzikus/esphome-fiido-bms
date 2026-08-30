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

bool should_retry_send(uint8_t retry_count, uint8_t max_retries, bool send_ok) {
  return !send_ok && retry_count < max_retries;
}

BurstStep advance_burst(PollCursor cursor, uint8_t retry_count, uint8_t max_retries, bool send_ok) {
  if (should_retry_send(retry_count, max_retries, send_ok))
    return {.retry = true, .retry_count = static_cast<uint8_t>(retry_count + 1), .cursor = cursor};
  return {.retry = false,
          .retry_count = 0,
          .cursor = {.index = (cursor.index + 1) % POLL_TABLE_SIZE, .remaining = cursor.remaining - 1}};
}

BurstGate evaluate_burst_gate(uint32_t now, uint32_t interval, uint32_t phase, BurstState state, bool forced) {
  if (interval == 0)
    interval = 1;
  const uint32_t slot = (now - (phase % interval)) / interval;
  if (forced || !state.started)
    return {.start = true, .slot = slot};
  if (slot == state.last_slot)
    return {.start = false, .slot = slot};
  // Half an interval, not a whole one. A full interval here would pace every hub
  // to the same period from whenever it last fired, which re-synchronises two
  // hubs that once fired together and holds them there, masking the phase. Half
  // is still wide enough to swallow a forced burst landing next to a boundary.
  if ((now - state.last_ms) < interval / 2)
    return {.start = false, .slot = slot};
  return {.start = true, .slot = slot};
}

// CRC is the XOR of all preceding bytes, confirmed against a live BMS.
uint8_t compute_crc(std::span<const uint8_t> data) {
  uint8_t crc = 0;
  for (const uint8_t byte : data)
    crc ^= byte;
  return crc;
}

std::array<uint8_t, POLL_FRAME_LEN> build_poll_frame(Addr addr, uint8_t len) {
  std::array<uint8_t, POLL_FRAME_LEN> frame{
      FIIDO_SIG_0, FIIDO_SIG_1, FRAME_TYPE_POLL, len, static_cast<uint8_t>(addr), 0};
  frame.back() = compute_crc(std::span<const uint8_t>(frame).first(POLL_FRAME_LEN - 1));
  return frame;
}

WriteFrame build_write_frame(FrameType type, Addr addr, std::span<const uint8_t> payload) {
  WriteFrame frame{};
  if (payload.size() > MAX_WRITE_PAYLOAD)
    return frame;
  frame.bytes[0] = FIIDO_SIG_0;
  frame.bytes[1] = FIIDO_SIG_1;
  frame.bytes[2] = static_cast<uint8_t>(type);
  frame.bytes[3] = static_cast<uint8_t>(payload.size());
  frame.bytes[4] = static_cast<uint8_t>(addr);
  std::ranges::copy(payload, frame.bytes.begin() + NOTIFY_HDR_LEN);
  const size_t crc_at = NOTIFY_HDR_LEN + payload.size();
  frame.bytes[crc_at] = compute_crc(std::span<const uint8_t>(frame.bytes).first(crc_at));
  frame.size = crc_at + 1;
  return frame;
}

MaskedWrite compute_masked_write(Addr addr, uint8_t cache, uint8_t mask, uint8_t new_bits) {
  const uint8_t value = static_cast<uint8_t>((cache & ~mask) | (new_bits & mask));
  if (addr == Addr::FLAGS_39) {
    return {.type = FrameType::WRITE_J0, .value = flags_39::DEFINED.keep({value}).raw};
  }
  return {.type = FrameType::WRITE_L0, .value = value};
}

StatsView decode_stats(std::span<const uint8_t> payload) {
  StatsView v{};
  if (payload.size() != stats::PAYLOAD_LEN)
    return v;
  v.valid = true;
  v.total_km = static_cast<float>(u32be(payload, stats::TOTAL_KM_OFFSET)) / 10.0f;
  v.total_km_ok = v.total_km <= stats::MAX_TOTAL_KM;
  v.trip_km = static_cast<float>(u16be(payload, stats::TRIP_KM_OFFSET)) / 10.0f;
  v.trip_km_ok = v.trip_km <= stats::MAX_TRIP_KM;
  v.speed_kmh = static_cast<float>(u16be(payload, stats::SPEED_OFFSET)) / 10.0f;
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
  v.brake = (payload[stats::ADDR_2A_OFFSET] & ADDR_2A_BRAKE) != 0;
  v.b25 = {payload[stats::ADDR_25_OFFSET]};
  v.b27 = {payload[stats::ADDR_27_OFFSET]};
  v.b28 = {payload[stats::ADDR_28_OFFSET]};
  v.b2b = {payload[stats::ADDR_2B_OFFSET]};
  v.b2c = {payload[stats::ADDR_2C_OFFSET]};
  v.b38 = {payload[stats::ADDR_38_OFFSET]};
  // Only bits 4..0 of 0x39 are defined; a write must send the rest as zero.
  v.b39 = flags_39::DEFINED.keep({payload[stats::ADDR_39_OFFSET]});
  return v;
}

FlagView decode_flags(RegValue<Addr::FLAGS_27> b27, RegValue<Addr::FLAGS_28> b28, RegValue<Addr::FLAGS_2B> b2b,
                      RegValue<Addr::FLAGS_2C> b2c, RegValue<Addr::FLAGS_38> b38, RegValue<Addr::FLAGS_39> b39) {
  FlagView f{};
  f.motor_on = flags_27::CONTROLLER.in(b27);
  // With the controller off the lamp cannot be lit whatever the bit says.
  f.light_on = f.motor_on && flags_27::LIGHT.in(b27);
  f.speed_limit_on = flags_27::SPEED_LIMIT.in(b27);
  f.cruise_on = flags_27::CRUISE.in(b27);
  f.start_mode_on = flags_27::START_MODE.in(b27);
  f.insensitivity_on = flags_27::INSENSITIVITY.in(b27);
  f.speed_unit_mph = flags_28::SPEED_UNIT_MPH.in(b28);
  f.show_total_km_on = flags_28::SHOW_TOTAL_KM.in(b28);
  f.throttle_on = !flags_2b::THROTTLE_OFF.in(b2b);
  f.double_speed_on = flags_2b::DOUBLE_SPEED.in(b2b);
  f.bike_guard_on = flags_2b::BIKE_GUARD.in(b2b);
  f.key_sound_on = !flags_2c::KEY_SOUND_OFF.in(b2c);
  f.slow_mode_on = flags_2c::SLOW_MODE.in(b2c);
  f.pas_limit_on = flags_2c::PAS_LIMIT.in(b2c);
  f.speaker_audible = !flags_38::SPEAKER_SILENT.in(b38);
  f.auto_screen_off_on = flags_39::AUTO_SCREEN_OFF.in(b39);
  f.ring_on = flags_39::RING.in(b39);
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
  return {.valid = true, .addr = static_cast<Addr>(frame[4]), .payload = frame.subspan(NOTIFY_HDR_LEN, declared_len)};
}

}  // namespace esphome::fiido_bms
