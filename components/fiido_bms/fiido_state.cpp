#include "fiido_state.h"

namespace esphome::fiido_bms {

LifecycleAction decide_lifecycle(const LifecycleInput &in) {
  if (in.enabled && in.connected) {
    if (in.motor_off_since_ms != 0 && (in.now - in.motor_off_since_ms) >= in.idle_disconnect_ms && !in.pending_writes)
      return LifecycleAction::IDLE_DISCONNECT;
    return LifecycleAction::NONE;
  }
  if (in.enabled) {
    if (in.probe_started_ms != 0 && (in.now - in.probe_started_ms) >= in.probe_window_ms && !in.pending_writes &&
        (in.last_dispatch_ms == 0 || (in.now - in.last_dispatch_ms) >= in.write_verify_window_ms))
      return LifecycleAction::PROBE_TIMEOUT;
    return LifecycleAction::NONE;
  }
  if (in.disconnected_since_ms != 0 && (in.now - in.disconnected_since_ms) >= in.periodic_probe_ms)
    return LifecycleAction::START_PROBE;
  return LifecycleAction::NONE;
}

const char *resolve_speed_limit_option(uint8_t value, bool limit_on) {
  if (value == 100)
    return "No limit";
  if (limit_on && value == 6)
    return "6 km/h";
  if (limit_on && value == 25)
    return "25 km/h";
  return nullptr;
}

bool should_log_now(uint32_t now, uint32_t last_log_ms, uint32_t interval_ms) {
  return last_log_ms == 0 || (now - last_log_ms) >= interval_ms;
}

bool should_auto_shutdown(uint32_t now, uint32_t last_activity_ms, uint32_t idle_ms, bool enabled, bool motor_on) {
  return enabled && motor_on && (now - last_activity_ms) >= idle_ms;
}

}  // namespace esphome::fiido_bms
