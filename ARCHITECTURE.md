# ESPHome Fiido BMS architecture

How the component is structured, how it polls and writes, and how to add an entity.
Configuration and entities are in [README.md](README.md). Frames, registers and bit
meanings are in [PROTOCOL.md](PROTOCOL.md) and are not repeated here.

## Component layout

```
components/fiido_bms/
  __init__.py                  hub schema, model entity sets, auto-offset, dev gating
  sensor.py                    36 sensors (10 by default, 26 dev), poll group per sensor
  binary_sensor.py             3 binary sensors (2 by default, 1 dev)
  select.py                    4 selects
  switch.py                    16 switches (9 by default, 7 dev)
  number.py                    3 numbers, all dev
  button.py                    pair_watch, dev

  fiido_protocol.{h,cpp}       pure C++: register addresses and bits, frame builders,
                               notify validation, STATS and flag decode, masked write,
                               poll table, burst cadence
  fiido_state.{h,cpp}          pure C++: lifecycle decisions, write gate, pending write
                               queue, register cache, speed limit plan, gear and power
                               encoding, auto-shutdown
  fiido_model.h                pure C++: Model enum, GATT profiles, per-model traits
  fiido_link.{h,cpp}           GATT handles by UUID, notify subscription, writes, congestion
  fiido_bms.{h,cpp}            FiidoBMSHub: BLE client node + PollingComponent, notify
                               parsers, FLAG_CONTROLS and BYTE_CONTROLS tables

  fiido_bool_switch.h          FiidoBoolSwitch<Setter> (write_state only) and
                               FiidoBoolSwitchWithRestore<Setter> (restores in setup(),
                               used by bluetooth and auto_shutdown)
  fiido_number.h               FiidoNumber<Setter> for brightness, boost, guard_time
  fiido_button.h               pair_watch button
  fiido_gear_select.{h,cpp}         gear, OFF plus 3 or 5 gears
  fiido_mode_select.{h,cpp}         gear count
  fiido_speed_limit_select.{h,cpp}  speed limit
  fiido_speed_unit_select.{h,cpp}   speed unit
```

`fiido_protocol`, `fiido_state` and `fiido_model.h` have no ESPHome dependency and the
host unit tests build against them. Everything else needs the ESPHome runtime.

## Polling

`FiidoBMSHub` is a `PollingComponent` with a 1 s `update_interval`. `update()` returns
early while the `bluetooth` switch is off, while the link is not `READY` (notify
registered) and until `startup_delay` has passed since the connection opened. The
first call after that sends the HANDSHAKE poll. Later calls start a burst when
`evaluate_burst_gate()` allows it:

```
interval = desired_interval_ms_
slot     = (now - startup_delay_ms_ % interval) / interval
start if the burst is forced, or no burst ran on this connection,
      or (slot != last_burst_slot_ and now - last_burst_ms_ >= interval / 2)
```

The slot ties the phase to `millis()`, which every hub reads, so the `startup_delay`
offset between hubs holds for the whole uptime. The spacing check uses half an
interval: a full one would pace each hub from its own last burst and could lock two
hubs that once fired together, and half still absorbs a forced burst that lands next
to a slot boundary.

`desired_interval_ms_` is `update_interval_on` (3 s) while STATS reports the
controller on and `update_interval_off` (15 s) while it is off. `READY` resets it to
`update_interval_on`.

A burst walks [the poll table](PROTOCOL.md#polls) with 5 ms between polls
(`BURST_INTERVAL_MS`) and skips polls no entity on the hub reads. In testing 50, 20,
10 and 5 ms worked and 2 ms made entity states flicker. A failed send
is retried twice, 50 ms apart (`BURST_SEND_RETRIES`, `BURST_RETRY_MS`), then the burst
moves on. While L2CAP reports congestion the burst waits in 50 ms steps and resumes
when the congestion ends.

STATS is always polled. `default_poll_enables()` enables STATS only, and code
generation calls `enable_<group>_poll()` for each poll an entity of the hub reads:

| Poll                                | Enabled by                                           |
|-------------------------------------|------------------------------------------------------|
| BATTERY, CTRL, MOTOR, ENERGY, METER | a sensor mapped to it in `SENSOR_POLL_GROUP` (`sensor.py`) |
| SPEEDLIM                            | the `speed_limit` select                             |
| BOOST                               | the `boost` number                                   |
| DISPLAY                             | the `brightness` or `guard_time` number              |

CTRL and METER feed dev sensors only. A `model: air` hub without `expose_dev_sensors`
polls STATS and BATTERY. HANDSHAKE goes out once per connection, outside the burst.

After a write goes out, `schedule_write_verify_()` sets `force_poll_stats_` and calls
`update()` 500 ms later (`FORCE_STATS_DELAY_MS`). The forced call cancels a running
burst and starts a new one at STATS.

## Notify handling

`handle_notify_()` checks the frame with `validate_notify()` and hands the payload to
the parser for its address from the `PARSERS` table. A `static_assert` fails the build
when a polled address has no parser. Rejected frames and unknown addresses are logged
at most once every 5 s.

`parse_stats_()`, for a valid STATS payload:

1. restarts the silent-link timer, publishes speed, distances, SOC and gear start
   (`stats_samples()` drops readings outside their bounds) and syncs the gear and
   gear count selects, writing the gear below when the BMS reports one the gear
   count has no label for;
2. runs the `enforce_gear_mode_3` check and publishes `brake`;
3. copies the flag bytes into the register cache and dispatches queued writes;
4. publishes Power, Light, the `FLAG_CONTROLS` switches, the speed limit and speed
   unit selects and `pas_limit` from the cache, so a write dispatched in step 3
   already shows;
5. adapts the poll interval, clears the persisted light bit on the controller's
   falling edge (models with `light_bit_persists`), feeds auto-shutdown and the
   idle-disconnect timer, and settles a running probe.

BATTERY, CTRL, MOTOR, ENERGY and METER payloads are length-checked and published
through the `SensorField` tables in `fiido_bms.h`. The signed motor and controller
temperatures are decoded outside the tables. SPEEDLIM, BOOST and DISPLAY update the
register cache and their entities.

Sensors, selects and numbers go through `publish_changed()`, which skips a value the
entity already holds. Switches and binary sensors deduplicate in ESPHome itself.

## Register cache and writes

A write replaces a whole byte, so a control that changes one bit builds the byte from
the last value read. `RegisterCache` (`fiido_state.h`) holds one optional byte per
address in `CACHED_REGISTERS`: 0x25, 0x27, 0x28, 0x2B, 0x2C, 0x38 and 0x39 from STATS,
0x3C from SPEEDLIM, 0x52 from BOOST, 0x57 and 0x58 from DISPLAY. A disconnect clears
it. Bit meanings are in [STATS payload](PROTOCOL.md#stats-payload).

Every setter asks `gate_()` first, which calls `gate_write()` (`fiido_state.cpp`):

| Verdict                 | When                                                         | Result |
|-------------------------|--------------------------------------------------------------|--------|
| `REJECT_BLE_DISABLED`   | the `bluetooth` switch is off                                | entity reverts |
| `REJECT_WRONG_MODEL`    | the bike offers the GATT service of another model            | entity reverts |
| `QUEUE_DISCONNECTED`    | the link is not `READY`                                      | write queued, client enabled, probe armed |
| `DEFER_COLD_CACHE`      | a byte the write builds on has not been read yet             | write queued until the next STATS |
| `REJECT_CONTROLLER_OFF` | light, gear or gear count while the controller is off        | entity reverts |
| `SEND`                  | otherwise                                                    | frame goes out |

A reverted switch republishes the opposite of the request (Light always shows off), a
select or number its previous value. The queue (`PendingWrites`, 32 slots, the oldest entry dropped when full) runs
in order when a valid STATS arrives. Turning the `bluetooth` switch off clears it,
cancels pending timeouts and disables the client. Turning it on lifts the GATT
mismatch block and starts a probe.

Single-bit switches are rows in `FLAG_CONTROLS` (`fiido_bms.h`): address, cache slot,
mask, the bits written for on and for off, and the entity. `set_flag_()` runs the
gate, then `write_masked_bits_()` keeps the cached bits outside the mask
(`compute_masked_write()`, see [Which write type](PROTOCOL.md#which-write-type) for
ADDR 0x39), sends the frame, stores the written byte in the cache and schedules the
verification STATS. The numbers write whole bytes from `BYTE_CONTROLS`. Power, light,
gear, gear count, speed limit and speed unit have their own setters, and the speed
limit sequence is in [Speed limit](PROTOCOL.md#speed-limit).

## BLE lifecycle

`manage_lifecycle_()` runs every second and acts on `decide_lifecycle()`
(`fiido_state.cpp`):

| Action            | Condition                                                                 | Effect |
|-------------------|---------------------------------------------------------------------------|--------|
| `IDLE_DISCONNECT` | `READY`, controller off for `idle_disconnect` (15 min), no queued write    | client disabled |
| `SILENT_LINK`     | link open and no valid STATS for the `Silent link timeout` (see [Link behaviour](README.md#link-behaviour)) | queued writes dropped with a warning, client disabled |
| `PROBE_TIMEOUT`   | probe running, not `READY` after 60 s, no queued write, no write dispatched in the last 10 s | client disabled |
| `START_PROBE`     | client disabled for 5 min and no GATT mismatch block                      | client enabled, probe started |

A probe settles on its first valid STATS (`decide_probe_outcome()`): with the
controller on the link stays; with it off the link stays for 10 s after a queued write
went out (`WRITE_VERIFY_WINDOW_MS`), otherwise the client is disabled.

After service discovery `FiidoLink::resolve()` looks up the characteristics of the
model's GATT profile. When they are missing the hub sets a warning status and disables
the client. When the bike offers the other known profile it also clears the queue,
rejects writes and stops probing until the `bluetooth` switch is cycled or the node
restarts.

Auto-shutdown is separate: with the `auto_shutdown` switch on, after 15 minutes
without activity (controller switched on, gear change, movement, light change) while
the controller is on, the hub writes Power OFF. It does not release the link; `idle_disconnect` does that once the controller is
off.

## Adding an entity

`tests/python/test_tables.py` cross-checks the tables below: dev key sets, poll groups
against the `enable_*_poll()` methods of the hub, default names, restore modes and the
Air entity set.

### A switch on one bit

1. `fiido_protocol.h`: a `RegBit` in the `flags_XX` namespace of the register, a
   `FlagView` field and its line in `decode_flags()`. For a register not cached yet,
   also a `stats` offset, a `StatsView` field and its copy in `decode_stats()`, the
   address in `CACHED_REGISTERS` (`fiido_state.h`) and the `set` call in
   `cache_flag_registers_()`.
2. `fiido_bms.h`: a `FlagId` value and a `FLAG_CONTROLS` row at the same position
   (`inverted_flag_control()` when the bit is set while the feature is off), a
   `flag_row_is()` `static_assert`, the setter declaration and `SUB_SWITCH(name)`. Dev
   controls go inside `#ifdef USE_FIIDO_BMS_DEV`.
3. `fiido_bms.cpp`: the setter calls `set_flag_()`. `publish_flag_entities_()` already
   publishes every row.
4. `fiido_bool_switch.h`: a class deriving from `FiidoBoolSwitch<&FiidoBMSHub::set_name_enable>`.
5. `switch.py`: the class and a `SWITCHES` row. In `__init__.py`, the key in
   `DEV_SWITCH_KEYS` for a dev control, and in `MODEL_ENTITY_SETS` when an Air hub
   should build it by default or never.
6. A `decode_flags()` test in `tests/test_protocol/test_decode.cpp` and a row in the
   [switch table](README.md#entities-switch).

### A sensor

1. `fiido_protocol.h`: the field offset in the namespace of its poll (`battery`,
   `ctrl`, `motor`, `energy`, `meter`).
2. `fiido_bms.h`: `SUB_SENSOR(name)` and a row with offset, width and divisor in the
   `SensorField` table of that poll. A field that needs its own decoding gets code in
   the `parse_*_()` function instead.
3. `sensor.py`: a `SENSORS` row and a `SENSOR_POLL_GROUP` entry, and in `__init__.py`
   the key in `DEV_SENSOR_KEYS` for a dev sensor.
4. A row in the [sensor table](README.md#entities-sensor).

A sensor fed by STATS uses `None` in `SENSOR_POLL_GROUP` and goes through
`StatsView`, `stats_samples()` and `publish_stats_samples_()` instead of a field table,
with a `STATS_SENSORS` row for `dump_config`.

## Testing

- 144 host unit tests (PlatformIO + Unity) in `tests/test_protocol/` cover
  `fiido_protocol`, `fiido_state` and `fiido_model.h`:

  ```
  pio test -d tests -e native
  ```

- 71 Python tests in `tests/python/` cover code generation and the tables; they need
  ESPHome installed:

  ```
  python -m unittest discover -s tests/python
  ```

- CI (`.github/workflows/test.yml`) also runs pre-commit, compiles the pure layers
  under strict GCC warnings, runs clang-tidy over an ESP32 compile database, builds
  `.github/ci-build.yaml` for four ESP32 boards with and without the dev code and
  fails on any component warning, and validates and builds at the ESPHome floor.
  `audit.yml` runs zizmor and dependency review, `codeql.yml` runs CodeQL.
