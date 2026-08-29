#include "fiido_bms.h"

#include "fiido_bool_switch.h"
#include "fiido_button.h"
#include "fiido_gear_select.h"
#include "fiido_mode_select.h"
#include "fiido_number.h"
#include "fiido_speed_limit_select.h"
#include "fiido_speed_unit_select.h"
#include "fiido_state.h"

#ifdef USE_ESP32

#include <esp_bt_device.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::fiido_bms {

namespace {

void publish_changed(sensor::Sensor *s, float value) {
  if (s == nullptr || (s->has_state() && s->state == value))
    return;
  s->publish_state(value);
}

void publish_changed(binary_sensor::BinarySensor *s, bool value) {
  if (s != nullptr)
    s->publish_state(value);
}

void publish_changed(select::Select *s, const char *option) {
  if (s == nullptr)
    return;
  const auto idx = s->index_of(option);
  if (idx.has_value() && s->has_state()) {
    const auto current = s->active_index();
    if (current.has_value() && *current == *idx)
      return;
  }
  s->publish_state(option);
}

void publish_changed(select::Select *s, size_t index) {
  if (s == nullptr)
    return;
  const auto current = s->active_index();
  if (s->has_state() && current.has_value() && *current == index)
    return;
  s->publish_state(index);
}

#ifdef USE_FIIDO_BMS_DEV
void publish_changed(number::Number *n, float value) {
  if (n == nullptr || (n->has_state() && n->state == value))
    return;
  n->publish_state(value);
}

void revert_number(number::Number *n) {
  if (n != nullptr && n->has_state())
    n->publish_state(n->state);
}
#endif

// publish_changed dedups; the entity still holds the old value.
void revert_select(select::Select *s) {
  if (s == nullptr)
    return;
  const auto shown = s->current_option();
  if (!shown.empty())
    s->publish_state(shown.c_str());
}

}  // namespace

static const char *const TAG = FIIDO_BMS_TAG;

void FiidoBMSHub::setup() {
  if (this->startup_delay_ms_ == 0) {
    this->startup_delay_ms_ = auto_startup_delay(this->hub_index_, this->total_hubs_, this->update_interval_on_ms_);
    if (this->startup_delay_ms_ != 0) {
      ESP_LOGI(TAG, "[%s] AUTO startup_delay=%ums (hub %d of %d, interval_on=%ums)", this->parent_->address_str(),
               (unsigned)this->startup_delay_ms_, this->hub_index_, this->total_hubs_,
               (unsigned)this->update_interval_on_ms_);
    }
  }
  this->desired_interval_ms_ = this->update_interval_off_ms_;
  // Baseline so periodic-probe branch can fire if first BLE connect never lands.
  this->disconnected_since_ms_ = millis();
  this->set_interval("lifecycle", LIFECYCLE_TICK_MS, [this]() { this->manage_lifecycle_(); });
}

void FiidoBMSHub::dump_config() {
  ESP_LOGCONFIG(TAG, "Fiido BMS Hub:");
  ESP_LOGCONFIG(TAG, "  MAC: %s", this->parent_->address_str());
  ESP_LOGCONFIG(TAG, "  Startup delay: %u ms (hub %d of %d)", (unsigned)this->startup_delay_ms_, this->hub_index_,
                this->total_hubs_);
  ESP_LOGCONFIG(TAG, "  Update interval ON/OFF: %u / %u ms", (unsigned)this->update_interval_on_ms_,
                (unsigned)this->update_interval_off_ms_);
  ESP_LOGCONFIG(TAG, "  Polls: %u (burst %ums apart)", (unsigned)POLL_TABLE_SIZE, (unsigned)BURST_INTERVAL_MS);
  ESP_LOGCONFIG(TAG, "  Idle auto-shutdown: %u min", (unsigned)(IDLE_SHUTDOWN_MS / 60000));
  // The LOG_ macros print nothing for an entity the yaml left out.
  this->log_sensors_(BATTERY_FIELDS);
  this->log_sensors_(CTRL_FIELDS);
  this->log_sensors_(MOTOR_FIELDS);
  this->log_sensors_(ENERGY_FIELDS);
  this->log_sensors_(METER_FIELDS);
  for (const auto &[member, label] : STATS_SENSORS)
    LOG_SENSOR("  ", label, this->*member);
  LOG_BINARY_SENSOR("  ", "Connected", this->connected_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Brake", this->brake_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Pas Limit", this->pas_limit_binary_sensor_);
  LOG_SWITCH("  ", "Motor", this->motor_switch_);
  LOG_SWITCH("  ", "Light", this->light_switch_);
  LOG_SWITCH("  ", "Autoshutdown", this->autoshutdown_switch_);
  LOG_SWITCH("  ", "Ble", this->ble_switch_);
  for (const FlagControl &control : FLAG_CONTROLS)
    LOG_SWITCH("  ", control.name, this->*(control.entity));
#ifdef USE_FIIDO_BMS_DEV
  for (const ByteControl &control : BYTE_CONTROLS)
    LOG_NUMBER("  ", control.name, this->*(control.entity));
#endif
  LOG_SELECT("  ", "Mode", this->mode_select_);
  LOG_SELECT("  ", "Speed Limit", this->speed_limit_select_);
  LOG_SELECT("  ", "Speed Unit", this->speed_unit_select_);
  LOG_SELECT("  ", "Gear", this->gear_select_);
#ifdef USE_FIIDO_BMS_DEV
  LOG_BUTTON("  ", "Pair Watch", this->pair_watch_button_);
#endif
}

void FiidoBMSHub::log_sensors_(std::span<const SensorField> fields) const {
  for (const SensorField &field : fields)
    LOG_SENSOR("  ", field.label, this->*(field.entity));
}

void FiidoBMSHub::schedule_write_verify_() {
  this->force_poll_stats_ = true;
  this->set_timeout("force_stats_tick", FORCE_STATS_DELAY_MS, [this]() { this->update(); });
}

void FiidoBMSHub::settle_probe_(bool motor_on) {
  if (this->probe_started_ms_ == 0)
    return;
  switch (decide_probe_outcome(motor_on, millis(), this->last_dispatch_ms_, WRITE_VERIFY_WINDOW_MS)) {
    case ProbeOutcome::STAY_BIKE_ON:
      ESP_LOGI(TAG, "[%s] LIFECYCLE: probe success -> bike ON, staying connected", this->parent_->address_str());
      this->probe_started_ms_ = 0;
      break;
    case ProbeOutcome::STAY_VERIFY_WINDOW:
      ESP_LOGD(TAG, "[%s] LIFECYCLE: probe -> verify window open (%ums left)", this->parent_->address_str(),
               (unsigned)(WRITE_VERIFY_WINDOW_MS - (millis() - this->last_dispatch_ms_)));
      this->probe_started_ms_ = millis();
      break;
    case ProbeOutcome::DROP_LINK:
      ESP_LOGI(TAG, "[%s] LIFECYCLE: probe -> bike still OFF, disabling ble_client", this->parent_->address_str());
      this->probe_started_ms_ = 0;
      this->parent_->set_enabled(false);
      this->disconnected_since_ms_ = millis();
      this->motor_off_since_ms_ = 0;
      break;
  }
}

void FiidoBMSHub::publish_flag_entities_(const FlagView &f) {
  publish_changed(this->pas_limit_binary_sensor_, f.pas_limit_on);
  // A press of the physical button on the bike shows up here.
  if (this->motor_switch_ != nullptr)
    this->motor_switch_->publish_state(f.motor_on);
  if (this->light_switch_ != nullptr)
    this->light_switch_->publish_state(f.light_on);
  // STATS arrives more often than the 0x3C poll. An ambiguous pair is the BMS
  // reloading from flash; the previous option stands.
  if (this->speed_limit_select_ != nullptr && this->registers_.has<Addr::SPEED_LIMIT>()) {
    const char *opt = resolve_speed_limit_option(this->registers_.value_or<Addr::SPEED_LIMIT>().raw, f.speed_limit_on);
    if (opt != nullptr)
      publish_changed(this->speed_limit_select_, opt);
  }
  if (this->speed_unit_select_ != nullptr)
    publish_changed(this->speed_unit_select_, f.speed_unit_mph ? "mph" : "km/h");
  for (const FlagControl &control : FLAG_CONTROLS) {
    switch_::Switch *entity = this->*(control.entity);
    if (entity != nullptr)
      entity->publish_state(f.*(control.state));
  }
}

void FiidoBMSHub::reset_session_state_() {
  this->link_.reset();
  this->handshake_sent_ = false;
  this->cancel_timeout("burst");
  this->burst_remaining_ = 0;
  this->burst_idx_ = 0;
  this->burst_retry_ = 0;
  // The bike can change while the link is down.
  this->registers_.clear();
  this->prev_ride_ = {.gear = 0xFF, .motor_on = false, .light_on = false};
  this->last_dispatch_ms_ = 0;
  this->bad_notify_log_.reset();
  this->unknown_addr_log_.reset();
  this->ambiguous_limit_log_.reset();
}

void FiidoBMSHub::publish_connected_(bool state) {
  publish_changed(this->connected_binary_sensor_, state);
}

void FiidoBMSHub::mark_activity_(const char *reason) {
  this->last_activity_ms_ = millis();
  ESP_LOGD(TAG, "[%s] activity: %s", this->parent_->address_str(), reason);
}

void FiidoBMSHub::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                      esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      this->on_open_();
      break;
    case ESP_GATTC_CONGEST_EVT:
      if (param->congest.conn_id == this->parent_->get_conn_id())
        this->on_congestion_(param->congest.congested);
      break;
    case ESP_GATTC_DISCONNECT_EVT:
      this->on_disconnect_();
      break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
      this->on_services_resolved_();
      break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      this->on_notify_registered_();
      break;
    case ESP_GATTC_NOTIFY_EVT:
      if (this->link_.owns_notify(param->notify.handle))
        this->handle_notify_(std::span<const uint8_t>(param->notify.value, size_t{param->notify.value_len}));
      break;
    default:
      break;
  }
}

void FiidoBMSHub::on_open_() {
  ESP_LOGI(TAG, "[%s] Connection opened", this->parent_->address_str());
  this->connect_time_ms_ = millis();
  this->handshake_sent_ = false;
  this->link_.set_congested(false);
}

void FiidoBMSHub::on_congestion_(bool congested) {
  this->link_.set_congested(congested);
  ESP_LOGD(TAG, "[%s] l2cap %scongested", this->parent_->address_str(), congested ? "" : "un");
  if (!congested && this->burst_remaining_ > 0) {
    this->cancel_timeout("burst");
    this->send_burst_poll_();
  }
}

void FiidoBMSHub::on_disconnect_() {
  ESP_LOGW(TAG, "[%s] Disconnected", this->parent_->address_str());
  this->node_state = espbt::ClientState::IDLE;
  this->link_.unsubscribe(this->parent_);
  this->reset_session_state_();
  this->publish_connected_(false);
}

void FiidoBMSHub::on_services_resolved_() {
  if (!this->link_.resolve(this->parent_)) {
    ESP_LOGE(TAG, "[%s] FFE1/FFE2 not found, not a Fiido BMS?", this->parent_->address_str());
    return;
  }
  ESP_LOGD(TAG, "[%s] FFE2 (write)=0x%02X  FFE1 (notify)=0x%02X", this->parent_->address_str(),
           this->link_.write_handle(), this->link_.notify_handle());
  this->link_.subscribe(this->parent_);
}

void FiidoBMSHub::on_notify_registered_() {
  this->node_state = espbt::ClientState::ESTABLISHED;
  this->publish_connected_(true);
  ESP_LOGI(TAG, "[%s] READY (FFE1 notify enabled)", this->parent_->address_str());
  // A stale desired_interval_ms_ (e.g. the 15 s OFF window) would stall the first
  // poll after a HA-triggered reconnect that carries pending writes.
  this->burst_started_ = false;
  this->desired_interval_ms_ = this->update_interval_on_ms_;
}

void FiidoBMSHub::handle_notify_(std::span<const uint8_t> frame) {
  const NotifyView notify = validate_notify(frame);
  if (!notify.valid) {
    if (const uint32_t dropped = this->bad_notify_log_.tick(millis(), BAD_NOTIFY_LOG_INTERVAL_MS); dropped != 0) {
      const size_t dump = std::min(frame.size(), BAD_NOTIFY_DUMP_LEN);
      ESP_LOGW(TAG, "[%s] NOTIFY invalid (len=%u, %u dropped since last log), head: %s", this->parent_->address_str(),
               (unsigned)frame.size(), (unsigned)dropped, format_hex_pretty(frame.data(), dump).c_str());
    }
    return;
  }
  static constexpr auto PARSERS = std::to_array<NotifyParser>({
      {.addr = Addr::BATTERY, .parse = &FiidoBMSHub::parse_battery_},
      {.addr = Addr::CTRL, .parse = &FiidoBMSHub::parse_ctrl_},
      {.addr = Addr::MOTOR, .parse = &FiidoBMSHub::parse_motor_},
      {.addr = Addr::ENERGY, .parse = &FiidoBMSHub::parse_energy_},
      {.addr = Addr::STATS, .parse = &FiidoBMSHub::parse_stats_},
      {.addr = Addr::METER, .parse = &FiidoBMSHub::parse_meter_},
      {.addr = Addr::SPEED_LIMIT, .parse = &FiidoBMSHub::parse_speed_limit_},
      {.addr = Addr::PAS_BOOST, .parse = &FiidoBMSHub::parse_boost_},
      {.addr = Addr::DISPLAY, .parse = &FiidoBMSHub::parse_display_},
  });
  static_assert(
      [] {
        for (const PollDef &poll : POLL_TABLE) {
          bool found = false;
          for (const NotifyParser &entry : PARSERS)
            found = found || entry.addr == poll.addr;
          if (!found)
            return false;
        }
        return true;
      }(),
      "a polled address with no parser drops its reply without a word");

  for (const NotifyParser &entry : PARSERS) {
    if (entry.addr == notify.addr) {
      (this->*(entry.parse))(notify.payload);
      return;
    }
  }
  if (notify.addr == Addr::HANDSHAKE) {
    ESP_LOGD(TAG, "[%s] HANDSHAKE response OK", this->parent_->address_str());
    return;
  }
  if (const uint32_t dropped = this->unknown_addr_log_.tick(millis(), BAD_NOTIFY_LOG_INTERVAL_MS); dropped != 0) {
    ESP_LOGW(TAG, "[%s] NOTIFY unhandled addr=0x%02X (%u dropped since last log)", this->parent_->address_str(),
             static_cast<uint8_t>(notify.addr), (unsigned)dropped);
  }
}

void FiidoBMSHub::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (event != ESP_GAP_BLE_SCAN_RESULT_EVT)
    return;
  if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT)
    return;
  uint64_t found = 0;
  for (const uint8_t byte : param->scan_rst.bda) {
    found = (found << 8) | byte;
  }
  if (found != this->address_)
    return;
  ESP_LOGV(TAG, "[%s] ADVERTISE rssi=%d adv_type=%d adv_data_len=%d scan_rsp_len=%d", this->parent_->address_str(),
           param->scan_rst.rssi, (int)param->scan_rst.ble_evt_type, (int)param->scan_rst.adv_data_len,
           (int)param->scan_rst.scan_rsp_len);
}

void FiidoBMSHub::update() {
  // set_enabled(false) is async, so node_state can stay ESTABLISHED for a while.
  if (!this->ble_user_enabled_)
    return;
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    return;
  }
  if (this->startup_delay_ms_ > 0 && (millis() - this->connect_time_ms_) < this->startup_delay_ms_) {
    return;
  }
  if (!this->handshake_sent_) {
    this->send_handshake_();
    this->handshake_sent_ = true;
    return;
  }
  bool start_with_stats = false;
  size_t start_idx = 0;
  if (this->force_poll_stats_) {
    this->force_poll_stats_ = false;
    start_with_stats = true;
    start_idx = poll_index(Addr::STATS);
  }
  const uint32_t now = millis();
  const BurstGate gate = evaluate_burst_gate(
      now, this->desired_interval_ms_, this->startup_delay_ms_,
      {.last_ms = this->last_burst_ms_, .last_slot = this->last_burst_slot_, .started = this->burst_started_},
      start_with_stats);
  if (!gate.start)
    return;
  if (this->burst_remaining_ > 0) {
    if (!start_with_stats)
      return;
    this->cancel_timeout("burst");
  }
  // Recorded only once the burst is committed: a consumed slot with no burst
  // behind it would stall polling until the next boundary.
  this->last_burst_slot_ = gate.slot;
  this->last_burst_ms_ = now;
  this->burst_started_ = true;
  this->burst_idx_ = start_idx;
  this->burst_remaining_ = POLL_TABLE_SIZE;
  this->burst_retry_ = 0;
  this->send_burst_poll_();
}

void FiidoBMSHub::send_burst_poll_() {
  if (this->burst_remaining_ == 0)
    return;
  // Bail out if connection dropped mid-burst.
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    this->burst_remaining_ = 0;
    return;
  }
  const PollCursor cursor =
      skip_disabled_polls({.index = this->burst_idx_, .remaining = this->burst_remaining_}, this->poll_enabled_);
  this->burst_idx_ = cursor.index;
  this->burst_remaining_ = cursor.remaining;
  if (this->burst_remaining_ == 0)
    return;
  if (this->link_.congested()) {
    this->set_timeout("burst", BURST_RETRY_MS, [this]() { this->send_burst_poll_(); });
    return;
  }
  const bool last_try = this->burst_retry_ >= BURST_SEND_RETRIES;
  const bool send_ok = this->send_poll_(this->burst_idx_, last_try) == WriteError::NONE;
  const BurstStep step = advance_burst(cursor, this->burst_retry_, BURST_SEND_RETRIES, send_ok);
  this->burst_retry_ = step.retry_count;
  this->burst_idx_ = step.cursor.index;
  this->burst_remaining_ = step.cursor.remaining;
  if (step.retry) {
    this->set_timeout("burst", BURST_RETRY_MS, [this]() { this->send_burst_poll_(); });
  } else if (this->burst_remaining_ > 0) {
    this->set_timeout("burst", BURST_INTERVAL_MS, [this]() { this->send_burst_poll_(); });
  }
}

void FiidoBMSHub::send_handshake_() {
  (void)this->send_frame_(build_poll_frame(Addr::HANDSHAKE, static_cast<uint8_t>(Addr::HANDSHAKE)), "HANDSHAKE");
}

WriteError FiidoBMSHub::send_poll_(size_t idx, bool warn_on_fail) {
  const PollDef &poll = POLL_TABLE[idx];
  return this->send_frame_(build_poll_frame(poll.addr, poll.len), poll.name, warn_on_fail);
}

WriteError FiidoBMSHub::send_raw_write_(FrameType type, Addr addr, std::span<const uint8_t> payload) {
  const WriteFrame frame = build_write_frame(type, addr, payload);
  if (frame.empty()) {
    ESP_LOGW(TAG, "[%s] send_raw_write payload too long (%u)", this->parent_->address_str(), (unsigned)payload.size());
    return WriteError::PAYLOAD_TOO_LONG;
  }
  ESP_LOGV(TAG, "[%s] RAW WRITE type=0x%02X addr=0x%02X len=%u -> %s", this->parent_->address_str(),
           static_cast<unsigned>(type), static_cast<uint8_t>(addr), (unsigned)payload.size(),
           format_hex_pretty(frame.bytes.data(), frame.size).c_str());
  return this->send_frame_(frame.span(), "RAW_WRITE");
}

WriteError FiidoBMSHub::send_frame_(std::span<const uint8_t> frame, const char *name, bool warn_on_fail) {
  if (!this->link_.ready()) {
    ESP_LOGW(TAG, "[%s] send %s skipped, FFE2 handle not yet known", this->parent_->address_str(), name);
    return WriteError::NO_HANDLE;
  }
  ESP_LOGV(TAG, "[%s] POLL %-9s -> %s", this->parent_->address_str(), name,
           format_hex_pretty(frame.data(), frame.size()).c_str());
  const WriteError result = this->link_.send(this->parent_, frame);
  if (result != WriteError::NONE) {
    if (warn_on_fail) {
      ESP_LOGW(TAG, "[%s] write %s failed", this->parent_->address_str(), name);
    } else {
      ESP_LOGD(TAG, "[%s] write %s failed, will retry", this->parent_->address_str(), name);
    }
  }
  return result;
}

// === Parse helpers ===

void FiidoBMSHub::publish_fields_(std::span<const uint8_t> p, std::span<const SensorField> fields) {
  for (const SensorField &field : fields) {
    sensor::Sensor *entity = this->*(field.entity);
    if (entity == nullptr)
      continue;
    float raw = 0.0f;
    switch (field.width) {
      case FieldWidth::U8:
        raw = p[field.offset];
        break;
      case FieldWidth::U16BE:
        raw = u16be(p, field.offset);
        break;
      case FieldWidth::U32BE:
        raw = static_cast<float>(u32be(p, field.offset));
        break;
    }
    publish_changed(entity, raw / field.divisor);
  }
}

void FiidoBMSHub::parse_battery_(std::span<const uint8_t> p) {
  if (p.size() != battery::PAYLOAD_LEN)
    return;
  this->publish_fields_(p, BATTERY_FIELDS);
}

void FiidoBMSHub::parse_ctrl_(std::span<const uint8_t> p) {
  if (p.size() != ctrl::PAYLOAD_LEN)
    return;
  this->publish_fields_(p, CTRL_FIELDS);
}

void FiidoBMSHub::parse_motor_(std::span<const uint8_t> p) {
  if (p.size() != motor::PAYLOAD_LEN)
    return;
  this->publish_fields_(p, MOTOR_FIELDS);
  // Two's complement signed 16-bit, and the only field with a plausibility range.
  const int16_t temp_c = static_cast<int16_t>(u16be(p, motor::TEMPERATURE_C));
  if (temp_c >= motor::MIN_TEMPERATURE_C && temp_c <= motor::MAX_TEMPERATURE_C)
    publish_changed(this->motor_temperature_sensor_, temp_c);
}

void FiidoBMSHub::parse_energy_(std::span<const uint8_t> p) {
  if (p.size() != energy::PAYLOAD_LEN)
    return;
  this->publish_fields_(p, ENERGY_FIELDS);
}

void FiidoBMSHub::manage_lifecycle_() {
  if (!this->ble_user_enabled_)
    return;
  const uint32_t now = millis();
  const LifecycleInput in{
      .now = now,
      .enabled = this->parent_->enabled,
      .connected = this->node_state == espbt::ClientState::ESTABLISHED,
      .motor_off_since_ms = this->motor_off_since_ms_,
      .disconnected_since_ms = this->disconnected_since_ms_,
      .probe_started_ms = this->probe_started_ms_,
      .last_dispatch_ms = this->last_dispatch_ms_,
      .pending_writes = !this->pending_writes_.empty(),
      .idle_disconnect_ms = this->idle_disconnect_ms_,
      .probe_window_ms = PROBE_WINDOW_MS,
      .periodic_probe_ms = PERIODIC_PROBE_MS,
      .write_verify_window_ms = WRITE_VERIFY_WINDOW_MS,
  };

  switch (decide_lifecycle(in)) {
    case LifecycleAction::IDLE_DISCONNECT:
      ESP_LOGI(TAG, "[%s] LIFECYCLE: motor OFF for %u min, disabling ble_client", this->parent_->address_str(),
               (unsigned)((now - this->motor_off_since_ms_) / 60000));
      this->parent_->set_enabled(false);
      this->disconnected_since_ms_ = now;
      this->motor_off_since_ms_ = 0;
      this->probe_started_ms_ = 0;
      break;
    case LifecycleAction::PROBE_TIMEOUT:
      ESP_LOGI(TAG, "[%s] LIFECYCLE: probe window expired without reconnect, disabling", this->parent_->address_str());
      this->parent_->set_enabled(false);
      this->disconnected_since_ms_ = now;
      this->probe_started_ms_ = 0;
      break;
    case LifecycleAction::START_PROBE:
      ESP_LOGI(TAG, "[%s] LIFECYCLE: %u min elapsed, starting probe", this->parent_->address_str(),
               (unsigned)((now - this->disconnected_since_ms_) / 60000));
      this->parent_->set_enabled(true);
      this->probe_started_ms_ = now;
      this->disconnected_since_ms_ = 0;
      break;
    case LifecycleAction::NONE:
      break;
  }
}

void FiidoBMSHub::enqueue_pending_write_(PendingWrite fn) {
  if (!this->pending_writes_.push(fn)) {
    ESP_LOGW(TAG, "[%s] pending_writes_ at cap %u - dropping oldest", this->parent_->address_str(),
             (unsigned)PENDING_WRITE_SLOTS);
  }
}

void FiidoBMSHub::dispatch_pending_writes_() {
  if (this->pending_writes_.empty())
    return;
  ESP_LOGD(TAG, "[%s] LIFECYCLE: dispatching %u pending writes", this->parent_->address_str(),
           (unsigned)this->pending_writes_.size());
  const auto writes = this->pending_writes_.drain();
  this->last_dispatch_ms_ = millis();
  for (const PendingWrite &write : writes)
    write();
}

void FiidoBMSHub::ensure_enabled_for_write_() {
  if (!this->ble_user_enabled_) {
    ESP_LOGW(TAG, "[%s] HA action ignored: BLE user-disabled", this->parent_->address_str());
    return;
  }
  if (!this->parent_->enabled) {
    ESP_LOGI(TAG, "[%s] LIFECYCLE: HA action while disabled, enabling ble_client", this->parent_->address_str());
    this->parent_->set_enabled(true);
  }
  // Always arm probe deadline so lifecycle can recover from enabled-but-disconnected.
  if (this->probe_started_ms_ == 0) {
    this->probe_started_ms_ = millis();
    this->disconnected_since_ms_ = 0;
  }
}

void FiidoBMSHub::set_auto_shutdown_enabled(bool en) {
  this->auto_shutdown_enabled_ = en;
  if (this->autoshutdown_switch_ != nullptr)
    this->autoshutdown_switch_->publish_state(en);
}

void FiidoBMSHub::set_ble_user_enabled(bool en) {
  if (en == this->ble_user_enabled_)
    return;
  this->ble_user_enabled_ = en;
  if (this->ble_switch_ != nullptr)
    this->ble_switch_->publish_state(en);
  if (!en) {
    this->pending_writes_.clear();
    this->cancel_timeout("burst");
    this->cancel_timeout("speed_limit_phase2");
    this->cancel_timeout("force_stats_tick");
    this->burst_remaining_ = 0;
    this->burst_idx_ = 0;
    this->burst_retry_ = 0;
    this->parent_->set_enabled(false);
    this->motor_off_since_ms_ = 0;
    this->probe_started_ms_ = 0;
    this->disconnected_since_ms_ = 0;
    ESP_LOGI(TAG, "[%s] BLE user-disabled, halting all activity", this->parent_->address_str());
  } else {
    this->parent_->set_enabled(true);
    this->probe_started_ms_ = millis();
    this->disconnected_since_ms_ = 0;
    this->motor_off_since_ms_ = 0;
    // No DISCONNECT_EVT fires when re-enabling from already-disconnected state.
    this->last_activity_ms_ = millis();
    this->prev_ride_ = {.gear = 0xFF, .motor_on = false, .light_on = false};
    ESP_LOGI(TAG, "[%s] BLE user-enabled, starting probe", this->parent_->address_str());
  }
}

void FiidoBMSHub::publish_stats_samples_(const StatsView &sv) {
  for (const StatsSample &sample : stats_samples(sv)) {
    switch (sample.channel) {
      case StatsChannel::TOTAL_KM:
        publish_changed(this->total_kilometers_sensor_, sample.value);
        break;
      case StatsChannel::TRIP_KM:
        publish_changed(this->current_kilometers_sensor_, sample.value);
        break;
      case StatsChannel::SPEED:
        publish_changed(this->bicycle_speed_sensor_, sample.value);
        break;
      case StatsChannel::SOC:
        publish_changed(this->battery_soc_sensor_, sample.value);
        break;
      case StatsChannel::GEAR_START:
        publish_changed(this->bicycle_gear_start_sensor_, sample.value);
        break;
    }
  }
}

// The gear count must land before the label, which is read from the resulting
// list. max_gear is 0 when the nibble pair is neither 3 nor 5, which keeps the
// last good count.
void FiidoBMSHub::sync_gear_entities_(const StatsView &sv) {
  if (this->gear_select_ == nullptr)
    return;
  const uint8_t count =
      resolve_gear_count(sv.max_gear, this->gear_select_->gear_count_pinned(), this->gear_select_->get_gear_count());
  if (count != 0)
    this->gear_select_->set_gear_count(count);
  if (this->mode_select_ != nullptr)
    publish_changed(this->mode_select_, resolve_mode_option(this->gear_select_->get_gear_count()));
  const auto names = this->gear_select_->gear_names();
  if (sv.gear < names.size()) {
    publish_changed(this->gear_select_, size_t{sv.gear});
    return;
  }
  // The BMS keeps its gear across a mode change.
  if (this->ble_user_enabled_ && flags_27::CONTROLLER.in(sv.b27) &&
      this->gear_drop_throttle_.tick(millis(), GEAR_DROP_COOLDOWN_MS) != 0)
    this->set_gear(sv.gear);
}

// Holds the BMS at 3-gear even when another BLE central out of range of this
// ESP32 flips it back to 5. set_gear_mode rejects the write with the controller
// OFF, and a rejection still spends the cooldown.
void FiidoBMSHub::enforce_gear_mode_(const StatsView &sv) {
  if (!should_enforce_gear_mode_3({.enabled = this->enforce_gear_mode_3_,
                                   .max_gear = sv.max_gear,
                                   .ble_enabled = this->ble_user_enabled_,
                                   .controller_on = flags_27::CONTROLLER.in(sv.b27),
                                   .now = millis(),
                                   .last_write_ms = this->last_enforce_gear_3_ms_,
                                   .cooldown_ms = ENFORCE_GEAR_MODE_3_COOLDOWN_MS}))
    return;
  ESP_LOGI(TAG, "[%s] enforce_gear_mode_3: BMS reports 5-gear, writing 3", this->parent_->address_str());
  this->last_enforce_gear_3_ms_ = millis();
  this->set_gear_mode(3);
}

void FiidoBMSHub::cache_flag_registers_(const StatsView &sv) {
  this->registers_.set<Addr::GEAR_RANGE>(sv.b25);
  this->registers_.set<Addr::FLAGS_27>(sv.b27);
  this->registers_.set<Addr::FLAGS_28>(sv.b28);
  this->registers_.set<Addr::FLAGS_2B>(sv.b2b);
  this->registers_.set<Addr::FLAGS_2C>(sv.b2c);
  this->registers_.set<Addr::FLAGS_38>(sv.b38);
  this->registers_.set<Addr::FLAGS_39>(sv.b39);
}

FlagView FiidoBMSHub::cached_flags_() const {
  return decode_flags(this->registers_.value_or<Addr::FLAGS_27>(), this->registers_.value_or<Addr::FLAGS_28>(),
                      this->registers_.value_or<Addr::FLAGS_2B>(), this->registers_.value_or<Addr::FLAGS_2C>(),
                      this->registers_.value_or<Addr::FLAGS_38>(), this->registers_.value_or<Addr::FLAGS_39>());
}

void FiidoBMSHub::apply_adaptive_interval_(bool motor_on) {
  const uint32_t desired = motor_on ? this->update_interval_on_ms_ : this->update_interval_off_ms_;
  if (this->desired_interval_ms_ == desired)
    return;
  ESP_LOGD(TAG, "[%s] ADAPTIVE interval: motor %s -> %ums (from %ums)", this->parent_->address_str(),
           motor_on ? "ON" : "OFF", (unsigned)desired, (unsigned)this->desired_interval_ms_);
  this->desired_interval_ms_ = desired;
}

// Runs on the motor ON -> OFF edge. The cache, not the payload, carries bits a
// dispatched write has already set in 0x27.
void FiidoBMSHub::clear_persisted_light_bit_(bool motor_on) {
  const RegValue<Addr::FLAGS_27> cache_27 = this->registers_.value_or<Addr::FLAGS_27>();
  if (!should_clear_light_bit(this->ble_user_enabled_, this->prev_ride_.motor_on, motor_on, cache_27))
    return;
  const RegValue<Addr::FLAGS_27> b = flags_27::LIGHT.with(cache_27, false);
  ESP_LOGD(TAG, "[%s] clearing persisted light bit on motor OFF (0x%02X -> 0x%02X)", this->parent_->address_str(),
           cache_27.raw, b.raw);
  if (WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_27, std::array<uint8_t, 1>{b.raw})) {
    this->registers_.set<Addr::FLAGS_27>(b);
  } else {
    ESP_LOGW(TAG, "[%s] WRITE 0x27 (light auto-clear) failed - cache not updated", this->parent_->address_str());
  }
}

void FiidoBMSHub::track_activity_(const RideState &ride, uint16_t speed_raw) {
  const ActivitySignals activity = detect_activity(this->prev_ride_, ride, speed_raw);
  this->prev_ride_ = ride;
  if (activity.motor_turned_on)
    this->mark_activity_("motor on");
  if (activity.gear_changed)
    this->mark_activity_("gear");
  if (activity.moving)
    this->mark_activity_("speed");
  if (activity.light_changed)
    this->mark_activity_("light");
  if (!should_auto_shutdown({.now = millis(),
                             .last_activity_ms = this->last_activity_ms_,
                             .idle_ms = IDLE_SHUTDOWN_MS,
                             .enabled = this->auto_shutdown_enabled_,
                             .motor_on = ride.motor_on}))
    return;
  ESP_LOGI(TAG, "[%s] AUTO-SHUTDOWN: no activity for %u min, disabling motor", this->parent_->address_str(),
           (unsigned)((millis() - this->last_activity_ms_) / 60000));
  // Holds off a re-trigger until the next STATS confirms the write.
  this->last_activity_ms_ = millis();
  this->set_motor_enable(false);
}

void FiidoBMSHub::update_idle_timer_(bool motor_on) {
  const uint32_t motor_off_since = track_motor_off(this->motor_off_since_ms_, motor_on, millis());
  if (motor_off_since == this->motor_off_since_ms_)
    return;
  ESP_LOGD(TAG, "[%s] LIFECYCLE: idle disconnect timer %s", this->parent_->address_str(),
           motor_off_since != 0 ? "started" : "cleared");
  this->motor_off_since_ms_ = motor_off_since;
}

void FiidoBMSHub::parse_stats_(std::span<const uint8_t> p) {
  const StatsView sv = decode_stats(p);
  if (!sv.valid)
    return;
  this->publish_stats_samples_(sv);
  this->sync_gear_entities_(sv);
  this->enforce_gear_mode_(sv);
  // ADDR 0x2A bit 5 = brake. Squeezing the lever moves nothing on C11 Pro or
  // M1 Pro 2025 (physical test 2026-08-29).
  publish_changed(this->brake_binary_sensor_, sv.brake);
  this->cache_flag_registers_(sv);
  this->dispatch_pending_writes_();
  // Entity sync reads the caches, which dispatch may have just moved on. What
  // follows reads sv.
  this->publish_flag_entities_(this->cached_flags_());

  const bool motor_on = flags_27::CONTROLLER.in(sv.b27);
  this->apply_adaptive_interval_(motor_on);
  this->clear_persisted_light_bit_(motor_on);
  this->track_activity_({.gear = sv.gear, .motor_on = motor_on, .light_on = motor_on && flags_27::LIGHT.in(sv.b27)},
                        u16be(p, stats::SPEED_OFFSET));
  this->update_idle_timer_(motor_on);
  this->settle_probe_(motor_on);

  ESP_LOGD(TAG,
           "[%s] STATS speed=%.1f km=%.1f total=%.1fkm gear=%u SOC=%u%% addr25=0x%02X addr27=0x%02X addr28=0x%02X "
           "addr2A=0x%02X addr2B=0x%02X addr2C=0x%02X addr38=0x%02X motor=%s idle=%us",
           this->parent_->address_str(), u16be(p, stats::SPEED_OFFSET) / 10.0, u16be(p, stats::TRIP_KM_OFFSET) / 10.0,
           u32be(p, stats::TOTAL_KM_OFFSET) / 10.0, sv.gear, p[stats::ADDR_24_OFFSET], p[stats::ADDR_25_OFFSET],
           p[stats::ADDR_27_OFFSET], p[stats::ADDR_28_OFFSET], p[stats::ADDR_2A_OFFSET], p[stats::ADDR_2B_OFFSET],
           p[stats::ADDR_2C_OFFSET], p[stats::ADDR_38_OFFSET], motor_on ? "ON" : "OFF",
           (unsigned)((millis() - this->last_activity_ms_) / 1000));
}

void FiidoBMSHub::set_motor_enable(bool on) {
  const WriteGate verdict =
      this->gate_(this->registers_.has<Addr::FLAGS_27>(), false, "MOTOR", [this, on]() { this->set_motor_enable(on); });
  if (verdict == WriteGate::REJECT_BLE_DISABLED || verdict == WriteGate::REJECT_CONTROLLER_OFF) {
    if (this->motor_switch_ != nullptr)
      this->motor_switch_->publish_state(!on);
  }
  if (verdict != WriteGate::SEND)
    return;
  const RegValue<Addr::FLAGS_27> cached = this->registers_.value_or<Addr::FLAGS_27>();
  RegValue<Addr::FLAGS_27> b = flags_27::CONTROLLER.with(cached, on);
  // The BMS keeps the light bit across an OFF/ON cycle.
  if (!on && flags_27::LIGHT.in(b)) {
    b = flags_27::LIGHT.with(b, false);
    if (this->light_switch_ != nullptr)
      this->light_switch_->publish_state(false);
  }
  ESP_LOGI(TAG, "[%s] MOTOR %s ADDR 0x27: 0x%02X -> 0x%02X", this->parent_->address_str(), on ? "ENABLE" : "DISABLE",
           cached.raw, b.raw);
  if (WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_27, std::array<uint8_t, 1>{b.raw})) {
    this->registers_.set<Addr::FLAGS_27>(b);
    this->schedule_write_verify_();
  } else {
    ESP_LOGW(TAG, "[%s] WRITE 0x27 (motor) failed - cache not updated", this->parent_->address_str());
  }
}

void FiidoBMSHub::set_light_enable(bool on) {
  const WriteGate verdict =
      this->gate_(this->registers_.has<Addr::FLAGS_27>(), true, "LIGHT", [this, on]() { this->set_light_enable(on); });
  if (verdict == WriteGate::REJECT_BLE_DISABLED || verdict == WriteGate::REJECT_CONTROLLER_OFF) {
    if (this->light_switch_ != nullptr)
      this->light_switch_->publish_state(false);
  }
  if (verdict != WriteGate::SEND)
    return;
  const RegValue<Addr::FLAGS_27> cached = this->registers_.value_or<Addr::FLAGS_27>();
  const RegValue<Addr::FLAGS_27> b = flags_27::LIGHT.with(cached, on);
  ESP_LOGI(TAG, "[%s] LIGHT %s ADDR 0x27: 0x%02X -> 0x%02X (bit3)", this->parent_->address_str(),
           on ? "ENABLE" : "DISABLE", cached.raw, b.raw);
  if (WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_27, std::array<uint8_t, 1>{b.raw})) {
    this->registers_.set<Addr::FLAGS_27>(b);
    this->schedule_write_verify_();
  } else {
    ESP_LOGW(TAG, "[%s] WRITE 0x27 (light) failed - cache not updated", this->parent_->address_str());
  }
}

void FiidoBMSHub::set_gear(uint8_t gear) {
  const uint8_t max_gear = (this->gear_select_ != nullptr) ? this->gear_select_->get_gear_count() : 5;
  if (clamp_gear(gear, max_gear) != gear) {
    ESP_LOGI(TAG, "[%s] set_gear(%u) clamped to %u (active gear count)", this->parent_->address_str(), gear, max_gear);
    gear = clamp_gear(gear, max_gear);
  }
  const WriteGate verdict =
      this->gate_(this->registers_.has<Addr::FLAGS_27>(), true, "GEAR", [this, gear]() { this->set_gear(gear); });
  if (verdict == WriteGate::REJECT_BLE_DISABLED || verdict == WriteGate::REJECT_CONTROLLER_OFF)
    revert_select(this->gear_select_);
  if (verdict != WriteGate::SEND)
    return;
  ESP_LOGI(TAG, "[%s] GEAR set to %u (WRITE ADDR 0x26)", this->parent_->address_str(), gear);
  if (WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::GEAR, std::array<uint8_t, 1>{gear})) {
    this->schedule_write_verify_();
  } else {
    ESP_LOGW(TAG, "[%s] WRITE 0x26 (gear) failed", this->parent_->address_str());
  }
}

void FiidoBMSHub::set_gear_mode(uint8_t mode) {
  // ADDR 0x25 upper nibble = max_gear, lower = bike config preserved. Frame 0xFF.
  if (mode != 3 && mode != 5) {
    ESP_LOGW(TAG, "[%s] set_gear_mode(%u) REJECTED - must be 3 or 5", this->parent_->address_str(), mode);
    revert_select(this->mode_select_);
    return;
  }
  const bool cache_ready = this->registers_.has<Addr::FLAGS_27>() && this->registers_.has<Addr::GEAR_RANGE>();
  const WriteGate verdict = this->gate_(cache_ready, true, "GEAR MODE", [this, mode]() { this->set_gear_mode(mode); });
  if (verdict == WriteGate::REJECT_BLE_DISABLED || verdict == WriteGate::REJECT_CONTROLLER_OFF)
    revert_select(this->mode_select_);
  if (verdict != WriteGate::SEND)
    return;
  const RegValue<Addr::GEAR_RANGE> cached = this->registers_.value_or<Addr::GEAR_RANGE>();
  const RegValue<Addr::GEAR_RANGE> encoded = encode_gear_mode(mode, cached);
  ESP_LOGI(TAG, "[%s] GEAR MODE set to %u (ADDR 0x25: 0x%02X -> 0x%02X) frame type 0xFF", this->parent_->address_str(),
           mode, cached.raw, encoded.raw);
  if (WriteError::NONE ==
      this->send_raw_write_(FrameType::WRITE_J0, Addr::GEAR_RANGE, std::array<uint8_t, 1>{encoded.raw})) {
    this->registers_.set<Addr::GEAR_RANGE>(encoded);
    this->schedule_write_verify_();
  } else {
    ESP_LOGW(TAG, "[%s] WRITE 0x25 (gear_mode) failed - cache not updated", this->parent_->address_str());
  }
}

void FiidoBMSHub::parse_meter_(std::span<const uint8_t> p) {
  if (p.size() != meter::PAYLOAD_LEN)
    return;
  this->publish_fields_(p, METER_FIELDS);
  ESP_LOGV(TAG, "[%s] METER HW=%u SW=%u modeData=%u", this->parent_->address_str(), p[meter::HW_VERSION],
           p[meter::SW_VERSION], p[meter::MODE_DATA]);
}

// Map (ADDR 0x3C value, bit 5 ADDR 0x27) pair to select option. Keep last good on ambiguous.
void FiidoBMSHub::parse_speed_limit_(std::span<const uint8_t> p) {
  if (p.size() != speed_limit::PAYLOAD_LEN)
    return;
  this->registers_.set<Addr::SPEED_LIMIT>({p[speed_limit::VALUE_KMH]});
  ESP_LOGD(TAG, "[%s] SPEED_LIMIT value=%u 0x%02X", this->parent_->address_str(), p[speed_limit::VALUE_KMH],
           p[speed_limit::VALUE_KMH]);
  if (this->speed_limit_select_ != nullptr && this->registers_.has<Addr::FLAGS_27>()) {
    const bool limit_on = flags_27::SPEED_LIMIT.in(this->registers_.value_or<Addr::FLAGS_27>());
    const char *opt = resolve_speed_limit_option(p[speed_limit::VALUE_KMH], limit_on);
    if (opt != nullptr) {
      publish_changed(this->speed_limit_select_, opt);
    } else {
      // bit5 clear with value != 100 is the resting state after a ride: the BMS
      // re-arms the PAS cap itself. Rate limit or it warns on every SPEEDLIM poll.
      if (const uint32_t seen = this->ambiguous_limit_log_.tick(millis(), AMBIGUOUS_LIMIT_LOG_INTERVAL_MS); seen != 0) {
        ESP_LOGW(TAG, "[%s] SPEED_LIMIT ambiguous (value=%u bit5=%u, %u since last log) keep prev",
                 this->parent_->address_str(), p[speed_limit::VALUE_KMH], limit_on ? 1 : 0, (unsigned)seen);
      }
    }
  }
}

void FiidoBMSHub::set_speed_limit(const std::string &option) {
  const std::optional<SpeedLimitOption> parsed = parse_speed_limit_option(option);
  if (!parsed.has_value()) {
    ESP_LOGW(TAG, "[%s] set_speed_limit('%s') REJECTED - unknown option", this->parent_->address_str(), option.c_str());
    revert_select(this->speed_limit_select_);
    return;
  }
  this->apply_speed_limit_(*parsed);
}

void FiidoBMSHub::apply_speed_limit_(SpeedLimitOption option) {
  const char *name = speed_limit_option_name(option);
  const SpeedLimitPlan plan = plan_speed_limit(option, this->registers_.value_or<Addr::FLAGS_2C>());
  // Newer command supersedes a pending phase2, which holds the old target.
  this->cancel_timeout("speed_limit_phase2");
  const bool cache_ready = this->registers_.has<Addr::FLAGS_27>() && this->registers_.has<Addr::FLAGS_2C>();
  const WriteGate verdict =
      this->gate_(cache_ready, false, "SPEED_LIMIT", [this, option]() { this->apply_speed_limit_(option); });
  if (verdict == WriteGate::REJECT_BLE_DISABLED || verdict == WriteGate::REJECT_CONTROLLER_OFF)
    revert_select(this->speed_limit_select_);
  if (verdict != WriteGate::SEND)
    return;
  if (plan.needs_pas_write) {
    ESP_LOGI(TAG, "[%s] SPEED_LIMIT phase1: PAS %s ADDR 0x2C 0x%02X->0x%02X (bit 7)", this->parent_->address_str(),
             plan.limit_on ? "ON" : "OFF", this->registers_.value_or<Addr::FLAGS_2C>().raw, plan.pas_byte.raw);
    if (WriteError::NONE ==
        this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_2C, std::array<uint8_t, 1>{plan.pas_byte.raw})) {
      this->registers_.set<Addr::FLAGS_2C>(plan.pas_byte);
    } else {
      ESP_LOGW(TAG, "[%s] WRITE 0x2C (speed_limit PAS) failed - cache not updated", this->parent_->address_str());
    }
  }
  // The 50ms delay opens a window for a disconnect or a transient reconnect.
  // The link and cache are checked again inside.
  auto phase2 = [this, name, plan]() {
    if (!this->ble_user_enabled_ || this->node_state != espbt::ClientState::ESTABLISHED ||
        !this->registers_.has<Addr::FLAGS_27>()) {
      ESP_LOGW(TAG, "[%s] SPEED_LIMIT phase2 aborted - BLE disabled or link/cache not ready",
               this->parent_->address_str());
      return;
    }
    const RegValue<Addr::FLAGS_27> cached_27 = this->registers_.value_or<Addr::FLAGS_27>();
    const RegValue<Addr::FLAGS_27> b27 = apply_speed_limit_bit(cached_27, plan.limit_on);
    ESP_LOGI(TAG, "[%s] SPEED_LIMIT phase2: '%s' WRITE 0x3C=%u + 0x27 0x%02X->0x%02X (bit5=%u)",
             this->parent_->address_str(), name, plan.value.raw, cached_27.raw, b27.raw, plan.limit_on ? 1 : 0);
    const bool ok_3c = WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::SPEED_LIMIT,
                                                                 std::array<uint8_t, 1>{plan.value.raw});
    const bool ok_27 =
        WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_27, std::array<uint8_t, 1>{b27.raw});
    if (ok_3c) {
      this->registers_.set<Addr::SPEED_LIMIT>(plan.value);
    } else {
      ESP_LOGW(TAG, "[%s] WRITE 0x3C (speed_limit value) failed - cache not updated", this->parent_->address_str());
    }
    if (ok_27) {
      this->registers_.set<Addr::FLAGS_27>(b27);
    } else {
      ESP_LOGW(TAG, "[%s] WRITE 0x27 (speed_limit bit5) failed - cache not updated", this->parent_->address_str());
    }
    if (ok_3c || ok_27)
      this->schedule_write_verify_();
  };
  if (plan.delay_phase2) {
    this->set_timeout("speed_limit_phase2", SPEED_LIMIT_PHASE2_DELAY_MS, phase2);
  } else {
    phase2();
  }
}

void FiidoBMSHub::set_speed_unit(const std::string &option) {
  if (option != "km/h" && option != "mph") {
    ESP_LOGW(TAG, "[%s] set_speed_unit('%s') unknown option", this->parent_->address_str(), option.c_str());
    revert_select(this->speed_unit_select_);
    return;
  }
  this->apply_speed_unit_(option == "mph");
}

void FiidoBMSHub::apply_speed_unit_(bool mile) {
  const WriteGate verdict = this->gate_(this->registers_.has<Addr::FLAGS_28>(), false, "SPEED_UNIT",
                                        [this, mile]() { this->apply_speed_unit_(mile); });
  if (verdict == WriteGate::REJECT_BLE_DISABLED || verdict == WriteGate::REJECT_CONTROLLER_OFF)
    revert_select(this->speed_unit_select_);
  if (verdict != WriteGate::SEND)
    return;
  this->write_flag_bit_(flags_28::SPEED_UNIT_MPH, mile, "SPEED_UNIT");
}

WriteGate FiidoBMSHub::gate_(bool cache_valid, bool needs_controller, const char *name, PendingWrite retry) {
  const WriteGate verdict = gate_write({
      .ble_enabled = this->ble_user_enabled_,
      .connected = this->node_state == espbt::ClientState::ESTABLISHED,
      .cache_valid = cache_valid,
      .needs_controller = needs_controller,
      .controller_on = flags_27::CONTROLLER.in(this->registers_.value_or<Addr::FLAGS_27>()),
  });
  switch (verdict) {
    case WriteGate::QUEUE_DISCONNECTED:
      ESP_LOGI(TAG, "[%s] %s queued (disconnected)", this->parent_->address_str(), name);
      this->enqueue_pending_write_(retry);
      this->ensure_enabled_for_write_();
      break;
    case WriteGate::DEFER_COLD_CACHE:
      ESP_LOGI(TAG, "[%s] %s deferred: cache cold", this->parent_->address_str(), name);
      this->enqueue_pending_write_(retry);
      break;
    case WriteGate::REJECT_BLE_DISABLED:
      ESP_LOGW(TAG, "[%s] %s rejected: BLE user-disabled", this->parent_->address_str(), name);
      break;
    case WriteGate::REJECT_CONTROLLER_OFF:
      ESP_LOGW(TAG, "[%s] %s rejected: bike controller is OFF (bit 7 ADDR 0x27)", this->parent_->address_str(), name);
      break;
    case WriteGate::SEND:
      break;
  }
  return verdict;
}

void FiidoBMSHub::write_masked_bits_(Addr addr, size_t slot, uint8_t mask, uint8_t bits, const char *name) {
  std::optional<uint8_t> &cache = this->registers_.at(slot);
  if (!cache.has_value()) {
    ESP_LOGW(TAG, "[%s] %s skipped: ADDR 0x%02X cache cold, preserve-bits would be built from zero",
             this->parent_->address_str(), name, static_cast<unsigned>(addr));
    return;
  }
  const MaskedWrite w = compute_masked_write(addr, *cache, mask, bits);
  ESP_LOGI(TAG, "[%s] %s ADDR 0x%02X: 0x%02X -> 0x%02X (mask 0x%02X)", this->parent_->address_str(), name,
           static_cast<unsigned>(addr), *cache, w.value, mask);
  if (WriteError::NONE == this->send_raw_write_(w.type, addr, std::array<uint8_t, 1>{w.value})) {
    cache = w.value;
    this->schedule_write_verify_();
  } else {
    ESP_LOGW(TAG, "[%s] WRITE 0x%02X (%s) failed - cache not updated", this->parent_->address_str(),
             static_cast<unsigned>(addr), name);
  }
}

void FiidoBMSHub::set_flag_(FlagId id, bool on) {
  const FlagControl &c = FLAG_CONTROLS[static_cast<size_t>(id)];
  const WriteGate verdict = this->gate_(this->registers_.at(c.slot).has_value(), false, c.name,
                                        [this, id, on]() { this->set_flag_(id, on); });
  if (verdict == WriteGate::REJECT_BLE_DISABLED || verdict == WriteGate::REJECT_CONTROLLER_OFF) {
    switch_::Switch *entity = this->*(c.entity);
    if (entity != nullptr)
      entity->publish_state(!on);
  }
  if (verdict != WriteGate::SEND)
    return;
  this->write_masked_bits_(c.addr, c.slot, c.mask, on ? c.bits_on : c.bits_off, c.name);
}

void FiidoBMSHub::set_speaker_enable(bool on) {
  this->set_flag_(FlagId::SPEAKER, on);
}
void FiidoBMSHub::set_key_sound_enable(bool on) {
  this->set_flag_(FlagId::KEY_SOUND, on);
}
void FiidoBMSHub::set_throttle_enable(bool on) {
  this->set_flag_(FlagId::THROTTLE, on);
}
void FiidoBMSHub::set_slow_mode_enable(bool on) {
  this->set_flag_(FlagId::SLOW_MODE, on);
}
void FiidoBMSHub::set_bike_guard_enable(bool on) {
  this->set_flag_(FlagId::BIKE_GUARD, on);
}

#ifdef USE_FIIDO_BMS_DEV
void FiidoBMSHub::set_cruise_enable(bool on) {
  this->set_flag_(FlagId::CRUISE, on);
}
void FiidoBMSHub::set_start_mode_enable(bool on) {
  this->set_flag_(FlagId::START_MODE, on);
}
void FiidoBMSHub::set_insensitivity_enable(bool on) {
  this->set_flag_(FlagId::INSENSITIVITY, on);
}
void FiidoBMSHub::set_show_total_km_enable(bool on) {
  this->set_flag_(FlagId::SHOW_TOTAL_KM, on);
}
void FiidoBMSHub::set_auto_screen_off_enable(bool on) {
  this->set_flag_(FlagId::AUTO_SCREEN_OFF, on);
}
void FiidoBMSHub::set_ring_enable(bool on) {
  this->set_flag_(FlagId::RING, on);
}
void FiidoBMSHub::set_double_speed_enable(bool on) {
  this->set_flag_(FlagId::DOUBLE_SPEED, on);
}
#endif

#ifdef USE_FIIDO_BMS_DEV
// Raw 1-byte value write for number entities. Clamps to 0..255 then sends.
bool FiidoBMSHub::write_value_byte_(FrameType type, Addr addr, uint8_t value, const char *name) {
  ESP_LOGI(TAG, "[%s] %s WRITE ADDR 0x%02X = %u (type 0x%02X)", this->parent_->address_str(), name,
           static_cast<unsigned>(addr), value, static_cast<unsigned>(type));
  if (WriteError::NONE != this->send_raw_write_(type, addr, std::array<uint8_t, 1>{value})) {
    ESP_LOGW(TAG, "[%s] WRITE 0x%02X (%s) failed - cache not updated", this->parent_->address_str(),
             static_cast<unsigned>(addr), name);
    return false;
  }
  this->schedule_write_verify_();
  return true;
}

void FiidoBMSHub::set_byte_(ByteId id, float value) {
  const ByteControl &c = BYTE_CONTROLS[static_cast<size_t>(id)];
  number::Number *entity = this->*(c.entity);
  // NaN passes both clamp compares and the cast below is UB.
  if (!std::isfinite(value)) {
    revert_number(entity);
    return;
  }
  const uint8_t v = static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
  // A whole-byte write has no cache to wait for.
  const WriteGate verdict = this->gate_(true, false, c.name, [this, id, value]() { this->set_byte_(id, value); });
  if (verdict == WriteGate::REJECT_BLE_DISABLED || verdict == WriteGate::REJECT_CONTROLLER_OFF)
    revert_number(entity);
  if (verdict != WriteGate::SEND)
    return;
  if (this->write_value_byte_(c.type, c.addr, v, c.name)) {
    this->registers_.at(c.slot) = v;
    if (entity != nullptr)
      entity->publish_state(v);
  } else {
    revert_number(entity);
  }
}

void FiidoBMSHub::set_brightness(float value) {
  this->set_byte_(ByteId::BRIGHTNESS, value);
}
void FiidoBMSHub::set_boost(float value) {
  this->set_byte_(ByteId::BOOST, value);
}
void FiidoBMSHub::set_guard_time(float value) {
  this->set_byte_(ByteId::GUARD_TIME, value);
}

// Pair this ESP32 as a proximity unlock companion: send its own BLE address as a
// 6-byte value. The on-air byte order is logged so it can be checked later.
void FiidoBMSHub::pair_watch() {
  if (!this->ble_user_enabled_) {
    ESP_LOGW(TAG, "[%s] PAIR_WATCH rejected: BLE user-disabled", this->parent_->address_str());
    return;
  }
  // One-shot with no undo here, so never queued for replay like other setters.
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    ESP_LOGW(TAG, "[%s] PAIR_WATCH dropped: link down, press again while connected", this->parent_->address_str());
    return;
  }
  const uint8_t *mac = esp_bt_dev_get_address();
  if (mac == nullptr) {
    ESP_LOGW(TAG, "[%s] PAIR_WATCH: own BLE address not available", this->parent_->address_str());
    return;
  }
  const std::span<const uint8_t> payload(mac, 6);
  ESP_LOGI(TAG, "[%s] PAIR_WATCH ADDR 0x09 MAC bytes (send order) = %s", this->parent_->address_str(),
           format_hex_pretty(payload.data(), payload.size()).c_str());
  // Write-only address: no notify carries 0x09 back, so the send result is the
  // only signal there is.
  if (WriteError::NONE != this->send_raw_write_(FrameType::WRITE_J0, Addr::WATCH_PAIR, payload)) {
    ESP_LOGW(TAG, "[%s] PAIR_WATCH write failed", this->parent_->address_str());
  }
}
#endif

void FiidoBMSHub::parse_boost_(std::span<const uint8_t> p) {
  if (p.size() != pas_boost::PAYLOAD_LEN)
    return;
  this->registers_.set<Addr::PAS_BOOST>({p[pas_boost::LEVEL]});
#ifdef USE_FIIDO_BMS_DEV
  publish_changed(this->boost_number_, p[pas_boost::LEVEL]);
#endif
  ESP_LOGD(TAG, "[%s] BOOST value=%u", this->parent_->address_str(), p[pas_boost::LEVEL]);
}

void FiidoBMSHub::parse_display_(std::span<const uint8_t> p) {
  if (p.size() != display::PAYLOAD_LEN)
    return;
  this->registers_.set<Addr::DISPLAY>({p[display::BRIGHTNESS]});
  this->registers_.set<Addr::GUARD_TIME>({p[display::GUARD_TIME]});
#ifdef USE_FIIDO_BMS_DEV
  publish_changed(this->brightness_number_, p[display::BRIGHTNESS]);
  publish_changed(this->guard_time_number_, p[display::GUARD_TIME]);
#endif
  ESP_LOGD(TAG, "[%s] DISPLAY brightness=%u guard_time=%u", this->parent_->address_str(), p[display::BRIGHTNESS],
           p[display::GUARD_TIME]);
}

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
