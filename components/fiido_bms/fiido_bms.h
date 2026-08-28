#pragma once

#include <functional>
#include <span>
#include <vector>

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
#include "fiido_protocol.h"
#include "fiido_state.h"

namespace esphome::fiido_bms {

constexpr const char *FIIDO_BMS_TAG = "fiido_bms";

namespace espbt = esphome::esp32_ble_tracker;

class FiidoGearSelect;

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
  void set_cruise_enable(bool on);
  void set_start_mode_enable(bool on);
  void set_insensitivity_enable(bool on);
  void set_show_total_km_enable(bool on);
  void set_auto_screen_off_enable(bool on);
  void set_ring_enable(bool on);
  void set_double_speed_enable(bool on);
  void set_bike_guard_enable(bool on);

  void set_brightness(float value);
  void set_boost(float value);
  void set_guard_time(float value);
  void pair_watch();

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
  SUB_SWITCH(cruise)
  SUB_SWITCH(start_mode)
  SUB_SWITCH(insensitivity)
  SUB_SWITCH(show_total_km)
  SUB_SWITCH(auto_screen_off)
  SUB_SWITCH(ring)
  SUB_SWITCH(double_speed)
  SUB_SWITCH(bike_guard)

  SUB_SELECT(mode)
  SUB_SELECT(speed_limit)
  SUB_SELECT(speed_unit)

  SUB_NUMBER(brightness)
  SUB_NUMBER(boost)
  SUB_NUMBER(guard_time)

  SUB_BUTTON(pair_watch)

  void set_gear_select(FiidoGearSelect *s) { this->gear_select_ = s; }

  void enable_boost_poll() { this->poll_enabled_boost_ = true; }
  void enable_display_poll() { this->poll_enabled_display_ = true; }

  void set_auto_shutdown_enabled(bool en);

  void set_update_interval_on_ms(uint32_t ms) { this->update_interval_on_ms_ = ms; }
  void set_update_interval_off_ms(uint32_t ms) { this->update_interval_off_ms_ = ms; }
  void set_idle_disconnect_ms(uint32_t ms) { this->idle_disconnect_ms_ = ms; }
  void set_enforce_gear_mode_3(bool en) { this->enforce_gear_mode_3_ = en; }

  void enable_battery_poll() { this->poll_enabled_battery_ = true; }
  void enable_ctrl_poll() { this->poll_enabled_ctrl_ = true; }
  void enable_motor_poll() { this->poll_enabled_motor_ = true; }
  void enable_energy_poll() { this->poll_enabled_energy_ = true; }
  void enable_meter_poll() { this->poll_enabled_meter_ = true; }

 protected:
  static constexpr uint32_t BURST_INTERVAL_MS = 5;
  static constexpr uint32_t BURST_RETRY_MS = 50;
  static constexpr uint8_t BURST_SEND_RETRIES = 2;
  static constexpr uint32_t IDLE_SHUTDOWN_MS = 15 * 60 * 1000;
  static constexpr uint32_t PERIODIC_PROBE_MS = 5 * 60 * 1000;
  static constexpr uint32_t PROBE_WINDOW_MS = 60 * 1000;
  // Probe stays alive this long after dispatch to verify writes via STATS.
  static constexpr uint32_t WRITE_VERIFY_WINDOW_MS = 10 * 1000;
  // Cap pending_writes_ to bound RAM under spam clicking during disconnect.
  static constexpr size_t MAX_PENDING_WRITES = 32;
  // Delay between speed_limit PAS bit clear and 0x3C/0x27 override.
  // BMS side-effects 0x3C=25 when PAS bit clears; must wait past that.
  static constexpr uint32_t SPEED_LIMIT_PHASE2_DELAY_MS = 50;
  // Delay after WRITE before triggering force STATS poll for verify.
  static constexpr uint32_t FORCE_STATS_DELAY_MS = 500;
  // Lifecycle manager tick interval.
  static constexpr uint32_t LIFECYCLE_TICK_MS = 1000;
  // Minimum gap between two enforce_gear_mode_3 writes triggered by parse_stats_.
  static constexpr uint32_t ENFORCE_GEAR_MODE_3_COOLDOWN_MS = 60000;
  // Rate limit for rejected-frame logs; a fragmented stream rejects every frame.
  static constexpr uint32_t BAD_NOTIFY_LOG_INTERVAL_MS = 5000;
  // Ambiguous speed limit is a resting state, not an event, and SPEEDLIM is
  // polled once per burst. Must exceed update_interval_off or it never throttles.
  static constexpr uint32_t AMBIGUOUS_LIMIT_LOG_INTERVAL_MS = 60000;
  static constexpr size_t BAD_NOTIFY_DUMP_LEN = 8;

  WriteError send_raw_write(FrameType type, Addr addr, std::span<const uint8_t> payload);
  void send_handshake_();
  WriteError send_poll_(size_t idx, bool warn_on_fail);
  void send_burst_poll_();
  WriteError send_frame_(std::span<const uint8_t> frame, const char *name, bool warn_on_fail = true);
  void publish_connected_(bool state);
  void mark_activity_(const char *reason);

  void manage_lifecycle_();
  void enqueue_pending_write_(std::function<void()> fn);
  void dispatch_pending_writes_();
  void ensure_enabled_for_write_();

  void parse_battery_(std::span<const uint8_t> payload);
  void parse_ctrl_(std::span<const uint8_t> payload);
  void parse_motor_(std::span<const uint8_t> payload);
  void parse_energy_(std::span<const uint8_t> payload);
  void parse_stats_(std::span<const uint8_t> payload);
  void parse_meter_(std::span<const uint8_t> payload);
  void parse_speed_limit_(std::span<const uint8_t> payload);
  void parse_boost_(std::span<const uint8_t> payload);
  void parse_display_(std::span<const uint8_t> payload);
  [[nodiscard]] bool defer_flag_write_(bool cache_valid, const char *name, std::function<void()> retry);
  void write_masked_bits_(Addr addr, uint8_t mask, uint8_t bits, uint8_t *cache, const char *name);
  void write_flag_bit_(Addr addr, uint8_t mask, bool set, uint8_t *cache, const char *name);
  // Raw 1-byte value write (no bit-masking) for number entities. False when the
  // frame did not go out, so the caller keeps its cache and entity unchanged.
  [[nodiscard]] bool write_value_byte_(FrameType type, Addr addr, uint8_t value, const char *name);
  // Republish the last shown value, undoing an optimistic UI change.
  static void revert_number_(number::Number *n);

  // Drop a publish that repeats the current value. switch and binary_sensor do
  // this in their base class; sensor, select and number do not.
  static void publish_(sensor::Sensor *s, float value);
  static void publish_(binary_sensor::BinarySensor *s, bool value);
  static void publish_(select::Select *s, const char *option);
  static void publish_(number::Number *n, float value);

  uint32_t startup_delay_ms_{0};
  uint32_t connect_time_ms_{0};
  size_t burst_idx_{0};
  size_t burst_remaining_{0};
  uint8_t burst_retry_{0};
  bool link_congested_{false};
  uint16_t char_write_handle_{0};
  uint16_t char_notify_handle_{0};
  bool handshake_sent_{false};

  uint32_t last_bad_notify_log_ms_{0};
  uint32_t bad_notify_count_{0};
  uint32_t last_unknown_addr_log_ms_{0};
  uint32_t unknown_addr_count_{0};
  uint32_t last_ambiguous_limit_log_ms_{0};
  uint32_t ambiguous_limit_count_{0};

  FiidoGearSelect *gear_select_{nullptr};

  bool ble_user_enabled_{true};

  // payload[34] = ADDR 0x27. Cache for bit-masked writes (motor/light/...).
  uint8_t addr_27_cache_{0};
  bool addr_27_valid_{false};

  // payload[32] = ADDR 0x25 (gear range, nibble-encoded).
  uint8_t addr_25_cache_{0};
  bool addr_25_valid_{false};

  // ADDR 0x3C speed limit value (km/h), separate poll.
  uint8_t addr_3C_cache_{0};
  bool addr_3C_valid_{false};

  // payload[35] = ADDR 0x28.
  uint8_t addr_28_cache_{0};
  bool addr_28_valid_{false};

  // payload[38] = ADDR 0x2B.
  uint8_t addr_2B_cache_{0};
  bool addr_2B_valid_{false};

  // payload[39] = ADDR 0x2C.
  uint8_t addr_2C_cache_{0};
  bool addr_2C_valid_{false};

  // payload[51] = ADDR 0x38.
  uint8_t addr_38_cache_{0};
  bool addr_38_valid_{false};

  // payload[52] = ADDR 0x39. Only bits 4..0 are defined; bits 7..5 are written
  // as 0, so the cache keeps the low 5 bits and the write builds from them.
  uint8_t addr_39_cache_{0};
  bool addr_39_valid_{false};

  // ADDR 0x52 boost level, separate poll.
  uint8_t addr_52_cache_{0};
  bool addr_52_valid_{false};

  // ADDR 0x57 brightness + 0x58 guard time, separate display poll.
  uint8_t addr_57_cache_{0};
  uint8_t addr_58_cache_{0};
  bool addr_57_valid_{false};

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
  uint8_t prev_gear_{0xFF};
  bool prev_light_on_{false};
  bool prev_motor_on_{false};
  bool auto_shutdown_enabled_{true};

  uint32_t motor_off_since_ms_{0};
  uint32_t disconnected_since_ms_{0};
  uint32_t probe_started_ms_{0};
  uint32_t last_dispatch_ms_{0};
  PendingWrites pending_writes_{MAX_PENDING_WRITES};

  bool poll_enabled_battery_{false};
  bool poll_enabled_ctrl_{false};
  bool poll_enabled_motor_{false};
  bool poll_enabled_energy_{false};
  bool poll_enabled_meter_{false};
  bool poll_enabled_boost_{false};
  bool poll_enabled_display_{false};
};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
