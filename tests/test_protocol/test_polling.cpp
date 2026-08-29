#include <unity.h>

#include "fiido_protocol.h"
#include "test_groups.h"

using namespace esphome::fiido_bms;

static void test_burst_gate_first_burst_after_connect_is_free() {
  // started == false marks "nothing sent since the link came up".
  const BurstGate g = evaluate_burst_gate(12345, 3000, 0, {0, 0, false}, false);
  TEST_ASSERT_TRUE(g.start);
}

static void test_burst_gate_blocks_inside_the_same_slot() {
  const BurstGate first = evaluate_burst_gate(3100, 3000, 0, {100, 0, true}, false);
  TEST_ASSERT_TRUE(first.start);
  const BurstGate second = evaluate_burst_gate(3900, 3000, 0, {3100, first.slot, true}, false);
  TEST_ASSERT_FALSE(second.start);
}

static void test_burst_gate_opens_on_the_next_slot() {
  const BurstGate g = evaluate_burst_gate(6100, 3000, 0, {3100, 1, true}, false);
  TEST_ASSERT_TRUE(g.start);
  TEST_ASSERT_EQUAL_UINT32(2, g.slot);
}

static void test_burst_gate_keeps_minimum_spacing_after_a_forced_burst() {
  // A write verification poll bypasses both gates and can land just before a slot
  // boundary. Without the elapsed check a full burst followed 200 ms later.
  const BurstGate forced = evaluate_burst_gate(29999, 15000, 0, {20000, 1, true}, true);
  TEST_ASSERT_TRUE(forced.start);
  const BurstGate next = evaluate_burst_gate(30200, 15000, 0, {29999, forced.slot, true}, false);
  TEST_ASSERT_FALSE(next.start);
}

static void test_burst_gate_phase_separates_two_hubs() {
  // Worst case: two hubs whose 1 s poller ticks land on the same millisecond.
  // Only the phase argument keeps their bursts apart, and it has to keep doing so
  // for the whole run, not just for the first burst after connect.
  const uint32_t interval = 3000;
  BurstState a_state{0, 0, false}, b_state{0, 0, false};
  bool collided = false;
  for (uint32_t now = 0; now <= 60000; now += 1000) {
    const BurstGate a = evaluate_burst_gate(now, interval, 0, a_state, false);
    const BurstGate b = evaluate_burst_gate(now, interval, 1500, b_state, false);
    if (a.start)
      a_state = {now, a.slot, true};
    if (b.start)
      b_state = {now, b.slot, true};
    // Skip the warm-up: both hubs are due immediately on the first tick.
    if (now >= 10000 && a.start && b.start)
      collided = true;
  }
  TEST_ASSERT_FALSE(collided);
}

static void test_burst_gate_survives_zero_interval() {
  // update_interval_on: 0s validates, and the slot maths divides by it.
  const BurstGate g = evaluate_burst_gate(1000, 0, 0, {500, 0, true}, false);
  TEST_ASSERT_TRUE(g.start);
}

static void test_skip_disabled_polls_stops_on_an_enabled_one() {
  bool enabled[POLL_TABLE_SIZE];
  for (size_t i = 0; i < POLL_TABLE_SIZE; i++)
    enabled[i] = true;
  const PollCursor c = skip_disabled_polls({0, POLL_TABLE_SIZE}, enabled);
  TEST_ASSERT_EQUAL_UINT(0, c.index);
  TEST_ASSERT_EQUAL_UINT(POLL_TABLE_SIZE, c.remaining);
}

static void test_skip_disabled_polls_consumes_one_slot_each() {
  // Skipping must spend a slot, or a burst of nine could send more than nine.
  bool enabled[POLL_TABLE_SIZE];
  for (size_t i = 0; i < POLL_TABLE_SIZE; i++)
    enabled[i] = true;
  enabled[0] = false;
  enabled[1] = false;
  const PollCursor c = skip_disabled_polls({0, POLL_TABLE_SIZE}, enabled);
  TEST_ASSERT_EQUAL_UINT(2, c.index);
  TEST_ASSERT_EQUAL_UINT(POLL_TABLE_SIZE - 2, c.remaining);
}

static void test_skip_disabled_polls_terminates_when_all_are_off() {
  bool enabled[POLL_TABLE_SIZE];
  for (size_t i = 0; i < POLL_TABLE_SIZE; i++)
    enabled[i] = false;
  const PollCursor c = skip_disabled_polls({0, POLL_TABLE_SIZE}, enabled);
  TEST_ASSERT_EQUAL_UINT(0, c.remaining);
}

static void test_advance_burst_repeats_the_same_poll_until_the_retries_run_out() {
  const PollCursor at{.index = 3, .remaining = 5};
  const BurstStep first = advance_burst(at, 0, 2, false);
  TEST_ASSERT_TRUE(first.retry);
  TEST_ASSERT_EQUAL_UINT8(1, first.retry_count);
  TEST_ASSERT_EQUAL_UINT(3, first.cursor.index);
  TEST_ASSERT_EQUAL_UINT(5, first.cursor.remaining);

  const BurstStep second = advance_burst(at, 1, 2, false);
  TEST_ASSERT_TRUE(second.retry);
  TEST_ASSERT_EQUAL_UINT8(2, second.retry_count);
}

static void test_advance_burst_gives_up_after_the_last_retry() {
  // The failing poll is spent, not retried forever.
  const BurstStep step = advance_burst({.index = 3, .remaining = 5}, 2, 2, false);
  TEST_ASSERT_FALSE(step.retry);
  TEST_ASSERT_EQUAL_UINT8(0, step.retry_count);
  TEST_ASSERT_EQUAL_UINT(4, step.cursor.index);
  TEST_ASSERT_EQUAL_UINT(4, step.cursor.remaining);
}

static void test_advance_burst_moves_on_after_a_good_send() {
  const BurstStep step = advance_burst({.index = 3, .remaining = 5}, 1, 2, true);
  TEST_ASSERT_FALSE(step.retry);
  TEST_ASSERT_EQUAL_UINT8(0, step.retry_count);
  TEST_ASSERT_EQUAL_UINT(4, step.cursor.index);
  TEST_ASSERT_EQUAL_UINT(4, step.cursor.remaining);
}

static void test_advance_burst_wraps_the_index_at_the_table_end() {
  const BurstStep step = advance_burst({.index = POLL_TABLE_SIZE - 1, .remaining = 2}, 0, 2, true);
  TEST_ASSERT_EQUAL_UINT(0, step.cursor.index);
  TEST_ASSERT_EQUAL_UINT(1, step.cursor.remaining);
}

void run_polling_tests() {
  RUN_TEST(test_burst_gate_first_burst_after_connect_is_free);
  RUN_TEST(test_burst_gate_blocks_inside_the_same_slot);
  RUN_TEST(test_burst_gate_opens_on_the_next_slot);
  RUN_TEST(test_burst_gate_keeps_minimum_spacing_after_a_forced_burst);
  RUN_TEST(test_burst_gate_phase_separates_two_hubs);
  RUN_TEST(test_burst_gate_survives_zero_interval);
  RUN_TEST(test_skip_disabled_polls_stops_on_an_enabled_one);
  RUN_TEST(test_skip_disabled_polls_consumes_one_slot_each);
  RUN_TEST(test_skip_disabled_polls_terminates_when_all_are_off);
  RUN_TEST(test_advance_burst_repeats_the_same_poll_until_the_retries_run_out);
  RUN_TEST(test_advance_burst_gives_up_after_the_last_retry);
  RUN_TEST(test_advance_burst_moves_on_after_a_good_send);
  RUN_TEST(test_advance_burst_wraps_the_index_at_the_table_end);
}
