#include "fiido_link.h"

#ifdef USE_ESP32

#include <esp_gattc_api.h>

#include "esphome/core/log.h"

namespace esphome::fiido_bms {

static const char *const TAG = "fiido_bms";

// Custom Fiido service (FFE0 base): write to FFE2, notify from FFE1.
static const auto SERVICE_UUID = esp32_ble_tracker::ESPBTUUID::from_raw("00010203-0405-0607-0809-0a0b0c0dffe0");
static const auto NOTIFY_CHAR_UUID = esp32_ble_tracker::ESPBTUUID::from_raw("00010203-0405-0607-0809-0a0b0c0dffe1");
static const auto WRITE_CHAR_UUID = esp32_ble_tracker::ESPBTUUID::from_raw("00010203-0405-0607-0809-0a0b0c0dffe2");

bool FiidoLink::resolve(ble_client::BLEClient *parent) {
  auto *notify_chr = parent->get_characteristic(SERVICE_UUID, NOTIFY_CHAR_UUID);
  auto *write_chr = parent->get_characteristic(SERVICE_UUID, WRITE_CHAR_UUID);
  if (notify_chr == nullptr || write_chr == nullptr)
    return false;
  this->notify_handle_ = notify_chr->handle;
  this->write_handle_ = write_chr->handle;
  return true;
}

void FiidoLink::subscribe(ble_client::BLEClient *parent) const {
  const auto status =
      esp_ble_gattc_register_for_notify(parent->get_gattc_if(), parent->get_remote_bda(), this->notify_handle_);
  if (status) {
    ESP_LOGW(TAG, "register_for_notify failed, status=%d", status);
  }
}

void FiidoLink::unsubscribe(ble_client::BLEClient *parent) const {
  if (this->notify_handle_ == 0)
    return;
  const auto status =
      esp_ble_gattc_unregister_for_notify(parent->get_gattc_if(), parent->get_remote_bda(), this->notify_handle_);
  if (status) {
    ESP_LOGW(TAG, "[%s] unregister_for_notify failed, status=%d", parent->address_str(), status);
  }
}

void FiidoLink::reset() {
  this->write_handle_ = 0;
  this->notify_handle_ = 0;
  this->congested_ = false;
}

WriteError FiidoLink::send(ble_client::BLEClient *parent, std::span<const uint8_t> frame) const {
  if (this->write_handle_ == 0)
    return WriteError::NO_HANDLE;
  const auto status =
      esp_ble_gattc_write_char(parent->get_gattc_if(), parent->get_conn_id(), this->write_handle_, frame.size(),
                               const_cast<uint8_t *>(frame.data()), ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  return status ? WriteError::GATT_WRITE_FAILED : WriteError::NONE;
}

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
