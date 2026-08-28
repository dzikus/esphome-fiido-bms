// These index with their own literals. The stats:: offsets the decoder uses are
// covered in test_decode.cpp.
#include <unity.h>

#include "fiido_protocol.h"
#include "fixtures.h"
#include "test_groups.h"

using namespace esphome::fiido_bms;

static void test_fixture_battery_validate() {
  const NotifyView notify = validate_notify(fixtures::BATTERY_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x7B, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(13, notify.payload.size());
}

static void test_fixture_battery_voltage_48v() {
  // off 4-5 = batteryVoltage BE/10. Real bike reported 48.0V.
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::BATTERY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(480, u16be(p, 4));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 48.0f, u16be(p, 4) / 10.0f);
}

static void test_fixture_battery_capacity_11_6_ah() {
  // off 2-3 = totalLevel BE/10 -> 11.6 Ah (C11 Pro spec)
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::BATTERY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(116, u16be(p, 2));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.6f, u16be(p, 2) / 10.0f);
}

static void test_fixture_battery_hw_sw() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::BATTERY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);  // HW
  TEST_ASSERT_EQUAL_UINT8(1, p[1]);  // SW
}

static void test_fixture_battery_idle_no_current() {
  // off 7-8 = currentVoltage, off 9-10 = current -> idle = 0
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::BATTERY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 7));
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 9));
}

static void test_fixture_ctrl_validate() {
  const NotifyView notify = validate_notify(fixtures::CTRL_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0xAF, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(12, notify.payload.size());
}

static void test_fixture_ctrl_versions() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::CTRL_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);     // HW
  TEST_ASSERT_EQUAL_UINT8(0xD3, p[1]);  // SW = 211
  TEST_ASSERT_EQUAL_UINT8(18, p[10]);   // currentVersion = 0x12
  TEST_ASSERT_EQUAL_UINT8(1, p[11]);    // manufacturer
}

static void test_fixture_motor_validate() {
  const NotifyView notify = validate_notify(fixtures::MOTOR_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x96, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(12, notify.payload.size());
}

static void test_fixture_motor_wheel_28_inch() {
  // off 5-6 = wheel BE/10. C11 has 28" wheels (physically verified).
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::MOTOR_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(280, u16be(p, 5));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 28.0f, u16be(p, 5) / 10.0f);
}

static void test_fixture_motor_capacity_350w() {
  // off 9-10 = capacity -> C11 has a 350W motor (spec)
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::MOTOR_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(350, u16be(p, 9));
}

static void test_fixture_motor_version() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::MOTOR_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);  // emVersion
}

static void test_fixture_energy_validate() {
  const NotifyView notify = validate_notify(fixtures::ENERGY_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0xC8, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(12, notify.payload.size());
}

static void test_fixture_energy_startup_time() {
  // off 10-11 = startupTime BE = 57 s (BMS uptime since power-on)
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::ENERGY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(57, u16be(p, 10));
}

static void test_fixture_energy_idle_zero_totals() {
  // off 6-9 = totalTakeEnergy BE32/10 -> 0 when idle
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::ENERGY_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT32(0, u32be(p, 6));
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 0));  // torque
  TEST_ASSERT_EQUAL_UINT16(0, u16be(p, 2));  // RPM
}

static void test_fixture_stats_validate() {
  const NotifyView notify = validate_notify(fixtures::STATS_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x05, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(53, notify.payload.size());
}

static void test_fixture_stats_soc_90_percent() {
  // off 31 = statusBatteryValue = SOC% (90% = 4/5 bars, confirmed against display)
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(90, p[31]);
}

static void test_fixture_stats_total_km_42_8() {
  // off 23-26 = totalKilometers BE32/10
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT32(428, u32be(p, 23));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.8f, u32be(p, 23) / 10.0f);
}

static void test_fixture_stats_gear() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(1, p[33]);  // bicycleGear
}

static void test_fixture_stats_open_light_on() {
  // off 34 (0x27) bit 3 = openLight
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_HIGH(3, p[34]);
}

static void test_fixture_stats_charging_on() {
  // off 37 (0x2A) bit 3 = statusStateOfCharge
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_HIGH(3, p[37]);
}

static void test_fixture_stats_left_turn_signal() {
  // off 51 (0x38) bit 0 = leftTurnLight
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_HIGH(0, p[51]);
}

static void test_fixture_stats_right_turn_signal() {
  // off 51 (0x38) bit 1 = rightTurnLight
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_HIGH(1, p[51]);
}

static void test_fixture_stats_brake_off() {
  // off 37 (0x2A) bit 5 = brake handle status; low (not engaged) in idle fixture.
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_BIT_LOW(5, p[37]);
}

static void test_fixture_meter_validate() {
  const NotifyView notify = validate_notify(fixtures::METER_NOTIFY);
  TEST_ASSERT_TRUE(notify.valid);
  TEST_ASSERT_EQUAL_UINT8(0x60, static_cast<uint8_t>(notify.addr));
  TEST_ASSERT_EQUAL_UINT(13, notify.payload.size());
}

static void test_fixture_meter_hw_sw() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::METER_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(32, p[0]);  // HW=32
  TEST_ASSERT_EQUAL_UINT8(40, p[1]);  // SW=40
}

static void test_fixture_meter_mode_data() {
  const std::span<const uint8_t> p = std::span<const uint8_t>(fixtures::METER_NOTIFY).subspan(NOTIFY_HDR_LEN);
  TEST_ASSERT_EQUAL_UINT8(55, p[7]);  // modeData = 55
}

void run_captured_frame_tests() {
  RUN_TEST(test_fixture_battery_validate);
  RUN_TEST(test_fixture_battery_voltage_48v);
  RUN_TEST(test_fixture_battery_capacity_11_6_ah);
  RUN_TEST(test_fixture_battery_hw_sw);
  RUN_TEST(test_fixture_battery_idle_no_current);
  RUN_TEST(test_fixture_ctrl_validate);
  RUN_TEST(test_fixture_ctrl_versions);
  RUN_TEST(test_fixture_motor_validate);
  RUN_TEST(test_fixture_motor_wheel_28_inch);
  RUN_TEST(test_fixture_motor_capacity_350w);
  RUN_TEST(test_fixture_motor_version);
  RUN_TEST(test_fixture_energy_validate);
  RUN_TEST(test_fixture_energy_startup_time);
  RUN_TEST(test_fixture_energy_idle_zero_totals);
  RUN_TEST(test_fixture_stats_validate);
  RUN_TEST(test_fixture_stats_soc_90_percent);
  RUN_TEST(test_fixture_stats_total_km_42_8);
  RUN_TEST(test_fixture_stats_gear);
  RUN_TEST(test_fixture_stats_open_light_on);
  RUN_TEST(test_fixture_stats_charging_on);
  RUN_TEST(test_fixture_stats_left_turn_signal);
  RUN_TEST(test_fixture_stats_right_turn_signal);
  RUN_TEST(test_fixture_stats_brake_off);
  RUN_TEST(test_fixture_meter_validate);
  RUN_TEST(test_fixture_meter_hw_sw);
  RUN_TEST(test_fixture_meter_mode_data);
}
