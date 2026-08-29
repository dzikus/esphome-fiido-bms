#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

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

// Counts the events swallowed between two logs.
class LogThrottle {
 public:
  // 0 to stay quiet, otherwise the number of events since the last log, this
  // one included.
  [[nodiscard]] uint32_t tick(uint32_t now, uint32_t interval_ms) {
    this->count_++;
    if (!should_log_now(now, this->last_ms_, interval_ms))
      return 0;
    const uint32_t seen = this->count_;
    this->last_ms_ = now;
    this->count_ = 0;
    return seen;
  }

  void reset() {
    this->last_ms_ = 0;
    this->count_ = 0;
  }

 private:
  uint32_t last_ms_{0};
  uint32_t count_{0};
};

struct AutoShutdownInput {
  uint32_t now;
  uint32_t last_activity_ms;
  uint32_t idle_ms;
  bool enabled;
  bool motor_on;
};

[[nodiscard]] bool should_auto_shutdown(const AutoShutdownInput &in);

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
[[nodiscard]] RegValue<Addr::GEAR_RANGE> encode_gear_mode(uint8_t mode, RegValue<Addr::GEAR_RANGE> cache_25);

[[nodiscard]] uint8_t clamp_gear(uint8_t gear, uint8_t max_gear);

enum class SpeedLimitOption : uint8_t {
  SIX_KMH = 0,
  TWENTY_FIVE_KMH,
  NO_LIMIT,
};

[[nodiscard]] std::optional<SpeedLimitOption> parse_speed_limit_option(std::string_view option);

[[nodiscard]] const char *speed_limit_option_name(SpeedLimitOption option);

struct SpeedLimitPlan {
  RegValue<Addr::SPEED_LIMIT> value;
  bool limit_on;         // ADDR 0x27 bit 5
  bool needs_pas_write;  // ADDR 0x2C bit 7 differs from the target
  RegValue<Addr::FLAGS_2C> pas_byte;
  bool delay_phase2;  // clearing the PAS bit makes the BMS force 0x3C=25
};

[[nodiscard]] SpeedLimitPlan plan_speed_limit(SpeedLimitOption option, RegValue<Addr::FLAGS_2C> cache_2c);

[[nodiscard]] RegValue<Addr::FLAGS_27> apply_speed_limit_bit(RegValue<Addr::FLAGS_27> cache_27, bool limit_on);

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

[[nodiscard]] bool should_clear_light_bit(bool ble_enabled, bool prev_motor_on, bool motor_on,
                                          RegValue<Addr::FLAGS_27> cache_27);

struct EnforceGearModeInput {
  bool enabled;
  uint8_t max_gear;
  bool ble_enabled;
  bool controller_on;
  uint32_t now;
  uint32_t last_write_ms;
  uint32_t cooldown_ms;
};

[[nodiscard]] bool should_enforce_gear_mode_3(const EnforceGearModeInput &in);

// What the rider is doing, as far as one STATS frame can tell.
struct RideState {
  uint8_t gear;
  bool motor_on;
  bool light_on;
};

struct ActivitySignals {
  bool motor_turned_on;
  bool gear_changed;
  bool moving;
  bool light_changed;

  [[nodiscard]] bool any() const {
    return this->motor_turned_on || this->gear_changed || this->moving || this->light_changed;
  }
};

[[nodiscard]] ActivitySignals detect_activity(const RideState &prev, const RideState &now, uint16_t speed_raw);

// 0 while the motor is on, otherwise the timestamp the OFF window opened.
[[nodiscard]] uint32_t track_motor_off(uint32_t since_ms, bool motor_on, uint32_t now);

// 0 for a lone hub.
[[nodiscard]] uint32_t auto_startup_delay(int hub_index, int total_hubs, uint32_t interval_on_ms);

// Registers cached for read-modify-write.
inline constexpr std::array<Addr, 11> CACHED_REGISTERS{
    Addr::GEAR_RANGE, Addr::FLAGS_27,    Addr::FLAGS_28,  Addr::FLAGS_2B, Addr::FLAGS_2C,   Addr::FLAGS_38,
    Addr::FLAGS_39,   Addr::SPEED_LIMIT, Addr::PAS_BOOST, Addr::DISPLAY,  Addr::GUARD_TIME,
};

// A repeat would alias two registers onto one slot.
static_assert([] {
  for (size_t i = 0; i < CACHED_REGISTERS.size(); i++) {
    for (size_t j = i + 1; j < CACHED_REGISTERS.size(); j++) {
      if (CACHED_REGISTERS[i] == CACHED_REGISTERS[j])
        return false;
    }
  }
  return true;
}());

[[nodiscard]] consteval size_t cache_slot(Addr addr) {
  for (size_t i = 0; i < CACHED_REGISTERS.size(); i++) {
    if (CACHED_REGISTERS[i] == addr)
      return i;
  }
  return address_not_in_table();
}

// Empty until the poll that fills it lands.
class RegisterCache {
 public:
  template <Addr A>
  [[nodiscard]] bool has() const {
    return this->slots_[cache_slot(A)].has_value();
  }
  // Zero for a cold slot.
  template <Addr A>
  [[nodiscard]] RegValue<A> value_or(RegValue<A> fallback = {}) const {
    return {this->slots_[cache_slot(A)].value_or(fallback.raw)};
  }
  template <Addr A>
  void set(RegValue<A> value) {
    this->slots_[cache_slot(A)] = value.raw;
  }

  [[nodiscard]] std::optional<uint8_t> &at(size_t slot) { return this->slots_[slot]; }
  [[nodiscard]] const std::optional<uint8_t> &at(size_t slot) const { return this->slots_[slot]; }

  // Called on disconnect.
  void clear() { this->slots_.fill(std::nullopt); }

 private:
  std::array<std::optional<uint8_t>, CACHED_REGISTERS.size()> slots_{};
};

// A capture is this plus one small value.
inline constexpr size_t PENDING_WRITE_CAPACITY = 24;

// Nothing runs a destructor on the stored bytes, and a queued write is copied
// by value into the slot array.
template <typename F>
concept QueuedWrite = sizeof(F) <= PENDING_WRITE_CAPACITY && std::is_trivially_copyable_v<F> &&
                      std::is_trivially_destructible_v<F> && std::invocable<F>;

class PendingWrite {
 public:
  static constexpr size_t CAPACITY = PENDING_WRITE_CAPACITY;

  PendingWrite() = default;

  template <QueuedWrite F>
  PendingWrite(F callable)  // NOLINT(google-explicit-constructor)
      : invoke_([](const std::byte *storage) { (*std::launder(reinterpret_cast<const F *>(storage)))(); }) {
    new (this->storage_.data()) F(callable);
  }

  void operator()() const { this->invoke_(this->storage_.data()); }
  [[nodiscard]] explicit operator bool() const { return this->invoke_ != nullptr; }

 private:
  alignas(std::max_align_t) std::array<std::byte, CAPACITY> storage_{};
  void (*invoke_)(const std::byte *){nullptr};
};

inline constexpr size_t PENDING_WRITE_SLOTS = 32;

// At capacity the oldest entry is dropped.
class PendingWrites {
 public:
  // false when the oldest was dropped.
  bool push(PendingWrite fn);
  [[nodiscard]] size_t size() const { return this->size_; }
  [[nodiscard]] bool empty() const { return this->size_ == 0; }

  // Hands the queue over; anything queued while these run waits for the next drain.
  [[nodiscard]] std::array<PendingWrite, PENDING_WRITE_SLOTS> drain(size_t &count);
  void clear() { this->size_ = 0; }

 private:
  std::array<PendingWrite, PENDING_WRITE_SLOTS> queue_{};
  size_t size_{0};
};

}  // namespace esphome::fiido_bms
