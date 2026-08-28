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

WriteGate gate_write(const WriteGateInput &in) {
  if (!in.ble_enabled)
    return WriteGate::REJECT_BLE_DISABLED;
  if (!in.connected)
    return WriteGate::QUEUE_DISCONNECTED;
  if (!in.cache_valid)
    return WriteGate::DEFER_COLD_CACHE;
  if (in.needs_controller && !in.controller_on)
    return WriteGate::REJECT_CONTROLLER_OFF;
  return WriteGate::SEND;
}

uint8_t encode_gear_mode(uint8_t mode, uint8_t cache_25) {
  if (mode != 3 && mode != 5)
    return cache_25;
  return static_cast<uint8_t>((mode << 4) | (cache_25 & 0x0F));
}

uint8_t clamp_gear(uint8_t gear, uint8_t max_gear) {
  return gear > max_gear ? max_gear : gear;
}

bool PendingWrites::push(std::function<void()> fn) {
  const bool dropped = this->queue_.size() >= this->capacity_;
  if (dropped)
    this->queue_.erase(this->queue_.begin());
  this->queue_.push_back(std::move(fn));
  return !dropped;
}

std::vector<std::function<void()>> PendingWrites::drain() {
  std::vector<std::function<void()>> taken;
  taken.swap(this->queue_);
  return taken;
}

}  // namespace esphome::fiido_bms
