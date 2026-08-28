#include <unity.h>

#include <vector>

#include "fiido_state.h"
#include "test_groups.h"

using namespace esphome::fiido_bms;

static LifecycleInput lifecycle_base() {
  LifecycleInput in{};
  in.now = 1'000'000;
  in.idle_disconnect_ms = 15 * 60 * 1000;
  in.probe_window_ms = 60 * 1000;
  in.periodic_probe_ms = 5 * 60 * 1000;
  in.write_verify_window_ms = 10 * 1000;
  return in;
}

static void test_lifecycle_idle_disconnect_needs_the_full_window() {
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = true;
  in.motor_off_since_ms = in.now - in.idle_disconnect_ms + 1;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
  in.motor_off_since_ms = in.now - in.idle_disconnect_ms;
  TEST_ASSERT_EQUAL(LifecycleAction::IDLE_DISCONNECT, decide_lifecycle(in));
}

static void test_lifecycle_never_drops_the_link_with_a_write_queued() {
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = true;
  in.motor_off_since_ms = in.now - in.idle_disconnect_ms * 10;
  in.pending_writes = true;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
}

static void test_lifecycle_zero_timestamp_is_not_an_elapsed_window() {
  // millis() is 0 once per boot; 0 means "not started", not "long ago".
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = true;
  in.motor_off_since_ms = 0;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));

  in.connected = false;
  in.probe_started_ms = 0;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));

  in.enabled = false;
  in.disconnected_since_ms = 0;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
}

static void test_lifecycle_probe_timeout_waits_for_the_write_verify_window() {
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = false;
  in.probe_started_ms = in.now - in.probe_window_ms;
  in.last_dispatch_ms = in.now - in.write_verify_window_ms + 1;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
  in.last_dispatch_ms = in.now - in.write_verify_window_ms;
  TEST_ASSERT_EQUAL(LifecycleAction::PROBE_TIMEOUT, decide_lifecycle(in));
  in.last_dispatch_ms = 0;
  TEST_ASSERT_EQUAL(LifecycleAction::PROBE_TIMEOUT, decide_lifecycle(in));
}

static void test_lifecycle_disconnected_probes_on_its_period() {
  LifecycleInput in = lifecycle_base();
  in.enabled = false;
  in.disconnected_since_ms = in.now - in.periodic_probe_ms + 1;
  TEST_ASSERT_EQUAL(LifecycleAction::NONE, decide_lifecycle(in));
  in.disconnected_since_ms = in.now - in.periodic_probe_ms;
  TEST_ASSERT_EQUAL(LifecycleAction::START_PROBE, decide_lifecycle(in));
}

static void test_lifecycle_survives_the_millis_wrap() {
  LifecycleInput in = lifecycle_base();
  in.enabled = true;
  in.connected = true;
  in.now = 1000;
  in.motor_off_since_ms = 0xFFFFFFFFu - (in.idle_disconnect_ms - 1000) + 1;
  TEST_ASSERT_EQUAL(LifecycleAction::IDLE_DISCONNECT, decide_lifecycle(in));
}

static void test_should_log_now_lets_the_first_one_through() {
  TEST_ASSERT_TRUE(should_log_now(0, 0, 5000));
  TEST_ASSERT_TRUE(should_log_now(1, 0, 5000));
  TEST_ASSERT_FALSE(should_log_now(6000, 5000, 5000));
  TEST_ASSERT_TRUE(should_log_now(10000, 5000, 5000));
}

static void test_auto_shutdown_only_with_the_motor_on_and_enabled() {
  TEST_ASSERT_TRUE(should_auto_shutdown(20000, 5000, 15000, true, true));
  TEST_ASSERT_FALSE(should_auto_shutdown(20000, 5000, 15000, false, true));
  TEST_ASSERT_FALSE(should_auto_shutdown(20000, 5000, 15000, true, false));
  TEST_ASSERT_FALSE(should_auto_shutdown(19999, 5000, 15000, true, true));
}

static WriteGateInput gate_base() {
  WriteGateInput in{};
  in.ble_enabled = true;
  in.connected = true;
  in.cache_valid = true;
  return in;
}

static void test_write_gate_ladder_order() {
  WriteGateInput in = gate_base();
  TEST_ASSERT_EQUAL(WriteGate::SEND, gate_write(in));

  in.cache_valid = false;
  TEST_ASSERT_EQUAL(WriteGate::DEFER_COLD_CACHE, gate_write(in));
  in.connected = false;
  TEST_ASSERT_EQUAL(WriteGate::QUEUE_DISCONNECTED, gate_write(in));
  in.ble_enabled = false;
  TEST_ASSERT_EQUAL(WriteGate::REJECT_BLE_DISABLED, gate_write(in));
}

static void test_write_gate_controller_only_when_required() {
  WriteGateInput in = gate_base();
  in.controller_on = false;
  in.needs_controller = false;
  TEST_ASSERT_EQUAL(WriteGate::SEND, gate_write(in));
  in.needs_controller = true;
  TEST_ASSERT_EQUAL(WriteGate::REJECT_CONTROLLER_OFF, gate_write(in));
  in.controller_on = true;
  TEST_ASSERT_EQUAL(WriteGate::SEND, gate_write(in));
}

static void test_write_gate_cold_cache_beats_controller_check() {
  WriteGateInput in = gate_base();
  in.cache_valid = false;
  in.needs_controller = true;
  in.controller_on = false;
  TEST_ASSERT_EQUAL(WriteGate::DEFER_COLD_CACHE, gate_write(in));
}

static void test_encode_gear_mode_preserves_the_low_nibble() {
  TEST_ASSERT_EQUAL_UINT8(0x35, encode_gear_mode(3, 0x55));
  TEST_ASSERT_EQUAL_UINT8(0x55, encode_gear_mode(5, 0x35));
  TEST_ASSERT_EQUAL_UINT8(0x30, encode_gear_mode(3, 0x50));
}

static void test_encode_gear_mode_refuses_anything_but_3_or_5() {
  TEST_ASSERT_EQUAL_UINT8(0x55, encode_gear_mode(4, 0x55));
  TEST_ASSERT_EQUAL_UINT8(0x55, encode_gear_mode(0, 0x55));
  TEST_ASSERT_EQUAL_UINT8(0x55, encode_gear_mode(255, 0x55));
}

static void test_clamp_gear_holds_the_ceiling() {
  TEST_ASSERT_EQUAL_UINT8(3, clamp_gear(9, 3));
  TEST_ASSERT_EQUAL_UINT8(2, clamp_gear(2, 3));
  TEST_ASSERT_EQUAL_UINT8(0, clamp_gear(0, 3));
}

static std::vector<int> g_ran;
static PendingWrites g_queue;

static void test_pending_writes_drops_the_oldest_at_capacity() {
  g_ran.clear();
  g_queue.clear();
  for (size_t i = 0; i < PENDING_WRITE_SLOTS; i++)
    TEST_ASSERT_TRUE(g_queue.push([i]() { g_ran.push_back((int)i); }));
  TEST_ASSERT_FALSE(g_queue.push([]() { g_ran.push_back(99); }));
  TEST_ASSERT_EQUAL_UINT(PENDING_WRITE_SLOTS, g_queue.size());
  size_t count = 0;
  const auto taken = g_queue.drain(count);
  for (size_t i = 0; i < count; i++)
    taken[i]();
  TEST_ASSERT_EQUAL_UINT(PENDING_WRITE_SLOTS, g_ran.size());
  // The first push fell out. The run starts at 1 and ends with the newest.
  TEST_ASSERT_EQUAL_INT(1, g_ran.front());
  TEST_ASSERT_EQUAL_INT(99, g_ran.back());
}

static void test_pending_writes_drain_empties_the_queue() {
  g_queue.clear();
  (void)g_queue.push([]() {});
  TEST_ASSERT_FALSE(g_queue.empty());
  size_t count = 0;
  (void)g_queue.drain(count);
  TEST_ASSERT_EQUAL_UINT(1, count);
  TEST_ASSERT_TRUE(g_queue.empty());
  (void)g_queue.drain(count);
  TEST_ASSERT_EQUAL_UINT(0, count);
}

static void test_pending_writes_requeue_during_drain_waits_for_the_next_one() {
  g_ran.clear();
  g_queue.clear();
  (void)g_queue.push([]() {
    g_ran.push_back(1);
    (void)g_queue.push([]() { g_ran.push_back(2); });
  });
  size_t count = 0;
  auto taken = g_queue.drain(count);
  for (size_t i = 0; i < count; i++)
    taken[i]();
  TEST_ASSERT_EQUAL_UINT(1, g_ran.size());
  TEST_ASSERT_EQUAL_UINT(1, g_queue.size());
  taken = g_queue.drain(count);
  for (size_t i = 0; i < count; i++)
    taken[i]();
  TEST_ASSERT_EQUAL_UINT(2, g_ran.size());
  TEST_ASSERT_EQUAL_INT(2, g_ran.back());
}

static void test_should_retry_send_stops_at_the_retry_cap() {
  TEST_ASSERT_FALSE(should_retry_send(0, 2, true));
  TEST_ASSERT_TRUE(should_retry_send(0, 2, false));
  TEST_ASSERT_TRUE(should_retry_send(1, 2, false));
  TEST_ASSERT_FALSE(should_retry_send(2, 2, false));
  TEST_ASSERT_FALSE(should_retry_send(3, 2, false));
}

static void test_probe_outcome_motor_on_keeps_the_link() {
  TEST_ASSERT_EQUAL(ProbeOutcome::STAY_BIKE_ON, decide_probe_outcome(true, 100000, 0, 10000));
  TEST_ASSERT_EQUAL(ProbeOutcome::STAY_BIKE_ON, decide_probe_outcome(true, 100000, 99999, 10000));
}

static void test_probe_outcome_holds_the_link_while_a_write_is_being_verified() {
  TEST_ASSERT_EQUAL(ProbeOutcome::STAY_VERIFY_WINDOW, decide_probe_outcome(false, 100000, 95000, 10000));
  TEST_ASSERT_EQUAL(ProbeOutcome::DROP_LINK, decide_probe_outcome(false, 100000, 90000, 10000));
  TEST_ASSERT_EQUAL(ProbeOutcome::DROP_LINK, decide_probe_outcome(false, 100000, 0, 10000));
  // Just after boot millis() is small, and no dispatch has happened: 0 means
  // "never", not "just now".
  TEST_ASSERT_EQUAL(ProbeOutcome::DROP_LINK, decide_probe_outcome(false, 5000, 0, 10000));
}

static void test_resolve_gear_count_keeps_what_the_select_has() {
  TEST_ASSERT_EQUAL_UINT8(0, resolve_gear_count(0, false, 5));
  TEST_ASSERT_EQUAL_UINT8(0, resolve_gear_count(3, true, 5));
  TEST_ASSERT_EQUAL_UINT8(0, resolve_gear_count(5, false, 5));
  TEST_ASSERT_EQUAL_UINT8(3, resolve_gear_count(3, false, 5));
}

static void test_resolve_mode_option_is_3_only_for_three_gears() {
  TEST_ASSERT_EQUAL_STRING("3", resolve_mode_option(3));
  TEST_ASSERT_EQUAL_STRING("5", resolve_mode_option(5));
  TEST_ASSERT_EQUAL_STRING("5", resolve_mode_option(0));
}

static void test_light_bit_clears_only_on_the_motor_off_edge() {
  TEST_ASSERT_TRUE(should_clear_light_bit(true, true, false, 0x08));
  TEST_ASSERT_FALSE(should_clear_light_bit(true, true, false, 0x00));
  TEST_ASSERT_FALSE(should_clear_light_bit(true, false, false, 0x08));
  TEST_ASSERT_FALSE(should_clear_light_bit(true, true, true, 0x08));
  TEST_ASSERT_FALSE(should_clear_light_bit(false, true, false, 0x08));
}

static void test_enforce_gear_mode_3_respects_every_gate() {
  const uint32_t cooldown = 60000;
  TEST_ASSERT_TRUE(should_enforce_gear_mode_3(true, 5, true, true, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(false, 5, true, true, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(true, 3, true, true, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(true, 5, false, true, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(true, 5, true, false, 100000, 0, cooldown));
  TEST_ASSERT_FALSE(should_enforce_gear_mode_3(true, 5, true, true, 100000, 50000, cooldown));
  TEST_ASSERT_TRUE(should_enforce_gear_mode_3(true, 5, true, true, 160000, 100000, cooldown));
}

static void test_speed_limit_option_needs_the_enable_bit() {
  TEST_ASSERT_EQUAL_STRING("No limit", resolve_speed_limit_option(100, false));
  TEST_ASSERT_EQUAL_STRING("6 km/h", resolve_speed_limit_option(6, true));
  TEST_ASSERT_EQUAL_STRING("25 km/h", resolve_speed_limit_option(25, true));
  TEST_ASSERT_NULL(resolve_speed_limit_option(6, false));
  TEST_ASSERT_NULL(resolve_speed_limit_option(25, false));
  TEST_ASSERT_NULL(resolve_speed_limit_option(30, true));
}

static void test_speed_limit_option_parsing_rejects_anything_else() {
  TEST_ASSERT_FALSE(parse_speed_limit_option("30 km/h").has_value());
  TEST_ASSERT_FALSE(parse_speed_limit_option("").has_value());
  TEST_ASSERT_FALSE(parse_speed_limit_option("no limit").has_value());
  TEST_ASSERT_TRUE(parse_speed_limit_option("6 km/h").has_value());
}

static void test_speed_limit_plan_writes_the_pas_bit_only_when_it_differs() {
  TEST_ASSERT_FALSE(plan_speed_limit(SpeedLimitOption::SIX_KMH, 0x80).needs_pas_write);
  TEST_ASSERT_TRUE(plan_speed_limit(SpeedLimitOption::SIX_KMH, 0x00).needs_pas_write);
  TEST_ASSERT_FALSE(plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0x00).needs_pas_write);
  TEST_ASSERT_TRUE(plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0x80).needs_pas_write);
}

static void test_speed_limit_plan_keeps_the_other_bits_of_0x2c() {
  TEST_ASSERT_EQUAL_UINT8(0xD5, plan_speed_limit(SpeedLimitOption::SIX_KMH, 0x55).pas_byte);
  TEST_ASSERT_EQUAL_UINT8(0x55, plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0xD5).pas_byte);
}

static void test_speed_limit_phase2_is_delayed_only_when_clearing_the_cap() {
  TEST_ASSERT_TRUE(plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0x80).delay_phase2);
  TEST_ASSERT_TRUE(plan_speed_limit(SpeedLimitOption::NO_LIMIT, 0x00).delay_phase2);
  TEST_ASSERT_FALSE(plan_speed_limit(SpeedLimitOption::SIX_KMH, 0x00).delay_phase2);
  TEST_ASSERT_FALSE(plan_speed_limit(SpeedLimitOption::TWENTY_FIVE_KMH, 0x00).delay_phase2);
}

static void test_speed_limit_bit_keeps_the_other_bits_of_0x27() {
  TEST_ASSERT_EQUAL_UINT8(0xA8, apply_speed_limit_bit(0x88, true));
  TEST_ASSERT_EQUAL_UINT8(0x88, apply_speed_limit_bit(0xA8, false));
  TEST_ASSERT_EQUAL_UINT8(0xA8, apply_speed_limit_bit(0xA8, true));
}

static void test_speed_limit_plan_and_readback_agree() {
  // What the writer sends must be what the reader turns back into the option.
  for (const auto option : {SpeedLimitOption::SIX_KMH, SpeedLimitOption::TWENTY_FIVE_KMH, SpeedLimitOption::NO_LIMIT}) {
    const SpeedLimitPlan plan = plan_speed_limit(option, 0x00);
    TEST_ASSERT_EQUAL_STRING(speed_limit_option_name(option), resolve_speed_limit_option(plan.value, plan.limit_on));
  }
}

void run_state_tests() {
  RUN_TEST(test_lifecycle_idle_disconnect_needs_the_full_window);
  RUN_TEST(test_lifecycle_never_drops_the_link_with_a_write_queued);
  RUN_TEST(test_lifecycle_zero_timestamp_is_not_an_elapsed_window);
  RUN_TEST(test_lifecycle_probe_timeout_waits_for_the_write_verify_window);
  RUN_TEST(test_lifecycle_disconnected_probes_on_its_period);
  RUN_TEST(test_lifecycle_survives_the_millis_wrap);
  RUN_TEST(test_should_log_now_lets_the_first_one_through);
  RUN_TEST(test_auto_shutdown_only_with_the_motor_on_and_enabled);
  RUN_TEST(test_write_gate_ladder_order);
  RUN_TEST(test_write_gate_controller_only_when_required);
  RUN_TEST(test_write_gate_cold_cache_beats_controller_check);
  RUN_TEST(test_encode_gear_mode_preserves_the_low_nibble);
  RUN_TEST(test_encode_gear_mode_refuses_anything_but_3_or_5);
  RUN_TEST(test_clamp_gear_holds_the_ceiling);
  RUN_TEST(test_pending_writes_drops_the_oldest_at_capacity);
  RUN_TEST(test_pending_writes_drain_empties_the_queue);
  RUN_TEST(test_pending_writes_requeue_during_drain_waits_for_the_next_one);
  RUN_TEST(test_should_retry_send_stops_at_the_retry_cap);
  RUN_TEST(test_probe_outcome_motor_on_keeps_the_link);
  RUN_TEST(test_probe_outcome_holds_the_link_while_a_write_is_being_verified);
  RUN_TEST(test_resolve_gear_count_keeps_what_the_select_has);
  RUN_TEST(test_resolve_mode_option_is_3_only_for_three_gears);
  RUN_TEST(test_light_bit_clears_only_on_the_motor_off_edge);
  RUN_TEST(test_enforce_gear_mode_3_respects_every_gate);
  RUN_TEST(test_speed_limit_option_needs_the_enable_bit);
  RUN_TEST(test_speed_limit_option_parsing_rejects_anything_else);
  RUN_TEST(test_speed_limit_plan_writes_the_pas_bit_only_when_it_differs);
  RUN_TEST(test_speed_limit_plan_keeps_the_other_bits_of_0x2c);
  RUN_TEST(test_speed_limit_phase2_is_delayed_only_when_clearing_the_cap);
  RUN_TEST(test_speed_limit_bit_keeps_the_other_bits_of_0x27);
  RUN_TEST(test_speed_limit_plan_and_readback_agree);
}
