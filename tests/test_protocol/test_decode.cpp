#include <unity.h>

#include "fiido_protocol.h"
#include "fiido_state.h"
#include "fixtures.h"
#include "test_groups.h"

using namespace esphome::fiido_bms;

static void test_decode_stats_rejects_wrong_length() {
  const uint8_t short_payload[4] = {0, 0, 0, 0};
  TEST_ASSERT_FALSE(decode_stats(short_payload).valid);
}

static void test_decode_stats_reads_the_captured_frame() {
  // Ties the offset constants to a captured payload.
  const StatsView v = decode_stats(std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN, 53));
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_EQUAL_UINT8(90, v.soc_pct);
  TEST_ASSERT_EQUAL_UINT8(1, v.gear);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.8f, v.total_km);
}

static void test_decode_stats_masks_0x39_to_low_bits() {
  uint8_t p[53] = {0};
  p[stats::ADDR_39_OFFSET] = 0xE8;
  TEST_ASSERT_EQUAL_UINT8(0x08, decode_stats(p).b39.raw);
}

static void test_decode_stats_gear_count_from_nibble_pair() {
  // Read as a plain number the byte never equals 3 or 5.
  uint8_t p[53] = {0};
  p[stats::ADDR_25_OFFSET] = 0x30;
  TEST_ASSERT_EQUAL_UINT8(3, decode_stats(p).max_gear);
  p[stats::ADDR_25_OFFSET] = 0x53;
  TEST_ASSERT_EQUAL_UINT8(5, decode_stats(p).max_gear);
}

static void test_decode_stats_rejects_a_nibble_pair_that_means_nothing() {
  uint8_t p[53] = {0};
  p[stats::ADDR_25_OFFSET] = 0x77;
  TEST_ASSERT_EQUAL_UINT8(0, decode_stats(p).max_gear);
}

static void test_decode_stats_bounds_reject_impossible_values() {
  // total_kilometers feeds a total_increasing sensor, where one bad sample sticks
  // in long-term statistics for good.
  uint8_t p[53] = {0};
  p[stats::TOTAL_KM_OFFSET] = 0xFF;
  p[stats::TOTAL_KM_OFFSET + 1] = 0xFF;
  p[stats::TOTAL_KM_OFFSET + 2] = 0xFF;
  p[stats::TOTAL_KM_OFFSET + 3] = 0xFF;
  p[stats::ADDR_24_OFFSET] = 200;
  const StatsView v = decode_stats(p);
  TEST_ASSERT_FALSE(v.total_km_ok);
  TEST_ASSERT_FALSE(v.soc_ok);
  TEST_ASSERT_TRUE(v.speed_ok);
}

static void test_decode_flags_light_needs_the_controller_on() {
  // Bit 3 can be set with the controller off; the lamp is not lit then.
  TEST_ASSERT_FALSE(decode_flags({0x08}, {}, {}, {}, {}, {}).light_on);
  TEST_ASSERT_TRUE(decode_flags({0x88}, {}, {}, {}, {}, {}).light_on);
}

static void test_decode_flags_inverted_bits() {
  // throttle and key sound read the opposite way round.
  TEST_ASSERT_TRUE(decode_flags({}, {}, {0x00}, {0x00}, {}, {}).throttle_on);
  TEST_ASSERT_FALSE(decode_flags({}, {}, {0x02}, {0x00}, {}, {}).throttle_on);
  TEST_ASSERT_TRUE(decode_flags({}, {}, {}, {0x00}, {}, {}).key_sound_on);
  TEST_ASSERT_FALSE(decode_flags({}, {}, {}, {0x10}, {}, {}).key_sound_on);
}

static void test_decode_flags_speaker_is_audible_only_on_zero() {
  TEST_ASSERT_TRUE(decode_flags({}, {}, {}, {}, {0x00}, {}).speaker_audible);
  TEST_ASSERT_FALSE(decode_flags({}, {}, {}, {}, {0x04}, {}).speaker_audible);
  TEST_ASSERT_FALSE(decode_flags({}, {}, {}, {}, {0x0C}, {}).speaker_audible);
}

// The register each mask belongs to is now a compile-time fact; this covers the
// bit position within the byte.
static void test_decode_flags_each_bit_reads_its_own_byte() {
  TEST_ASSERT_TRUE(decode_flags({0x40}, {}, {}, {}, {}, {}).cruise_on);
  TEST_ASSERT_TRUE(decode_flags({}, {0x40}, {}, {}, {}, {}).show_total_km_on);
  TEST_ASSERT_TRUE(decode_flags({}, {}, {0x40}, {}, {}, {}).bike_guard_on);
  TEST_ASSERT_TRUE(decode_flags({}, {}, {}, {0x80}, {}, {}).pas_limit_on);
  TEST_ASSERT_TRUE(decode_flags({}, {}, {}, {}, {}, {0x02}).ring_on);
}

static const StatsSample *find_sample(const StatsSamples &s, StatsChannel c) {
  for (const auto &item : s)
    if (item.channel == c)
      return &item;
  return nullptr;
}

// Fails the test instead of dereferencing a missing channel.
static float sample_value(const StatsSamples &s, StatsChannel c) {
  const StatsSample *found = find_sample(s, c);
  TEST_ASSERT_NOT_NULL(found);
  return found->value;
}

static void test_stats_samples_carry_each_reading_to_its_own_channel() {
  const StatsView v = decode_stats(std::span<const uint8_t>(fixtures::STATS_NOTIFY).subspan(NOTIFY_HDR_LEN, 53));
  const StatsSamples s = stats_samples(v);
  TEST_ASSERT_EQUAL_UINT(5, s.size);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.8f, sample_value(s, StatsChannel::TOTAL_KM));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, v.trip_km, sample_value(s, StatsChannel::TRIP_KM));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, v.speed_kmh, sample_value(s, StatsChannel::SPEED));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, sample_value(s, StatsChannel::SOC));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)v.gear_start, sample_value(s, StatsChannel::GEAR_START));
}

static void test_stats_samples_drop_a_reading_that_failed_its_bound() {
  uint8_t p[53] = {0};
  p[stats::TOTAL_KM_OFFSET] = 0xFF;
  p[stats::TOTAL_KM_OFFSET + 1] = 0xFF;
  p[stats::TOTAL_KM_OFFSET + 2] = 0xFF;
  p[stats::TOTAL_KM_OFFSET + 3] = 0xFF;
  p[stats::ADDR_24_OFFSET] = 200;
  const StatsSamples s = stats_samples(decode_stats(p));
  TEST_ASSERT_NULL(find_sample(s, StatsChannel::TOTAL_KM));
  TEST_ASSERT_NULL(find_sample(s, StatsChannel::SOC));
  TEST_ASSERT_NOT_NULL(find_sample(s, StatsChannel::SPEED));
  TEST_ASSERT_EQUAL_UINT(3, s.size);
}

static void test_stats_samples_are_empty_for_an_invalid_frame() {
  const uint8_t too_short[4] = {0, 0, 0, 0};
  TEST_ASSERT_EQUAL_UINT(0, stats_samples(decode_stats(too_short)).size);
}

void run_decode_tests() {
  RUN_TEST(test_decode_stats_rejects_wrong_length);
  RUN_TEST(test_decode_stats_reads_the_captured_frame);
  RUN_TEST(test_decode_stats_masks_0x39_to_low_bits);
  RUN_TEST(test_decode_stats_gear_count_from_nibble_pair);
  RUN_TEST(test_decode_stats_rejects_a_nibble_pair_that_means_nothing);
  RUN_TEST(test_decode_stats_bounds_reject_impossible_values);
  RUN_TEST(test_decode_flags_light_needs_the_controller_on);
  RUN_TEST(test_decode_flags_inverted_bits);
  RUN_TEST(test_decode_flags_speaker_is_audible_only_on_zero);
  RUN_TEST(test_decode_flags_each_bit_reads_its_own_byte);
  RUN_TEST(test_stats_samples_carry_each_reading_to_its_own_channel);
  RUN_TEST(test_stats_samples_drop_a_reading_that_failed_its_bound);
  RUN_TEST(test_stats_samples_are_empty_for_an_invalid_frame);
}
