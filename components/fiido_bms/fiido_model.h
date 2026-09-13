#pragma once

#include <algorithm>
#include <array>
#include <concepts>
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
  const char *used_by;
};

inline constexpr GattProfile FFE0_GATT{
    .label = "FFE0",
    .service = "00010203-0405-0607-0809-0a0b0c0dffe0",
    .notify = "00010203-0405-0607-0809-0a0b0c0dffe1",
    .write = "00010203-0405-0607-0809-0a0b0c0dffe2",
    .used_by = "c11_pro or m1_pro_2025",
};

inline constexpr GattProfile FEA0_GATT{
    .label = "FEA0",
    .service = "c3e6fea0-e966-1000-8000-be99c223df6a",
    .notify = "c3e6fea2-e966-1000-8000-be99c223df6a",
    .write = "c3e6fea1-e966-1000-8000-be99c223df6a",
    .used_by = "air",
};

inline constexpr std::array KNOWN_GATT_PROFILES{FFE0_GATT, FEA0_GATT};

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
static_assert(std::ranges::all_of(KNOWN_GATT_PROFILES, [](const GattProfile &gatt) {
  return is_uuid128_text(gatt.service) && is_uuid128_text(gatt.notify) && is_uuid128_text(gatt.write);
}));

static_assert(std::ranges::all_of(KNOWN_GATT_PROFILES, [](const GattProfile &gatt) {
  const std::string_view service{gatt.service};
  const std::string_view notify{gatt.notify};
  return service != notify && service != gatt.write && notify != gatt.write && *gatt.used_by != '\0';
}));

// foreign_gatt() tells known profiles apart by service and prints used_by as the
// model to set.
static_assert([] {
  for (size_t i = 0; i < KNOWN_GATT_PROFILES.size(); i++) {
    for (size_t j = i + 1; j < KNOWN_GATT_PROFILES.size(); j++) {
      if (same_service(KNOWN_GATT_PROFILES[i], KNOWN_GATT_PROFILES[j]))
        return false;
    }
  }
  return true;
}());

static_assert(std::ranges::all_of(MODEL_PROFILES, [](const ModelProfile &row) {
  return std::ranges::any_of(KNOWN_GATT_PROFILES,
                             [&row](const GattProfile &known) { return same_service(known, row.gatt); });
}));

static_assert(std::ranges::all_of(KNOWN_GATT_PROFILES, [](const GattProfile &known) {
  return std::ranges::any_of(MODEL_PROFILES,
                             [&known](const ModelProfile &row) { return same_service(known, row.gatt); });
}));

[[nodiscard]] constexpr const ModelProfile &model_profile(Model model) {
  return MODEL_PROFILES[static_cast<size_t>(model)];
}

template <std::predicate<std::string_view> HasService>
[[nodiscard]] constexpr const GattProfile *foreign_gatt(const GattProfile &configured, HasService has_service) {
  if (has_service(std::string_view{configured.service}))
    return nullptr;
  for (const GattProfile &known : KNOWN_GATT_PROFILES) {
    if (!same_service(known, configured) && has_service(std::string_view{known.service}))
      return &known;
  }
  return nullptr;
}

}  // namespace esphome::fiido_bms
