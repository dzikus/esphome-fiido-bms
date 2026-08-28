#pragma once

#include <functional>
#include <optional>
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
  CRUISE,
  START_MODE,
  INSENSITIVITY,
  SHOW_TOTAL_KM,
  AUTO_SCREEN_OFF,
  RING,
  DOUBLE_SPEED,
  BIKE_GUARD,
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

// Row order in BYTE_CONTROLS.
enum class ByteId : size_t {
  BRIGHTNESS = 0,
  BOOST,
  GUARD_TIME,
  COUNT,
};

// Whole-byte writes, no mask.
struct ByteControl {
  FrameType type;
  Addr addr;
  size_t slot;
  const char *name;
  number::Number *FiidoBMSHub::*entity;
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
  static constexpr std::array<FlagControl, 12> FLAG_CONTROLS{{
      {Addr::FLAGS_38, cache_slot(Addr::FLAGS_38), 0x0C, 0x00, 0x04, "SPEAKER", &FiidoBMSHub::speaker_switch_,
       &FlagView::speaker_audible},
      {Addr::FLAGS_2C, cache_slot(Addr::FLAGS_2C), 0x10, 0x00, 0x10, "KEY_SOUND", &FiidoBMSHub::key_sound_switch_,
       &FlagView::key_sound_on},
      {Addr::FLAGS_2B, cache_slot(Addr::FLAGS_2B), 0x02, 0x00, 0x02, "THROTTLE", &FiidoBMSHub::throttle_switch_,
       &FlagView::throttle_on},
      {Addr::FLAGS_2C, cache_slot(Addr::FLAGS_2C), 0x40, 0x40, 0x00, "SLOW_MODE", &FiidoBMSHub::slow_mode_switch_,
       &FlagView::slow_mode_on},
      {Addr::FLAGS_27, cache_slot(Addr::FLAGS_27), 0x40, 0x40, 0x00, "CRUISE", &FiidoBMSHub::cruise_switch_,
       &FlagView::cruise_on},
      {Addr::FLAGS_27, cache_slot(Addr::FLAGS_27), 0x02, 0x02, 0x00, "START_MODE", &FiidoBMSHub::start_mode_switch_,
       &FlagView::start_mode_on},
      {Addr::FLAGS_27, cache_slot(Addr::FLAGS_27), 0x01, 0x01, 0x00, "INSENS", &FiidoBMSHub::insensitivity_switch_,
       &FlagView::insensitivity_on},
      {Addr::FLAGS_28, cache_slot(Addr::FLAGS_28), 0x40, 0x40, 0x00, "SHOW_TOTAL_KM",
       &FiidoBMSHub::show_total_km_switch_, &FlagView::show_total_km_on},
      {Addr::FLAGS_39, cache_slot(Addr::FLAGS_39), 0x08, 0x08, 0x00, "AUTO_SCREEN_OFF",
       &FiidoBMSHub::auto_screen_off_switch_, &FlagView::auto_screen_off_on},
      {Addr::FLAGS_39, cache_slot(Addr::FLAGS_39), 0x02, 0x02, 0x00, "RING", &FiidoBMSHub::ring_switch_,
       &FlagView::ring_on},
      {Addr::FLAGS_2B, cache_slot(Addr::FLAGS_2B), 0x20, 0x20, 0x00, "DOUBLE_SPEED", &FiidoBMSHub::double_speed_switch_,
       &FlagView::double_speed_on},
      {Addr::FLAGS_2B, cache_slot(Addr::FLAGS_2B), 0x40, 0x40, 0x00, "BIKE_GUARD", &FiidoBMSHub::bike_guard_switch_,
       &FlagView::bike_guard_on},
  }};
  static_assert(FLAG_CONTROLS.size() == static_cast<size_t>(FlagId::COUNT), "FlagId and FLAG_CONTROLS disagree");
  void set_flag_(FlagId id, bool on);

  static constexpr std::array<ByteControl, 3> BYTE_CONTROLS{{
      {FrameType::WRITE_J0, Addr::DISPLAY, cache_slot(Addr::DISPLAY), "BRIGHTNESS", &FiidoBMSHub::brightness_number_},
      {FrameType::WRITE_L0, Addr::PAS_BOOST, cache_slot(Addr::PAS_BOOST), "BOOST", &FiidoBMSHub::boost_number_},
      {FrameType::WRITE_J0, Addr::GUARD_TIME, cache_slot(Addr::GUARD_TIME), "GUARD_TIME",
       &FiidoBMSHub::guard_time_number_},
  }};
  static_assert(BYTE_CONTROLS.size() == static_cast<size_t>(ByteId::COUNT), "ByteId and BYTE_CONTROLS disagree");
  void set_byte_(ByteId id, float value);

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

  WriteError send_raw_write_(FrameType type, Addr addr, std::span<const uint8_t> payload);
  void send_handshake_();
  WriteError send_poll_(size_t idx, bool warn_on_fail);
  void send_burst_poll_();
  WriteError send_frame_(std::span<const uint8_t> frame, const char *name, bool warn_on_fail = true);
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
  void write_masked_bits_(uint8_t mask, uint8_t bits, const char *name) {
    this->write_masked_bits_(A, cache_slot(A), mask, bits, name);
  }
  template <Addr A>
  void write_flag_bit_(uint8_t mask, bool set, const char *name) {
    this->write_masked_bits_<A>(mask, set ? mask : 0x00, name);
  }
  void write_masked_bits_(Addr addr, size_t slot, uint8_t mask, uint8_t bits, const char *name);
  // Raw 1-byte value write (no bit-masking) for number entities. False when the
  // frame did not go out, so the caller keeps its cache and entity unchanged.
  [[nodiscard]] bool write_value_byte_(FrameType type, Addr addr, uint8_t value, const char *name);

  // Everything the session has to forget when the link drops.
  void reset_session_state_();

  uint32_t startup_delay_ms_{0};
  uint32_t connect_time_ms_{0};
  size_t burst_idx_{0};
  size_t burst_remaining_{0};
  uint8_t burst_retry_{0};
  bool handshake_sent_{false};
  FiidoLink link_;

  uint32_t last_bad_notify_log_ms_{0};
  uint32_t bad_notify_count_{0};
  uint32_t last_unknown_addr_log_ms_{0};
  uint32_t unknown_addr_count_{0};
  uint32_t last_ambiguous_limit_log_ms_{0};
  uint32_t ambiguous_limit_count_{0};

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
  uint8_t prev_gear_{0xFF};
  bool prev_light_on_{false};
  bool prev_motor_on_{false};
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
