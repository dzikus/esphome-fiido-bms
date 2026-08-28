#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace esphome::fiido_bms {

// Header bytes
static constexpr uint8_t FIIDO_SIG_0 = 0x46;  // 'F'
static constexpr uint8_t FIIDO_SIG_1 = 0x64;  // 'd'
static constexpr uint8_t FRAME_TYPE_POLL = 0x55;
static constexpr uint8_t FRAME_TYPE_NOTIFY = 0xAA;

// Some addresses latch only under one of these, and a write under the other is
// acknowledged and dropped, so mixing them up must not compile. Shares its value
// with FRAME_TYPE_NOTIFY, which means something else.
enum class FrameType : uint8_t {
  WriteL0 = 0xAA,
  WriteJ0 = 0xFF,
};

struct MaskedWrite {
  FrameType type;
  uint8_t value;
};

// Bits outside mask keep their cached value. ADDR 0x39 defines only bits 4..0 and
// latches only under J0, so it is masked down and switched.
[[nodiscard]] MaskedWrite compute_masked_write(uint8_t addr, uint8_t cache, uint8_t mask, uint8_t new_bits);

// Poll table entry: addr/len pair.
struct PollDef {
  uint8_t addr;
  uint8_t len;
  const char *name;
};

static constexpr size_t POLL_FRAME_LEN = 6;  // [F][d][55][len][addr][crc]
static constexpr size_t NOTIFY_HDR_LEN = 5;  // [F][d][AA][len][addr]
static constexpr size_t NOTIFY_CRC_LEN = 1;
static constexpr size_t NOTIFY_OVERHEAD = NOTIFY_HDR_LEN + NOTIFY_CRC_LEN;  // 6

// The length byte caps the payload at 0xFF.
inline constexpr size_t WRITE_FRAME_OVERHEAD = 6;  // [F][d][type][len][addr] + crc
inline constexpr size_t MAX_WRITE_PAYLOAD = 0xFF;

[[nodiscard]] uint8_t compute_crc(std::span<const uint8_t> data);

// [F][d][55][len][addr][crc]
[[nodiscard]] std::array<uint8_t, POLL_FRAME_LEN> build_poll_frame(uint8_t addr, uint8_t len);

// [F][d][type][payload_len][addr][...payload][crc]. Empty = refused.
[[nodiscard]] std::vector<uint8_t> build_write_frame(FrameType type, uint8_t addr, std::span<const uint8_t> payload);

// payload views the caller's frame; empty unless valid.
struct NotifyView {
  bool valid;
  uint8_t addr;
  std::span<const uint8_t> payload;
};

// Checks header, declared length and CRC.
[[nodiscard]] NotifyView validate_notify(std::span<const uint8_t> frame);

// Returns 0 past the end instead of reading it.
[[nodiscard]] inline uint16_t u16be(std::span<const uint8_t> buf, size_t off) {
  if (off + 2 > buf.size())
    return 0;
  return (uint16_t(buf[off]) << 8) | uint16_t(buf[off + 1]);
}

[[nodiscard]] inline uint32_t u32be(std::span<const uint8_t> buf, size_t off) {
  if (off + 4 > buf.size())
    return 0;
  return (uint32_t(buf[off]) << 24) | (uint32_t(buf[off + 1]) << 16) | (uint32_t(buf[off + 2]) << 8) |
         uint32_t(buf[off + 3]);
}

// Rotated by the hub. POLL_TABLE_SIZE is the table's own size().
inline constexpr std::array<PollDef, 9> POLL_TABLE = {{
    {0x7B, 13, "BATTERY"},
    {0xAF, 12, "CTRL"},
    {0x96, 12, "MOTOR"},
    {0xC8, 12, "ENERGY"},
    {0x05, 53, "STATS"},
    {0x60, 13, "METER"},
    // Speed limit value (read-only baseline, log-only). Frame: 46 64 55 01 3C 4A.
    {0x3C, 1, "SPEEDLIM"},
    // Boost level read-back (1 byte). Frame: 46 64 55 01 52 24.
    {0x52, 1, "BOOST"},
    // Display block read-back: 0x57 brightness + 0x58 guard time (2 bytes).
    // Frame: 46 64 55 02 57 22.
    {0x57, 2, "DISPLAY"},
}};
inline constexpr size_t POLL_TABLE_SIZE = POLL_TABLE.size();

struct PollCursor {
  size_t index;
  size_t remaining;
};

// Skipping a poll spends a slot, so a burst still ends after POLL_TABLE_SIZE steps.
[[nodiscard]] PollCursor skip_disabled_polls(PollCursor cursor, std::span<const bool, POLL_TABLE_SIZE> enabled);

struct BurstGate {
  bool start;
  uint32_t slot;
};

// started rather than last_ms == 0: millis() really does return 0, once per boot
// and once per wrap.
struct BurstState {
  uint32_t last_ms;
  uint32_t last_slot;
  bool started;
};

[[nodiscard]] BurstGate evaluate_burst_gate(uint32_t now, uint32_t interval, uint32_t phase, BurstState state,
                                            bool forced);

// STATS poll payload byte offsets (covers ADDR 0x05..0x39).
// Frame layout: [F][d][AA][len][addr=0x05][...payload...][crc]
// payload starts at index 0; STATS ADDR_XX corresponds to payload[XX - 0x05 + 0].
namespace stats {
inline constexpr size_t PAYLOAD_LEN = 53;
// These three predate the ADDR_ naming and do not follow the ADDR - 0x05 rule.
// Kept as measured against a live BMS; the fixtures agree.
inline constexpr size_t TOTAL_KM_OFFSET = 23;  // 4B BE, tenths of a km
inline constexpr size_t TRIP_KM_OFFSET = 27;   // 2B BE, tenths of a km
inline constexpr size_t SPEED_OFFSET = 29;     // 2B BE, tenths of a km/h
inline constexpr size_t ADDR_24_OFFSET = 31;   // SOC %
inline constexpr size_t ADDR_25_OFFSET = 32;   // gear range / mode encoding
inline constexpr size_t ADDR_26_OFFSET = 33;   // current gear
inline constexpr size_t ADDR_27_OFFSET = 34;   // motor/light/cruise/speed_limit flags
inline constexpr size_t ADDR_28_OFFSET = 35;   // speed unit / display flags
inline constexpr size_t ADDR_2A_OFFSET = 37;   // brake bit 5
inline constexpr size_t ADDR_2B_OFFSET = 38;   // throttle / pairing / guard flags
inline constexpr size_t ADDR_2C_OFFSET = 39;   // key_sound / slow_mode / pas_limit / gear_way flags
inline constexpr size_t ADDR_38_OFFSET = 51;   // speaker / switchStatus / CAN protocol flags
inline constexpr size_t ADDR_39_OFFSET = 52;   // auto_screen_off / ring / lock flags
// A XOR checksum lets a corrupted frame validate, and total_kilometers feeds a
// total_increasing sensor where one bogus sample sticks in long-term statistics.
// Samples outside these bounds are dropped and the last published value stays.
inline constexpr float MAX_TOTAL_KM = 200000.0f;
inline constexpr float MAX_TRIP_KM = 1000.0f;
inline constexpr float MAX_SPEED_KMH = 100.0f;
inline constexpr uint8_t MAX_SOC_PCT = 100;
}  // namespace stats

// *_ok false means the value failed its bound and the caller keeps the previous
// one. Flag bytes stay raw so the caller can cache them and decode bits from the
// cache, which a pending write may have moved on from.
struct StatsView {
  bool valid;
  float total_km;
  bool total_km_ok;
  float trip_km;
  bool trip_km_ok;
  float speed_kmh;
  bool speed_ok;
  uint8_t soc_pct;
  bool soc_ok;
  uint8_t gear;
  uint8_t gear_start;
  uint8_t max_gear;  // 3 or 5 when the nibble pair is meaningful, else 0
  bool brake;
  uint8_t b25, b27, b28, b2B, b2C, b38, b39;
};

[[nodiscard]] StatsView decode_stats(std::span<const uint8_t> payload);

// Two of these are inverted on the wire; see decode_flags.
struct FlagView {
  bool motor_on;
  bool light_on;
  bool speed_limit_on;
  bool cruise_on;
  bool start_mode_on;
  bool insensitivity_on;
  bool speed_unit_mph;
  bool show_total_km_on;
  bool throttle_on;
  bool double_speed_on;
  bool bike_guard_on;
  bool key_sound_on;
  bool slow_mode_on;
  bool pas_limit_on;
  bool speaker_audible;
  bool auto_screen_off_on;
  bool ring_on;
};

[[nodiscard]] FlagView decode_flags(uint8_t b27, uint8_t b28, uint8_t b2B, uint8_t b2C, uint8_t b38, uint8_t b39);

}  // namespace esphome::fiido_bms
