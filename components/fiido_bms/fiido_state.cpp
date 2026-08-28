#include "fiido_state.h"

namespace esphome::fiido_bms {

StatsSamples stats_samples(const StatsView &view) {
  StatsSamples out{};
  auto add = [&out](StatsChannel channel, float value) { out.items[out.size++] = {channel, value}; };
  if (!view.valid)
    return out;
  if (view.total_km_ok)
    add(StatsChannel::TOTAL_KM, view.total_km);
  if (view.trip_km_ok)
    add(StatsChannel::TRIP_KM, view.trip_km);
  if (view.speed_ok)
    add(StatsChannel::SPEED, view.speed_kmh);
  if (view.soc_ok)
    add(StatsChannel::SOC, static_cast<float>(view.soc_pct));
  add(StatsChannel::GEAR_START, static_cast<float>(view.gear_start));
  return out;
}

ActivitySignals detect_activity(const RideState &prev, const RideState &now, uint16_t speed_raw) {
  return {
      .motor_turned_on = now.motor_on && !prev.motor_on,
      .gear_changed = now.gear != prev.gear,
      .moving = speed_raw > 0,
      .light_changed = now.light_on != prev.light_on,
  };
}

uint32_t auto_startup_delay(int hub_index, int total_hubs, uint32_t interval_on_ms) {
  if (total_hubs <= 1 || hub_index <= 0)
    return 0;
  // 64-bit: index * interval overflows uint32 well inside the allowed interval range.
  const uint64_t product = static_cast<uint64_t>(hub_index) * interval_on_ms;
  return static_cast<uint32_t>(product / static_cast<uint64_t>(total_hubs));
}

uint32_t track_motor_off(uint32_t since_ms, bool motor_on, uint32_t now) {
  if (motor_on)
    return 0;
  return since_ms != 0 ? since_ms : now;
}

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

std::optional<SpeedLimitOption> parse_speed_limit_option(std::string_view option) {
  if (option == "6 km/h")
    return SpeedLimitOption::SIX_KMH;
  if (option == "25 km/h")
    return SpeedLimitOption::TWENTY_FIVE_KMH;
  if (option == "No limit")
    return SpeedLimitOption::NO_LIMIT;
  return std::nullopt;
}

const char *speed_limit_option_name(SpeedLimitOption option) {
  switch (option) {
    case SpeedLimitOption::SIX_KMH:
      return "6 km/h";
    case SpeedLimitOption::TWENTY_FIVE_KMH:
      return "25 km/h";
    case SpeedLimitOption::NO_LIMIT:
      break;
  }
  return "No limit";
}

SpeedLimitPlan plan_speed_limit(SpeedLimitOption option, uint8_t cache_2c) {
  SpeedLimitPlan plan{};
  switch (option) {
    case SpeedLimitOption::SIX_KMH:
      plan.value = 6;
      plan.limit_on = true;
      break;
    case SpeedLimitOption::TWENTY_FIVE_KMH:
      plan.value = 25;
      plan.limit_on = true;
      break;
    case SpeedLimitOption::NO_LIMIT:
      plan.value = 100;
      plan.limit_on = false;
      break;
  }
  const bool pas_on = (cache_2c & 0x80) != 0;
  plan.needs_pas_write = pas_on != plan.limit_on;
  plan.pas_byte = plan.limit_on ? static_cast<uint8_t>(cache_2c | 0x80) : static_cast<uint8_t>(cache_2c & ~0x80);
  plan.delay_phase2 = !plan.limit_on;
  return plan;
}

uint8_t apply_speed_limit_bit(uint8_t cache_27, bool limit_on) {
  return limit_on ? static_cast<uint8_t>(cache_27 | 0x20) : static_cast<uint8_t>(cache_27 & ~0x20);
}

ProbeOutcome decide_probe_outcome(bool motor_on, uint32_t now, uint32_t last_dispatch_ms,
                                  uint32_t write_verify_window_ms) {
  if (motor_on)
    return ProbeOutcome::STAY_BIKE_ON;
  if (last_dispatch_ms != 0 && (now - last_dispatch_ms) < write_verify_window_ms)
    return ProbeOutcome::STAY_VERIFY_WINDOW;
  return ProbeOutcome::DROP_LINK;
}

uint8_t resolve_gear_count(uint8_t max_gear, bool pinned, uint8_t current_count) {
  if (pinned || max_gear == 0 || max_gear == current_count)
    return 0;
  return max_gear;
}

const char *resolve_mode_option(uint8_t gear_count) {
  return gear_count == 3 ? "3" : "5";
}

bool should_clear_light_bit(bool ble_enabled, bool prev_motor_on, bool motor_on, uint8_t cache_27) {
  return ble_enabled && prev_motor_on && !motor_on && (cache_27 & 0x08) != 0;
}

bool should_enforce_gear_mode_3(bool enabled, uint8_t max_gear, bool ble_enabled, bool controller_on, uint32_t now,
                                uint32_t last_write_ms, uint32_t cooldown_ms) {
  if (!enabled || max_gear != 5 || !ble_enabled || !controller_on)
    return false;
  return should_log_now(now, last_write_ms, cooldown_ms);
}

bool should_retry_send(uint8_t retry_count, uint8_t max_retries, bool send_ok) {
  return !send_ok && retry_count < max_retries;
}

bool PendingWrites::push(PendingWrite fn) {
  const bool dropped = this->size_ >= PENDING_WRITE_SLOTS;
  if (dropped) {
    for (size_t i = 1; i < PENDING_WRITE_SLOTS; i++)
      this->queue_[i - 1] = this->queue_[i];
    this->size_ = PENDING_WRITE_SLOTS - 1;
  }
  this->queue_[this->size_++] = fn;
  return !dropped;
}

std::array<PendingWrite, PENDING_WRITE_SLOTS> PendingWrites::drain(size_t &count) {
  std::array<PendingWrite, PENDING_WRITE_SLOTS> taken = this->queue_;
  count = this->size_;
  this->size_ = 0;
  return taken;
}

}  // namespace esphome::fiido_bms
