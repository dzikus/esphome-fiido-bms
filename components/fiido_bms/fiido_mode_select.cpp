#include "fiido_mode_select.h"

#include <algorithm>

#include "esphome/core/log.h"
#include "fiido_bms.h"

#ifdef USE_ESP32

namespace esphome::fiido_bms {

void FiidoModeSelect::control(const std::string &value) {
  if (std::ranges::find(MODE_OPTIONS, value) != MODE_OPTIONS.end()) {
    this->parent_->set_gear_mode(value == "3" ? 3 : 5);
    return;
  }
  ESP_LOGW(FIIDO_BMS_TAG, "value '%s' not in options - rejected", value.c_str());
  auto opt = this->current_option();
  if (!opt.empty()) {
    this->publish_state(opt.c_str());
  }
}

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
