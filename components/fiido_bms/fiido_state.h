#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "fiido_protocol.h"

namespace esphome::fiido_bms {

enum class StatsChannel : uint8_t {
  TOTAL_KM = 0,
  TRIP_KM,
  SPEED,
  SOC,
  GEAR_START,
};

struct StatsSample {
  StatsChannel channel;
  float value;
};

// Only readings that passed their bounds.
struct StatsSamples {
  std::array<StatsSample, 5> items;
  size_t size;

  [[nodiscard]] const StatsSample *begin() const { return items.data(); }
  [[nodiscard]] const StatsSample *end() const { return items.data() + size; }
};

[[nodiscard]] StatsSamples stats_samples(const StatsView &view);

enum class LifecycleAction : uint8_t {
  NONE = 0,
  IDLE_DISCONNECT,
  PROBE_TIMEOUT,
  START_PROBE,
};

struct LifecycleInput {
  uint32_t now;
  bool enabled;
  bool connected;
  uint32_t motor_off_since_ms;
  uint32_t disconnected_since_ms;
  uint32_t probe_started_ms;
  uint32_t last_dispatch_ms;
  bool pending_writes;
  uint32_t idle_disconnect_ms;
  uint32_t probe_window_ms;
  uint32_t periodic_probe_ms;
  uint32_t write_verify_window_ms;
};

[[nodiscard]] LifecycleAction decide_lifecycle(const LifecycleInput &in);

// nullptr = keep the previous option.
[[nodiscard]] const char *resolve_speed_limit_option(uint8_t value, bool limit_on);

[[nodiscard]] bool should_log_now(uint32_t now, uint32_t last_log_ms, uint32_t interval_ms);

[[nodiscard]] bool should_auto_shutdown(uint32_t now, uint32_t last_activity_ms, uint32_t idle_ms, bool enabled,
                                        bool motor_on);

enum class WriteGate : uint8_t {
  SEND = 0,
  REJECT_BLE_DISABLED,
  QUEUE_DISCONNECTED,
  DEFER_COLD_CACHE,
  REJECT_CONTROLLER_OFF,
};

struct WriteGateInput {
  bool ble_enabled;
  bool connected;
  bool cache_valid;
  bool needs_controller;
  bool controller_on;
};

[[nodiscard]] WriteGate gate_write(const WriteGateInput &in);

// Upper nibble = gear count, lower = bike config. Cache unchanged unless mode is 3 or 5.
[[nodiscard]] uint8_t encode_gear_mode(uint8_t mode, uint8_t cache_25);

[[nodiscard]] uint8_t clamp_gear(uint8_t gear, uint8_t max_gear);

enum class SpeedLimitOption : uint8_t {
  SIX_KMH = 0,
  TWENTY_FIVE_KMH,
  NO_LIMIT,
};

[[nodiscard]] std::optional<SpeedLimitOption> parse_speed_limit_option(std::string_view option);

[[nodiscard]] const char *speed_limit_option_name(SpeedLimitOption option);

struct SpeedLimitPlan {
  uint8_t value;         // ADDR 0x3C
  bool limit_on;         // ADDR 0x27 bit 5
  bool needs_pas_write;  // ADDR 0x2C bit 7 differs from the target
  uint8_t pas_byte;      // ADDR 0x2C after the bit is applied
  bool delay_phase2;     // clearing the PAS bit makes the BMS force 0x3C=25
};

[[nodiscard]] SpeedLimitPlan plan_speed_limit(SpeedLimitOption option, uint8_t cache_2c);

[[nodiscard]] uint8_t apply_speed_limit_bit(uint8_t cache_27, bool limit_on);

enum class ProbeOutcome : uint8_t {
  STAY_BIKE_ON = 0,
  STAY_VERIFY_WINDOW,
  DROP_LINK,
};

[[nodiscard]] ProbeOutcome decide_probe_outcome(bool motor_on, uint32_t now, uint32_t last_dispatch_ms,
                                                uint32_t write_verify_window_ms);

// 0 = keep the count the select already has.
[[nodiscard]] uint8_t resolve_gear_count(uint8_t max_gear, bool pinned, uint8_t current_count);

[[nodiscard]] const char *resolve_mode_option(uint8_t gear_count);

[[nodiscard]] bool should_clear_light_bit(bool ble_enabled, bool prev_motor_on, bool motor_on, uint8_t cache_27);

[[nodiscard]] bool should_enforce_gear_mode_3(bool enabled, uint8_t max_gear, bool ble_enabled, bool controller_on,
                                              uint32_t now, uint32_t last_write_ms, uint32_t cooldown_ms);

[[nodiscard]] bool should_retry_send(uint8_t retry_count, uint8_t max_retries, bool send_ok);

// At capacity the oldest entry is dropped.
class PendingWrites {
 public:
  explicit PendingWrites(size_t capacity) : capacity_(capacity) {}

  // false when the oldest was dropped.
  [[nodiscard]] bool push(std::function<void()> fn);
  [[nodiscard]] size_t size() const { return queue_.size(); }
  [[nodiscard]] bool empty() const { return queue_.empty(); }

  [[nodiscard]] std::vector<std::function<void()>> drain();
  void clear() { this->queue_.clear(); }

 private:
  size_t capacity_;
  std::vector<std::function<void()>> queue_;
};

}  // namespace esphome::fiido_bms
