#include "fiido_gear_select.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome {
namespace fiido_bms {

void FiidoGearSelect::set_gear_count(uint8_t count) {
  if (count != 3 && count != 5) count = 5;
  this->gear_count_ = count;
  // HA caches the option list from setup-time ListEntitiesSelectResponse; runtime
  // traits changes are ignored. select.py registers the 5-gear list by default,
  // or the 3-gear list when ui_gear_mode_3=true. set_gear_count() switches the
  // active mapping used by control() (label -> raw byte) and parse_stats_
  // (raw byte -> label).
}

void FiidoGearSelect::control(const std::string &value) {
  // Direct match: label valid in active list.
  const auto &active = this->gear_names();
  for (size_t i = 0; i < active.size(); i++) {
    if (active[i] == value) {
      this->parent_->set_gear(static_cast<uint8_t>(i));
      return;
    }
  }
  // Fallback hierarchy: 3-gear list is a subset of 5-gear list with order
  // preserved. Map the clicked label to its index in names_5_, then pick the
  // largest active entry whose names_5_ index is <= clicked. Example with
  // active=3-gear: "turbo+" -> "turbo", "normal" -> "eco". Avoids strict
  // reject when HA UI caches the 5-gear options but runtime mode is 3-gear.
  int picked_idx_full = -1;
  for (size_t i = 0; i < this->names_5_.size(); i++) {
    if (this->names_5_[i] == value) {
      picked_idx_full = static_cast<int>(i);
      break;
    }
  }
  if (picked_idx_full >= 0) {
    int best_active = -1;
    for (size_t j = 0; j < active.size(); j++) {
      int idx_full = -1;
      for (size_t k = 0; k < this->names_5_.size(); k++) {
        if (this->names_5_[k] == active[j]) {
          idx_full = static_cast<int>(k);
          break;
        }
      }
      if (idx_full >= 0 && idx_full <= picked_idx_full) {
        best_active = static_cast<int>(j);
      }
    }
    if (best_active >= 0) {
      ESP_LOGI("fiido_gear_select", "value '%s' not valid in %u-gear mode - falling back to '%s'",
               value.c_str(), this->gear_count_, active[best_active].c_str());
      this->parent_->set_gear(static_cast<uint8_t>(best_active));
      return;
    }
  }
  ESP_LOGW("fiido_gear_select", "value '%s' is not valid in current gear mode (count=%u) - rejected",
           value.c_str(), this->gear_count_);
  auto opt = this->current_option();
  if (!opt.empty()) {
    this->publish_state(opt.c_str());
  }
}

}  // namespace fiido_bms
}  // namespace esphome

#endif  // USE_ESP32
