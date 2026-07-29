// Unit tests for fiido_protocol. Run `pio test -e native` from tests/.
// Fixtures in fixtures.h (sources: live BMS log + reconstructions from parsed values).
#include <unity.h>
#include "fiido_protocol.h"
#include "fixtures.h"
// Pure C++ functions in fiido_protocol via single-TU include (no separate .o linkage).
#include "../../components/fiido_bms/fiido_protocol.cpp"

using namespace esphome::fiido_bms;

void setUp() {}
void tearDown() {}

// === CRC / build_poll / validate (sanity) ===

void test_compute_crc_battery_poll() {
  const uint8_t frame[] = {0x46, 0x64, 0x55, 0x0D, 0x7B};
  TEST_ASSERT_EQUAL_UINT8(0x01, compute_crc(frame, sizeof(frame)));
}

void test_build_poll_frame_battery() {
  uint8_t out[POLL_FRAME_LEN];
  build_poll_frame(0x7B, 0x0D, out);
  const uint8_t expected[] = {0x46, 0x64, 0x55, 0x0D, 0x7B, 0x01};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, POLL_FRAME_LEN);
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
  uint8_t out[POLL_FRAME_LEN];
  for (size_t i = 0; i < POLL_TABLE_SIZE; i++) {
    build_poll_frame(POLL_TABLE[i].addr, POLL_TABLE[i].len, out);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected[i], out, POLL_FRAME_LEN);
  }
}

// === build_write_frame ===

void test_build_write_frame_motor_enable() {
  // L0 write (0xAA) to ADDR 0x27, one payload byte with bit 7 set.
  const uint8_t payload[] = {0x80};
  uint8_t out[8];
  size_t n = build_write_frame(FRAME_TYPE_WRITE_L0, 0x27, payload, 1, out);
  const uint8_t expected[] = {0x46, 0x64, 0xAA, 0x01, 0x27, 0x80, 0x2E};
  TEST_ASSERT_EQUAL_UINT(7, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 7);
}

void test_build_write_frame_gear_mode() {
  // J0 write (0xFF) to ADDR 0x25, nibble-encoded 3-gear payload 0x30.
  const uint8_t payload[] = {0x30};
  uint8_t out[8];
  size_t n = build_write_frame(FRAME_TYPE_WRITE_J0, 0x25, payload, 1, out);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x25, 0x30, 0xC9};
  TEST_ASSERT_EQUAL_UINT(7, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 7);
}

void test_build_write_frame_brightness() {
  // J0 write (0xFF) to ADDR 0x57, one raw payload byte (display brightness).
  const uint8_t payload[] = {0xFF};
  uint8_t out[8];
  size_t n = build_write_frame(FRAME_TYPE_WRITE_J0, 0x57, payload, 1, out);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x57, 0xFF, 0x74};
  TEST_ASSERT_EQUAL_UINT(7, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 7);
}

void test_build_write_frame_boost() {
  // L0 write (0xAA) to ADDR 0x52, one raw payload byte (PAS boost level).
  const uint8_t payload[] = {100};
  uint8_t out[8];
  size_t n = build_write_frame(FRAME_TYPE_WRITE_L0, 0x52, payload, 1, out);
  const uint8_t expected[] = {0x46, 0x64, 0xAA, 0x01, 0x52, 0x64, 0xBF};
  TEST_ASSERT_EQUAL_UINT(7, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 7);
}

void test_build_write_frame_guard_time() {
  // J0 write (0xFF) to ADDR 0x58, one raw payload byte (guard timeout seconds).
  const uint8_t payload[] = {0x00};
  uint8_t out[8];
  size_t n = build_write_frame(FRAME_TYPE_WRITE_J0, 0x58, payload, 1, out);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x58, 0x00, 0x84};
  TEST_ASSERT_EQUAL_UINT(7, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 7);
}

void test_build_write_frame_addr_39() {
  // Byte 0x39 latches only via the J0 (0xFF) frame type; 0xAA writes are ack'd
  // but not applied. Payload carries bits 4..0 only (mask 0x1F), so a cache of
  // 0xE8 with bit 3 kept becomes 0x08.
  uint8_t b = 0xE8 & 0x1F;
  TEST_ASSERT_EQUAL_UINT8(0x08, b);
  const uint8_t payload[] = {b};
  uint8_t out[8];
  size_t n = build_write_frame(FRAME_TYPE_WRITE_J0, 0x39, payload, 1, out);
  const uint8_t expected[] = {0x46, 0x64, 0xFF, 0x01, 0x39, 0x08, 0xED};
  TEST_ASSERT_EQUAL_UINT(7, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 7);
}

void test_build_write_frame_multibyte_payload() {
  // Multi-byte payload exercises the copy loop and CRC offset (5 + payload_len).
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  uint8_t out[16];
  size_t n = build_write_frame(FRAME_TYPE_WRITE_L0, 0xAB, payload, 3, out);
  const uint8_t expected[] = {0x46, 0x64, 0xAA, 0x03, 0xAB, 0x01, 0x02, 0x03, 0x20};
  TEST_ASSERT_EQUAL_UINT(9, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 9);
}

void test_validate_notify_bad_crc() {
  uint8_t bad[sizeof(fixtures::BATTERY_NOTIFY)];
  for (size_t i = 0; i < sizeof(bad); i++) bad[i] = fixtures::BATTERY_NOTIFY[i];
  bad[sizeof(bad) - 1] ^= 0xFF;  // corrupted CRC
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_FALSE(validate_notify(bad, sizeof(bad), &addr, &payload_len));
}

void test_validate_notify_bad_signature() {
  uint8_t bad[sizeof(fixtures::BATTERY_NOTIFY)];
  for (size_t i = 0; i < sizeof(bad); i++) bad[i] = fixtures::BATTERY_NOTIFY[i];
  bad[0] = 0x00;  // missing "Fd" signature
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_FALSE(validate_notify(bad, sizeof(bad), &addr, &payload_len));
}

void test_validate_notify_wrong_length() {
  uint8_t bad[sizeof(fixtures::BATTERY_NOTIFY)];
  for (size_t i = 0; i < sizeof(bad); i++) bad[i] = fixtures::BATTERY_NOTIFY[i];
  bad[3] = 0x0E;  // declared_len 1 larger than actual
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_FALSE(validate_notify(bad, sizeof(bad), &addr, &payload_len));
}

void test_validate_notify_too_short() {
  const uint8_t tiny[] = {0x46, 0x64};
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_FALSE(validate_notify(tiny, sizeof(tiny), &addr, &payload_len));
}

// === HANDSHAKE ===

void test_validate_handshake() {
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_TRUE(validate_notify(fixtures::HANDSHAKE_NOTIFY,
                                   sizeof(fixtures::HANDSHAKE_NOTIFY),
                                   &addr, &payload_len));
  TEST_ASSERT_EQUAL_UINT8(0x0D, addr);
  TEST_ASSERT_EQUAL_UINT(13, payload_len);
}

// === BATTERY decode ===

void test_decode_battery_validate() {
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_TRUE(validate_notify(fixtures::BATTERY_NOTIFY,
                                   sizeof(fixtures::BATTERY_NOTIFY),
                                   &addr, &payload_len));
  TEST_ASSERT_EQUAL_UINT8(0x7B, addr);
  TEST_ASSERT_EQUAL_UINT(13, payload_len);
}

void test_decode_battery_voltage_48v() {
  // off 4-5 = batteryVoltage BE/10. Real bike reported 48.0V.
  const uint8_t *p = fixtures::BATTERY_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT16(480, u16be(p, 4));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 48.0f, u16be(p, 4) / 10.0f);
}

void test_decode_battery_capacity_11_6_ah() {
  // off 2-3 = totalLevel BE/10 -> 11.6 Ah (C11 Pro spec)
  const uint8_t *p = fixtures::BATTERY_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT16(116, u16be(p, 2));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.6f, u16be(p, 2) / 10.0f);
}

void test_decode_battery_hw_sw() {
  const uint8_t *p = fixtures::BATTERY_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);  // HW
  TEST_ASSERT_EQUAL_UINT8(1, p[1]);  // SW
}

void test_decode_battery_idle_no_current() {
  // off 7-8 = currentVoltage, off 9-10 = current -> idle = 0
  const uint8_t *p = fixtures::BATTERY_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 7));
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 9));
}

// === CTRL decode ===

void test_decode_ctrl_validate() {
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_TRUE(validate_notify(fixtures::CTRL_NOTIFY,
                                   sizeof(fixtures::CTRL_NOTIFY),
                                   &addr, &payload_len));
  TEST_ASSERT_EQUAL_UINT8(0xAF, addr);
  TEST_ASSERT_EQUAL_UINT(12, payload_len);
}

void test_decode_ctrl_versions() {
  const uint8_t *p = fixtures::CTRL_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);     // HW
  TEST_ASSERT_EQUAL_UINT8(0xD3, p[1]);  // SW = 211
  TEST_ASSERT_EQUAL_UINT8(18, p[10]);   // currentVersion = 0x12
  TEST_ASSERT_EQUAL_UINT8(1, p[11]);    // manufacturer
}

// === MOTOR decode ===

void test_decode_motor_validate() {
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_TRUE(validate_notify(fixtures::MOTOR_NOTIFY,
                                   sizeof(fixtures::MOTOR_NOTIFY),
                                   &addr, &payload_len));
  TEST_ASSERT_EQUAL_UINT8(0x96, addr);
  TEST_ASSERT_EQUAL_UINT(12, payload_len);
}

void test_decode_motor_wheel_28_inch() {
  // off 5-6 = wheel BE/10. C11 has 28" wheels (physically verified).
  const uint8_t *p = fixtures::MOTOR_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT16(280, u16be(p, 5));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 28.0f, u16be(p, 5) / 10.0f);
}

void test_decode_motor_capacity_350w() {
  // off 9-10 = capacity -> C11 has a 350W motor (spec)
  const uint8_t *p = fixtures::MOTOR_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT16(350, u16be(p, 9));
}

void test_decode_motor_version() {
  const uint8_t *p = fixtures::MOTOR_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);  // emVersion
}

// === ENERGY decode ===

void test_decode_energy_validate() {
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_TRUE(validate_notify(fixtures::ENERGY_NOTIFY,
                                   sizeof(fixtures::ENERGY_NOTIFY),
                                   &addr, &payload_len));
  TEST_ASSERT_EQUAL_UINT8(0xC8, addr);
  TEST_ASSERT_EQUAL_UINT(12, payload_len);
}

void test_decode_energy_startup_time() {
  // off 10-11 = startupTime BE = 57 s (BMS uptime since power-on)
  const uint8_t *p = fixtures::ENERGY_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT16(57, u16be(p, 10));
}

void test_decode_energy_idle_zero_totals() {
  // off 6-9 = totalTakeEnergy BE32/10 -> 0 when idle
  const uint8_t *p = fixtures::ENERGY_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT32(0, u32be(p, 6));
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 0));  // torque
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 2));  // RPM
}

// === STATS decode ===

void test_decode_stats_validate() {
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_TRUE(validate_notify(fixtures::STATS_NOTIFY,
                                   sizeof(fixtures::STATS_NOTIFY),
                                   &addr, &payload_len));
  TEST_ASSERT_EQUAL_UINT8(0x05, addr);
  TEST_ASSERT_EQUAL_UINT(53, payload_len);
}

void test_decode_stats_soc_90_percent() {
  // off 31 = statusBatteryValue = SOC% (90% = 4/5 bars, confirmed against display)
  const uint8_t *p = fixtures::STATS_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT8(90, p[31]);
}

void test_decode_stats_total_km_42_8() {
  // off 23-26 = totalKilometers BE32/10
  const uint8_t *p = fixtures::STATS_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT32(428, u32be(p, 23));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.8f, u32be(p, 23) / 10.0f);
}

void test_decode_stats_gear() {
  const uint8_t *p = fixtures::STATS_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT8(1, p[33]);  // bicycleGear
}

// === STATS bit-decode (lights/charging/turns) ===

void test_decode_stats_open_light_on() {
  // off 34 (0x27) bit 3 = openLight
  const uint8_t *p = fixtures::STATS_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_BIT_HIGH(3, p[34]);
}

void test_decode_stats_charging_on() {
  // off 37 (0x2A) bit 3 = statusStateOfCharge
  const uint8_t *p = fixtures::STATS_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_BIT_HIGH(3, p[37]);
}

void test_decode_stats_left_turn_signal() {
  // off 51 (0x38) bit 0 = leftTurnLight
  const uint8_t *p = fixtures::STATS_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_BIT_HIGH(0, p[51]);
}

void test_decode_stats_right_turn_signal() {
  // off 51 (0x38) bit 1 = rightTurnLight
  const uint8_t *p = fixtures::STATS_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_BIT_HIGH(1, p[51]);
}

void test_decode_stats_brake_off() {
  // off 37 (0x2A) bit 5 = brake handle status; low (not engaged) in idle fixture.
  const uint8_t *p = fixtures::STATS_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_BIT_LOW(5, p[37]);
}

// === METER decode ===

void test_decode_meter_validate() {
  uint8_t addr = 0;
  size_t payload_len = 0;
  TEST_ASSERT_TRUE(validate_notify(fixtures::METER_NOTIFY,
                                   sizeof(fixtures::METER_NOTIFY),
                                   &addr, &payload_len));
  TEST_ASSERT_EQUAL_UINT8(0x60, addr);
  TEST_ASSERT_EQUAL_UINT(13, payload_len);
}

void test_decode_meter_hw_sw() {
  const uint8_t *p = fixtures::METER_NOTIFY + NOTIFY_HDR_LEN;
  TEST_ASSERT_EQUAL_UINT8(32, p[0]);  // HW=32
  TEST_ASSERT_EQUAL_UINT8(40, p[1]);  // SW=40
}

void test_decode_meter_mode_data() {
  const uint8_t *p = fixtures::METER_NOTIFY + NOTIFY_HDR_LEN;
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

int main(int argc, char **argv) {
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
  RUN_TEST(test_validate_notify_bad_crc);
  RUN_TEST(test_validate_notify_bad_signature);
  RUN_TEST(test_validate_notify_wrong_length);
  RUN_TEST(test_validate_notify_too_short);

  // HANDSHAKE
  RUN_TEST(test_validate_handshake);

  // BATTERY
  RUN_TEST(test_decode_battery_validate);
  RUN_TEST(test_decode_battery_voltage_48v);
  RUN_TEST(test_decode_battery_capacity_11_6_ah);
  RUN_TEST(test_decode_battery_hw_sw);
  RUN_TEST(test_decode_battery_idle_no_current);

  // CTRL
  RUN_TEST(test_decode_ctrl_validate);
  RUN_TEST(test_decode_ctrl_versions);

  // MOTOR
  RUN_TEST(test_decode_motor_validate);
  RUN_TEST(test_decode_motor_wheel_28_inch);
  RUN_TEST(test_decode_motor_capacity_350w);
  RUN_TEST(test_decode_motor_version);

  // ENERGY
  RUN_TEST(test_decode_energy_validate);
  RUN_TEST(test_decode_energy_startup_time);
  RUN_TEST(test_decode_energy_idle_zero_totals);

  // STATS + bonus bits
  RUN_TEST(test_decode_stats_validate);
  RUN_TEST(test_decode_stats_soc_90_percent);
  RUN_TEST(test_decode_stats_total_km_42_8);
  RUN_TEST(test_decode_stats_gear);
  RUN_TEST(test_decode_stats_open_light_on);
  RUN_TEST(test_decode_stats_charging_on);
  RUN_TEST(test_decode_stats_left_turn_signal);
  RUN_TEST(test_decode_stats_right_turn_signal);
  RUN_TEST(test_decode_stats_brake_off);

  // METER
  RUN_TEST(test_decode_meter_validate);
  RUN_TEST(test_decode_meter_hw_sw);
  RUN_TEST(test_decode_meter_mode_data);

  // u16be/u32be
  RUN_TEST(test_u16be_basic);
  RUN_TEST(test_u32be_basic);

  return UNITY_END();
}
