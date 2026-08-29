#include "fiido_gear_select.h"

#include "esphome/core/log.h"
#include "fiido_bms.h"

#ifdef USE_ESP32

namespace esphome::fiido_bms {

void FiidoGearSelect::set_gear_count(uint8_t count) {
  if (count != 3 && count != 5)
    count = 5;
  this->gear_count_ = count;
  // HA caches the option list from setup-time ListEntitiesSelectResponse; runtime
  // traits changes are ignored. select.py registers the 5-gear list by default,
  // or the 3-gear list when ui_gear_mode_3=true. set_gear_count() switches the
  // active mapping used by control() (label -> raw byte) and parse_stats_
  // (raw byte -> label).
}

void FiidoGearSelect::control(const std::string &value) {
  const auto active = this->gear_names();
  for (size_t i = 0; i < active.size(); i++) {
    if (active[i] == value) {
      this->parent_->set_gear(static_cast<uint8_t>(i));
      return;
    }
  }
  // HA keeps offering the 5-gear labels it cached at setup even in 3-gear mode.
  for (size_t i = 0; i < NAMES_5.size(); i++) {
    if (NAMES_5[i] != value)
      continue;
    const uint8_t gear = gear_in_mode(static_cast<uint8_t>(i), this->gear_count_);
    ESP_LOGI(FIIDO_BMS_TAG, "value '%s' not valid in %u-gear mode - falling back to '%.*s'", value.c_str(),
             this->gear_count_, static_cast<int>(active[gear].size()), active[gear].data());
    this->parent_->set_gear(gear);
    return;
  }
  ESP_LOGW(FIIDO_BMS_TAG, "value '%s' is not valid in current gear mode (count=%u) - rejected", value.c_str(),
           this->gear_count_);
  auto opt = this->current_option();
  if (!opt.empty()) {
    this->publish_state(opt.c_str());
  }
}

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
