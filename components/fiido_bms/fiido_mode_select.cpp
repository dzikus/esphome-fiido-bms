#include "fiido_mode_select.h"

#ifdef USE_ESP32

namespace esphome {
namespace fiido_bms {

const std::vector<std::string> FiidoModeSelect::MODE_OPTIONS = {"3", "5"};

void FiidoModeSelect::control(const std::string &value) {
  uint8_t mode = (value == "3") ? 3 : 5;
  this->parent_->set_gear_mode(mode);
}

}  // namespace fiido_bms
}  // namespace esphome

#endif  // USE_ESP32
