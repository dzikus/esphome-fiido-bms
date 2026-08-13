#include "fiido_gear_select.h"

#include "esphome/core/log.h"

#include "fiido_bms.h"

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
  // HA keeps offering the 5-gear labels it cached at setup even when the active
  // list is 3-gear. The active list is a subsequence of names_5_ in order, so one
  // walk finds the last active entry at or before the clicked label:
  // "turbo+" -> "turbo", "normal" -> "eco".
  int fallback = -1;
  size_t next_active = 0;
  for (const auto &name : this->names_5_) {
    if (name == value) {
      if (fallback < 0) break;
      ESP_LOGI(FIIDO_BMS_TAG, "value '%s' not valid in %u-gear mode - falling back to '%s'",
               value.c_str(), this->gear_count_, active[fallback].c_str());
      this->parent_->set_gear(static_cast<uint8_t>(fallback));
      return;
    }
    if (next_active < active.size() && name == active[next_active]) {
      fallback = static_cast<int>(next_active++);
    }
  }
  ESP_LOGW(FIIDO_BMS_TAG, "value '%s' is not valid in current gear mode (count=%u) - rejected",
           value.c_str(), this->gear_count_);
  auto opt = this->current_option();
  if (!opt.empty()) {
    this->publish_state(opt.c_str());
  }
}

}  // namespace fiido_bms
}  // namespace esphome

#endif  // USE_ESP32
