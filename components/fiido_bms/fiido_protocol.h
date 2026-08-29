#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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
  WRITE_L0 = 0xAA,
  WRITE_J0 = 0xFF,
};

// Register addresses. Poll targets and write targets share one space.
enum class Addr : uint8_t {
  STATS = 0x05,
  WATCH_PAIR = 0x09,
  HANDSHAKE = 0x0D,
  GEAR_RANGE = 0x25,
  GEAR = 0x26,
  FLAGS_27 = 0x27,
  FLAGS_28 = 0x28,
  FLAGS_2B = 0x2B,
  FLAGS_2C = 0x2C,
  SPEED_LIMIT = 0x3C,
  PAS_BOOST = 0x52,
  DISPLAY = 0x57,
  GUARD_TIME = 0x58,
  METER = 0x60,
  BATTERY = 0x7B,
  MOTOR = 0x96,
  CTRL = 0xAF,
  ENERGY = 0xC8,
  FLAGS_38 = 0x38,
  FLAGS_39 = 0x39,
};

// Constructing one from a bare byte asserts the register; nothing checks it.
template <Addr A>
struct RegValue {
  uint8_t raw;

  [[nodiscard]] constexpr bool operator==(const RegValue &) const = default;
};

template <Addr A>
struct RegBit {
  uint8_t mask;

  [[nodiscard]] constexpr bool in(RegValue<A> reg) const { return (reg.raw & this->mask) != 0; }
  [[nodiscard]] constexpr RegValue<A> with(RegValue<A> reg, bool on) const {
    return {static_cast<uint8_t>(on ? (reg.raw | this->mask) : (reg.raw & ~this->mask))};
  }
  [[nodiscard]] constexpr RegValue<A> keep(RegValue<A> reg) const {
    return {static_cast<uint8_t>(reg.raw & this->mask)};
  }
};

// A name ending in _OFF is set when the feature is off; decode_flags inverts it.
namespace flags_27 {
inline constexpr RegBit<Addr::FLAGS_27> INSENSITIVITY{0x01};
inline constexpr RegBit<Addr::FLAGS_27> START_MODE{0x02};
inline constexpr RegBit<Addr::FLAGS_27> LIGHT{0x08};
inline constexpr RegBit<Addr::FLAGS_27> SPEED_LIMIT{0x20};
inline constexpr RegBit<Addr::FLAGS_27> CRUISE{0x40};
inline constexpr RegBit<Addr::FLAGS_27> CONTROLLER{0x80};
}  // namespace flags_27

namespace flags_28 {
inline constexpr RegBit<Addr::FLAGS_28> SHOW_TOTAL_KM{0x40};
inline constexpr RegBit<Addr::FLAGS_28> SPEED_UNIT_MPH{0x80};
}  // namespace flags_28

namespace flags_2b {
inline constexpr RegBit<Addr::FLAGS_2B> THROTTLE_OFF{0x02};
inline constexpr RegBit<Addr::FLAGS_2B> DOUBLE_SPEED{0x20};
inline constexpr RegBit<Addr::FLAGS_2B> BIKE_GUARD{0x40};
}  // namespace flags_2b

namespace flags_2c {
inline constexpr RegBit<Addr::FLAGS_2C> KEY_SOUND_OFF{0x10};
inline constexpr RegBit<Addr::FLAGS_2C> SLOW_MODE{0x40};
inline constexpr RegBit<Addr::FLAGS_2C> PAS_LIMIT{0x80};
}  // namespace flags_2c

namespace flags_38 {
// Two bits, either of which silences the speaker. A write clears both or sets
// the lower one.
inline constexpr RegBit<Addr::FLAGS_38> SPEAKER_SILENT{0x0C};
inline constexpr uint8_t SPEAKER_SILENCE_PATTERN = 0x04;
}  // namespace flags_38

namespace flags_39 {
inline constexpr RegBit<Addr::FLAGS_39> RING{0x02};
inline constexpr RegBit<Addr::FLAGS_39> AUTO_SCREEN_OFF{0x08};
// Bits 7..5 are undefined and a write must send them as zero.
inline constexpr RegBit<Addr::FLAGS_39> DEFINED{0x1F};
}  // namespace flags_39

// STATS carries 0x2A, which is not cached and has no write path.
inline constexpr uint8_t ADDR_2A_BRAKE = 0x20;

enum class [[nodiscard]] WriteError : uint8_t {
  NONE = 0,
  NO_HANDLE,
  PAYLOAD_TOO_LONG,
  GATT_WRITE_FAILED,
};

struct MaskedWrite {
  FrameType type;
  uint8_t value;
};

// Bits outside mask keep their cached value. ADDR 0x39 defines only bits 4..0 and
// latches only under J0, so it is masked down and switched.
[[nodiscard]] MaskedWrite compute_masked_write(Addr addr, uint8_t cache, uint8_t mask, uint8_t new_bits);

// Poll table entry: addr/len pair.
struct PollDef {
  Addr addr;
  uint8_t len;
  const char *name;
};

static constexpr size_t POLL_FRAME_LEN = 6;  // [F][d][55][len][addr][crc]
static constexpr size_t NOTIFY_HDR_LEN = 5;  // [F][d][AA][len][addr]
static constexpr size_t NOTIFY_CRC_LEN = 1;
static constexpr size_t NOTIFY_OVERHEAD = NOTIFY_HDR_LEN + NOTIFY_CRC_LEN;  // 6

inline constexpr size_t WRITE_FRAME_OVERHEAD = 6;  // [F][d][type][len][addr] + crc
// The length byte allows 255; the largest real command is the 6-byte watch MAC.
inline constexpr size_t MAX_WRITE_PAYLOAD = 32;

// size 0 = refused.
struct WriteFrame {
  std::array<uint8_t, MAX_WRITE_PAYLOAD + WRITE_FRAME_OVERHEAD> bytes;
  size_t size;

  [[nodiscard]] bool empty() const { return size == 0; }
  [[nodiscard]] std::span<const uint8_t> span() const { return {bytes.data(), size}; }
};

[[nodiscard]] uint8_t compute_crc(std::span<const uint8_t> data);

// [F][d][55][len][addr][crc]
[[nodiscard]] std::array<uint8_t, POLL_FRAME_LEN> build_poll_frame(Addr addr, uint8_t len);

// [F][d][type][payload_len][addr][...payload][crc]. Empty = refused.
[[nodiscard]] WriteFrame build_write_frame(FrameType type, Addr addr, std::span<const uint8_t> payload);

// payload views the caller's frame; empty unless valid.
struct NotifyView {
  bool valid;
  Addr addr;
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
inline constexpr auto POLL_TABLE = std::to_array<PollDef>({
    {.addr = Addr::BATTERY, .len = 13, .name = "BATTERY"},
    {.addr = Addr::CTRL, .len = 12, .name = "CTRL"},
    {.addr = Addr::MOTOR, .len = 12, .name = "MOTOR"},
    {.addr = Addr::ENERGY, .len = 12, .name = "ENERGY"},
    {.addr = Addr::STATS, .len = 53, .name = "STATS"},
    {.addr = Addr::METER, .len = 13, .name = "METER"},
    // Speed limit value (read-only baseline, log-only). Frame: 46 64 55 01 3C 4A.
    {.addr = Addr::SPEED_LIMIT, .len = 1, .name = "SPEEDLIM"},
    // Boost level read-back (1 byte). Frame: 46 64 55 01 52 24.
    {.addr = Addr::PAS_BOOST, .len = 1, .name = "BOOST"},
    // Display block read-back: 0x57 brightness + 0x58 guard time (2 bytes).
    // Frame: 46 64 55 02 57 22.
    {.addr = Addr::DISPLAY, .len = 2, .name = "DISPLAY"},
});
inline constexpr size_t POLL_TABLE_SIZE = POLL_TABLE.size();

struct PollCursor {
  size_t index;
  size_t remaining;
};

// Skipping a poll spends a slot, so a burst still ends after POLL_TABLE_SIZE steps.
[[nodiscard]] PollCursor skip_disabled_polls(PollCursor cursor, std::span<const bool, POLL_TABLE_SIZE> enabled);

// Where the burst stands after one send attempt.
struct BurstStep {
  bool retry;  // resend the same index
  uint8_t retry_count;
  PollCursor cursor;
};

[[nodiscard]] BurstStep advance_burst(PollCursor cursor, uint8_t retry_count, uint8_t max_retries, bool send_ok);

[[nodiscard]] bool should_retry_send(uint8_t retry_count, uint8_t max_retries, bool send_ok);

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

namespace battery {
inline constexpr size_t PAYLOAD_LEN = 13;
inline constexpr size_t HW_VERSION = 0;
inline constexpr size_t SW_VERSION = 1;
inline constexpr size_t CAPACITY_AH = 2;        // 2B BE, tenths
inline constexpr size_t VOLTAGE_V = 4;          // 2B BE, tenths
inline constexpr size_t CURRENT_VOLTAGE_V = 7;  // 2B BE, tenths
inline constexpr size_t CURRENT_A = 9;          // 2B BE, tenths
inline constexpr size_t MANUFACTURER = 12;
}  // namespace battery

namespace ctrl {
inline constexpr size_t PAYLOAD_LEN = 12;
inline constexpr size_t HW_VERSION = 0;
inline constexpr size_t SW_VERSION = 1;
inline constexpr size_t UPPER_VOLTAGE_V = 2;  // 2B BE, tenths
inline constexpr size_t LOWER_VOLTAGE_V = 4;  // 2B BE, tenths
inline constexpr size_t CURRENT_A = 7;        // 2B BE, tenths
inline constexpr size_t TEMPERATURE_C = 9;
inline constexpr size_t VERSION = 10;
inline constexpr size_t MANUFACTURER = 11;
}  // namespace ctrl

namespace motor {
inline constexpr size_t PAYLOAD_LEN = 12;
inline constexpr size_t VERSION = 0;
inline constexpr size_t MAGNETIC = 1;
inline constexpr size_t WIRE_COUNT = 2;
inline constexpr size_t STEEL_COUNT = 3;
inline constexpr size_t REDUCTION_RATIO = 4;    // tenths
inline constexpr size_t WHEEL_DIAMETER_IN = 5;  // 2B BE, tenths
inline constexpr size_t TEMPERATURE_C = 7;      // 2B BE, signed
inline constexpr size_t CAPACITY_W = 9;         // 2B BE
inline constexpr int16_t MIN_TEMPERATURE_C = -40;
inline constexpr int16_t MAX_TEMPERATURE_C = 125;
}  // namespace motor

namespace energy {
inline constexpr size_t PAYLOAD_LEN = 12;
inline constexpr size_t CRANK_TORQUE_NM = 0;  // 2B BE, tenths
inline constexpr size_t CRANK_RPM = 2;        // 2B BE
inline constexpr size_t THIS_TAKE_WH = 4;     // 2B BE, tenths
inline constexpr size_t TOTAL_TAKE_WH = 6;    // 4B BE, tenths
inline constexpr size_t STARTUP_TIME_S = 10;  // 2B BE
}  // namespace energy

namespace meter {
inline constexpr size_t PAYLOAD_LEN = 13;
inline constexpr size_t HW_VERSION = 0;
inline constexpr size_t SW_VERSION = 1;
inline constexpr size_t MODE_DATA = 7;
}  // namespace meter

namespace speed_limit {
inline constexpr size_t PAYLOAD_LEN = 1;
inline constexpr size_t VALUE_KMH = 0;
inline constexpr uint8_t NO_LIMIT = 100;
}  // namespace speed_limit

namespace pas_boost {
inline constexpr size_t PAYLOAD_LEN = 1;
inline constexpr size_t LEVEL = 0;
}  // namespace pas_boost

namespace display {
inline constexpr size_t PAYLOAD_LEN = 2;
inline constexpr size_t BRIGHTNESS = 0;
inline constexpr size_t GUARD_TIME = 1;
}  // namespace display

// Not defined: -fno-exceptions rules out throw.
size_t address_not_in_table();

consteval size_t poll_index(Addr addr) {
  for (size_t i = 0; i < POLL_TABLE.size(); i++) {
    if (POLL_TABLE[i].addr == addr)
      return i;
  }
  return address_not_in_table();
}

// No yaml option turns these two off.
consteval std::array<bool, POLL_TABLE_SIZE> default_poll_enables() {
  std::array<bool, POLL_TABLE_SIZE> out{};
  out[poll_index(Addr::STATS)] = true;
  out[poll_index(Addr::SPEED_LIMIT)] = true;
  return out;
}

// Poll length against what the parser reads.
consteval uint8_t poll_len(Addr addr) {
  for (const PollDef &poll : POLL_TABLE) {
    if (poll.addr == addr)
      return poll.len;
  }
  return static_cast<uint8_t>(address_not_in_table());
}
static_assert(poll_len(Addr::BATTERY) == battery::PAYLOAD_LEN);
static_assert(poll_len(Addr::CTRL) == ctrl::PAYLOAD_LEN);
static_assert(poll_len(Addr::MOTOR) == motor::PAYLOAD_LEN);
static_assert(poll_len(Addr::ENERGY) == energy::PAYLOAD_LEN);
static_assert(poll_len(Addr::METER) == meter::PAYLOAD_LEN);
static_assert(poll_len(Addr::SPEED_LIMIT) == speed_limit::PAYLOAD_LEN);
static_assert(poll_len(Addr::PAS_BOOST) == pas_boost::PAYLOAD_LEN);
static_assert(poll_len(Addr::DISPLAY) == display::PAYLOAD_LEN);
static_assert(poll_len(Addr::STATS) == stats::PAYLOAD_LEN);

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
  RegValue<Addr::GEAR_RANGE> b25;
  RegValue<Addr::FLAGS_27> b27;
  RegValue<Addr::FLAGS_28> b28;
  RegValue<Addr::FLAGS_2B> b2b;
  RegValue<Addr::FLAGS_2C> b2c;
  RegValue<Addr::FLAGS_38> b38;
  RegValue<Addr::FLAGS_39> b39;
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

[[nodiscard]] FlagView decode_flags(RegValue<Addr::FLAGS_27> b27, RegValue<Addr::FLAGS_28> b28,
                                    RegValue<Addr::FLAGS_2B> b2b, RegValue<Addr::FLAGS_2C> b2c,
                                    RegValue<Addr::FLAGS_38> b38, RegValue<Addr::FLAGS_39> b39);

}  // namespace esphome::fiido_bms
