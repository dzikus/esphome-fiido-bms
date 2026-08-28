#include "fiido_mode_select.h"

#include "esphome/core/log.h"
#include "fiido_bms.h"

#ifdef USE_ESP32

namespace esphome {
namespace fiido_bms {

const std::vector<std::string> FiidoModeSelect::MODE_OPTIONS = {"3", "5"};

void FiidoModeSelect::control(const std::string &value) {
  for (const auto &opt : MODE_OPTIONS) {
    if (opt == value) {
      this->parent_->set_gear_mode(value == "3" ? 3 : 5);
      return;
    }
  }
  ESP_LOGW(FIIDO_BMS_TAG, "value '%s' not in options - rejected", value.c_str());
  auto opt = this->current_option();
  if (!opt.empty()) {
    this->publish_state(opt.c_str());
  }
}

}  // namespace fiido_bms
}  // namespace esphome

#endif  // USE_ESP32
