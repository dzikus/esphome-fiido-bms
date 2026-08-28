#pragma once

#include <cstddef>
#include <cstdint>

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

}  // namespace esphome::fiido_bms
