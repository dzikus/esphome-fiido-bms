#pragma once

#ifdef USE_ESP32

#include <array>
#include <span>
#include <string>
#include <string_view>

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "fiido_bms.h"

namespace esphome::fiido_bms {

class FiidoGearSelect : public select::Select, public Parented<FiidoBMSHub> {
 public:
  static constexpr std::array<std::string_view, 4> NAMES_3{"OFF", "eco", "sport", "turbo"};
  static constexpr std::array<std::string_view, 6> NAMES_5{"OFF", "eco", "normal", "sport", "turbo", "turbo+"};

  void control(const std::string &value) override;

  void set_gear_count(uint8_t count);
  uint8_t get_gear_count() const { return this->gear_count_; }
  // Set when codegen registered only the 3-gear options. Runtime detection must
  // then leave the count alone or gear_names() yields unregistered labels.
  void set_gear_count_pinned(bool pinned) { this->gear_count_pinned_ = pinned; }
  bool gear_count_pinned() const { return this->gear_count_pinned_; }
  [[nodiscard]] std::span<const std::string_view> gear_names() const {
    if (this->gear_count_ == 3)
      return NAMES_3;
    return NAMES_5;
  }

 protected:
  uint8_t gear_count_{5};
  bool gear_count_pinned_{false};
};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
