// NOTIFY frame fixtures for parser unit tests.
//
// Sources:
//   BATTERY, CTRL, HANDSHAKE: captured from a live BMS.
//   MOTOR, ENERGY, STATS, METER: reconstructed from parsed live values.
//   CRCs for synthetic frames computed by compute_crc (XOR of preceding bytes).
//
// STATS also has 4 bonus bits set (light/charging/left+right turn) for bit-decode tests.
#pragma once

#include <cstdint>
#include <cstddef>

namespace fixtures {

// HANDSHAKE response (memory test pattern 0x15..0x25)
static constexpr uint8_t HANDSHAKE_NOTIFY[] = {
    0x46, 0x64, 0xAA, 0x0D, 0x0D,
    0x00, 0x00, 0x15, 0x16, 0x17, 0x18, 0x19, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
    0x9C,
};

// BATTERY (0x7B, 13B payload): V=48.0, capacity=11.6Ah, HW=1, SW=1, idle (real C11 PRO)
static constexpr uint8_t BATTERY_NOTIFY[] = {
    0x46, 0x64, 0xAA, 0x0D, 0x7B,
    0x01,  // off 0  (0x7B) HW=1
    0x01,  // off 1  (0x7C) SW=1
    0x00, 0x74,  // off 2-3 (0x7D-0x7E) totalLevel = 0x0074 = 116 -> 11.6 Ah
    0x01, 0xE0,  // off 4-5 (0x7F-0x80) voltage    = 0x01E0 = 480 -> 48.0 V
    0x00,        // off 6
    0x00, 0x00,  // off 7-8  currentVoltage = 0
    0x00, 0x00,  // off 9-10 current = 0
    0x00,        // off 11 failureList
    0x00,        // off 12 manufacturer
    0x6B,        // CRC
};

// CTRL (0xAF, 12B payload): HW=1, SW=211, version=18, manufacturer=1
static constexpr uint8_t CTRL_NOTIFY[] = {
    0x46, 0x64, 0xAA, 0x0C, 0xAF,
    0x01,        // off 0 HW=1
    0xD3,        // off 1 SW=211
    0x00, 0x00,  // off 2-3 upperLimitVoltage
    0x00, 0x00,  // off 4-5 lowerLimitVoltage
    0x00,        // off 6
    0x00, 0x00,  // off 7-8 currentElectricCurrent
    0x00,        // off 9   currentTemperature
    0x12,        // off 10  currentVersion = 18
    0x01,        // off 11  manufacturer = 1
    0xEA,        // CRC
};

// MOTOR (0x96, 12B payload): ver=1, wheel=28.0 inch (C11), capacity=350W
static constexpr uint8_t MOTOR_NOTIFY[] = {
    0x46, 0x64, 0xAA, 0x0C, 0x96,
    0x01,        // off 0 emVersion=1
    0x00,        // off 1 emMagnetic
    0x00,        // off 2 emWirePackageCount
    0x00,        // off 3 emMeasurementMagneticSteelCount
    0x00,        // off 4 emReductionRatio
    0x01, 0x18,  // off 5-6 emWheelDiameter = 0x0118 = 280 -> 28.0 inch (C11)
    0x00, 0x00,  // off 7-8 emTemperature
    0x01, 0x5E,  // off 9-10 emCapacity = 0x015E = 350 W
    0x00,        // off 11
    0x55,        // CRC
};

// ENERGY (0xC8, 12B payload): startupTime=57s, rest idle (0)
static constexpr uint8_t ENERGY_NOTIFY[] = {
    0x46, 0x64, 0xAA, 0x0C, 0xC8,
    0x00, 0x00,  // off 0-1 crankTorque
    0x00, 0x00,  // off 2-3 crankRPM
    0x00, 0x00,  // off 4-5 thisTakeEnergy
    0x00, 0x00, 0x00, 0x00,  // off 6-9 totalTakeEnergy (32-bit BE)
    0x00, 0x39,  // off 10-11 startupTime = 0x0039 = 57 s
    0x75,        // CRC
};

// STATS (0x05, 53B payload): totalKm=42.8, SOC=90%, gear=1
// BONUS BITS (for lights/charging/brake/drive tests):
//   off 34 (ADDR 0x27) bit 3 = openLight, ON
//   off 37 (ADDR 0x2A) bit 3 = statusStateOfCharge (charging), ON
//   off 51 (ADDR 0x38) bit 0 = leftTurnLight, ON
//   off 51 (ADDR 0x38) bit 1 = rightTurnLight, ON
static constexpr uint8_t STATS_NOTIFY[] = {
    0x46, 0x64, 0xAA, 0x35, 0x05,
    // payload [0..52]:
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // off 0-9
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // off 10-19
    0x00, 0x00, 0x00,                    // off 20-22 (version, sellArea)
    0x00, 0x00, 0x01, 0xAC,              // off 23-26 totalKm = 0x000001AC = 428 -> 42.8 km
    0x00, 0x00,                          // off 27-28 currentKm = 0
    0x00, 0x00,                          // off 29-30 bicycleSpeed = 0
    0x5A,                                // off 31 statusBatteryValue = 90% SOC
    0x00,                                // off 32 gearStartingValue
    0x01,                                // off 33 bicycleGear = 1
    0x08,                                // off 34 (0x27) bit 3 = openLight
    0x00, 0x00,                          // off 35-36
    0x08,                                // off 37 (0x2A) bit 3 = statusStateOfCharge (charging)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // off 38-50
    0x03,                                // off 51 (0x38) bits 0+1 = both turn signals
    0x00,                                // off 52
    0x4D,                                // CRC
};

// METER (0x60, 13B payload): HW=32, SW=40, modeData=55
static constexpr uint8_t METER_NOTIFY[] = {
    0x46, 0x64, 0xAA, 0x0D, 0x60,
    0x20,        // off 0 HW=32
    0x28,        // off 1 SW=40
    0x00,        // off 2 resourceVersion
    0x00, 0x00, 0x00, 0x00,  // off 3-6 mode (4B string)
    0x37,        // off 7 modeData = 55
    0x00,        // off 8 agreementVersion
    0x00,        // off 9 canProtocolVersion
    0x00, 0x00, 0x00,  // off 10-12
    0xDA,        // CRC
};

}  // namespace fixtures
