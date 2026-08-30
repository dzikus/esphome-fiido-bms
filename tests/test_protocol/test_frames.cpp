#include <unity.h>

#include <vector>

#include "fiido_protocol.h"
#include "fixtures.h"
#include "test_groups.h"

using namespace esphome::fiido_bms;

static void test_compute_crc_battery_poll() {
  const uint8_t frame[] = {0x46, 0x64, 0x55, 0x0D, 0x7B};
  TEST_ASSERT_EQUAL_UINT8(0x01, compute_crc(frame));
}

static void test_build_poll_frame_battery() {
  const auto out = build_poll_frame(Addr::BATTERY, 0x0D);
  const uint8_t expected[] = {0x46, 0x64, 0x55, 0x0D, 0x7B, 0x01};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), POLL_FRAME_LEN);
}

static void test_build_poll_frame_all_polls() {
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

static void test_build_write_frame_motor_enable() {
  // L0 write (0xAA) to ADDR 0x27, one payload byte with bit 7 set.
  const uint8_t payload[] = {0x80};
  const WriteFrame out = build_write_frame(FrameType::WRITE_L0, Addr::FLAGS_27, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xAA, 0x01, 0x27, 0x80, 0x2E};
  TEST_ASSERT_EQUAL_UINT(7, out.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.bytes.data(), 7);
}

static void test_build_write_frame_gear_mode() {
  // J0 write (0xFF) to ADDR 0x25, nibble-encoded 3-gear payload 0x30.
  const uint8_t payload[] = {0x30};
  const WriteFrame out = build_write_frame(FrameType::WRITE_J0, Addr::GEAR_RANGE, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x25, 0x30, 0xC9};
  TEST_ASSERT_EQUAL_UINT(7, out.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.bytes.data(), 7);
}

static void test_build_write_frame_brightness() {
  // J0 write (0xFF) to ADDR 0x57, one raw payload byte (display brightness).
  const uint8_t payload[] = {0xFF};
  const WriteFrame out = build_write_frame(FrameType::WRITE_J0, Addr::DISPLAY, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x57, 0xFF, 0x74};
  TEST_ASSERT_EQUAL_UINT(7, out.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.bytes.data(), 7);
}

static void test_build_write_frame_boost() {
  // L0 write (0xAA) to ADDR 0x52, one raw payload byte (PAS boost level).
  const uint8_t payload[] = {100};
  const WriteFrame out = build_write_frame(FrameType::WRITE_L0, Addr::PAS_BOOST, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xAA, 0x01, 0x52, 0x64, 0xBF};
  TEST_ASSERT_EQUAL_UINT(7, out.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.bytes.data(), 7);
}

static void test_build_write_frame_guard_time() {
  // J0 write (0xFF) to ADDR 0x58, one raw payload byte (guard timeout seconds).
  const uint8_t payload[] = {0x00};
  const WriteFrame out = build_write_frame(FrameType::WRITE_J0, Addr::GUARD_TIME, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x58, 0x00, 0x84};
  TEST_ASSERT_EQUAL_UINT(7, out.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.bytes.data(), 7);
}

static void test_build_write_frame_addr_39() {
  // Byte 0x39 latches only via the J0 (0xFF) frame type; 0xAA writes are ack'd
  // but not applied. The payload carries bits 4..0 only (mask 0x1F): a cache of
  // 0xE8 with bit 3 kept becomes 0x08.
  uint8_t b = 0xE8 & 0x1F;
  TEST_ASSERT_EQUAL_UINT8(0x08, b);
  const uint8_t payload[] = {b};
  const WriteFrame out = build_write_frame(FrameType::WRITE_J0, Addr::FLAGS_39, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x39, 0x08, 0xED};
  TEST_ASSERT_EQUAL_UINT(7, out.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.bytes.data(), 7);
}

static void test_build_write_frame_multibyte_payload() {
  // Multi-byte payload exercises the copy loop and CRC offset (5 + payload_len).
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  const WriteFrame out = build_write_frame(FrameType::WRITE_L0, Addr::WATCH_PAIR, payload);
  const uint8_t expected[] = {0x46, 0x64, 0xAA, 0x03, 0x09, 0x01, 0x02, 0x03, 0x82};
  TEST_ASSERT_EQUAL_UINT(9, out.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.bytes.data(), 9);
}

static void test_build_write_frame_refuses_a_payload_over_the_cap() {
  const std::vector<uint8_t> payload(MAX_WRITE_PAYLOAD + 1, 0x00);
  TEST_ASSERT_TRUE(build_write_frame(FrameType::WRITE_L0, Addr::FLAGS_27, payload).empty());
  const std::vector<uint8_t> largest(MAX_WRITE_PAYLOAD, 0x00);
  const WriteFrame frame = build_write_frame(FrameType::WRITE_L0, Addr::FLAGS_27, largest);
  TEST_ASSERT_EQUAL_UINT(MAX_WRITE_PAYLOAD + WRITE_FRAME_OVERHEAD, frame.size);
  TEST_ASSERT_EQUAL_UINT8(MAX_WRITE_PAYLOAD, frame.bytes[3]);
}

static void test_masked_write_keeps_bits_outside_mask() {
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_27, 0xB5, 0x08, 0x08);
  TEST_ASSERT_EQUAL_UINT8(0xBD, w.value);
}

static void test_masked_write_clears_bit_inside_mask() {
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_27, 0xBD, 0x08, 0x00);
  TEST_ASSERT_EQUAL_UINT8(0xB5, w.value);
}

static void test_masked_write_is_idempotent() {
  // Setting a bit that is already set must reproduce the cached byte, or the
  // verification poll would report a change that never happened.
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_27, 0xBD, 0x08, 0x08);
  TEST_ASSERT_EQUAL_UINT8(0xBD, w.value);
}

static void test_masked_write_uses_l0_outside_0x39() {
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_2C, 0x00, 0x10, 0x10);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FrameType::WRITE_L0), static_cast<uint8_t>(w.type));
}

static void test_masked_write_0x39_uses_j0() {
  // ADDR 0x39 latches only under J0; an L0 write is acknowledged and dropped.
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_39, 0x00, 0x08, 0x08);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FrameType::WRITE_J0), static_cast<uint8_t>(w.type));
}

static void test_masked_write_0x39_drops_high_bits() {
  // Only bits 4..0 are defined there; the rest must go out as zero.
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_39, 0xE8, 0x02, 0x02);
  TEST_ASSERT_EQUAL_UINT8(0x0A, w.value);
}

static void test_masked_write_speaker_off_pattern() {
  // Horn off is bits 3:2 = 01, not 11.
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_38, 0x20, 0x0C, 0x04);
  TEST_ASSERT_EQUAL_UINT8(0x24, w.value);
}

static void test_masked_write_speaker_on_pattern() {
  const MaskedWrite w = compute_masked_write(Addr::FLAGS_38, 0x24, 0x0C, 0x00);
  TEST_ASSERT_EQUAL_UINT8(0x20, w.value);
}

static void test_validate_notify_bad_crc() {
  uint8_t bad[sizeof(fixtures::BATTERY_NOTIFY)];
  for (size_t i = 0; i < sizeof(bad); i++)
    bad[i] = fixtures::BATTERY_NOTIFY[i];
  bad[sizeof(bad) - 1] ^= 0xFF;  // corrupted CRC
  TEST_ASSERT_FALSE(validate_notify(bad).valid);
}

static void test_validate_notify_bad_signature() {
  uint8_t bad[sizeof(fixtures::BATTERY_NOTIFY)];
  for (size_t i = 0; i < sizeof(bad); i++)
    bad[i] = fixtures::BATTERY_NOTIFY[i];
  bad[0] = 0x00;  // missing "Fd" signature
  TEST_ASSERT_FALSE(validate_notify(bad).valid);
}

static void test_validate_notify_wrong_length() {
  uint8_t bad[sizeof(fixtures::BATTERY_NOTIFY)];
  for (size_t i = 0; i < sizeof(bad); i++)
    bad[i] = fixtures::BATTERY_NOTIFY[i];
  bad[3] = 0x0E;  // declared_len 1 larger than actual
  TEST_ASSERT_FALSE(validate_notify(bad).valid);
}

static void test_validate_notify_too_short() {
  const uint8_t tiny[] = {0x46, 0x64};
  TEST_ASSERT_FALSE(validate_notify(tiny).valid);
}

static void test_validate_notify_hands_back_the_payload_it_declared() {
  const NotifyView notify = validate_notify(fixtures::BATTERY_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT(sizeof(fixtures::BATTERY_NOTIFY) - NOTIFY_OVERHEAD, notify.payload.size());
  TEST_ASSERT_EQUAL_PTR(fixtures::BATTERY_NOTIFY + NOTIFY_HDR_LEN, notify.payload.data());
}

static void test_validate_handshake() {
  const NotifyView notify = validate_notify(fixtures::HANDSHAKE_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x0D, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(13, notify.payload.size());
}

static void test_u16be_basic() {
  const uint8_t buf[] = {0xFF, 0x00, 0x01, 0xE0};
  TEST_ASSERT_EQUAL_UINT16(0xFF00, u16be(buf, 0));
  TEST_ASSERT_EQUAL_UINT16(0x0001, u16be(buf, 1));
  TEST_ASSERT_EQUAL_UINT16(0x01E0, u16be(buf, 2));
}

static void test_u32be_basic() {
  const uint8_t buf[] = {0x12, 0x34, 0x56, 0x78, 0xDE, 0xAD, 0xBE, 0xEF};
  TEST_ASSERT_EQUAL_UINT32(0x12345678u, u32be(buf, 0));
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, u32be(buf, 4));
}

static void test_endian_helpers_read_zero_past_the_end() {
  const uint8_t buf[] = {0x12, 0x34, 0x56};
  TEST_ASSERT_EQUAL_UINT16(0, u16be(buf, 2));
  TEST_ASSERT_EQUAL_UINT32(0, u32be(buf, 0));
}

void run_frame_tests() {
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
  RUN_TEST(test_build_write_frame_refuses_a_payload_over_the_cap);
  RUN_TEST(test_masked_write_keeps_bits_outside_mask);
  RUN_TEST(test_masked_write_clears_bit_inside_mask);
  RUN_TEST(test_masked_write_is_idempotent);
  RUN_TEST(test_masked_write_uses_l0_outside_0x39);
  RUN_TEST(test_masked_write_0x39_uses_j0);
  RUN_TEST(test_masked_write_0x39_drops_high_bits);
  RUN_TEST(test_masked_write_speaker_off_pattern);
  RUN_TEST(test_masked_write_speaker_on_pattern);
  RUN_TEST(test_validate_notify_bad_crc);
  RUN_TEST(test_validate_notify_bad_signature);
  RUN_TEST(test_validate_notify_wrong_length);
  RUN_TEST(test_validate_notify_too_short);
  RUN_TEST(test_validate_notify_hands_back_the_payload_it_declared);
  RUN_TEST(test_validate_handshake);
  RUN_TEST(test_u16be_basic);
  RUN_TEST(test_u32be_basic);
  RUN_TEST(test_endian_helpers_read_zero_past_the_end);
}
