#include "fiido_protocol.h"

namespace esphome {
namespace fiido_bms {

// CRC is the XOR of all preceding bytes, confirmed against a live BMS.
const PollDef POLL_TABLE[] = {
    {0x7B, 13, "BATTERY"},
    {0xAF, 12, "CTRL"},
    {0x96, 12, "MOTOR"},
    {0xC8, 12, "ENERGY"},
    {0x05, 53, "STATS"},
    {0x60, 13, "METER"},
    // Speed limit value (read-only baseline, log-only). Frame: 46 64 55 01 3C 4A.
    {0x3C, 1, "SPEEDLIM"},
};
const size_t POLL_TABLE_SIZE = sizeof(POLL_TABLE) / sizeof(POLL_TABLE[0]);

uint8_t compute_crc(const uint8_t *buf, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) crc ^= buf[i];
  return crc;
}

void build_poll_frame(uint8_t addr, uint8_t len, uint8_t *out) {
  out[0] = FIIDO_SIG_0;
  out[1] = FIIDO_SIG_1;
  out[2] = FRAME_TYPE_POLL;
  out[3] = len;
  out[4] = addr;
  out[5] = compute_crc(out, 5);
}

size_t build_write_frame(uint8_t type, uint8_t addr, const uint8_t *payload,
                         uint8_t payload_len, uint8_t *out) {
  out[0] = FIIDO_SIG_0;
  out[1] = FIIDO_SIG_1;
  out[2] = type;
  out[3] = payload_len;
  out[4] = addr;
  for (uint8_t i = 0; i < payload_len; i++) out[5 + i] = payload[i];
  size_t crc_off = 5 + payload_len;
  out[crc_off] = compute_crc(out, crc_off);
  return crc_off + 1;
}

bool validate_notify(const uint8_t *buf, size_t len, uint8_t *out_addr, size_t *out_payload_len) {
  if (len < NOTIFY_OVERHEAD) return false;
  if (buf[0] != FIIDO_SIG_0 || buf[1] != FIIDO_SIG_1) return false;
  if (buf[2] != FRAME_TYPE_NOTIFY) return false;
  uint8_t declared_len = buf[3];
  // Frame total = 5 header bytes + declared_len payload + 1 CRC
  if (size_t(declared_len) + NOTIFY_OVERHEAD != len) return false;
  uint8_t expected_crc = compute_crc(buf, len - 1);
  if (buf[len - 1] != expected_crc) return false;
  *out_addr = buf[4];
  *out_payload_len = declared_len;
  return true;
}

}  // namespace fiido_bms
}  // namespace esphome
