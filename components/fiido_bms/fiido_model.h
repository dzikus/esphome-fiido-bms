#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace esphome::fiido_bms {

enum class Model : uint8_t {
  C11_PRO = 0,
  M1_PRO_2025,
  AIR,
};

struct GattProfile {
  const char *label;
  const char *service;
  const char *notify;
  const char *write;
};

inline constexpr GattProfile FFE0_GATT{
    .label = "FFE0",
    .service = "00010203-0405-0607-0809-0a0b0c0dffe0",
    .notify = "00010203-0405-0607-0809-0a0b0c0dffe1",
    .write = "00010203-0405-0607-0809-0a0b0c0dffe2",
};

inline constexpr GattProfile FEA0_GATT{
    .label = "FEA0",
    .service = "c3e6fea0-e966-1000-8000-be99c223df6a",
    .notify = "c3e6fea2-e966-1000-8000-be99c223df6a",
    .write = "c3e6fea1-e966-1000-8000-be99c223df6a",
};

struct ModelTraits {
  // Known to keep ADDR 0x27 bit 3 set across a controller OFF/ON cycle.
  bool light_bit_persists;
};

struct ModelProfile {
  Model model;
  const char *name;
  GattProfile gatt;
  ModelTraits traits;
};

inline constexpr auto MODEL_PROFILES = std::to_array<ModelProfile>({
    {.model = Model::C11_PRO, .name = "C11 Pro", .gatt = FFE0_GATT, .traits = {.light_bit_persists = true}},
    {.model = Model::M1_PRO_2025, .name = "M1 Pro 2025", .gatt = FFE0_GATT, .traits = {.light_bit_persists = true}},
    {.model = Model::AIR, .name = "Air", .gatt = FEA0_GATT, .traits = {.light_bit_persists = false}},
});

[[nodiscard]] constexpr bool is_uuid128_text(std::string_view text) {
  if (text.size() != 36)
    return false;
  for (size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-')
        return false;
    } else if (!hex) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr bool same_service(const GattProfile &a, const GattProfile &b) {
  return std::string_view{a.service} == std::string_view{b.service};
}

// model_profile() indexes the table by enum value.
static_assert([] {
  for (size_t i = 0; i < MODEL_PROFILES.size(); i++) {
    if (static_cast<size_t>(MODEL_PROFILES[i].model) != i)
      return false;
  }
  return true;
}());

// A malformed UUID matches no characteristic.
static_assert(std::ranges::all_of(MODEL_PROFILES, [](const ModelProfile &row) {
  return is_uuid128_text(row.gatt.service) && is_uuid128_text(row.gatt.notify) && is_uuid128_text(row.gatt.write);
}));

static_assert(std::ranges::all_of(MODEL_PROFILES, [](const ModelProfile &row) {
  const std::string_view service{row.gatt.service};
  const std::string_view notify{row.gatt.notify};
  return service != notify && service != row.gatt.write && notify != row.gatt.write;
}));

static_assert(!same_service(FFE0_GATT, FEA0_GATT));

[[nodiscard]] constexpr const ModelProfile &model_profile(Model model) {
  return MODEL_PROFILES[static_cast<size_t>(model)];
}

}  // namespace esphome::fiido_bms
