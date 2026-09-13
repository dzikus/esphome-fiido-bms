#include <unity.h>

#include <string_view>

#include "fiido_model.h"
#include "test_groups.h"

using namespace esphome::fiido_bms;

static void test_uuid_text_accepts_the_profile_uuids() {
  for (const char *uuid :
       {FFE0_GATT.service, FFE0_GATT.notify, FFE0_GATT.write, FEA0_GATT.service, FEA0_GATT.notify, FEA0_GATT.write})
    TEST_ASSERT_TRUE_MESSAGE(is_uuid128_text(uuid), uuid);
  TEST_ASSERT_TRUE(is_uuid128_text("C3E6FEA0-E966-1000-8000-BE99C223DF6A"));
}

static void test_uuid_text_rejects_the_wrong_length() {
  TEST_ASSERT_FALSE(is_uuid128_text(""));
  TEST_ASSERT_FALSE(is_uuid128_text("c3e6fea0-e966-1000-8000-be99c223df6"));
  TEST_ASSERT_FALSE(is_uuid128_text("c3e6fea0-e966-1000-8000-be99c223df6a0"));
  TEST_ASSERT_FALSE(is_uuid128_text("c3e6fea0e96610008000be99c223df6a"));
}

static void test_uuid_text_rejects_a_misplaced_dash() {
  TEST_ASSERT_FALSE(is_uuid128_text("c3e6fea0e-966-1000-8000-be99c223df6a"));
  TEST_ASSERT_FALSE(is_uuid128_text("c3e6fea0-e966-1000-80000-be99c223df6"));
  TEST_ASSERT_FALSE(is_uuid128_text("c3e6fea0-e966-1000-8000-be99c223df-a"));
}

static void test_uuid_text_rejects_a_non_hex_digit() {
  TEST_ASSERT_FALSE(is_uuid128_text("g3e6fea0-e966-1000-8000-be99c223df6a"));
  TEST_ASSERT_FALSE(is_uuid128_text("c3e6fea0-e966-1000-8000-be99c223df6 "));
  TEST_ASSERT_FALSE(is_uuid128_text("c3e6fea0-e966-1000-8000-be99c223df6:"));
}

static void test_air_uses_the_fea0_profile() {
  const ModelProfile &air = model_profile(Model::AIR);
  TEST_ASSERT_EQUAL_STRING("Air", air.name);
  TEST_ASSERT_EQUAL_STRING("FEA0", air.gatt.label);
  TEST_ASSERT_EQUAL_STRING("c3e6fea0-e966-1000-8000-be99c223df6a", air.gatt.service);
  TEST_ASSERT_EQUAL_STRING("c3e6fea2-e966-1000-8000-be99c223df6a", air.gatt.notify);
  TEST_ASSERT_EQUAL_STRING("c3e6fea1-e966-1000-8000-be99c223df6a", air.gatt.write);
  TEST_ASSERT_FALSE(air.traits.light_bit_persists);
}

static void test_reference_models_use_the_ffe0_profile() {
  for (const Model model : {Model::C11_PRO, Model::M1_PRO_2025}) {
    const ModelProfile &profile = model_profile(model);
    TEST_ASSERT_EQUAL_STRING("FFE0", profile.gatt.label);
    TEST_ASSERT_EQUAL_STRING("00010203-0405-0607-0809-0a0b0c0dffe0", profile.gatt.service);
    TEST_ASSERT_EQUAL_STRING("00010203-0405-0607-0809-0a0b0c0dffe1", profile.gatt.notify);
    TEST_ASSERT_EQUAL_STRING("00010203-0405-0607-0809-0a0b0c0dffe2", profile.gatt.write);
    TEST_ASSERT_TRUE(profile.traits.light_bit_persists);
  }
  TEST_ASSERT_EQUAL_STRING("C11 Pro", model_profile(Model::C11_PRO).name);
  TEST_ASSERT_EQUAL_STRING("M1 Pro 2025", model_profile(Model::M1_PRO_2025).name);
}

static void test_foreign_gatt_is_null_while_the_configured_service_is_present() {
  const auto offers_every_service = [](std::string_view) { return true; };
  TEST_ASSERT_NULL(foreign_gatt(FFE0_GATT, offers_every_service));
  TEST_ASSERT_NULL(foreign_gatt(FEA0_GATT, offers_every_service));
}

static void test_foreign_gatt_names_the_other_known_profile() {
  const auto offers_fea0 = [](std::string_view uuid) { return uuid == FEA0_GATT.service; };
  const GattProfile *found = foreign_gatt(FFE0_GATT, offers_fea0);
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL_STRING("FEA0", found->label);
  TEST_ASSERT_EQUAL_STRING("air", found->used_by);
}

static void test_foreign_gatt_is_null_for_a_bike_with_no_known_service() {
  const auto offers_nothing = [](std::string_view) { return false; };
  TEST_ASSERT_NULL(foreign_gatt(FFE0_GATT, offers_nothing));
  TEST_ASSERT_NULL(foreign_gatt(FEA0_GATT, offers_nothing));
}

static void test_air_on_an_ffe0_bike_points_to_the_reference_models() {
  const auto offers_ffe0 = [](std::string_view uuid) { return uuid == FFE0_GATT.service; };
  const GattProfile *found = foreign_gatt(model_profile(Model::AIR).gatt, offers_ffe0);
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL_STRING("FFE0", found->label);
  TEST_ASSERT_EQUAL_STRING("c11_pro or m1_pro_2025", found->used_by);
}

void run_model_tests() {
  RUN_TEST(test_uuid_text_accepts_the_profile_uuids);
  RUN_TEST(test_uuid_text_rejects_the_wrong_length);
  RUN_TEST(test_uuid_text_rejects_a_misplaced_dash);
  RUN_TEST(test_uuid_text_rejects_a_non_hex_digit);
  RUN_TEST(test_air_uses_the_fea0_profile);
  RUN_TEST(test_reference_models_use_the_ffe0_profile);
  RUN_TEST(test_foreign_gatt_is_null_while_the_configured_service_is_present);
  RUN_TEST(test_foreign_gatt_names_the_other_known_profile);
  RUN_TEST(test_foreign_gatt_is_null_for_a_bike_with_no_known_service);
  RUN_TEST(test_air_on_an_ffe0_bike_points_to_the_reference_models);
}
