#pragma once

#ifdef USE_ESP32

#include <array>
#include <string>
#include <string_view>

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "fiido_bms.h"

namespace esphome::fiido_bms {

class FiidoSpeedLimitSelect : public select::Select, public Parented<FiidoBMSHub> {
 public:
  void control(const std::string &value) override;

  static constexpr std::array<std::string_view, 3> OPTIONS{"6 km/h", "25 km/h", "No limit"};
};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
