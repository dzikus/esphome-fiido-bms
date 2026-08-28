#pragma once

#ifdef USE_ESP32

#include <string>
#include <vector>

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "fiido_bms.h"

namespace esphome::fiido_bms {

class FiidoGearSelect : public select::Select, public Parented<FiidoBMSHub> {
 public:
  void control(const std::string &value) override;

  void set_gear_count(uint8_t count);
  uint8_t get_gear_count() const { return this->gear_count_; }
  // Set when codegen registered only the 3-gear options. Runtime detection must
  // then leave the count alone or gear_names() yields unregistered labels.
  void set_gear_count_pinned(bool pinned) { this->gear_count_pinned_ = pinned; }
  bool gear_count_pinned() const { return this->gear_count_pinned_; }
  void set_names_3(std::vector<std::string> n) { this->names_3_ = std::move(n); }
  void set_names_5(std::vector<std::string> n) { this->names_5_ = std::move(n); }
  const std::vector<std::string> &gear_names() const {
    return (this->gear_count_ == 3) ? this->names_3_ : this->names_5_;
  }

 protected:
  uint8_t gear_count_{5};
  bool gear_count_pinned_{false};
  std::vector<std::string> names_3_;
  std::vector<std::string> names_5_;
};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
