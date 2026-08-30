#include "fiido_speed_limit_select.h"

#include <algorithm>

#include "esphome/core/log.h"
#include "fiido_bms.h"

#ifdef USE_ESP32

namespace esphome::fiido_bms {

void FiidoSpeedLimitSelect::control(const std::string &value) {
  if (std::ranges::find(OPTIONS, value) != OPTIONS.end()) {
    this->parent_->set_speed_limit(value);
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
