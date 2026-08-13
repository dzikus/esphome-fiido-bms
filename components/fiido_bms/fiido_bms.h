#pragma once

#include "esphome/core/component.h"

#include <functional>
#include <vector>

#ifdef USE_ESP32
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include <esp_gattc_api.h>

#include "fiido_protocol.h"

namespace esphome {
namespace fiido_bms {

constexpr const char *FIIDO_BMS_TAG = "fiido_bms";

namespace espbt = esphome::esp32_ble_tracker;

class FiidoMotorSwitch;
class FiidoLightSwitch;
class FiidoAutoshutdownSwitch;
class FiidoGearSelect;
class FiidoModeSelect;
class FiidoSpeedLimitSelect;
class FiidoSpeedUnitSelect;
class FiidoSpeakerSwitch;
class FiidoKeySoundSwitch;
class FiidoThrottleSwitch;
class FiidoSlowModeSwitch;
class FiidoBleSwitch;
class FiidoCruiseSwitch;
class FiidoStartModeSwitch;
class FiidoInsensitivitySwitch;
class FiidoShowTotalKmSwitch;
class FiidoAutoScreenOffSwitch;
class FiidoRingSwitch;
class FiidoDoubleSpeedSwitch;
class FiidoBikeGuardSwitch;
class FiidoBrightnessNumber;
class FiidoBoostNumber;
class FiidoGuardTimeNumber;
class FiidoPairWatchButton;

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

  void gap_event_handler(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *param) override;

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

  void set_motor_switch(FiidoMotorSwitch *sw) { motor_switch_ = sw; }
  void set_light_switch(FiidoLightSwitch *sw) { light_switch_ = sw; }
  void set_autoshutdown_switch(FiidoAutoshutdownSwitch *sw) { autoshutdown_switch_ = sw; }
  void set_gear_select(FiidoGearSelect *s) { gear_select_ = s; }
  void set_mode_select(FiidoModeSelect *s) { mode_select_ = s; }
  void set_speed_limit_select(FiidoSpeedLimitSelect *s) { speed_limit_select_ = s; }
  void set_speed_unit_select(FiidoSpeedUnitSelect *s) { speed_unit_select_ = s; }
  void set_speaker_switch(FiidoSpeakerSwitch *sw) { speaker_switch_ = sw; }
  void set_key_sound_switch(FiidoKeySoundSwitch *sw) { key_sound_switch_ = sw; }
  void set_throttle_switch(FiidoThrottleSwitch *sw) { throttle_switch_ = sw; }
  void set_slow_mode_switch(FiidoSlowModeSwitch *sw) { slow_mode_switch_ = sw; }
  void set_ble_switch(FiidoBleSwitch *sw) { ble_switch_ = sw; }
  void set_cruise_switch(FiidoCruiseSwitch *sw) { cruise_switch_ = sw; }
  void set_start_mode_switch(FiidoStartModeSwitch *sw) { start_mode_switch_ = sw; }
  void set_insensitivity_switch(FiidoInsensitivitySwitch *sw) { insensitivity_switch_ = sw; }
  void set_show_total_km_switch(FiidoShowTotalKmSwitch *sw) { show_total_km_switch_ = sw; }
  void set_auto_screen_off_switch(FiidoAutoScreenOffSwitch *sw) { auto_screen_off_switch_ = sw; }
  void set_ring_switch(FiidoRingSwitch *sw) { ring_switch_ = sw; }
  void set_double_speed_switch(FiidoDoubleSpeedSwitch *sw) { double_speed_switch_ = sw; }
  void set_bike_guard_switch(FiidoBikeGuardSwitch *sw) { bike_guard_switch_ = sw; }

  void set_brightness_number(FiidoBrightnessNumber *n) { brightness_number_ = n; }
  void set_boost_number(FiidoBoostNumber *n) { boost_number_ = n; }
  void set_guard_time_number(FiidoGuardTimeNumber *n) { guard_time_number_ = n; }
  void set_pair_watch_button(FiidoPairWatchButton *b) { pair_watch_button_ = b; }

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

  void set_battery_voltage_sensor(sensor::Sensor *s) { battery_voltage_sensor_ = s; }
  void set_battery_current_voltage_sensor(sensor::Sensor *s) { battery_current_voltage_sensor_ = s; }
  void set_battery_current_sensor(sensor::Sensor *s) { battery_current_sensor_ = s; }
  void set_battery_capacity_sensor(sensor::Sensor *s) { battery_capacity_sensor_ = s; }
  void set_battery_manufacturer_sensor(sensor::Sensor *s) { battery_manufacturer_sensor_ = s; }
  void set_battery_hw_version_sensor(sensor::Sensor *s) { battery_hw_version_sensor_ = s; }
  void set_battery_sw_version_sensor(sensor::Sensor *s) { battery_sw_version_sensor_ = s; }

  void set_ctrl_upper_voltage_sensor(sensor::Sensor *s) { ctrl_upper_voltage_sensor_ = s; }
  void set_ctrl_lower_voltage_sensor(sensor::Sensor *s) { ctrl_lower_voltage_sensor_ = s; }
  void set_ctrl_current_sensor(sensor::Sensor *s) { ctrl_current_sensor_ = s; }
  void set_ctrl_temperature_sensor(sensor::Sensor *s) { ctrl_temperature_sensor_ = s; }
  void set_ctrl_hw_version_sensor(sensor::Sensor *s) { ctrl_hw_version_sensor_ = s; }
  void set_ctrl_sw_version_sensor(sensor::Sensor *s) { ctrl_sw_version_sensor_ = s; }
  void set_ctrl_version_sensor(sensor::Sensor *s) { ctrl_version_sensor_ = s; }
  void set_ctrl_manufacturer_sensor(sensor::Sensor *s) { ctrl_manufacturer_sensor_ = s; }

  void set_motor_version_sensor(sensor::Sensor *s) { motor_version_sensor_ = s; }
  void set_motor_magnetic_sensor(sensor::Sensor *s) { motor_magnetic_sensor_ = s; }
  void set_motor_wire_count_sensor(sensor::Sensor *s) { motor_wire_count_sensor_ = s; }
  void set_motor_steel_count_sensor(sensor::Sensor *s) { motor_steel_count_sensor_ = s; }
  void set_motor_reduction_ratio_sensor(sensor::Sensor *s) { motor_reduction_ratio_sensor_ = s; }
  void set_motor_wheel_diameter_sensor(sensor::Sensor *s) { motor_wheel_diameter_sensor_ = s; }
  void set_motor_temperature_sensor(sensor::Sensor *s) { motor_temperature_sensor_ = s; }
  void set_motor_capacity_sensor(sensor::Sensor *s) { motor_capacity_sensor_ = s; }

  void set_crank_torque_sensor(sensor::Sensor *s) { crank_torque_sensor_ = s; }
  void set_crank_rpm_sensor(sensor::Sensor *s) { crank_rpm_sensor_ = s; }
  void set_this_take_energy_sensor(sensor::Sensor *s) { this_take_energy_sensor_ = s; }
  void set_total_take_energy_sensor(sensor::Sensor *s) { total_take_energy_sensor_ = s; }
  void set_startup_time_sensor(sensor::Sensor *s) { startup_time_sensor_ = s; }

  void set_bicycle_speed_sensor(sensor::Sensor *s) { bicycle_speed_sensor_ = s; }
  void set_current_kilometers_sensor(sensor::Sensor *s) { current_kilometers_sensor_ = s; }
  void set_total_kilometers_sensor(sensor::Sensor *s) { total_kilometers_sensor_ = s; }
  void set_battery_soc_sensor(sensor::Sensor *s) { battery_soc_sensor_ = s; }
  void set_bicycle_gear_start_sensor(sensor::Sensor *s) { bicycle_gear_start_sensor_ = s; }

  void set_meter_hw_version_sensor(sensor::Sensor *s) { meter_hw_version_sensor_ = s; }
  void set_meter_sw_version_sensor(sensor::Sensor *s) { meter_sw_version_sensor_ = s; }
  void set_meter_mode_data_sensor(sensor::Sensor *s) { meter_mode_data_sensor_ = s; }

  void set_connected_binary_sensor(binary_sensor::BinarySensor *s) { connected_binary_sensor_ = s; }
  void set_brake_binary_sensor(binary_sensor::BinarySensor *s) { brake_binary_sensor_ = s; }
  void set_pas_limit_binary_sensor(binary_sensor::BinarySensor *s) { pas_limit_binary_sensor_ = s; }

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
  // Frame CRC is a plain XOR, so a corrupted frame can still validate.
  // Sample outside these bounds is dropped, last published value stays.
  static constexpr float MAX_TOTAL_KM = 200000.0f;
  static constexpr float MAX_TRIP_KM = 1000.0f;
  static constexpr float MAX_SPEED_KMH = 100.0f;
  static constexpr uint8_t MAX_SOC_PCT = 100;
  static constexpr int16_t MIN_MOTOR_TEMP_C = -40;
  static constexpr int16_t MAX_MOTOR_TEMP_C = 125;
  // Rate limit for rejected-frame logs; a fragmented stream rejects every frame.
  static constexpr uint32_t BAD_NOTIFY_LOG_INTERVAL_MS = 5000;
  // Ambiguous speed limit is a resting state, not an event, and SPEEDLIM is
  // polled once per burst. Must exceed update_interval_off or it never throttles.
  static constexpr uint32_t AMBIGUOUS_LIMIT_LOG_INTERVAL_MS = 60000;
  static constexpr size_t BAD_NOTIFY_DUMP_LEN = 8;

  [[nodiscard]] bool send_raw_write(FrameType type, uint8_t addr,
                                    const std::vector<uint8_t> &payload);
  void send_handshake_();
  [[nodiscard]] bool send_poll_(size_t idx, bool warn_on_fail);
  void send_burst_poll_();
  bool send_frame_(const uint8_t *frame, size_t len, const char *name,
                   bool warn_on_fail = true);
  void publish_connected_(bool state);
  void mark_activity_(const char *reason);

  void manage_lifecycle_();
  void enqueue_pending_write_(std::function<void()> fn);
  void dispatch_pending_writes_();
  void ensure_enabled_for_write_();

  void parse_battery_(const uint8_t *payload, size_t len);
  void parse_ctrl_(const uint8_t *payload, size_t len);
  void parse_motor_(const uint8_t *payload, size_t len);
  void parse_energy_(const uint8_t *payload, size_t len);
  void parse_stats_(const uint8_t *payload, size_t len);
  void parse_meter_(const uint8_t *payload, size_t len);
  void parse_speed_limit_(const uint8_t *payload, size_t len);
  void parse_boost_(const uint8_t *payload, size_t len);
  void parse_display_(const uint8_t *payload, size_t len);
  [[nodiscard]] bool defer_flag_write_(bool cache_valid, const char *name,
                                       std::function<void()> retry);
  void write_masked_bits_(uint8_t addr, uint8_t mask, uint8_t bits, uint8_t *cache,
                          const char *name);
  void write_flag_bit_(uint8_t addr, uint8_t mask, bool set, uint8_t *cache,
                       const char *name);
  // Raw 1-byte value write (no bit-masking) for number entities. False when the
  // frame did not go out, so the caller keeps its cache and entity unchanged.
  [[nodiscard]] bool write_value_byte_(FrameType type, uint8_t addr, uint8_t value,
                                       const char *name);
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

  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_current_voltage_sensor_{nullptr};
  sensor::Sensor *battery_current_sensor_{nullptr};
  sensor::Sensor *battery_capacity_sensor_{nullptr};
  sensor::Sensor *battery_manufacturer_sensor_{nullptr};
  sensor::Sensor *battery_hw_version_sensor_{nullptr};
  sensor::Sensor *battery_sw_version_sensor_{nullptr};

  sensor::Sensor *ctrl_upper_voltage_sensor_{nullptr};
  sensor::Sensor *ctrl_lower_voltage_sensor_{nullptr};
  sensor::Sensor *ctrl_current_sensor_{nullptr};
  sensor::Sensor *ctrl_temperature_sensor_{nullptr};
  sensor::Sensor *ctrl_hw_version_sensor_{nullptr};
  sensor::Sensor *ctrl_sw_version_sensor_{nullptr};
  sensor::Sensor *ctrl_version_sensor_{nullptr};
  sensor::Sensor *ctrl_manufacturer_sensor_{nullptr};

  sensor::Sensor *motor_version_sensor_{nullptr};
  sensor::Sensor *motor_magnetic_sensor_{nullptr};
  sensor::Sensor *motor_wire_count_sensor_{nullptr};
  sensor::Sensor *motor_steel_count_sensor_{nullptr};
  sensor::Sensor *motor_reduction_ratio_sensor_{nullptr};
  sensor::Sensor *motor_wheel_diameter_sensor_{nullptr};
  sensor::Sensor *motor_temperature_sensor_{nullptr};
  sensor::Sensor *motor_capacity_sensor_{nullptr};

  sensor::Sensor *crank_torque_sensor_{nullptr};
  sensor::Sensor *crank_rpm_sensor_{nullptr};
  sensor::Sensor *this_take_energy_sensor_{nullptr};
  sensor::Sensor *total_take_energy_sensor_{nullptr};
  sensor::Sensor *startup_time_sensor_{nullptr};

  sensor::Sensor *bicycle_speed_sensor_{nullptr};
  sensor::Sensor *current_kilometers_sensor_{nullptr};
  sensor::Sensor *total_kilometers_sensor_{nullptr};
  sensor::Sensor *battery_soc_sensor_{nullptr};
  sensor::Sensor *bicycle_gear_start_sensor_{nullptr};

  sensor::Sensor *meter_hw_version_sensor_{nullptr};
  sensor::Sensor *meter_sw_version_sensor_{nullptr};
  sensor::Sensor *meter_mode_data_sensor_{nullptr};

  binary_sensor::BinarySensor *connected_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *brake_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *pas_limit_binary_sensor_{nullptr};

  FiidoMotorSwitch *motor_switch_{nullptr};
  FiidoLightSwitch *light_switch_{nullptr};
  FiidoAutoshutdownSwitch *autoshutdown_switch_{nullptr};

  FiidoGearSelect *gear_select_{nullptr};
  FiidoModeSelect *mode_select_{nullptr};
  FiidoSpeedLimitSelect *speed_limit_select_{nullptr};
  FiidoSpeedUnitSelect *speed_unit_select_{nullptr};
  FiidoSpeakerSwitch *speaker_switch_{nullptr};
  FiidoKeySoundSwitch *key_sound_switch_{nullptr};
  FiidoThrottleSwitch *throttle_switch_{nullptr};
  FiidoSlowModeSwitch *slow_mode_switch_{nullptr};
  FiidoBleSwitch *ble_switch_{nullptr};
  FiidoCruiseSwitch *cruise_switch_{nullptr};
  FiidoStartModeSwitch *start_mode_switch_{nullptr};
  FiidoInsensitivitySwitch *insensitivity_switch_{nullptr};
  FiidoShowTotalKmSwitch *show_total_km_switch_{nullptr};
  FiidoAutoScreenOffSwitch *auto_screen_off_switch_{nullptr};
  FiidoRingSwitch *ring_switch_{nullptr};
  FiidoDoubleSpeedSwitch *double_speed_switch_{nullptr};
  FiidoBikeGuardSwitch *bike_guard_switch_{nullptr};

  FiidoBrightnessNumber *brightness_number_{nullptr};
  FiidoBoostNumber *boost_number_{nullptr};
  FiidoGuardTimeNumber *guard_time_number_{nullptr};
  FiidoPairWatchButton *pair_watch_button_{nullptr};

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
  std::vector<std::function<void()>> pending_writes_;

  bool poll_enabled_battery_{false};
  bool poll_enabled_ctrl_{false};
  bool poll_enabled_motor_{false};
  bool poll_enabled_energy_{false};
  bool poll_enabled_meter_{false};
  bool poll_enabled_boost_{false};
  bool poll_enabled_display_{false};
};

}  // namespace fiido_bms
}  // namespace esphome

#endif  // USE_ESP32
