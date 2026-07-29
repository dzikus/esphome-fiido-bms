#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace fiido_bms {

// Header bytes
static constexpr uint8_t FIIDO_SIG_0 = 0x46;  // 'F'
static constexpr uint8_t FIIDO_SIG_1 = 0x64;  // 'd'
static constexpr uint8_t FRAME_TYPE_POLL = 0x55;
static constexpr uint8_t FRAME_TYPE_NOTIFY = 0xAA;
static constexpr uint8_t FRAME_TYPE_WRITE_J0 = 0xFF;
static constexpr uint8_t FRAME_TYPE_WRITE_L0 = 0xAA;

// Poll table entry: addr/len pair.
struct PollDef {
  uint8_t addr;
  uint8_t len;
  const char *name;
};

static constexpr size_t POLL_FRAME_LEN = 6;     // [F][d][55][len][addr][crc]
static constexpr size_t NOTIFY_HDR_LEN = 5;     // [F][d][AA][len][addr]
static constexpr size_t NOTIFY_CRC_LEN = 1;
static constexpr size_t NOTIFY_OVERHEAD = NOTIFY_HDR_LEN + NOTIFY_CRC_LEN;  // 6

// Pure decode helpers
uint8_t compute_crc(const uint8_t *buf, size_t len);

// Build poll frame: [F][d][55][len][addr][crc]. out must be POLL_FRAME_LEN bytes.
void build_poll_frame(uint8_t addr, uint8_t len, uint8_t *out);

// Build write frame: [F][d][type][payload_len][addr][...payload][crc].
// type = FRAME_TYPE_WRITE_J0 (0xFF) or FRAME_TYPE_WRITE_L0 (0xAA).
// out must have capacity >= (6 + payload_len). Returns total length written.
size_t build_write_frame(uint8_t type, uint8_t addr, const uint8_t *payload,
                         uint8_t payload_len, uint8_t *out);

// Validate notify frame: header + CRC. Returns true if frame is well-formed.
// Sets *payload_addr to ADDR byte, *payload_len to payload length (= len - NOTIFY_OVERHEAD).
bool validate_notify(const uint8_t *buf, size_t len, uint8_t *out_addr, size_t *out_payload_len);

// Endianness helpers
inline uint16_t u16be(const uint8_t *buf, size_t off) {
  return (uint16_t(buf[off]) << 8) | uint16_t(buf[off + 1]);
}

inline uint32_t u32be(const uint8_t *buf, size_t off) {
  return (uint32_t(buf[off]) << 24) | (uint32_t(buf[off + 1]) << 16) |
         (uint32_t(buf[off + 2]) << 8) | uint32_t(buf[off + 3]);
}

// POLL_TABLE: rotated by the hub.
extern const PollDef POLL_TABLE[];
extern const size_t POLL_TABLE_SIZE;

// STATS poll payload byte offsets (covers ADDR 0x05..0x39).
// Frame layout: [F][d][AA][len][addr=0x05][...payload...][crc]
// payload starts at index 0; STATS ADDR_XX corresponds to payload[XX - 0x05 + 0].
// Hub strips frame header before calling parse_stats_(p, len), so p is the
// 53-byte payload that starts at ADDR 0x05.
namespace stats {
inline constexpr size_t ADDR_24_OFFSET = 31;  // SOC %
inline constexpr size_t ADDR_25_OFFSET = 32;  // gear range / mode encoding
inline constexpr size_t ADDR_26_OFFSET = 33;  // current gear
inline constexpr size_t ADDR_27_OFFSET = 34;  // motor/light/cruise/speed_limit flags
inline constexpr size_t ADDR_28_OFFSET = 35;  // speed unit / display flags
inline constexpr size_t ADDR_2A_OFFSET = 37;  // brake bit 5
inline constexpr size_t ADDR_2B_OFFSET = 38;  // throttle / pairing / guard flags
inline constexpr size_t ADDR_2C_OFFSET = 39;  // key_sound / slow_mode / pas_limit / gear_way flags
inline constexpr size_t ADDR_38_OFFSET = 51;  // speaker / switchStatus / CAN protocol flags
inline constexpr size_t ADDR_39_OFFSET = 52;  // auto_screen_off / ring / lock flags
}  // namespace stats

}  // namespace fiido_bms
}  // namespace esphome
