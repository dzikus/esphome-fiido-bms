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

#include <array>
#include <cmath>

#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"
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

void publish_changed(number::Number *n, float value) {
  if (n == nullptr || (n->has_state() && n->state == value))
    return;
  n->publish_state(value);
}

void revert_number(number::Number *n) {
  if (n != nullptr && n->has_state())
    n->publish_state(n->state);
}

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

// Custom Fiido service (FFE0 base): write to FFE2, notify from FFE1.
static const auto FIIDO_SERVICE_UUID = esp32_ble_tracker::ESPBTUUID::from_raw("00010203-0405-0607-0809-0a0b0c0dffe0");
static const auto FIIDO_NOTIFY_CHAR_UUID =
    esp32_ble_tracker::ESPBTUUID::from_raw("00010203-0405-0607-0809-0a0b0c0dffe1");
static const auto FIIDO_WRITE_CHAR_UUID =
    esp32_ble_tracker::ESPBTUUID::from_raw("00010203-0405-0607-0809-0a0b0c0dffe2");

void FiidoBMSHub::setup() {
  // Stagger hubs across one ON-interval so parallel bikes do not collide on BLE airtime.
  if (this->startup_delay_ms_ == 0 && this->total_hubs_ > 1) {
    uint64_t product = static_cast<uint64_t>(this->hub_index_) * this->update_interval_on_ms_;
    this->startup_delay_ms_ = static_cast<uint32_t>(product / this->total_hubs_);
    ESP_LOGI(TAG, "[%s] AUTO startup_delay=%ums (hub %d of %d, interval_on=%ums)", this->parent_->address_str(),
             (unsigned)this->startup_delay_ms_, this->hub_index_, this->total_hubs_,
             (unsigned)this->update_interval_on_ms_);
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
  LOG_SENSOR("  ", "Battery Voltage", this->battery_voltage_sensor_);
  LOG_SENSOR("  ", "Battery Current Voltage", this->battery_current_voltage_sensor_);
  LOG_SENSOR("  ", "Battery Current", this->battery_current_sensor_);
  LOG_SENSOR("  ", "Battery Capacity", this->battery_capacity_sensor_);
  LOG_SENSOR("  ", "Battery Manufacturer", this->battery_manufacturer_sensor_);
  LOG_SENSOR("  ", "Battery Hw Version", this->battery_hw_version_sensor_);
  LOG_SENSOR("  ", "Battery Sw Version", this->battery_sw_version_sensor_);
  LOG_SENSOR("  ", "Ctrl Upper Voltage", this->ctrl_upper_voltage_sensor_);
  LOG_SENSOR("  ", "Ctrl Lower Voltage", this->ctrl_lower_voltage_sensor_);
  LOG_SENSOR("  ", "Ctrl Current", this->ctrl_current_sensor_);
  LOG_SENSOR("  ", "Ctrl Temperature", this->ctrl_temperature_sensor_);
  LOG_SENSOR("  ", "Ctrl Hw Version", this->ctrl_hw_version_sensor_);
  LOG_SENSOR("  ", "Ctrl Sw Version", this->ctrl_sw_version_sensor_);
  LOG_SENSOR("  ", "Ctrl Version", this->ctrl_version_sensor_);
  LOG_SENSOR("  ", "Ctrl Manufacturer", this->ctrl_manufacturer_sensor_);
  LOG_SENSOR("  ", "Motor Version", this->motor_version_sensor_);
  LOG_SENSOR("  ", "Motor Magnetic", this->motor_magnetic_sensor_);
  LOG_SENSOR("  ", "Motor Wire Count", this->motor_wire_count_sensor_);
  LOG_SENSOR("  ", "Motor Steel Count", this->motor_steel_count_sensor_);
  LOG_SENSOR("  ", "Motor Reduction Ratio", this->motor_reduction_ratio_sensor_);
  LOG_SENSOR("  ", "Motor Wheel Diameter", this->motor_wheel_diameter_sensor_);
  LOG_SENSOR("  ", "Motor Temperature", this->motor_temperature_sensor_);
  LOG_SENSOR("  ", "Motor Capacity", this->motor_capacity_sensor_);
  LOG_SENSOR("  ", "Crank Torque", this->crank_torque_sensor_);
  LOG_SENSOR("  ", "Crank Rpm", this->crank_rpm_sensor_);
  LOG_SENSOR("  ", "This Take Energy", this->this_take_energy_sensor_);
  LOG_SENSOR("  ", "Total Take Energy", this->total_take_energy_sensor_);
  LOG_SENSOR("  ", "Startup Time", this->startup_time_sensor_);
  LOG_SENSOR("  ", "Bicycle Speed", this->bicycle_speed_sensor_);
  LOG_SENSOR("  ", "Current Kilometers", this->current_kilometers_sensor_);
  LOG_SENSOR("  ", "Total Kilometers", this->total_kilometers_sensor_);
  LOG_SENSOR("  ", "Battery Soc", this->battery_soc_sensor_);
  LOG_SENSOR("  ", "Bicycle Gear Start", this->bicycle_gear_start_sensor_);
  LOG_SENSOR("  ", "Meter Hw Version", this->meter_hw_version_sensor_);
  LOG_SENSOR("  ", "Meter Sw Version", this->meter_sw_version_sensor_);
  LOG_SENSOR("  ", "Meter Mode Data", this->meter_mode_data_sensor_);
  LOG_BINARY_SENSOR("  ", "Connected", this->connected_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Brake", this->brake_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Pas Limit", this->pas_limit_binary_sensor_);
  LOG_SWITCH("  ", "Motor", this->motor_switch_);
  LOG_SWITCH("  ", "Light", this->light_switch_);
  LOG_SWITCH("  ", "Autoshutdown", this->autoshutdown_switch_);
  LOG_SWITCH("  ", "Speaker", this->speaker_switch_);
  LOG_SWITCH("  ", "Key Sound", this->key_sound_switch_);
  LOG_SWITCH("  ", "Throttle", this->throttle_switch_);
  LOG_SWITCH("  ", "Slow Mode", this->slow_mode_switch_);
  LOG_SWITCH("  ", "Ble", this->ble_switch_);
  LOG_SWITCH("  ", "Cruise", this->cruise_switch_);
  LOG_SWITCH("  ", "Start Mode", this->start_mode_switch_);
  LOG_SWITCH("  ", "Insensitivity", this->insensitivity_switch_);
  LOG_SWITCH("  ", "Show Total Km", this->show_total_km_switch_);
  LOG_SWITCH("  ", "Auto Screen Off", this->auto_screen_off_switch_);
  LOG_SWITCH("  ", "Ring", this->ring_switch_);
  LOG_SWITCH("  ", "Double Speed", this->double_speed_switch_);
  LOG_SWITCH("  ", "Bike Guard", this->bike_guard_switch_);
  LOG_SELECT("  ", "Mode", this->mode_select_);
  LOG_SELECT("  ", "Speed Limit", this->speed_limit_select_);
  LOG_SELECT("  ", "Speed Unit", this->speed_unit_select_);
  LOG_NUMBER("  ", "Brightness", this->brightness_number_);
  LOG_NUMBER("  ", "Boost", this->boost_number_);
  LOG_NUMBER("  ", "Guard Time", this->guard_time_number_);
  LOG_BUTTON("  ", "Pair Watch", this->pair_watch_button_);
  LOG_SELECT("  ", "Gear", this->gear_select_);
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
    case ESP_GATTC_OPEN_EVT: {
      ESP_LOGI(TAG, "[%s] Connection opened", this->parent_->address_str());
      this->connect_time_ms_ = millis();
      this->handshake_sent_ = false;
      this->link_congested_ = false;
      break;
    }
    case ESP_GATTC_CONGEST_EVT: {
      if (param->congest.conn_id != this->parent_->get_conn_id())
        break;
      this->link_congested_ = param->congest.congested;
      ESP_LOGD(TAG, "[%s] l2cap %scongested", this->parent_->address_str(), this->link_congested_ ? "" : "un");
      if (!this->link_congested_ && this->burst_remaining_ > 0) {
        this->cancel_timeout("burst");
        this->send_burst_poll_();
      }
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGW(TAG, "[%s] Disconnected", this->parent_->address_str());
      this->node_state = espbt::ClientState::IDLE;
      this->link_congested_ = false;
      if (this->char_notify_handle_ != 0) {
        auto unreg = esp_ble_gattc_unregister_for_notify(this->parent()->get_gattc_if(),
                                                         this->parent()->get_remote_bda(), this->char_notify_handle_);
        if (unreg) {
          ESP_LOGW(TAG, "[%s] unregister_for_notify failed, status=%d", this->parent_->address_str(), unreg);
        }
      }
      this->char_write_handle_ = 0;
      this->char_notify_handle_ = 0;
      this->handshake_sent_ = false;
      this->cancel_timeout("burst");
      this->burst_remaining_ = 0;
      this->burst_idx_ = 0;
      this->burst_retry_ = 0;
      this->publish_connected_(false);
      // Bike state may change while disconnected; need fresh STATS post-reconnect.
      this->registers_.clear();
      this->prev_motor_on_ = false;
      this->prev_light_on_ = false;
      this->prev_gear_ = 0xFF;
      this->last_dispatch_ms_ = 0;
      this->last_bad_notify_log_ms_ = 0;
      this->bad_notify_count_ = 0;
      this->last_unknown_addr_log_ms_ = 0;
      this->unknown_addr_count_ = 0;
      this->last_ambiguous_limit_log_ms_ = 0;
      this->ambiguous_limit_count_ = 0;
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *notify_chr = this->parent_->get_characteristic(FIIDO_SERVICE_UUID, FIIDO_NOTIFY_CHAR_UUID);
      auto *write_chr = this->parent_->get_characteristic(FIIDO_SERVICE_UUID, FIIDO_WRITE_CHAR_UUID);
      if (notify_chr == nullptr || write_chr == nullptr) {
        ESP_LOGE(TAG, "[%s] FFE1/FFE2 not found, not a Fiido BMS?", this->parent_->address_str());
        break;
      }
      this->char_notify_handle_ = notify_chr->handle;
      this->char_write_handle_ = write_chr->handle;
      ESP_LOGD(TAG, "[%s] FFE2 (write)=0x%02X  FFE1 (notify)=0x%02X", this->parent_->address_str(),
               this->char_write_handle_, this->char_notify_handle_);

      auto status = esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(),
                                                      this->char_notify_handle_);
      if (status) {
        ESP_LOGW(TAG, "register_for_notify failed, status=%d", status);
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      this->node_state = espbt::ClientState::ESTABLISHED;
      this->publish_connected_(true);
      ESP_LOGI(TAG, "[%s] READY (FFE1 notify enabled)", this->parent_->address_str());
      // Reset burst gate on (re)connect: a stale desired_interval_ms_ (e.g. 5min
      // OFF window) would otherwise stall the first poll for minutes after a
      // HA-triggered reconnect with pending writes.
      this->burst_started_ = false;
      this->desired_interval_ms_ = this->update_interval_on_ms_;
      // Handshake sent on first update() after startup_delay
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle != this->char_notify_handle_)
        break;
      const uint8_t *buf = param->notify.value;
      const size_t len = param->notify.value_len;

      const NotifyView notify = validate_notify(std::span<const uint8_t>(buf, len));
      if (!notify.valid) {
        this->bad_notify_count_++;
        uint32_t now = millis();
        if (should_log_now(now, this->last_bad_notify_log_ms_, BAD_NOTIFY_LOG_INTERVAL_MS)) {
          size_t dump = len < BAD_NOTIFY_DUMP_LEN ? len : BAD_NOTIFY_DUMP_LEN;
          ESP_LOGW(TAG, "[%s] NOTIFY invalid (len=%u, %u dropped since last log), head: %s",
                   this->parent_->address_str(), (unsigned)len, (unsigned)this->bad_notify_count_,
                   format_hex_pretty(buf, dump).c_str());
          this->last_bad_notify_log_ms_ = now;
          this->bad_notify_count_ = 0;
        }
        break;
      }
      const std::span<const uint8_t> payload = notify.payload;
      switch (notify.addr) {
        case Addr::BATTERY:
          this->parse_battery_(payload);
          break;
        case Addr::CTRL:
          this->parse_ctrl_(payload);
          break;
        case Addr::MOTOR:
          this->parse_motor_(payload);
          break;
        case Addr::ENERGY:
          this->parse_energy_(payload);
          break;
        case Addr::STATS:
          this->parse_stats_(payload);
          break;
        case Addr::METER:
          this->parse_meter_(payload);
          break;
        case Addr::SPEED_LIMIT:
          this->parse_speed_limit_(payload);
          break;
        case Addr::PAS_BOOST:
          this->parse_boost_(payload);
          break;
        case Addr::DISPLAY:
          this->parse_display_(payload);
          break;
        case Addr::HANDSHAKE:
          ESP_LOGD(TAG, "[%s] HANDSHAKE response OK", this->parent_->address_str());
          break;
        default: {
          this->unknown_addr_count_++;
          uint32_t now = millis();
          if (should_log_now(now, this->last_unknown_addr_log_ms_, BAD_NOTIFY_LOG_INTERVAL_MS)) {
            ESP_LOGW(TAG, "[%s] NOTIFY unhandled addr=0x%02X (%u dropped since last log)", this->parent_->address_str(),
                     static_cast<uint8_t>(notify.addr), (unsigned)this->unknown_addr_count_);
            this->last_unknown_addr_log_ms_ = now;
            this->unknown_addr_count_ = 0;
          }
          break;
        }
      }
      break;
    }
    default:
      break;
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
    for (size_t i = 0; i < POLL_TABLE_SIZE; i++) {
      if (POLL_TABLE[i].addr == Addr::STATS) {
        start_idx = i;
        break;
      }
    }
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
  if (this->link_congested_) {
    this->set_timeout("burst", BURST_RETRY_MS, [this]() { this->send_burst_poll_(); });
    return;
  }
  const bool last_try = this->burst_retry_ >= BURST_SEND_RETRIES;
  const bool send_ok = this->send_poll_(this->burst_idx_, last_try) == WriteError::NONE;
  if (should_retry_send(this->burst_retry_, BURST_SEND_RETRIES, send_ok)) {
    this->burst_retry_++;
    this->set_timeout("burst", BURST_RETRY_MS, [this]() { this->send_burst_poll_(); });
    return;
  }
  this->burst_retry_ = 0;
  this->burst_idx_ = (this->burst_idx_ + 1) % POLL_TABLE_SIZE;
  this->burst_remaining_--;
  if (this->burst_remaining_ > 0) {
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
  if (this->char_write_handle_ == 0) {
    ESP_LOGW(TAG, "[%s] send %s skipped, FFE2 handle not yet known", this->parent_->address_str(), name);
    return WriteError::NO_HANDLE;
  }
  ESP_LOGV(TAG, "[%s] POLL %-9s -> %s", this->parent_->address_str(), name,
           format_hex_pretty(frame.bytes.data(), frame.size).c_str());
  auto status = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(),
                                         this->char_write_handle_, frame.size(), const_cast<uint8_t *>(frame.data()),
                                         ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status) {
    if (warn_on_fail) {
      ESP_LOGW(TAG, "[%s] write %s failed, status=%d", this->parent_->address_str(), name, status);
    } else {
      ESP_LOGD(TAG, "[%s] write %s failed, status=%d, will retry", this->parent_->address_str(), name, status);
    }
    return WriteError::GATT_WRITE_FAILED;
  }
  return WriteError::NONE;
}

// === Parse helpers ===

void FiidoBMSHub::parse_battery_(std::span<const uint8_t> p) {
  if (p.size() != battery::PAYLOAD_LEN)
    return;
  publish_changed(this->battery_hw_version_sensor_, p[battery::HW_VERSION]);
  publish_changed(this->battery_sw_version_sensor_, p[battery::SW_VERSION]);
  publish_changed(this->battery_capacity_sensor_, u16be(p, battery::CAPACITY_AH) / 10.0f);
  publish_changed(this->battery_voltage_sensor_, u16be(p, battery::VOLTAGE_V) / 10.0f);
  publish_changed(this->battery_current_voltage_sensor_, u16be(p, battery::CURRENT_VOLTAGE_V) / 10.0f);
  publish_changed(this->battery_current_sensor_, u16be(p, battery::CURRENT_A) / 10.0f);
  publish_changed(this->battery_manufacturer_sensor_, p[battery::MANUFACTURER]);
  ESP_LOGV(TAG, "[%s] BATTERY V=%.1f I=%.1f Ah=%.1f", this->parent_->address_str(), u16be(p, battery::VOLTAGE_V) / 10.0,
           u16be(p, battery::CURRENT_A) / 10.0, u16be(p, battery::CAPACITY_AH) / 10.0);
}

void FiidoBMSHub::parse_ctrl_(std::span<const uint8_t> p) {
  if (p.size() != ctrl::PAYLOAD_LEN)
    return;
  const uint16_t current = u16be(p, ctrl::CURRENT_A);
  publish_changed(this->ctrl_hw_version_sensor_, p[ctrl::HW_VERSION]);
  publish_changed(this->ctrl_sw_version_sensor_, p[ctrl::SW_VERSION]);
  publish_changed(this->ctrl_upper_voltage_sensor_, u16be(p, ctrl::UPPER_VOLTAGE_V) / 10.0f);
  publish_changed(this->ctrl_lower_voltage_sensor_, u16be(p, ctrl::LOWER_VOLTAGE_V) / 10.0f);
  publish_changed(this->ctrl_current_sensor_, current / 10.0f);
  publish_changed(this->ctrl_temperature_sensor_, p[ctrl::TEMPERATURE_C]);
  publish_changed(this->ctrl_version_sensor_, p[ctrl::VERSION]);
  publish_changed(this->ctrl_manufacturer_sensor_, p[ctrl::MANUFACTURER]);
  ESP_LOGV(TAG, "[%s] CTRL upper=%.1fV lower=%.1fV I=%.1fA T=%uC ver=%u", this->parent_->address_str(),
           u16be(p, ctrl::UPPER_VOLTAGE_V) / 10.0, u16be(p, ctrl::LOWER_VOLTAGE_V) / 10.0, current / 10.0,
           p[ctrl::TEMPERATURE_C], p[ctrl::VERSION]);
}

void FiidoBMSHub::parse_motor_(std::span<const uint8_t> p) {
  if (p.size() != motor::PAYLOAD_LEN)
    return;
  publish_changed(this->motor_version_sensor_, p[motor::VERSION]);
  publish_changed(this->motor_magnetic_sensor_, p[motor::MAGNETIC]);
  publish_changed(this->motor_wire_count_sensor_, p[motor::WIRE_COUNT]);
  publish_changed(this->motor_steel_count_sensor_, p[motor::STEEL_COUNT]);
  publish_changed(this->motor_reduction_ratio_sensor_, p[motor::REDUCTION_RATIO] / 10.0f);
  publish_changed(this->motor_wheel_diameter_sensor_, u16be(p, motor::WHEEL_DIAMETER_IN) / 10.0f);
  // Two's complement signed 16-bit.
  const int16_t temp_c = static_cast<int16_t>(u16be(p, motor::TEMPERATURE_C));
  if (temp_c >= motor::MIN_TEMPERATURE_C && temp_c <= motor::MAX_TEMPERATURE_C) {
    publish_changed(this->motor_temperature_sensor_, temp_c);
  }
  publish_changed(this->motor_capacity_sensor_, u16be(p, motor::CAPACITY_W));
  ESP_LOGV(TAG, "[%s] MOTOR ver=%u T=%dC wheel=%.1f cap=%u", this->parent_->address_str(), p[motor::VERSION], temp_c,
           u16be(p, motor::WHEEL_DIAMETER_IN) / 10.0, u16be(p, motor::CAPACITY_W));
}

void FiidoBMSHub::parse_energy_(std::span<const uint8_t> p) {
  if (p.size() != energy::PAYLOAD_LEN)
    return;
  const uint16_t torque = u16be(p, energy::CRANK_TORQUE_NM);
  const uint16_t rpm = u16be(p, energy::CRANK_RPM);
  publish_changed(this->crank_torque_sensor_, torque / 10.0f);
  publish_changed(this->crank_rpm_sensor_, rpm);
  publish_changed(this->this_take_energy_sensor_, u16be(p, energy::THIS_TAKE_WH) / 10.0f);
  publish_changed(this->total_take_energy_sensor_, u32be(p, energy::TOTAL_TAKE_WH) / 10.0f);
  publish_changed(this->startup_time_sensor_, u16be(p, energy::STARTUP_TIME_S));
  ESP_LOGV(TAG, "[%s] ENERGY torque=%.1fNm rpm=%u this=%.1fWh total=%.1fWh up=%us", this->parent_->address_str(),
           torque / 10.0, rpm, u16be(p, energy::THIS_TAKE_WH) / 10.0, u32be(p, energy::TOTAL_TAKE_WH) / 10.0,
           u16be(p, energy::STARTUP_TIME_S));
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
             (unsigned)MAX_PENDING_WRITES);
  }
}

void FiidoBMSHub::dispatch_pending_writes_() {
  if (this->pending_writes_.empty())
    return;
  ESP_LOGD(TAG, "[%s] LIFECYCLE: dispatching %u pending writes", this->parent_->address_str(),
           (unsigned)this->pending_writes_.size());
  size_t count = 0;
  const auto writes = this->pending_writes_.drain(count);
  this->last_dispatch_ms_ = millis();
  for (size_t i = 0; i < count; i++)
    writes[i]();
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
    this->prev_motor_on_ = false;
    this->prev_light_on_ = false;
    this->prev_gear_ = 0xFF;
    ESP_LOGI(TAG, "[%s] BLE user-enabled, starting probe", this->parent_->address_str());
  }
}

void FiidoBMSHub::parse_stats_(std::span<const uint8_t> p) {
  const StatsView sv = decode_stats(p);
  if (!sv.valid)
    return;
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

  // Resolved before the gear label below, which reads the resulting list.
  // max_gear is 0 when the nibble pair is not 3 or 5, which keeps the last-good
  // count. Runs without the mode select, which the yaml may omit.
  const uint8_t max_gear = sv.max_gear;
  if (this->gear_select_ != nullptr) {
    const uint8_t count =
        resolve_gear_count(max_gear, this->gear_select_->gear_count_pinned(), this->gear_select_->get_gear_count());
    if (count != 0)
      this->gear_select_->set_gear_count(count);
    if (this->mode_select_ != nullptr)
      publish_changed(this->mode_select_, resolve_mode_option(this->gear_select_->get_gear_count()));
  }

  if (this->gear_select_ != nullptr) {
    const auto &names = this->gear_select_->gear_names();
    if (sv.gear < names.size())
      publish_changed(this->gear_select_, names[sv.gear].c_str());
  }
  // enforce_gear_mode_3 keeps the BMS pinned to 3-gear even when something
  // else (e.g. another BLE central out of range of this ESP32) flips it back to 5.
  // Gated on motor controller ON because set_gear_mode rejects writes when the
  // controller is OFF; gating here avoids burning the cooldown on a rejected
  // write. The cooldown caps write traffic to once per minute per hub.
  const bool ctrl_on = (sv.b27 & 0x80) != 0;
  if (should_enforce_gear_mode_3(this->enforce_gear_mode_3_, max_gear, this->ble_user_enabled_, ctrl_on, millis(),
                                 this->last_enforce_gear_3_ms_, ENFORCE_GEAR_MODE_3_COOLDOWN_MS)) {
    ESP_LOGI(TAG, "[%s] enforce_gear_mode_3: BMS reports 5-gear, writing 3", this->parent_->address_str());
    this->last_enforce_gear_3_ms_ = millis();
    this->set_gear_mode(3);
  }

  // ADDR 0x2A bit 5 = brake. Not user-verified on physical bike yet.
  publish_changed(this->brake_binary_sensor_, sv.brake);

  this->registers_.set<Addr::GEAR_RANGE>(sv.b25);
  this->registers_.set<Addr::FLAGS_27>(sv.b27);
  this->registers_.set<Addr::FLAGS_28>(sv.b28);
  this->registers_.set<Addr::FLAGS_2B>(sv.b2b);
  this->registers_.set<Addr::FLAGS_2C>(sv.b2c);
  this->registers_.set<Addr::FLAGS_38>(sv.b38);
  this->registers_.set<Addr::FLAGS_39>(sv.b39);

  this->dispatch_pending_writes_();

  // Entity sync reads the caches, which dispatch may have just moved on; sv holds
  // the payload the write was built from. Behaviour below stays on sv, so
  // auto-shutdown and lifecycle act on state the bike confirmed.
  const FlagView f =
      decode_flags(this->registers_.value_or<Addr::FLAGS_27>(0), this->registers_.value_or<Addr::FLAGS_28>(0),
                   this->registers_.value_or<Addr::FLAGS_2B>(0), this->registers_.value_or<Addr::FLAGS_2C>(0),
                   this->registers_.value_or<Addr::FLAGS_38>(0), this->registers_.value_or<Addr::FLAGS_39>(0));

  publish_changed(this->pas_limit_binary_sensor_, f.pas_limit_on);

  const bool motor_on = (sv.b27 & 0x80) != 0;
  const bool light_on = motor_on && ((sv.b27 & 0x08) != 0);
  uint32_t desired = motor_on ? this->update_interval_on_ms_ : this->update_interval_off_ms_;
  if (this->desired_interval_ms_ != desired) {
    ESP_LOGD(TAG, "[%s] ADAPTIVE interval: motor %s -> %ums (from %ums)", this->parent_->address_str(),
             motor_on ? "ON" : "OFF", (unsigned)desired, (unsigned)this->desired_interval_ms_);
    this->desired_interval_ms_ = desired;
  }

  // A press of the physical button on the bike shows up here.
  if (this->motor_switch_ != nullptr) {
    this->motor_switch_->publish_state(f.motor_on);
  }
  if (this->light_switch_ != nullptr) {
    this->light_switch_->publish_state(f.light_on);
  }
  // STATS arrives more often than the 0x3C poll, so publish whichever combination
  // is on hand.
  if (this->speed_limit_select_ != nullptr && this->registers_.has<Addr::SPEED_LIMIT>()) {
    const char *opt = resolve_speed_limit_option(*this->registers_.get<Addr::SPEED_LIMIT>(), f.speed_limit_on);
    if (opt != nullptr) {
      publish_changed(this->speed_limit_select_, opt);
    }
    // Ambiguous combo is transient (BMS reloading from flash on boot).
    // Keep previous publish to avoid flicker.
  }

  if (this->speed_unit_select_ != nullptr) {
    publish_changed(this->speed_unit_select_, f.speed_unit_mph ? "mph" : "km/h");
  }
  for (const FlagControl &control : FLAG_CONTROLS) {
    switch_::Switch *entity = this->*(control.entity);
    if (entity != nullptr)
      entity->publish_state(f.*(control.state));
  }

  // Auto-shutdown idle tracking.
  // Edge motor OFF -> ON: reset idle timer to give the user a grace period.
  if (motor_on && !this->prev_motor_on_) {
    this->last_activity_ms_ = millis();
    ESP_LOGD(TAG, "[%s] motor ON edge -> idle timer reset", this->parent_->address_str());
  }
  // Edge motor ON -> OFF (physical button): BMS persists bit 3 across the
  // OFF/ON cycle so the bike would come back ON with stale lights. Clear
  // bit 3 now while the link is still live.
  // Byte built from the cache, not p[]: dispatch above may have set other bits in
  // 0x27 already and writing p[] back would undo them.
  const uint8_t cache_27 = this->registers_.value_or<Addr::FLAGS_27>(0);
  if (should_clear_light_bit(this->ble_user_enabled_, this->prev_motor_on_, motor_on, cache_27)) {
    const uint8_t b = cache_27 & ~0x08;
    ESP_LOGD(TAG, "[%s] clearing persisted light bit on motor OFF (0x%02X -> 0x%02X)", this->parent_->address_str(),
             cache_27, b);
    if (WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_27, std::array<uint8_t, 1>{b})) {
      this->registers_.set<Addr::FLAGS_27>(b);
    } else {
      ESP_LOGW(TAG, "[%s] WRITE 0x27 (light auto-clear) failed - cache not updated", this->parent_->address_str());
    }
  }
  this->prev_motor_on_ = motor_on;
  // Activity signals from STATS: gear change, non-zero speed, light toggle.
  uint8_t gear = p[stats::ADDR_26_OFFSET];
  uint16_t speed_raw = u16be(p, stats::SPEED_OFFSET);
  if (gear != this->prev_gear_)
    this->mark_activity_("gear");
  this->prev_gear_ = gear;
  if (speed_raw > 0)
    this->mark_activity_("speed");
  if (light_on != this->prev_light_on_)
    this->mark_activity_("light");
  this->prev_light_on_ = light_on;
  // Trigger auto-shutdown after IDLE_SHUTDOWN_MS of no activity (if enabled).
  if (should_auto_shutdown(millis(), this->last_activity_ms_, IDLE_SHUTDOWN_MS, this->auto_shutdown_enabled_,
                           motor_on)) {
    ESP_LOGI(TAG, "[%s] AUTO-SHUTDOWN: no activity for %u min, disabling motor", this->parent_->address_str(),
             (unsigned)((millis() - this->last_activity_ms_) / 60000));
    this->last_activity_ms_ = millis();  // prevent re-trigger before STATS verify
    this->set_motor_enable(false);
  }

  // Lifecycle: track motor OFF duration for idle disconnect. Cleared on motor ON.
  if (!motor_on) {
    if (this->motor_off_since_ms_ == 0) {
      this->motor_off_since_ms_ = millis();
      ESP_LOGD(TAG, "[%s] LIFECYCLE: motor OFF -> idle disconnect timer started", this->parent_->address_str());
    }
  } else {
    if (this->motor_off_since_ms_ != 0) {
      ESP_LOGD(TAG, "[%s] LIFECYCLE: motor ON -> idle disconnect timer cleared", this->parent_->address_str());
      this->motor_off_since_ms_ = 0;
    }
  }

  // Probe done: motor_on -> stay; verify window still open -> stay; else drop.
  if (this->probe_started_ms_ != 0) {
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

  ESP_LOGV(TAG,
           "[%s] STATS speed=%.1f km=%.1f total=%.1fkm gear=%u SOC=%u%% addr25=0x%02X addr27=0x%02X addr2A=0x%02X "
           "addr2C=0x%02X motor=%s idle=%us",
           this->parent_->address_str(), u16be(p, stats::SPEED_OFFSET) / 10.0, u16be(p, stats::TRIP_KM_OFFSET) / 10.0,
           u32be(p, stats::TOTAL_KM_OFFSET) / 10.0, gear, p[stats::ADDR_24_OFFSET], p[stats::ADDR_25_OFFSET],
           p[stats::ADDR_27_OFFSET], p[stats::ADDR_2A_OFFSET], p[stats::ADDR_2C_OFFSET], motor_on ? "ON" : "OFF",
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
  const uint8_t cached = *this->registers_.get<Addr::FLAGS_27>();
  uint8_t b = cached;
  if (on) {
    b |= 0x80;
  } else {
    b &= ~0x80;
    // BMS persists bit 3 across OFF/ON, so clear it here to avoid stale-light wake.
    if (b & 0x08) {
      b &= ~0x08;
      if (this->light_switch_ != nullptr)
        this->light_switch_->publish_state(false);
    }
  }
  ESP_LOGI(TAG, "[%s] MOTOR %s ADDR 0x27: 0x%02X -> 0x%02X", this->parent_->address_str(), on ? "ENABLE" : "DISABLE",
           cached, b);
  if (WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_27, std::array<uint8_t, 1>{b})) {
    this->registers_.set<Addr::FLAGS_27>(b);
    this->force_poll_stats_ = true;
    this->set_timeout("force_stats_tick", FORCE_STATS_DELAY_MS, [this]() { this->update(); });
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
  const uint8_t cached = *this->registers_.get<Addr::FLAGS_27>();
  uint8_t b = cached;
  if (on) {
    b |= 0x08;
  } else {
    b &= ~0x08;
  }
  ESP_LOGI(TAG, "[%s] LIGHT %s ADDR 0x27: 0x%02X -> 0x%02X (bit3)", this->parent_->address_str(),
           on ? "ENABLE" : "DISABLE", cached, b);
  if (WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_27, std::array<uint8_t, 1>{b})) {
    this->registers_.set<Addr::FLAGS_27>(b);
    this->force_poll_stats_ = true;
    this->set_timeout("force_stats_tick", FORCE_STATS_DELAY_MS, [this]() { this->update(); });
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
    this->force_poll_stats_ = true;
    this->set_timeout("force_stats_tick", FORCE_STATS_DELAY_MS, [this]() { this->update(); });
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
  const uint8_t cached = *this->registers_.get<Addr::GEAR_RANGE>();
  const uint8_t encoded = encode_gear_mode(mode, cached);
  ESP_LOGI(TAG, "[%s] GEAR MODE set to %u (ADDR 0x25: 0x%02X -> 0x%02X) frame type 0xFF", this->parent_->address_str(),
           mode, cached, encoded);
  if (WriteError::NONE ==
      this->send_raw_write_(FrameType::WRITE_J0, Addr::GEAR_RANGE, std::array<uint8_t, 1>{encoded})) {
    this->registers_.set<Addr::GEAR_RANGE>(encoded);
    this->force_poll_stats_ = true;
    this->set_timeout("force_stats_tick", FORCE_STATS_DELAY_MS, [this]() { this->update(); });
  } else {
    ESP_LOGW(TAG, "[%s] WRITE 0x25 (gear_mode) failed - cache not updated", this->parent_->address_str());
  }
}

void FiidoBMSHub::parse_meter_(std::span<const uint8_t> p) {
  if (p.size() != meter::PAYLOAD_LEN)
    return;
  publish_changed(this->meter_hw_version_sensor_, p[meter::HW_VERSION]);
  publish_changed(this->meter_sw_version_sensor_, p[meter::SW_VERSION]);
  publish_changed(this->meter_mode_data_sensor_, p[meter::MODE_DATA]);
  ESP_LOGV(TAG, "[%s] METER HW=%u SW=%u modeData=%u", this->parent_->address_str(), p[meter::HW_VERSION],
           p[meter::SW_VERSION], p[meter::MODE_DATA]);
}

// Map (ADDR 0x3C value, bit 5 ADDR 0x27) pair to select option. Keep last good on ambiguous.
void FiidoBMSHub::parse_speed_limit_(std::span<const uint8_t> p) {
  if (p.size() != speed_limit::PAYLOAD_LEN)
    return;
  this->registers_.set<Addr::SPEED_LIMIT>(p[speed_limit::VALUE_KMH]);
  ESP_LOGV(TAG, "[%s] SPEED_LIMIT value=%u 0x%02X", this->parent_->address_str(), p[speed_limit::VALUE_KMH],
           p[speed_limit::VALUE_KMH]);
  if (this->speed_limit_select_ != nullptr && this->registers_.has<Addr::FLAGS_27>()) {
    bool limit_on = (*this->registers_.get<Addr::FLAGS_27>() & 0x20) != 0;
    const char *opt = resolve_speed_limit_option(p[speed_limit::VALUE_KMH], limit_on);
    if (opt != nullptr) {
      publish_changed(this->speed_limit_select_, opt);
    } else {
      // bit5 clear with value != 100 is the resting state after a ride: the BMS
      // re-arms the PAS cap itself. Rate limit or it warns on every SPEEDLIM poll.
      this->ambiguous_limit_count_++;
      uint32_t now = millis();
      if (should_log_now(now, this->last_ambiguous_limit_log_ms_, AMBIGUOUS_LIMIT_LOG_INTERVAL_MS)) {
        ESP_LOGW(TAG, "[%s] SPEED_LIMIT ambiguous (value=%u bit5=%u, %u since last log) keep prev",
                 this->parent_->address_str(), p[speed_limit::VALUE_KMH], limit_on ? 1 : 0,
                 (unsigned)this->ambiguous_limit_count_);
        this->last_ambiguous_limit_log_ms_ = now;
        this->ambiguous_limit_count_ = 0;
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
  const SpeedLimitPlan plan = plan_speed_limit(option, this->registers_.value_or<Addr::FLAGS_2C>(0));
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
             plan.limit_on ? "ON" : "OFF", *this->registers_.get<Addr::FLAGS_2C>(), plan.pas_byte);
    if (WriteError::NONE ==
        this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_2C, std::array<uint8_t, 1>{plan.pas_byte})) {
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
    const uint8_t cached_27 = *this->registers_.get<Addr::FLAGS_27>();
    const uint8_t b27 = apply_speed_limit_bit(cached_27, plan.limit_on);
    ESP_LOGI(TAG, "[%s] SPEED_LIMIT phase2: '%s' WRITE 0x3C=%u + 0x27 0x%02X->0x%02X (bit5=%u)",
             this->parent_->address_str(), name, plan.value, cached_27, b27, plan.limit_on ? 1 : 0);
    const bool ok_3c = WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::SPEED_LIMIT,
                                                                 std::array<uint8_t, 1>{plan.value});
    const bool ok_27 =
        WriteError::NONE == this->send_raw_write_(FrameType::WRITE_L0, Addr::FLAGS_27, std::array<uint8_t, 1>{b27});
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
    if (ok_3c || ok_27) {
      this->force_poll_stats_ = true;
      this->set_timeout("force_stats_tick", FORCE_STATS_DELAY_MS, [this]() { this->update(); });
    }
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
  this->write_flag_bit_<Addr::FLAGS_28>(0x80, mile, "SPEED_UNIT");
}

WriteGate FiidoBMSHub::gate_(bool cache_valid, bool needs_controller, const char *name, PendingWrite retry) {
  const WriteGate verdict = gate_write({
      .ble_enabled = this->ble_user_enabled_,
      .connected = this->node_state == espbt::ClientState::ESTABLISHED,
      .cache_valid = cache_valid,
      .needs_controller = needs_controller,
      .controller_on = (this->registers_.value_or<Addr::FLAGS_27>(0) & 0x80) != 0,
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
    this->force_poll_stats_ = true;
    this->set_timeout("force_stats_tick", FORCE_STATS_DELAY_MS, [this]() { this->update(); });
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
void FiidoBMSHub::set_bike_guard_enable(bool on) {
  this->set_flag_(FlagId::BIKE_GUARD, on);
}

// Raw 1-byte value write for number entities. Clamps to 0..255 then sends.
bool FiidoBMSHub::write_value_byte_(FrameType type, Addr addr, uint8_t value, const char *name) {
  ESP_LOGI(TAG, "[%s] %s WRITE ADDR 0x%02X = %u (type 0x%02X)", this->parent_->address_str(), name,
           static_cast<unsigned>(addr), value, static_cast<unsigned>(type));
  if (WriteError::NONE != this->send_raw_write_(type, addr, std::array<uint8_t, 1>{value})) {
    ESP_LOGW(TAG, "[%s] WRITE 0x%02X (%s) failed - cache not updated", this->parent_->address_str(),
             static_cast<unsigned>(addr), name);
    return false;
  }
  this->force_poll_stats_ = true;
  this->set_timeout("force_stats_tick", FORCE_STATS_DELAY_MS, [this]() { this->update(); });
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
  const uint8_t v = (value < 0) ? 0 : (value > 255 ? 255 : static_cast<uint8_t>(value));
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
  std::vector<uint8_t> payload(mac, mac + 6);
  ESP_LOGI(TAG, "[%s] PAIR_WATCH ADDR 0x09 MAC bytes (send order) = %s", this->parent_->address_str(),
           format_hex_pretty(payload.data(), payload.size()).c_str());
  // Write-only address: no notify carries 0x09 back, so the send result is the
  // only signal there is.
  if (WriteError::NONE != this->send_raw_write_(FrameType::WRITE_J0, Addr::WATCH_PAIR, payload)) {
    ESP_LOGW(TAG, "[%s] PAIR_WATCH write failed", this->parent_->address_str());
  }
}

void FiidoBMSHub::parse_boost_(std::span<const uint8_t> p) {
  if (p.size() != pas_boost::PAYLOAD_LEN)
    return;
  this->registers_.set<Addr::PAS_BOOST>(p[pas_boost::LEVEL]);
  publish_changed(this->boost_number_, p[pas_boost::LEVEL]);
  ESP_LOGV(TAG, "[%s] BOOST value=%u", this->parent_->address_str(), p[pas_boost::LEVEL]);
}

void FiidoBMSHub::parse_display_(std::span<const uint8_t> p) {
  if (p.size() != display::PAYLOAD_LEN)
    return;
  this->registers_.set<Addr::DISPLAY>(p[display::BRIGHTNESS]);
  this->registers_.set<Addr::GUARD_TIME>(p[display::GUARD_TIME]);
  publish_changed(this->brightness_number_, p[display::BRIGHTNESS]);
  publish_changed(this->guard_time_number_, p[display::GUARD_TIME]);
  ESP_LOGV(TAG, "[%s] DISPLAY brightness=%u guard_time=%u", this->parent_->address_str(), p[display::BRIGHTNESS],
           p[display::GUARD_TIME]);
}

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
