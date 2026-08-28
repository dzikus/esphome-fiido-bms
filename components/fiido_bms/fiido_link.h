#pragma once

#ifdef USE_ESP32

#include <cstdint>
#include <span>

#include "esphome/components/ble_client/ble_client.h"
#include "fiido_protocol.h"

namespace esphome::fiido_bms {

// The GATT handles and every esp_ble_gattc_ call the hub needs.
class FiidoLink {
 public:
  // False when FFE1/FFE2 are missing, which means this is not a Fiido BMS.
  [[nodiscard]] bool resolve(ble_client::BLEClient *parent);
  void subscribe(ble_client::BLEClient *parent) const;
  void unsubscribe(ble_client::BLEClient *parent) const;
  void reset();

  [[nodiscard]] WriteError send(ble_client::BLEClient *parent, std::span<const uint8_t> frame) const;

  [[nodiscard]] bool ready() const { return this->write_handle_ != 0; }
  [[nodiscard]] bool owns_notify(uint16_t handle) const { return handle == this->notify_handle_ && handle != 0; }
  [[nodiscard]] uint16_t write_handle() const { return this->write_handle_; }
  [[nodiscard]] uint16_t notify_handle() const { return this->notify_handle_; }

  [[nodiscard]] bool congested() const { return this->congested_; }
  void set_congested(bool congested) { this->congested_ = congested; }

 private:
  uint16_t write_handle_{0};
  uint16_t notify_handle_{0};
  bool congested_{false};
};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
