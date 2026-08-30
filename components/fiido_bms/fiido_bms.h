#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "esphome/core/component.h"

#ifdef USE_ESP32
#include <esp_gattc_api.h>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/button/button.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "fiido_link.h"
#include "fiido_protocol.h"
#include "fiido_state.h"

namespace esphome::fiido_bms {

constexpr const char *FIIDO_BMS_TAG = "fiido_bms";

namespace espbt = esphome::esp32_ble_tracker;

class FiidoGearSelect;
class FiidoBMSHub;

// Row order in FLAG_CONTROLS.
enum class FlagId : size_t {
  SPEAKER = 0,
  KEY_SOUND,
  THROTTLE,
  SLOW_MODE,
  BIKE_GUARD,
#ifdef USE_FIIDO_BMS_DEV
  CRUISE,
  START_MODE,
  INSENSITIVITY,
  SHOW_TOTAL_KM,
  AUTO_SCREEN_OFF,
  RING,
  DOUBLE_SPEED,
#endif
  COUNT,
};

// Speaker is 00 audible / 01 silent; 0x2C bit 4 and 0x2B bit 1 read inverted.
struct FlagControl {
  Addr addr;
  size_t slot;
  uint8_t mask;
  uint8_t bits_on;
  uint8_t bits_off;
  const char *name;
  switch_::Switch *FiidoBMSHub::*entity;
  bool FlagView::*state;
};

// Not defined: -fno-exceptions rules out throw.
void flag_bits_outside_mask();

// The speaker takes one of two silencing patterns rather than the whole mask.
template <Addr A>
[[nodiscard]] consteval FlagControl flag_control(RegBit<A> bit, uint8_t bits_on, uint8_t bits_off, const char *name,
                                                 switch_::Switch *FiidoBMSHub::*entity, bool FlagView::*state) {
  if ((bits_on & ~bit.mask) != 0 || (bits_off & ~bit.mask) != 0)
    flag_bits_outside_mask();
  return {.addr = A,
          .slot = cache_slot(A),
          .mask = bit.mask,
          .bits_on = bits_on,
          .bits_off = bits_off,
          .name = name,
          .entity = entity,
          .state = state};
}

template <Addr A>
[[nodiscard]] consteval FlagControl flag_control(RegBit<A> bit, const char *name, switch_::Switch *FiidoBMSHub::*entity,
                                                 bool FlagView::*state) {
  return flag_control(bit, bit.mask, 0x00, name, entity, state);
}

// The bit is set when the feature is off.
template <Addr A>
[[nodiscard]] consteval FlagControl inverted_flag_control(RegBit<A> bit, const char *name,
                                                          switch_::Switch *FiidoBMSHub::*entity,
                                                          bool FlagView::*state) {
  return flag_control(bit, 0x00, bit.mask, name, entity, state);
}

template <Addr A, size_t N>
[[nodiscard]] consteval bool flag_row_is(const std::array<FlagControl, N> &rows, FlagId id, RegBit<A> bit) {
  const FlagControl &row = rows[static_cast<size_t>(id)];
  return row.addr == A && row.mask == bit.mask;
}

// Row order in BYTE_CONTROLS.
#ifdef USE_FIIDO_BMS_DEV
enum class ByteId : size_t {
  BRIGHTNESS = 0,
  BOOST,
  GUARD_TIME,
  COUNT,
};
#endif

enum class FieldWidth : uint8_t { U8, U16BE, U32BE };

// One scalar sensor fed straight from a poll payload.
struct SensorField {
  sensor::Sensor *FiidoBMSHub::*entity;
  uint8_t offset;
  FieldWidth width;
  float divisor;
  const char *label;
};

// Whole-byte writes, no mask.
struct ByteControl {
  FrameType type;
  Addr addr;
  size_t slot;
  const char *name;
  number::Number *FiidoBMSHub::*entity;
};

struct NotifyParser {
  Addr addr;
  void (FiidoBMSHub::*parse)(std::span<const uint8_t>);
};

class FiidoBMSHub : public ble_client::BLEClientNode, public PollingComponent {
 public:
  void setup() override;
  // Cadence comes from update() and from BLE callbacks. Dropping the override
  // would not help: BLEClientNode declares loop() too, so the trait that puts a
  // component in the main loop still resolves to true.
  void loop() override { this->disable_loop(); }
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;

  void set_startup_delay(uint32_t ms) { this->startup_delay_ms_ = ms; }
  void set_hub_index(int i) { this->hub_index_ = i; }
  void set_total_hubs(int n) { this->total_hubs_ = n; }

  void set_motor_enable(bool on);
  void set_light_enable(bool on);
  void set_gear(uint8_t gear);
  void set_gear_mode(uint8_t mode);
  void set_speed_limit(const std::string &option);
  void set_speed_unit(const std::string &option);
  void set_speaker_enable(bool on);
  void set_key_sound_enable(bool on);
  void set_throttle_enable(bool on);
  void set_slow_mode_enable(bool on);
  void set_ble_user_enabled(bool en);
  void set_bike_guard_enable(bool on);

#ifdef USE_FIIDO_BMS_DEV
  void set_cruise_enable(bool on);
  void set_start_mode_enable(bool on);
  void set_insensitivity_enable(bool on);
  void set_show_total_km_enable(bool on);
  void set_auto_screen_off_enable(bool on);
  void set_ring_enable(bool on);
  void set_double_speed_enable(bool on);

  void set_brightness(float value);
  void set_boost(float value);
  void set_guard_time(float value);
  void pair_watch();
#endif

  SUB_SENSOR(battery_voltage)
  SUB_SENSOR(battery_current_voltage)
  SUB_SENSOR(battery_current)
  SUB_SENSOR(battery_capacity)
  SUB_SENSOR(battery_manufacturer)
  SUB_SENSOR(battery_hw_version)
  SUB_SENSOR(battery_sw_version)
  SUB_SENSOR(ctrl_upper_voltage)
  SUB_SENSOR(ctrl_lower_voltage)
  SUB_SENSOR(ctrl_current)
  SUB_SENSOR(ctrl_temperature)
  SUB_SENSOR(ctrl_hw_version)
  SUB_SENSOR(ctrl_sw_version)
  SUB_SENSOR(ctrl_version)
  SUB_SENSOR(ctrl_manufacturer)
  SUB_SENSOR(motor_version)
  SUB_SENSOR(motor_magnetic)
  SUB_SENSOR(motor_wire_count)
  SUB_SENSOR(motor_steel_count)
  SUB_SENSOR(motor_reduction_ratio)
  SUB_SENSOR(motor_wheel_diameter)
  SUB_SENSOR(motor_temperature)
  SUB_SENSOR(motor_capacity)
  SUB_SENSOR(crank_torque)
  SUB_SENSOR(crank_rpm)
  SUB_SENSOR(this_take_energy)
  SUB_SENSOR(total_take_energy)
  SUB_SENSOR(startup_time)
  SUB_SENSOR(bicycle_speed)
  SUB_SENSOR(current_kilometers)
  SUB_SENSOR(total_kilometers)
  SUB_SENSOR(battery_soc)
  SUB_SENSOR(bicycle_gear_start)
  SUB_SENSOR(meter_hw_version)
  SUB_SENSOR(meter_sw_version)
  SUB_SENSOR(meter_mode_data)

  SUB_BINARY_SENSOR(connected)
  SUB_BINARY_SENSOR(brake)
  SUB_BINARY_SENSOR(pas_limit)

  SUB_SWITCH(motor)
  SUB_SWITCH(light)
  SUB_SWITCH(autoshutdown)
  SUB_SWITCH(speaker)
  SUB_SWITCH(key_sound)
  SUB_SWITCH(throttle)
  SUB_SWITCH(slow_mode)
  SUB_SWITCH(ble)
  SUB_SWITCH(bike_guard)

  SUB_SELECT(mode)
  SUB_SELECT(speed_limit)
  SUB_SELECT(speed_unit)

#ifdef USE_FIIDO_BMS_DEV
  SUB_SWITCH(cruise)
  SUB_SWITCH(start_mode)
  SUB_SWITCH(insensitivity)
  SUB_SWITCH(show_total_km)
  SUB_SWITCH(auto_screen_off)
  SUB_SWITCH(ring)
  SUB_SWITCH(double_speed)

  SUB_NUMBER(brightness)
  SUB_NUMBER(boost)
  SUB_NUMBER(guard_time)

  SUB_BUTTON(pair_watch)
#endif

  void set_gear_select(FiidoGearSelect *s) { this->gear_select_ = s; }

  void enable_boost_poll() { this->poll_enabled_[poll_index(Addr::PAS_BOOST)] = true; }
  void enable_display_poll() { this->poll_enabled_[poll_index(Addr::DISPLAY)] = true; }

  void set_auto_shutdown_enabled(bool en);

  void set_update_interval_on_ms(uint32_t ms) { this->update_interval_on_ms_ = ms; }
  void set_update_interval_off_ms(uint32_t ms) { this->update_interval_off_ms_ = ms; }
  void set_idle_disconnect_ms(uint32_t ms) { this->idle_disconnect_ms_ = ms; }
  void set_enforce_gear_mode_3(bool en) { this->enforce_gear_mode_3_ = en; }

  void enable_battery_poll() { this->poll_enabled_[poll_index(Addr::BATTERY)] = true; }
  void enable_ctrl_poll() { this->poll_enabled_[poll_index(Addr::CTRL)] = true; }
  void enable_motor_poll() { this->poll_enabled_[poll_index(Addr::MOTOR)] = true; }
  void enable_energy_poll() { this->poll_enabled_[poll_index(Addr::ENERGY)] = true; }
  void enable_meter_poll() { this->poll_enabled_[poll_index(Addr::METER)] = true; }

 protected:
  static constexpr auto FLAG_CONTROLS = std::to_array<FlagControl>({
      flag_control(flags_38::SPEAKER_SILENT, 0x00, flags_38::SPEAKER_SILENCE_PATTERN, "SPEAKER",
                   &FiidoBMSHub::speaker_switch_, &FlagView::speaker_audible),
      inverted_flag_control(flags_2c::KEY_SOUND_OFF, "KEY_SOUND", &FiidoBMSHub::key_sound_switch_,
                            &FlagView::key_sound_on),
      inverted_flag_control(flags_2b::THROTTLE_OFF, "THROTTLE", &FiidoBMSHub::throttle_switch_, &FlagView::throttle_on),
      flag_control(flags_2c::SLOW_MODE, "SLOW_MODE", &FiidoBMSHub::slow_mode_switch_, &FlagView::slow_mode_on),
      flag_control(flags_2b::BIKE_GUARD, "BIKE_GUARD", &FiidoBMSHub::bike_guard_switch_, &FlagView::bike_guard_on),
#ifdef USE_FIIDO_BMS_DEV
      flag_control(flags_27::CRUISE, "CRUISE", &FiidoBMSHub::cruise_switch_, &FlagView::cruise_on),
      flag_control(flags_27::START_MODE, "START_MODE", &FiidoBMSHub::start_mode_switch_, &FlagView::start_mode_on),
      flag_control(flags_27::INSENSITIVITY, "INSENS", &FiidoBMSHub::insensitivity_switch_, &FlagView::insensitivity_on),
      flag_control(flags_28::SHOW_TOTAL_KM, "SHOW_TOTAL_KM", &FiidoBMSHub::show_total_km_switch_,
                   &FlagView::show_total_km_on),
      flag_control(flags_39::AUTO_SCREEN_OFF, "AUTO_SCREEN_OFF", &FiidoBMSHub::auto_screen_off_switch_,
                   &FlagView::auto_screen_off_on),
      flag_control(flags_39::RING, "RING", &FiidoBMSHub::ring_switch_, &FlagView::ring_on),
      flag_control(flags_2b::DOUBLE_SPEED, "DOUBLE_SPEED", &FiidoBMSHub::double_speed_switch_,
                   &FlagView::double_speed_on),
#endif
  });
  static_assert(FLAG_CONTROLS.size() == static_cast<size_t>(FlagId::COUNT), "FlagId and FLAG_CONTROLS disagree");
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::SPEAKER, flags_38::SPEAKER_SILENT));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::KEY_SOUND, flags_2c::KEY_SOUND_OFF));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::THROTTLE, flags_2b::THROTTLE_OFF));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::SLOW_MODE, flags_2c::SLOW_MODE));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::BIKE_GUARD, flags_2b::BIKE_GUARD));
#ifdef USE_FIIDO_BMS_DEV
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::CRUISE, flags_27::CRUISE));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::START_MODE, flags_27::START_MODE));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::INSENSITIVITY, flags_27::INSENSITIVITY));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::SHOW_TOTAL_KM, flags_28::SHOW_TOTAL_KM));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::AUTO_SCREEN_OFF, flags_39::AUTO_SCREEN_OFF));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::RING, flags_39::RING));
  static_assert(flag_row_is(FLAG_CONTROLS, FlagId::DOUBLE_SPEED, flags_2b::DOUBLE_SPEED));
#endif
  void set_flag_(FlagId id, bool on);

#ifdef USE_FIIDO_BMS_DEV
  static constexpr std::array<ByteControl, 3> BYTE_CONTROLS{{
      {.type = FrameType::WRITE_J0,
       .addr = Addr::DISPLAY,
       .slot = cache_slot(Addr::DISPLAY),
       .name = "BRIGHTNESS",
       .entity = &FiidoBMSHub::brightness_number_},
      {.type = FrameType::WRITE_L0,
       .addr = Addr::PAS_BOOST,
       .slot = cache_slot(Addr::PAS_BOOST),
       .name = "BOOST",
       .entity = &FiidoBMSHub::boost_number_},
      {.type = FrameType::WRITE_J0,
       .addr = Addr::GUARD_TIME,
       .slot = cache_slot(Addr::GUARD_TIME),
       .name = "GUARD_TIME",
       .entity = &FiidoBMSHub::guard_time_number_},
  }};
  static_assert(BYTE_CONTROLS.size() == static_cast<size_t>(ByteId::COUNT), "ByteId and BYTE_CONTROLS disagree");
  void set_byte_(ByteId id, float value);
#endif

  static constexpr std::array<SensorField, 7> BATTERY_FIELDS{{
      {.entity = &FiidoBMSHub::battery_hw_version_sensor_,
       .offset = battery::HW_VERSION,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Battery Hw Version"},
      {.entity = &FiidoBMSHub::battery_sw_version_sensor_,
       .offset = battery::SW_VERSION,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Battery Sw Version"},
      {.entity = &FiidoBMSHub::battery_capacity_sensor_,
       .offset = battery::CAPACITY_AH,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "Battery Capacity"},
      {.entity = &FiidoBMSHub::battery_voltage_sensor_,
       .offset = battery::VOLTAGE_V,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "Battery Voltage"},
      {.entity = &FiidoBMSHub::battery_current_voltage_sensor_,
       .offset = battery::CURRENT_VOLTAGE_V,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "Battery Current Voltage"},
      {.entity = &FiidoBMSHub::battery_current_sensor_,
       .offset = battery::CURRENT_A,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "Battery Current"},
      {.entity = &FiidoBMSHub::battery_manufacturer_sensor_,
       .offset = battery::MANUFACTURER,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Battery Manufacturer"},
  }};

  static constexpr std::array<SensorField, 8> CTRL_FIELDS{{
      {.entity = &FiidoBMSHub::ctrl_hw_version_sensor_,
       .offset = ctrl::HW_VERSION,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Ctrl Hw Version"},
      {.entity = &FiidoBMSHub::ctrl_sw_version_sensor_,
       .offset = ctrl::SW_VERSION,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Ctrl Sw Version"},
      {.entity = &FiidoBMSHub::ctrl_upper_voltage_sensor_,
       .offset = ctrl::UPPER_VOLTAGE_V,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "Ctrl Upper Voltage"},
      {.entity = &FiidoBMSHub::ctrl_lower_voltage_sensor_,
       .offset = ctrl::LOWER_VOLTAGE_V,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "Ctrl Lower Voltage"},
      {.entity = &FiidoBMSHub::ctrl_current_sensor_,
       .offset = ctrl::CURRENT_A,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "Ctrl Current"},
      {.entity = &FiidoBMSHub::ctrl_temperature_sensor_,
       .offset = ctrl::TEMPERATURE_C,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Ctrl Temperature"},
      {.entity = &FiidoBMSHub::ctrl_version_sensor_,
       .offset = ctrl::VERSION,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Ctrl Version"},
      {.entity = &FiidoBMSHub::ctrl_manufacturer_sensor_,
       .offset = ctrl::MANUFACTURER,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Ctrl Manufacturer"},
  }};

  // Temperature is signed and range-checked; it is parsed outside the table.
  static constexpr std::array<SensorField, 7> MOTOR_FIELDS{{
      {.entity = &FiidoBMSHub::motor_version_sensor_,
       .offset = motor::VERSION,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Motor Version"},
      {.entity = &FiidoBMSHub::motor_magnetic_sensor_,
       .offset = motor::MAGNETIC,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Motor Magnetic"},
      {.entity = &FiidoBMSHub::motor_wire_count_sensor_,
       .offset = motor::WIRE_COUNT,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Motor Wire Count"},
      {.entity = &FiidoBMSHub::motor_steel_count_sensor_,
       .offset = motor::STEEL_COUNT,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Motor Steel Count"},
      {.entity = &FiidoBMSHub::motor_reduction_ratio_sensor_,
       .offset = motor::REDUCTION_RATIO,
       .width = FieldWidth::U8,
       .divisor = 10.0f,
       .label = "Motor Reduction Ratio"},
      {.entity = &FiidoBMSHub::motor_wheel_diameter_sensor_,
       .offset = motor::WHEEL_DIAMETER_IN,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "Motor Wheel Diameter"},
      {.entity = &FiidoBMSHub::motor_capacity_sensor_,
       .offset = motor::CAPACITY_W,
       .width = FieldWidth::U16BE,
       .divisor = 1.0f,
       .label = "Motor Capacity"},
  }};

  static constexpr std::array<SensorField, 5> ENERGY_FIELDS{{
      {.entity = &FiidoBMSHub::crank_torque_sensor_,
       .offset = energy::CRANK_TORQUE_NM,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "Crank Torque"},
      {.entity = &FiidoBMSHub::crank_rpm_sensor_,
       .offset = energy::CRANK_RPM,
       .width = FieldWidth::U16BE,
       .divisor = 1.0f,
       .label = "Crank Rpm"},
      {.entity = &FiidoBMSHub::this_take_energy_sensor_,
       .offset = energy::THIS_TAKE_WH,
       .width = FieldWidth::U16BE,
       .divisor = 10.0f,
       .label = "This Take Energy"},
      {.entity = &FiidoBMSHub::total_take_energy_sensor_,
       .offset = energy::TOTAL_TAKE_WH,
       .width = FieldWidth::U32BE,
       .divisor = 10.0f,
       .label = "Total Take Energy"},
      {.entity = &FiidoBMSHub::startup_time_sensor_,
       .offset = energy::STARTUP_TIME_S,
       .width = FieldWidth::U16BE,
       .divisor = 1.0f,
       .label = "Startup Time"},
  }};

  static constexpr std::array<SensorField, 3> METER_FIELDS{{
      {.entity = &FiidoBMSHub::meter_hw_version_sensor_,
       .offset = meter::HW_VERSION,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Meter Hw Version"},
      {.entity = &FiidoBMSHub::meter_sw_version_sensor_,
       .offset = meter::SW_VERSION,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Meter Sw Version"},
      {.entity = &FiidoBMSHub::meter_mode_data_sensor_,
       .offset = meter::MODE_DATA,
       .width = FieldWidth::U8,
       .divisor = 1.0f,
       .label = "Meter Mode Data"},
  }};

  // Sensors filled from STATS rather than from a poll payload offset.
  static constexpr std::array<std::pair<sensor::Sensor * FiidoBMSHub::*, const char *>, 6> STATS_SENSORS{{
      {&FiidoBMSHub::bicycle_speed_sensor_, "Bicycle Speed"},
      {&FiidoBMSHub::current_kilometers_sensor_, "Current Kilometers"},
      {&FiidoBMSHub::total_kilometers_sensor_, "Total Kilometers"},
      {&FiidoBMSHub::battery_soc_sensor_, "Battery Soc"},
      {&FiidoBMSHub::bicycle_gear_start_sensor_, "Bicycle Gear Start"},
      {&FiidoBMSHub::motor_temperature_sensor_, "Motor Temperature"},
  }};

  void publish_fields_(std::span<const uint8_t> payload, std::span<const SensorField> fields);
  void log_sensors_(std::span<const SensorField> fields) const;

  static constexpr uint32_t BURST_INTERVAL_MS = 5;
  static constexpr uint32_t BURST_RETRY_MS = 50;
  static constexpr uint8_t BURST_SEND_RETRIES = 2;
  static constexpr uint32_t IDLE_SHUTDOWN_MS = 15 * 60 * 1000;
  static constexpr uint32_t PERIODIC_PROBE_MS = 5 * 60 * 1000;
  static constexpr uint32_t PROBE_WINDOW_MS = 60 * 1000;
  // Probe stays alive this long after dispatch to verify writes via STATS.
  static constexpr uint32_t WRITE_VERIFY_WINDOW_MS = 10 * 1000;
  // Delay between speed_limit PAS bit clear and 0x3C/0x27 override.
  // BMS side-effects 0x3C=25 when PAS bit clears; must wait past that.
  static constexpr uint32_t SPEED_LIMIT_PHASE2_DELAY_MS = 50;
  // Delay after WRITE before triggering force STATS poll for verify.
  static constexpr uint32_t FORCE_STATS_DELAY_MS = 500;
  // Lifecycle manager tick interval.
  static constexpr uint32_t LIFECYCLE_TICK_MS = 1000;
  // Minimum gap between two enforce_gear_mode_3 writes triggered by parse_stats_.
  static constexpr uint32_t ENFORCE_GEAR_MODE_3_COOLDOWN_MS = 60000;
  // Minimum gap between two writes dropping an out-of-range gear into the mode.
  static constexpr uint32_t GEAR_DROP_COOLDOWN_MS = 10000;
  // Rate limit for rejected-frame logs; a fragmented stream rejects every frame.
  static constexpr uint32_t BAD_NOTIFY_LOG_INTERVAL_MS = 5000;
  // Ambiguous speed limit is a resting state, not an event, and SPEEDLIM is
  // polled once per burst. Must exceed update_interval_off or it never throttles.
  static constexpr uint32_t AMBIGUOUS_LIMIT_LOG_INTERVAL_MS = 60000;
  static constexpr size_t BAD_NOTIFY_DUMP_LEN = 8;

  WriteError send_raw_write_(FrameType type, Addr addr, std::span<const uint8_t> payload);
  void send_handshake_();
  WriteError send_poll_(size_t idx, bool warn_on_fail);
  void send_burst_poll_();
  WriteError send_frame_(std::span<const uint8_t> frame, const char *name, bool warn_on_fail = true);
  void on_open_();
  void on_congestion_(bool congested);
  void on_disconnect_();
  void on_services_resolved_();
  void on_notify_registered_();
  void handle_notify_(std::span<const uint8_t> frame);
  // A write is fire and forget; the next STATS is the only confirmation.
  void schedule_write_verify_();
  void publish_flag_entities_(const FlagView &flags);
  void settle_probe_(bool motor_on);
  void publish_connected_(bool state);
  void mark_activity_(const char *reason);

  void manage_lifecycle_();
  void enqueue_pending_write_(PendingWrite fn);
  void dispatch_pending_writes_();
  void ensure_enabled_for_write_();

  void parse_battery_(std::span<const uint8_t> payload);
  void parse_ctrl_(std::span<const uint8_t> payload);
  void parse_motor_(std::span<const uint8_t> payload);
  void parse_energy_(std::span<const uint8_t> payload);
  void parse_stats_(std::span<const uint8_t> payload);
  void publish_stats_samples_(const StatsView &sv);
  void sync_gear_entities_(const StatsView &sv);
  void enforce_gear_mode_(const StatsView &sv);
  void cache_flag_registers_(const StatsView &sv);
  [[nodiscard]] FlagView cached_flags_() const;
  void apply_adaptive_interval_(bool motor_on);
  void clear_persisted_light_bit_(bool motor_on);
  void track_activity_(const RideState &ride, uint16_t speed_raw);
  void update_idle_timer_(bool motor_on);
  void parse_meter_(std::span<const uint8_t> payload);
  void parse_speed_limit_(std::span<const uint8_t> payload);
  void parse_boost_(std::span<const uint8_t> payload);
  void parse_display_(std::span<const uint8_t> payload);
  void apply_speed_limit_(SpeedLimitOption option);
  void apply_speed_unit_(bool mile);
  // Queues the retry itself on the two deferring verdicts. Caller republishes its
  // own entity on the two REJECT verdicts.
  [[nodiscard]] WriteGate gate_(bool cache_valid, bool needs_controller, const char *name, PendingWrite retry);
  template <Addr A>
  void write_masked_bits_(RegBit<A> bit, uint8_t bits, const char *name) {
    this->write_masked_bits_(A, cache_slot(A), bit.mask, bits, name);
  }
  template <Addr A>
  void write_flag_bit_(RegBit<A> bit, bool set, const char *name) {
    this->write_masked_bits_(bit, set ? bit.mask : uint8_t{0}, name);
  }
  void write_masked_bits_(Addr addr, size_t slot, uint8_t mask, uint8_t bits, const char *name);
  // Raw 1-byte value write (no bit-masking) for number entities. False when the
  // frame did not go out, so the caller keeps its cache and entity unchanged.
#ifdef USE_FIIDO_BMS_DEV
  [[nodiscard]] bool write_value_byte_(FrameType type, Addr addr, uint8_t value, const char *name);
#endif

  // Everything the session has to forget when the link drops.
  void reset_session_state_();

  uint32_t startup_delay_ms_{0};
  uint32_t connect_time_ms_{0};
  size_t burst_idx_{0};
  size_t burst_remaining_{0};
  uint8_t burst_retry_{0};
  bool handshake_sent_{false};
  FiidoLink link_;

  LogThrottle bad_notify_log_;
  LogThrottle unknown_addr_log_;
  LogThrottle ambiguous_limit_log_;
  LogThrottle gear_drop_throttle_;

  FiidoGearSelect *gear_select_{nullptr};

  bool ble_user_enabled_{true};

  // Only bits 4..0 of ADDR 0x39 are defined; the cache keeps those and a write
  // builds from them.
  RegisterCache registers_;

  uint32_t update_interval_on_ms_{3000};
  uint32_t update_interval_off_ms_{15000};
  uint32_t idle_disconnect_ms_{15 * 60 * 1000};
  bool enforce_gear_mode_3_{false};
  uint32_t last_enforce_gear_3_ms_{0};
  uint32_t desired_interval_ms_{3000};
  uint32_t last_burst_ms_{0};
  uint32_t last_burst_slot_{0};
  bool burst_started_{false};

  int hub_index_{0};
  int total_hubs_{1};

  bool force_poll_stats_{false};

  uint32_t last_activity_ms_{0};
  RideState prev_ride_{.gear = 0xFF, .motor_on = false, .light_on = false};
  bool auto_shutdown_enabled_{true};

  uint32_t motor_off_since_ms_{0};
  uint32_t disconnected_since_ms_{0};
  uint32_t probe_started_ms_{0};
  uint32_t last_dispatch_ms_{0};
  PendingWrites pending_writes_;

  std::array<bool, POLL_TABLE_SIZE> poll_enabled_{default_poll_enables()};
};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
