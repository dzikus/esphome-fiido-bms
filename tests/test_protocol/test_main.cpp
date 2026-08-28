// Unit tests for fiido_protocol. Run `pio test -e native` from tests/.
// Fixtures in fixtures.h (sources: live BMS log + reconstructions from parsed values).
#include <unity.h>

#include "fiido_protocol.h"
#include "fiido_state.h"
#include "fixtures.h"
// Pure C++ functions in fiido_protocol via single-TU include (no separate .o linkage).
#include "../../components/fiido_bms/fiido_protocol.cpp"
#include "../../components/fiido_bms/fiido_state.cpp"

using namespace esphome::fiido_bms;

void setUp() {}
void tearDown() {}

// === CRC / build_poll / validate (sanity) ===

void test_compute_crc_battery_poll() {
  const uint8_t frame[] = {0x46, 0x64, 0x55, 0x0D, 0x7B};
  TEST_ASSERT_EQUAL_UINT8(0x01, compute_crc(frame));
}

void test_build_poll_frame_battery() {
  const auto out = build_poll_frame(Addr::BATTERY, 0x0D);
  const uint8_t expected[] = {0x46, 0x64, 0x55, 0x0D, 0x7B, 0x01};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), POLL_FRAME_LEN);
}

void test_build_poll_frame_all_polls() {
  // Each POLL_TABLE entry must match the documented poll frame bytes.
  const uint8_t expected[][POLL_FRAME_LEN] = {
      {0x46, 0x64, 0x55, 0x0D, 0x7B, 0x01},  // BATTERY
      {0x46, 0x64, 0x55, 0x0C, 0xAF, 0xD4},  // CTRL
      {0x46, 0x64, 0x55, 0x0C, 0x96, 0xED},  // MOTOR
      {0x46, 0x64, 0x55, 0x0C, 0xC8, 0xB3},  // ENERGY
      {0x46, 0x64, 0x55, 0x35, 0x05, 0x47},  // STATS
      {0x46, 0x64, 0x55, 0x0D, 0x60, 0x1A},  // METER
      {0x46, 0x64, 0x55, 0x01, 0x3C, 0x4A},  // SPEEDLIM
      {0x46, 0x64, 0x55, 0x01, 0x52, 0x24},  // BOOST
      {0x46, 0x64, 0x55, 0x02, 0x57, 0x22},  // DISPLAY
  };
  TEST_ASSERT_EQUAL_UINT(9, POLL_TABLE_SIZE);
  for (size_t i = 0; i < POLL_TABLE_SIZE; i++) {
    const auto out = build_poll_frame(POLL_TABLE[i].addr, POLL_TABLE[i].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected[i], out.data(), POLL_FRAME_LEN);
  }
}

// === build_write_frame ===

void test_build_write_frame_motor_enable() {
  // L0 write (0xAA) to ADDR 0x27, one payload byte with bit 7 set.
  const uint8_t payload[] = {0x80};
  const std::vector<uint8_t> out = build_write_frame(FrameType::WRITE_L0, Addr::FLAGS_27, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xAA, 0x01, 0x27, 0x80, 0x2E};
  TEST_ASSERT_EQUAL_UINT(7, out.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), 7);
}

void test_build_write_frame_gear_mode() {
  // J0 write (0xFF) to ADDR 0x25, nibble-encoded 3-gear payload 0x30.
  const uint8_t payload[] = {0x30};
  const std::vector<uint8_t> out = build_write_frame(FrameType::WRITE_J0, Addr::GEAR_RANGE, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x25, 0x30, 0xC9};
  TEST_ASSERT_EQUAL_UINT(7, out.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), 7);
}

void test_build_write_frame_brightness() {
  // J0 write (0xFF) to ADDR 0x57, one raw payload byte (display brightness).
  const uint8_t payload[] = {0xFF};
  const std::vector<uint8_t> out = build_write_frame(FrameType::WRITE_J0, Addr::DISPLAY, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x57, 0xFF, 0x74};
  TEST_ASSERT_EQUAL_UINT(7, out.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), 7);
}

void test_build_write_frame_boost() {
  // L0 write (0xAA) to ADDR 0x52, one raw payload byte (PAS boost level).
  const uint8_t payload[] = {100};
  const std::vector<uint8_t> out = build_write_frame(FrameType::WRITE_L0, Addr::PAS_BOOST, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xAA, 0x01, 0x52, 0x64, 0xBF};
  TEST_ASSERT_EQUAL_UINT(7, out.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), 7);
}

void test_build_write_frame_guard_time() {
  // J0 write (0xFF) to ADDR 0x58, one raw payload byte (guard timeout seconds).
  const uint8_t payload[] = {0x00};
  const std::vector<uint8_t> out = build_write_frame(FrameType::WRITE_J0, Addr::GUARD_TIME, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x58, 0x00, 0x84};
  TEST_ASSERT_EQUAL_UINT(7, out.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), 7);
}

void test_build_write_frame_addr_39() {
  // Byte 0x39 latches only via the J0 (0xFF) frame type; 0xAA writes are ack'd
  // but not applied. Payload carries bits 4..0 only (mask 0x1F), so a cache of
  // 0xE8 with bit 3 kept becomes 0x08.
  uint8_t b = 0xE8 & 0x1F;
  TEST_ASSERT_EQUAL_UINT8(0x08, b);
  const uint8_t payload[] = {b};
  const std::vector<uint8_t> out = build_write_frame(FrameType::WRITE_J0, Addr::FLAGS_39, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x39, 0x08, 0xED};
  TEST_ASSERT_EQUAL_UINT(7, out.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), 7);
}

void test_build_write_frame_multibyte_payload() {
  // Multi-byte payload exercises the copy loop and CRC offset (5 + payload_len).
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  const std::vector<uint8_t> out = build_write_frame(FrameType::WRITE_L0, Addr::WATCH_PAIR, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xAA, 0x03, 0x09, 0x01, 0x02, 0x03, 0x82};
  TEST_ASSERT_EQUAL_UINT(9, out.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), 9);
}

void test_build_write_frame_refuses_a_payload_the_length_byte_cannot_describe() {
  const std::vector<uint8_t> payload(MAX_WRITE_PAYLOAD + 1, 0x00);
  TEST_ASSERT_TRUE(build_write_frame(FrameType::WRITE_L0, Addr::FLAGS_27, payload).empty());
  const std::vector<uint8_t> largest(MAX_WRITE_PAYLOAD, 0x00);
  const std::vector<uint8_t> frame = build_write_frame(FrameType::WRITE_L0, Addr::FLAGS_27, largest);
  TEST_ASSERT_EQUAL_UINT(MAX_WRITE_PAYLOAD + WRITE_FRAME_OVERHEAD, frame.size());
  TEST_ASSERT_EQUAL_UINT8(MAX_WRITE_PAYLOAD, frame[3]);
}

// === compute_masked_write ===

void test_masked_write_keeps_bits_outside_mask() {
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_27, 0xB5, 0x08, 0x08);
  TEST_ASSERT_EQUAL_UINT8(0xBD, w.value);
}

void test_masked_write_clears_bit_inside_mask() {
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_27, 0xBD, 0x08, 0x00);
  TEST_ASSERT_EQUAL_UINT8(0xB5, w.value);
}

void test_masked_write_is_idempotent() {
  // Setting a bit that is already set must reproduce the cached byte, or the
  // verification poll would report a change that never happened.
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_27, 0xBD, 0x08, 0x08);
  TEST_ASSERT_EQUAL_UINT8(0xBD, w.value);
}

void test_masked_write_uses_l0_outside_0x39() {
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_2C, 0x00, 0x10, 0x10);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FrameType::WRITE_L0), static_cast<uint8_t>(w.type));
}

void test_masked_write_0x39_uses_j0() {
  // ADDR 0x39 latches only under J0; an L0 write is acknowledged and dropped.
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_39, 0x00, 0x08, 0x08);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FrameType::WRITE_J0), static_cast<uint8_t>(w.type));
}

void test_masked_write_0x39_drops_high_bits() {
  // Only bits 4..0 are defined there; the rest must go out as zero.
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_39, 0xE8, 0x02, 0x02);
  TEST_ASSERT_EQUAL_UINT8(0x0A, w.value);
}

void test_masked_write_speaker_off_pattern() {
  // Horn off is bits 3:2 = 01, not 11.
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_38, 0x20, 0x0C, 0x04);
  TEST_ASSERT_EQUAL_UINT8(0x24, w.value);
}

void test_masked_write_speaker_on_pattern() {
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_38, 0x24, 0x0C, 0x00);
  TEST_ASSERT_EQUAL_UINT8(0x20, w.value);
}

// === burst gate and poll rotation ===

void test_burst_gate_first_burst_after_connect_is_free() {
  // started == false marks "nothing sent since the link came up".
  const BurstGate g = evaluate_burst_gate(12345, 3000, 0, {0, 0, false}, false);
  TEST_ASSERT_TRUE(g.start);
}

void test_burst_gate_blocks_inside_the_same_slot() {
  const BurstGate first = evaluate_burst_gate(3100, 3000, 0, {100, 0, true}, false);
  TEST_ASSERT_TRUE(first.start);
  const BurstGate second = evaluate_burst_gate(3900, 3000, 0, {3100, first.slot, true}, false);
  TEST_ASSERT_FALSE(second.start);
}

void test_burst_gate_opens_on_the_next_slot() {
  const BurstGate g = evaluate_burst_gate(6100, 3000, 0, {3100, 1, true}, false);
  TEST_ASSERT_TRUE(g.start);
  TEST_ASSERT_EQUAL_UINT32(2, g.slot);
}

void test_burst_gate_keeps_minimum_spacing_after_a_forced_burst() {
  // A write verification poll bypasses both gates and can land just before a slot
  // boundary. Without the elapsed check a full burst followed 200 ms later.
  const BurstGate forced = evaluate_burst_gate(29999, 15000, 0, {20000, 1, true}, true);
  TEST_ASSERT_TRUE(forced.start);
  const BurstGate next = evaluate_burst_gate(30200, 15000, 0, {29999, forced.slot, true}, false);
  TEST_ASSERT_FALSE(next.start);
}

void test_burst_gate_phase_separates_two_hubs() {
  // Worst case: two hubs whose 1 s poller ticks land on the same millisecond.
  // Only the phase argument keeps their bursts apart, and it has to keep doing so
  // for the whole run, not just for the first burst after connect.
  const uint32_t interval = 3000;
  BurstState a_state{0, 0, false}, b_state{0, 0, false};
  bool collided = false;
  for (uint32_t now = 0; now <= 60000; now += 1000) {
    const BurstGate a = evaluate_burst_gate(now, interval, 0, a_state, false);
    const BurstGate b = evaluate_burst_gate(now, interval, 1500, b_state, false);
    if (a.start)
      a_state = {now, a.slot, true};
    if (b.start)
      b_state = {now, b.slot, true};
    // Skip the warm-up: both hubs are due immediately on the first tick.
    if (now >= 10000 && a.start && b.start)
      collided = true;
  }
  TEST_ASSERT_FALSE(collided);
}

void test_burst_gate_survives_zero_interval() {
  // update_interval_on: 0s validates, and the slot maths divides by it.
  const BurstGate g = evaluate_burst_gate(1000, 0, 0, {500, 0, true}, false);
  TEST_ASSERT_TRUE(g.start);
}

void test_skip_disabled_polls_stops_on_an_enabled_one() {
  bool enabled[POLL_TABLE_SIZE];
  for (size_t i = 0; i < POLL_TABLE_SIZE; i++)
    enabled[i] = true;
  const PollCursor c = skip_disabled_polls({0, POLL_TABLE_SIZE}, enabled);
  TEST_ASSERT_EQUAL_UINT(0, c.index);
  TEST_ASSERT_EQUAL_UINT(POLL_TABLE_SIZE, c.remaining);
}

void test_skip_disabled_polls_consumes_one_slot_each() {
  // Skipping must spend a slot, or a burst of nine could send more than nine.
  bool enabled[POLL_TABLE_SIZE];
  for (size_t i = 0; i < POLL_TABLE_SIZE; i++)
    enabled[i] = true;
  enabled[0] = false;
  enabled[1] = false;
  const PollCursor c = skip_disabled_polls({0, POLL_TABLE_SIZE}, enabled);
  TEST_ASSERT_EQUAL_UINT(2, c.index);
  TEST_ASSERT_EQUAL_UINT(POLL_TABLE_SIZE - 2, c.remaining);
}

void test_skip_disabled_polls_terminates_when_all_are_off() {
  bool enabled[POLL_TABLE_SIZE];
  for (size_t i = 0; i < POLL_TABLE_SIZE; i++)
    enabled[i] = false;
  const PollCursor c = skip_disabled_polls({0, POLL_TABLE_SIZE}, enabled);
  TEST_ASSERT_EQUAL_UINT(0, c.remaining);
}

// === decode_stats / decode_flags ===

void test_decode_stats_rejects_wrong_length() {
  const uint8_t short_payload[4] = {0, 0, 0, 0};
  TEST_ASSERT_FALSE(decode_stats(short_payload).valid);
}

void test_decode_stats_reads_the_captured_frame() {
  // Ties the offset constants to a captured payload.
  const StatsView v = decode_stats(std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN, 53));
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_EQUAL_UINT8(90, v.soc_pct);
  TEST_ASSERT_EQUAL_UINT8(1, v.gear);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.8f, v.total_km);
}

void test_decode_stats_masks_0x39_to_low_bits() {
  uint8_t p[53] = {0};
  p[stats::ADDR_39_OFFSET] = 0xE8;
  TEST_ASSERT_EQUAL_UINT8(0x08, decode_stats(p).b39);
}

void test_decode_stats_gear_count_from_nibble_pair() {
  // Read as a plain number the byte never equals 3 or 5.
  uint8_t p[53] = {0};
  p[stats::ADDR_25_OFFSET] = 0x30;
  TEST_ASSERT_EQUAL_UINT8(3, decode_stats(p).max_gear);
  p[stats::ADDR_25_OFFSET] = 0x53;
  TEST_ASSERT_EQUAL_UINT8(5, decode_stats(p).max_gear);
}

void test_decode_stats_rejects_a_nibble_pair_that_means_nothing() {
  uint8_t p[53] = {0};
  p[stats::ADDR_25_OFFSET] = 0x77;
  TEST_ASSERT_EQUAL_UINT8(0, decode_stats(p).max_gear);
}

void test_decode_stats_bounds_reject_impossible_values() {
  // total_kilometers feeds a total_increasing sensor, where one bad sample sticks
  // in long-term statistics for good.
  uint8_t p[53] = {0};
  p[stats::TOTAL_KM_OFFSET] = 0xFF;
  p[stats::TOTAL_KM_OFFSET + 1] = 0xFF;
  p[stats::TOTAL_KM_OFFSET + 2] = 0xFF;
  p[stats::TOTAL_KM_OFFSET + 3] = 0xFF;
  p[stats::ADDR_24_OFFSET] = 200;
  const StatsView v = decode_stats(p);
  TEST_ASSERT_FALSE(v.total_km_ok);
  TEST_ASSERT_FALSE(v.soc_ok);
  TEST_ASSERT_TRUE(v.speed_ok);
}

void test_decode_flags_light_needs_the_controller_on() {
  // Bit 3 can be set with the controller off; the lamp is not lit then.
  TEST_ASSERT_FALSE(decode_flags(0x08, 0, 0, 0, 0, 0).light_on);
  TEST_ASSERT_TRUE(decode_flags(0x88, 0, 0, 0, 0, 0).light_on);
}

void test_decode_flags_inverted_bits() {
  // throttle and key sound read the opposite way round.
  TEST_ASSERT_TRUE(decode_flags(0, 0, 0x00, 0x00, 0, 0).throttle_on);
  TEST_ASSERT_FALSE(decode_flags(0, 0, 0x02, 0x00, 0, 0).throttle_on);
  TEST_ASSERT_TRUE(decode_flags(0, 0, 0, 0x00, 0, 0).key_sound_on);
  TEST_ASSERT_FALSE(decode_flags(0, 0, 0, 0x10, 0, 0).key_sound_on);
}

void test_decode_flags_speaker_is_audible_only_on_zero() {
  TEST_ASSERT_TRUE(decode_flags(0, 0, 0, 0, 0x00, 0).speaker_audible);
  TEST_ASSERT_FALSE(decode_flags(0, 0, 0, 0, 0x04, 0).speaker_audible);
  TEST_ASSERT_FALSE(decode_flags(0, 0, 0, 0, 0x0C, 0).speaker_audible);
}

void test_decode_flags_each_bit_reads_its_own_byte() {
  TEST_ASSERT_TRUE(decode_flags(0x40, 0, 0, 0, 0, 0).cruise_on);
  TEST_ASSERT_TRUE(decode_flags(0, 0x40, 0, 0, 0, 0).show_total_km_on);
  TEST_ASSERT_TRUE(decode_flags(0, 0, 0x40, 0, 0, 0).bike_guard_on);
  TEST_ASSERT_TRUE(decode_flags(0, 0, 0, 0x80, 0, 0).pas_limit_on);
  TEST_ASSERT_TRUE(decode_flags(0, 0, 0, 0, 0, 0x02).ring_on);
}

void test_validate_notify_bad_crc() {
  uint8_t bad[sizeof(fixtures::BATTERY_NOTIFY)];
  for (size_t i = 0; i < sizeof(bad); i++)
    bad[i] = fixtures::BATTERY_NOTIFY[i];
  bad[sizeof(bad) - 1] ^= 0xFF;  // corrupted CRC
  TEST_ASSERT_FALSE(validate_notify(bad).valid);
}

void test_validate_notify_bad_signature() {
  uint8_t bad[sizeof(fixtures::BATTERY_NOTIFY)];
  for (size_t i = 0; i < sizeof(bad); i++)
    bad[i] = fixtures::BATTERY_NOTIFY[i];
  bad[0] = 0x00;  // missing "Fd" signature
  TEST_ASSERT_FALSE(validate_notify(bad).valid);
}

void test_validate_notify_wrong_length() {
  uint8_t bad[sizeof(fixtures::BATTERY_NOTIFY)];
  for (size_t i = 0; i < sizeof(bad); i++)
    bad[i] = fixtures::BATTERY_NOTIFY[i];
  bad[3] = 0x0E;  // declared_len 1 larger than actual
  TEST_ASSERT_FALSE(validate_notify(bad).valid);
}

void test_validate_notify_too_short() {
  const uint8_t tiny[] = {0x46, 0x64};
  TEST_ASSERT_FALSE(validate_notify(tiny).valid);
}

void test_validate_notify_hands_back_the_payload_it_declared() {
  const NotifyView notify = validate_notify(fixtures::BATTERY_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT(sizeof(fixtures::BATTERY_NOTIFY) - NOTIFY_OVERHEAD, notify.payload.size());
  TEST_ASSERT_EQUAL_PTR(fixtures::BATTERY_NOTIFY + NOTIFY_HDR_LEN, notify.payload.data());
}

void test_endian_helpers_read_zero_past_the_end() {
  const uint8_t buf[] = {0x12, 0x34, 0x56};
  TEST_ASSERT_EQUAL_UINT16(0, u16be(buf, 2));
  TEST_ASSERT_EQUAL_UINT32(0, u32be(buf, 0));
}

// === HANDSHAKE ===

void test_validate_handshake() {
  const NotifyView notify = validate_notify(fixtures::HANDSHAKE_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x0D, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(13, notify.payload.size());
}

// === Captured fixtures ===
//
// These check a captured frame through validate_notify and the endian helpers.
// They index with their own literals, so they say nothing about the stats::
// offsets the decoder uses; decode_stats covers those.

void test_fixture_battery_validate() {
  const NotifyView notify = validate_notify(fixtures::BATTERY_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x7B, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(13, notify.payload.size());
}

void test_fixture_battery_voltage_48v() {
  // off 4-5 = batteryVoltage BE/10. Real bike reported 48.0V.
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::BATTERY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(480, u16be(p, 4));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 48.0f, u16be(p, 4) / 10.0f);
}

void test_fixture_battery_capacity_11_6_ah() {
  // off 2-3 = totalLevel BE/10 -> 11.6 Ah (C11 Pro spec)
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::BATTERY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(116, u16be(p, 2));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.6f, u16be(p, 2) / 10.0f);
}

void test_fixture_battery_hw_sw() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::BATTERY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);  // HW
  TEST_ASSERT_EQUAL_UINT8(1, p[1]);  // SW
}

void test_fixture_battery_idle_no_current() {
  // off 7-8 = currentVoltage, off 9-10 = current -> idle = 0
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::BATTERY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 7));
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 9));
}

// === CTRL decode ===

void test_fixture_ctrl_validate() {
  const NotifyView notify = validate_notify(fixtures::CTRL_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0xAF, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(12, notify.payload.size());
}

void test_fixture_ctrl_versions() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::CTRL_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);     // HW
  TEST_ASSERT_EQUAL_UINT8(0xD3, p[1]);  // SW = 211
  TEST_ASSERT_EQUAL_UINT8(18, p[10]);   // currentVersion = 0x12
  TEST_ASSERT_EQUAL_UINT8(1, p[11]);    // manufacturer
}

// === MOTOR decode ===

void test_fixture_motor_validate() {
  const NotifyView notify = validate_notify(fixtures::MOTOR_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x96, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(12, notify.payload.size());
}

void test_fixture_motor_wheel_28_inch() {
  // off 5-6 = wheel BE/10. C11 has 28" wheels (physically verified).
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::MOTOR_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(280, u16be(p, 5));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 28.0f, u16be(p, 5) / 10.0f);
}

void test_fixture_motor_capacity_350w() {
  // off 9-10 = capacity -> C11 has a 350W motor (spec)
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::MOTOR_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(350, u16be(p, 9));
}

void test_fixture_motor_version() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::MOTOR_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);  // emVersion
}

// === ENERGY decode ===

void test_fixture_energy_validate() {
  const NotifyView notify = validate_notify(fixtures::ENERGY_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0xC8, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(12, notify.payload.size());
}

void test_fixture_energy_startup_time() {
  // off 10-11 = startupTime BE = 57 s (BMS uptime since power-on)
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::ENERGY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(57, u16be(p, 10));
}

void test_fixture_energy_idle_zero_totals() {
  // off 6-9 = totalTakeEnergy BE32/10 -> 0 when idle
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::ENERGY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT32(0, u32be(p, 6));
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 0));  // torque
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 2));  // RPM
}

// === STATS decode ===

void test_fixture_stats_validate() {
  const NotifyView notify = validate_notify(fixtures::STATS_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x05, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(53, notify.payload.size());
}

void test_fixture_stats_soc_90_percent() {
  // off 31 = statusBatteryValue = SOC% (90% = 4/5 bars, confirmed against display)
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(90, p[31]);
}

void test_fixture_stats_total_km_42_8() {
  // off 23-26 = totalKilometers BE32/10
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT32(428, u32be(p, 23));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.8f, u32be(p, 23) / 10.0f);
}

void test_fixture_stats_gear() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(1, p[33]);  // bicycleGear
}

// === STATS bit-decode (lights/charging/turns) ===

void test_fixture_stats_open_light_on() {
  // off 34 (0x27) bit 3 = openLight
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_HIGH(3, p[34]);
}

void test_fixture_stats_charging_on() {
  // off 37 (0x2A) bit 3 = statusStateOfCharge
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_HIGH(3, p[37]);
}

void test_fixture_stats_left_turn_signal() {
  // off 51 (0x38) bit 0 = leftTurnLight
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_HIGH(0, p[51]);
}

void test_fixture_stats_right_turn_signal() {
  // off 51 (0x38) bit 1 = rightTurnLight
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_HIGH(1, p[51]);
}

void test_fixture_stats_brake_off() {
  // off 37 (0x2A) bit 5 = brake handle status; low (not engaged) in idle fixture.
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_LOW(5, p[37]);
}

// === METER decode ===

void test_fixture_meter_validate() {
  const NotifyView notify = validate_notify(fixtures::METER_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x60, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(13, notify.payload.size());
}

void test_fixture_meter_hw_sw() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::METER_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(32, p[0]);  // HW=32
  TEST_ASSERT_EQUAL_UINT8(40, p[1]);  // SW=40
}

void test_fixture_meter_mode_data() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::METER_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(55, p[7]);  // modeData = 55
}

// === u16be / u32be helpers (sanity) ===

void test_u16be_basic() {
  const uint8_t buf[] = {0xFF, 0x00, 0x01, 0xE0};
  TEST_ASSERT_EQUAL_UINT16(0xFF00, u16be(buf, 0));
  TEST_ASSERT_EQUAL_UINT16(0x0001, u16be(buf, 1));
  TEST_ASSERT_EQUAL_UINT16(0x01E0, u16be(buf, 2));
}

void test_u32be_basic() {
  const uint8_t buf[] = {0x12, 0x34, 0x56, 0x78, 0xDE, 0xAD, 0xBE, 0xEF};
  TEST_ASSERT_EQUAL_UINT32(0x12345678u, u32be(buf, 0));
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, u32be(buf, 4));
}

static LifecycleInput lifecycle_base() {
  LifecycleInput in{};
  in.now = 1'000'000;
  in.idle_disconnect_ms = 15 * 60 * 1000;
  in.probe_window_ms = 60 * 1000;
  in.periodic_probe_ms = 5 * 60 * 1000;
  in.write_verify_window_ms = 10 * 1000;
  return in;
}

void test_lifecycle_idle_disconnect_needs_the_full_window() {
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = true;
  in.motor_off_since_ms = in.now - in.idle_disconnect_ms + 1;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
  in.motor_off_since_ms = in.now - in.idle_disconnect_ms;
  TEST_ASSERT_EQUAL(LifecycleAction::IDLE_DISCONNECT, decide_lifecycle(in));
}

void test_lifecycle_never_drops_the_link_with_a_write_queued() {
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = true;
  in.motor_off_since_ms = in.now - in.idle_disconnect_ms * 10;
  in.pending_writes = true;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
}

void test_lifecycle_zero_timestamp_is_not_an_elapsed_window() {
  // millis() is 0 once per boot; 0 means "not started", not "long ago".
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = true;
  in.motor_off_since_ms = 0;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));

  in.connected = false;
  in.probe_started_ms = 0;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));

  in.enabled = false;
  in.disconnected_since_ms = 0;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
}

void test_lifecycle_probe_timeout_waits_for_the_write_verify_window() {
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = false;
  in.probe_started_ms = in.now - in.probe_window_ms;
  in.last_dispatch_ms = in.now - in.write_verify_window_ms + 1;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
  in.last_dispatch_ms = in.now - in.write_verify_window_ms;
  TEST_ASSERT_EQUAL(LifecycleAction::PROBE_TIMEOUT, decide_lifecycle(in));
  in.last_dispatch_ms = 0;
  TEST_ASSERT_EQUAL(LifecycleAction::PROBE_TIMEOUT, decide_lifecycle(in));
}

void test_lifecycle_disconnected_probes_on_its_period() {
  LifecycleInput in = lifecycle_base();
  in.enabled = false;
  in.disconnected_since_ms = in.now - in.periodic_probe_ms + 1;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
  in.disconnected_since_ms = in.now - in.periodic_probe_ms;
  TEST_ASSERT_EQUAL(LifecycleAction::START_PROBE, decide_lifecycle(in));
}

void test_lifecycle_survives_the_millis_wrap() {
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = true;
  in.now = 1000;
  in.motor_off_since_ms = 0xFFFFFFFFu - (in.idle_disconnect_ms - 1000) + 1;
  TEST_ASSERT_EQUAL(LifecycleAction::IDLE_DISCONNECT, decide_lifecycle(in));
}

void test_speed_limit_option_needs_the_enable_bit() {
  TEST_ASSERT_EQUAL_STRING("No limit", resolve_speed_limit_option(100, false));
  TEST_ASSERT_EQUAL_STRING("6 km/h", resolve_speed_limit_option(6, true));
  TEST_ASSERT_EQUAL_STRING("25 km/h", resolve_speed_limit_option(25, true));
  TEST_ASSERT_NULL(resolve_speed_limit_option(6, false));
  TEST_ASSERT_NULL(resolve_speed_limit_option(25, false));
  TEST_ASSERT_NULL(resolve_speed_limit_option(30, true));
}

void test_should_log_now_lets_the_first_one_through() {
  TEST_ASSERT_TRUE(should_log_now(0, 0, 5000));
  TEST_ASSERT_TRUE(should_log_now(1, 0, 5000));
  TEST_ASSERT_FALSE(should_log_now(6000, 5000, 5000));
  TEST_ASSERT_TRUE(should_log_now(10000, 5000, 5000));
}

void test_auto_shutdown_only_with_the_motor_on_and_enabled() {
  TEST_ASSERT_TRUE(should_auto_shutdown(20000, 5000, 15000, true, true));
  TEST_ASSERT_FALSE(should_auto_shutdown(20000, 5000, 15000, false, true));
  TEST_ASSERT_FALSE(should_auto_shutdown(20000, 5000, 15000, true, false));
  TEST_ASSERT_FALSE(should_auto_shutdown(19999, 5000, 15000, true, true));
}

static WriteGateInput gate_base() {
  WriteGateInput in{};
  in.ble_enabled = true;
  in.connected = true;
  in.cache_valid = true;
  return in;
}

void test_write_gate_ladder_order() {
  WriteGateInput in = gate_base();
  TEST_ASSERT_EQUAL(WriteGate::SEND, gate_write(in));

  in.cache_valid = false;
  TEST_ASSERT_EQUAL(WriteGate::DEFER_COLD_CACHE, gate_write(in));
  in.connected = false;
  TEST_ASSERT_EQUAL(WriteGate::QUEUE_DISCONNECTED, gate_write(in));
  in.ble_enabled = false;
  TEST_ASSERT_EQUAL(WriteGate::REJECT_BLE_DISABLED, gate_write(in));
}

void test_write_gate_controller_only_when_required() {
  WriteGateInput in = gate_base();
  in.controller_on = false;
  in.needs_controller = false;
  TEST_ASSERT_EQUAL(WriteGate::SEND, gate_write(in));
  in.needs_controller = true;
  TEST_ASSERT_EQUAL(WriteGate::REJECT_CONTROLLER_OFF, gate_write(in));
  in.controller_on = true;
  TEST_ASSERT_EQUAL(WriteGate::SEND, gate_write(in));
}

void test_write_gate_cold_cache_beats_controller_check() {
  WriteGateInput in = gate_base();
  in.cache_valid = false;
  in.needs_controller = true;
  in.controller_on = false;
  TEST_ASSERT_EQUAL(WriteGate::DEFER_COLD_CACHE, gate_write(in));
}

void test_encode_gear_mode_preserves_the_low_nibble() {
  TEST_ASSERT_EQUAL_UINT8(0x35, encode_gear_mode(3, 0x55));
  TEST_ASSERT_EQUAL_UINT8(0x55, encode_gear_mode(5, 0x35));
  TEST_ASSERT_EQUAL_UINT8(0x30, encode_gear_mode(3, 0x50));
}

void test_encode_gear_mode_refuses_anything_but_3_or_5() {
  TEST_ASSERT_EQUAL_UINT8(0x55, encode_gear_mode(4, 0x55));
  TEST_ASSERT_EQUAL_UINT8(0x55, encode_gear_mode(0, 0x55));
  TEST_ASSERT_EQUAL_UINT8(0x55, encode_gear_mode(255, 0x55));
}

void test_clamp_gear_holds_the_ceiling() {
  TEST_ASSERT_EQUAL_UINT8(3, clamp_gear(9, 3));
  TEST_ASSERT_EQUAL_UINT8(2, clamp_gear(2, 3));
  TEST_ASSERT_EQUAL_UINT8(0, clamp_gear(0, 3));
}

void test_pending_writes_drops_the_oldest_at_capacity() {
  PendingWrites q(3);
  std::vector<int> ran;
  for (int i = 0; i < 3; i++)
    TEST_ASSERT_TRUE(q.push([&ran, i]() { ran.push_back(i); }));
  TEST_ASSERT_FALSE(q.push([&ran]() { ran.push_back(99); }));
  TEST_ASSERT_EQUAL_UINT(3, q.size());
  for (auto &fn : q.drain())
    fn();
  TEST_ASSERT_EQUAL_INT(3, (int)ran.size());
  TEST_ASSERT_EQUAL_INT(1, ran[0]);
  TEST_ASSERT_EQUAL_INT(2, ran[1]);
  TEST_ASSERT_EQUAL_INT(99, ran[2]);
}

void test_pending_writes_drain_empties_the_queue() {
  PendingWrites q(4);
  (void)q.push([]() {});
  TEST_ASSERT_FALSE(q.empty());
  auto taken = q.drain();
  TEST_ASSERT_EQUAL_UINT(1, taken.size());
  TEST_ASSERT_TRUE(q.empty());
  TEST_ASSERT_EQUAL_UINT(0, q.drain().size());
}

void test_pending_writes_requeue_during_drain_waits_for_the_next_one() {
  PendingWrites q(4);
  int ran = 0;
  (void)q.push([&q, &ran]() {
    ran++;
    (void)q.push([&ran]() { ran += 10; });
  });
  for (auto &fn : q.drain())
    fn();
  TEST_ASSERT_EQUAL_INT(1, ran);
  TEST_ASSERT_EQUAL_UINT(1, q.size());
  for (auto &fn : q.drain())
    fn();
  TEST_ASSERT_EQUAL_INT(11, ran);
}

void test_should_retry_send_stops_at_the_retry_cap() {
  TEST_ASSERT_FALSE(should_retry_send(0, 2, true));
  TEST_ASSERT_TRUE(should_retry_send(0, 2, false));
  TEST_ASSERT_TRUE(should_retry_send(1, 2, false));
  TEST_ASSERT_FALSE(should_retry_send(2, 2, false));
  TEST_ASSERT_FALSE(should_retry_send(3, 2, false));
}

void test_probe_outcome_motor_on_keeps_the_link() {
  TEST_ASSERT_EQUAL(ProbeOutcome::STAY_BIKE_ON, decide_probe_outcome(true, 100000, 0, 10000));
  TEST_ASSERT_EQUAL(ProbeOutcome::STAY_BIKE_ON, decide_probe_outcome(true, 100000, 99999, 10000));
}

void test_probe_outcome_holds_the_link_while_a_write_is_being_verified() {
  TEST_ASSERT_EQUAL(ProbeOutcome::STAY_VERIFY_WINDOW, decide_probe_outcome(false, 100000, 95000, 10000));
  TEST_ASSERT_EQUAL(ProbeOutcome::DROP_LINK, decide_probe_outcome(false, 100000, 90000, 10000));
  TEST_ASSERT_EQUAL(ProbeOutcome::DROP_LINK, decide_probe_outcome(false, 100000, 0, 10000));
  // Just after boot millis() is small, and no dispatch has happened: 0 means
  // "never", not "just now".
  TEST_ASSERT_EQUAL(ProbeOutcome::DROP_LINK, decide_probe_outcome(false, 5000, 0, 10000));
}

void test_resolve_gear_count_keeps_what_the_select_has() {
  TEST_ASSERT_EQUAL_UINT8(0, resolve_gear_count(0, false, 5));
  TEST_ASSERT_EQUAL_UINT8(0, resolve_gear_count(3, true, 5));
  TEST_ASSERT_EQUAL_UINT8(0, resolve_gear_count(5, false, 5));
  TEST_ASSERT_EQUAL_UINT8(3, resolve_gear_count(3, false, 5));
}

void test_resolve_mode_option_is_3_only_for_three_gears() {
  TEST_ASSERT_EQUAL_STRING("3", resolve_mode_option(3));
  TEST_ASSERT_EQUAL_STRING("5", resolve_mode_option(5));
  TEST_ASSERT_EQUAL_STRING("5", resolve_mode_option(0));
}

void test_light_bit_clears_only_on_the_motor_off_edge() {
  TEST_ASSERT_TRUE(should_clear_light_bit(true, true, false, 0x08));
  TEST_ASSERT_FALSE(should_clear_light_bit(true, true, false, 0x00));
  TEST_ASSERT_FALSE(should_clear_light_bit(true, false, false, 0x08));
  TEST_ASSERT_FALSE(should_clear_light_bit(true, true, true, 0x08));
  TEST_ASSERT_FALSE(should_clear_light_bit(false, true, false, 0x08));
}

void test_enforce_gear_mode_3_respects_every_gate() {
  const uint32_t cooldown = 60000;
  TEST_ASSERT_TRUE(should_enforce_gear_mode_3(true, 5, true, true, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(false, 5, true, true, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(true, 3, true, true, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(true, 5, false, true, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(true, 5, true, false, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(true, 5, true, true, 100000, 50000, cooldown));
  TEST_ASSERT_TRUE(should_enforce_gear_mode_3(true, 5, true, true, 160000, 100000, cooldown));
}

void test_speed_limit_option_parsing_rejects_anything_else() {
  TEST_ASSERT_FALSE(parse_speed_limit_option("30 km/h").has_value());
  TEST_ASSERT_FALSE(parse_speed_limit_option("").has_value());
  TEST_ASSERT_FALSE(parse_speed_limit_option("no limit").has_value());
  TEST_ASSERT_TRUE(parse_speed_limit_option("6 km/h").has_value());
}

void test_speed_limit_plan_writes_the_pas_bit_only_when_it_differs() {
  TEST_ASSERT_FALSE(plan_speed_limit(SpeedLimitOption::SIX_KMH, 0x80).needs_pas_write);
  TEST_ASSERT_TRUE(plan_speed_limit(SpeedLimitOption::SIX_KMH, 0x00).needs_pas_write);
  TEST_ASSERT_FALSE(plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0x00).needs_pas_write);
  TEST_ASSERT_TRUE(plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0x80).needs_pas_write);
}

void test_speed_limit_plan_keeps_the_other_bits_of_0x2c() {
  TEST_ASSERT_EQUAL_UINT8(0xD5, plan_speed_limit(SpeedLimitOption::SIX_KMH, 0x55).pas_byte);
  TEST_ASSERT_EQUAL_UINT8(0x55, plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0xD5).pas_byte);
}

void test_speed_limit_phase2_is_delayed_only_when_clearing_the_cap() {
  TEST_ASSERT_TRUE(plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0x80).delay_phase2);
  TEST_ASSERT_TRUE(plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0x00).delay_phase2);
  TEST_ASSERT_FALSE(plan_speed_limit(SpeedLimitOption::SIX_KMH, 0x00).delay_phase2);
  TEST_ASSERT_FALSE(plan_speed_limit(SpeedLimitOption::TWENTY_FIVE_KMH, 0x00).delay_phase2);
}

void test_speed_limit_bit_keeps_the_other_bits_of_0x27() {
  TEST_ASSERT_EQUAL_UINT8(0xA8, apply_speed_limit_bit(0x88, true));
  TEST_ASSERT_EQUAL_UINT8(0x88, apply_speed_limit_bit(0xA8, false));
  TEST_ASSERT_EQUAL_UINT8(0xA8, apply_speed_limit_bit(0xA8, true));
}

void test_speed_limit_plan_and_readback_agree() {
  // What the writer sends must be what the reader turns back into the option.
  for (const auto option : {SpeedLimitOption::SIX_KMH, SpeedLimitOption::TWENTY_FIVE_KMH, SpeedLimitOption::NO_LIMIT}) {
    const SpeedLimitPlan plan = plan_speed_limit(option, 0x00);
    TEST_ASSERT_EQUAL_STRING(speed_limit_option_name(option), resolve_speed_limit_option(plan.value, plan.limit_on));
  }
}

int main() {
  UNITY_BEGIN();

  // Sanity / basic validations
  RUN_TEST(test_compute_crc_battery_poll);
  RUN_TEST(test_build_poll_frame_battery);
  RUN_TEST(test_build_poll_frame_all_polls);
  RUN_TEST(test_build_write_frame_motor_enable);
  RUN_TEST(test_build_write_frame_gear_mode);
  RUN_TEST(test_build_write_frame_brightness);
  RUN_TEST(test_build_write_frame_boost);
  RUN_TEST(test_build_write_frame_guard_time);
  RUN_TEST(test_build_write_frame_addr_39);
  RUN_TEST(test_build_write_frame_multibyte_payload);
  RUN_TEST(test_build_write_frame_refuses_a_payload_the_length_byte_cannot_describe);
  RUN_TEST(test_masked_write_keeps_bits_outside_mask);
  RUN_TEST(test_masked_write_clears_bit_inside_mask);
  RUN_TEST(test_masked_write_is_idempotent);
  RUN_TEST(test_masked_write_uses_l0_outside_0x39);
  RUN_TEST(test_masked_write_0x39_uses_j0);
  RUN_TEST(test_masked_write_0x39_drops_high_bits);
  RUN_TEST(test_masked_write_speaker_off_pattern);
  RUN_TEST(test_masked_write_speaker_on_pattern);
  RUN_TEST(test_burst_gate_first_burst_after_connect_is_free);
  RUN_TEST(test_burst_gate_blocks_inside_the_same_slot);
  RUN_TEST(test_burst_gate_opens_on_the_next_slot);
  RUN_TEST(test_burst_gate_keeps_minimum_spacing_after_a_forced_burst);
  RUN_TEST(test_burst_gate_phase_separates_two_hubs);
  RUN_TEST(test_burst_gate_survives_zero_interval);
  RUN_TEST(test_skip_disabled_polls_stops_on_an_enabled_one);
  RUN_TEST(test_skip_disabled_polls_consumes_one_slot_each);
  RUN_TEST(test_skip_disabled_polls_terminates_when_all_are_off);
  RUN_TEST(test_decode_stats_rejects_wrong_length);
  RUN_TEST(test_decode_stats_reads_the_captured_frame);
  RUN_TEST(test_decode_stats_masks_0x39_to_low_bits);
  RUN_TEST(test_decode_stats_gear_count_from_nibble_pair);
  RUN_TEST(test_decode_stats_rejects_a_nibble_pair_that_means_nothing);
  RUN_TEST(test_decode_stats_bounds_reject_impossible_values);
  RUN_TEST(test_decode_flags_light_needs_the_controller_on);
  RUN_TEST(test_decode_flags_inverted_bits);
  RUN_TEST(test_decode_flags_speaker_is_audible_only_on_zero);
  RUN_TEST(test_decode_flags_each_bit_reads_its_own_byte);
  RUN_TEST(test_validate_notify_bad_crc);
  RUN_TEST(test_validate_notify_bad_signature);
  RUN_TEST(test_validate_notify_wrong_length);
  RUN_TEST(test_validate_notify_too_short);
  RUN_TEST(test_validate_notify_hands_back_the_payload_it_declared);

  // HANDSHAKE
  RUN_TEST(test_validate_handshake);

  // BATTERY
  RUN_TEST(test_fixture_battery_validate);
  RUN_TEST(test_fixture_battery_voltage_48v);
  RUN_TEST(test_fixture_battery_capacity_11_6_ah);
  RUN_TEST(test_fixture_battery_hw_sw);
  RUN_TEST(test_fixture_battery_idle_no_current);

  // CTRL
  RUN_TEST(test_fixture_ctrl_validate);
  RUN_TEST(test_fixture_ctrl_versions);

  // MOTOR
  RUN_TEST(test_fixture_motor_validate);
  RUN_TEST(test_fixture_motor_wheel_28_inch);
  RUN_TEST(test_fixture_motor_capacity_350w);
  RUN_TEST(test_fixture_motor_version);

  // ENERGY
  RUN_TEST(test_fixture_energy_validate);
  RUN_TEST(test_fixture_energy_startup_time);
  RUN_TEST(test_fixture_energy_idle_zero_totals);

  // STATS + bonus bits
  RUN_TEST(test_fixture_stats_validate);
  RUN_TEST(test_fixture_stats_soc_90_percent);
  RUN_TEST(test_fixture_stats_total_km_42_8);
  RUN_TEST(test_fixture_stats_gear);
  RUN_TEST(test_fixture_stats_open_light_on);
  RUN_TEST(test_fixture_stats_charging_on);
  RUN_TEST(test_fixture_stats_left_turn_signal);
  RUN_TEST(test_fixture_stats_right_turn_signal);
  RUN_TEST(test_fixture_stats_brake_off);

  // METER
  RUN_TEST(test_fixture_meter_validate);
  RUN_TEST(test_fixture_meter_hw_sw);
  RUN_TEST(test_fixture_meter_mode_data);

  // u16be/u32be
  RUN_TEST(test_u16be_basic);
  RUN_TEST(test_u32be_basic);
  RUN_TEST(test_endian_helpers_read_zero_past_the_end);
  RUN_TEST(test_lifecycle_idle_disconnect_needs_the_full_window);
  RUN_TEST(test_lifecycle_never_drops_the_link_with_a_write_queued);
  RUN_TEST(test_lifecycle_zero_timestamp_is_not_an_elapsed_window);
  RUN_TEST(test_lifecycle_probe_timeout_waits_for_the_write_verify_window);
  RUN_TEST(test_lifecycle_disconnected_probes_on_its_period);
  RUN_TEST(test_lifecycle_survives_the_millis_wrap);
  RUN_TEST(test_speed_limit_option_needs_the_enable_bit);
  RUN_TEST(test_should_log_now_lets_the_first_one_through);
  RUN_TEST(test_auto_shutdown_only_with_the_motor_on_and_enabled);
  RUN_TEST(test_write_gate_ladder_order);
  RUN_TEST(test_write_gate_controller_only_when_required);
  RUN_TEST(test_write_gate_cold_cache_beats_controller_check);
  RUN_TEST(test_encode_gear_mode_preserves_the_low_nibble);
  RUN_TEST(test_encode_gear_mode_refuses_anything_but_3_or_5);
  RUN_TEST(test_clamp_gear_holds_the_ceiling);
  RUN_TEST(test_pending_writes_drops_the_oldest_at_capacity);
  RUN_TEST(test_pending_writes_drain_empties_the_queue);
  RUN_TEST(test_pending_writes_requeue_during_drain_waits_for_the_next_one);
  RUN_TEST(test_should_retry_send_stops_at_the_retry_cap);
  RUN_TEST(test_probe_outcome_motor_on_keeps_the_link);
  RUN_TEST(test_probe_outcome_holds_the_link_while_a_write_is_being_verified);
  RUN_TEST(test_resolve_gear_count_keeps_what_the_select_has);
  RUN_TEST(test_resolve_mode_option_is_3_only_for_three_gears);
  RUN_TEST(test_light_bit_clears_only_on_the_motor_off_edge);
  RUN_TEST(test_enforce_gear_mode_3_respects_every_gate);
  RUN_TEST(test_speed_limit_option_parsing_rejects_anything_else);
  RUN_TEST(test_speed_limit_plan_writes_the_pas_bit_only_when_it_differs);
  RUN_TEST(test_speed_limit_plan_keeps_the_other_bits_of_0x2c);
  RUN_TEST(test_speed_limit_phase2_is_delayed_only_when_clearing_the_cap);
  RUN_TEST(test_speed_limit_bit_keeps_the_other_bits_of_0x27);
  RUN_TEST(test_speed_limit_plan_and_readback_agree);

  return UNITY_END();
}
