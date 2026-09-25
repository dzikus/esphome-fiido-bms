# Changelog

## v3.2.0 (2026-09-13)

- New hub option `model: c11_pro | m1_pro_2025 | air`. `air` selects the Fiido
  Air GATT profile and entity set. Untested on hardware; see
  [Fiido Air (untested)](README.md#fiido-air-untested).
- The link is released when STATS frames stop arriving, including with writes
  queued, and when the bike lacks the configured GATT service.
- Writes are rejected while the bike offers another model's GATT service.
- Every received frame and the capability bytes are logged for captures.
- The speed limit is polled only for a hub that has the select.
- The component requires the ESPHome release it builds against.

## v3.1.1 (2026-09-08)

- The controller temperature is decoded as signed.
- Documentation of multi-byte field addresses and of the registers the C11 and
  M1 leave at zero.

## v3.1.0 (2026-08-31)

- The example points at the local component and a MAC from `secrets.yaml`.
- Tooling: shellcheck and actionlint, pinned clang-format, release gates run
  from the tests workflow, CI fails when a tool pin drifts from the
  requirements file.

## v3.0.0 (2026-08-30)

- Breaking: the controls the C11 and M1 report as unsupported, and the
  remaining hidden entities, are built only with `expose_dev_sensors: true`.
  Without it they are not compiled in.
- A gear the new mode does not have is dropped when the mode changes.
- `dump_config` lists the entities.
- Every flag register in the STATS frame is logged at debug level.
- `PROTOCOL.md` documents the protocol.
- The minimum ESPHome version was raised.
- Internal: protocol layer on `std::span`, table-driven controls, one write
  gate, no heap allocation on the write path, host tests split by concern,
  clang-tidy and a warning gate in CI, builds on four ESP32 boards.

## v2.2.0 (2026-08-14)

- Frame typing, masked writes, burst gating and STATS decoding moved into the
  protocol layer, with tests.
- The register cache, entities and burst cadence stay consistent on rejected
  writes and forced polls.
- The gear count is pinned from yaml; restore modes that never ran are
  disabled.

## v2.1.0 (2026-08-09)

- One entity-defaults helper shared by the platforms.
- The minimum ESPHome version is 2025.8.0.

## v2.0.0 (2026-08-09)

- Hub option `name_prefix` gives each bike its own entity names and API keys.
  Setting it renames entities; see
  [Upgrading](README.md#upgrading).

## v1.4.0 (2026-08-01)

- Flag setters deduplicated; switch state follows the bike.

## v1.3.1 (2026-08-01)

- L2CAP congestion flow control for poll bursts.

## v1.3.0 (2026-07-31)

- Write path hardening; CI builds more platforms.

## v1.2.0 (2026-07-29)

- Configuration controls, `number` and `button` platforms, frame validation
  hardening.
- The uptime sensor is hidden by default.

## v1.1.0 (2026-06-01)

- `pas_limit` binary sensor.
- Devcontainer, pre-commit and ruff configuration; lint job in CI.

## v1.0.0 (2026-05-30)

- First release.
