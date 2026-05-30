#include "fiido_speed_unit_select.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome {
namespace fiido_bms {

const std::vector<std::string> FiidoSpeedUnitSelect::OPTIONS = {"km/h", "mph"};

void FiidoSpeedUnitSelect::control(const std::string &value) {
  for (const auto &opt : OPTIONS) {
    if (opt == value) {
      this->parent_->set_speed_unit(value);
      return;
    }
  }
  ESP_LOGW("fiido_speed_unit_select", "value '%s' not in options - rejected",
           value.c_str());
  auto opt = this->current_option();
  if (!opt.empty()) {
    this->publish_state(opt.c_str());
  }
}

}  // namespace fiido_bms
}  // namespace esphome

#endif  // USE_ESP32
