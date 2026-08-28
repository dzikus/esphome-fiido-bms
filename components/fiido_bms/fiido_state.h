#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace esphome::fiido_bms {

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

[[nodiscard]] bool should_retry_send(uint8_t retry_count, uint8_t max_retries, bool send_ok);

// At capacity the oldest entry is dropped.
class PendingWrites {
 public:
  explicit PendingWrites(size_t capacity) : capacity_(capacity) {}

  // false when the oldest was dropped.
  bool push(std::function<void()> fn);
  [[nodiscard]] size_t size() const { return queue_.size(); }
  [[nodiscard]] bool empty() const { return queue_.empty(); }

  [[nodiscard]] std::vector<std::function<void()>> drain();
  void clear() { this->queue_.clear(); }

 private:
  size_t capacity_;
  std::vector<std::function<void()>> queue_;
};

}  // namespace esphome::fiido_bms
