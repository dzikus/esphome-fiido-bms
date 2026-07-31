# ESPHome Fiido BMS

![tests](https://github.com/dzikus/esphome-fiido-bms/actions/workflows/test.yml/badge.svg?branch=main)

<a href="https://www.buymeacoffee.com/dzikus" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>

ESPHome external component that exposes a Fiido ebike to Home
Assistant over BLE. One component instance ("hub") per bike. Multiple hubs run on a
single ESP32 with the burst poll of each hub offset in time so the radio is not
contended.

The component reads the bike state out of its BMS, parses the proprietary frames the
official app uses, and writes back to flip a small set of physical controls:
power, light, gear, gear count, speed limit, speed unit, horn, key sound, throttle,
slow mode on boot. A further set (cruise, start mode, insensitivity, show total km,
auto screen off, ring, double speed, bike guard, display brightness, boost, guard
time, watch pairing) is experimental and off by default; see Experimental controls.

The document is split in two:

- **Part 1 - Integrator** (yaml only): how to wire a bike into an ESPHome device and
  what entities you get.
- **Part 2 - Extender** (C++ + python): how the component is structured, how it polls,
  how it writes, and how to add a new sensor, binary sensor, select, switch, number,
  or button.

---

## What this is

### Hardware

The component speaks the BLE protocol of the official Fiido app (`com.fiido.meter`).
That protocol is model-agnostic: the same frame format and register map are used
across the adult Fiido ebike line, so the component is not tied to one model. It
was developed and verified empirically on a Fiido C11 Pro and a Fiido M1 Pro 2025;
everything here was confirmed on that hardware unless noted. Other adult Fiido
ebikes exposing the same `Fiido_*` BLE service are likely compatible but untested -
reports and PRs welcome. The K1 / Kidz children's line uses a different register
layout and is not covered.

| Bike                  | BLE name        | MAC (example)       | Spec             |
|-----------------------|-----------------|---------------------|------------------|
| Fiido C11 Pro         | `Fiido_C11Pro`  | `XX:XX:XX:XX:XX:XX` | 48V/11.6Ah, 350W, 28 inch |
| Fiido M1 Pro 2025     | `Fiido_M1PRO`   | `XX:XX:XX:XX:XX:XX` | 48V/11.6Ah, 500W, 22 inch |

MACs above are placeholders; scan your bike with any BLE tool to get the real
address and substitute it in `ble_client.mac_address`.

Both bikes use the same BLE topology and the same wire protocol. C11 is locked to
3-gear mode at the firmware level; M1 supports both 3-gear and 5-gear.

ESP32 side: any board capable of `esp32_ble_tracker` + `ble_client` plus enough
RAM/Flash headroom (component baseline: ~16% RAM, ~47% Flash on ESP32 with
`esp-idf` framework). The reference deployment uses an ESP32 with LAN8720 Ethernet
but Wi-Fi works too.

### BLE topology (identical on both bikes)

| Service                                  | Characteristic                            | Use         |
|------------------------------------------|-------------------------------------------|-------------|
| `00010203-0405-0607-0809-0A0B0C0DFFE0`   | `...FFE1` handle 0x12 (NOTIFY)            | Rx          |
| `00010203-0405-0607-0809-0A0B0C0DFFE0`   | `...FFE2` handle 0x10 (WRITE + WRITE_NR)  | Tx          |
| `0xFE59`                                 | Nordic Secure DFU                         | not used    |

Negotiated MTU: 247. The component does no DFU and never writes to `0xFE59`.

### What it exposes per bike

Numbers are after the default `expose_dev_sensors: false`. With `expose_dev_sensors:
true` you also get raw diagnostic readouts (HW/SW versions, controller upper/lower
voltage, motor magnetic/wire/steel/ratio, crank torque/rpm, this-trip / total energy,
meter mode data). Those are created with `disabled_by_default: true` so they stay
hidden in HA until you enable them per entity.

- 10 sensors always on: battery voltage, battery capacity, motor wheel diameter,
  motor temperature, motor capacity (W), startup time, speed, trip distance, total
  distance, battery SOC.
- 26 dev sensors gated by `expose_dev_sensors` (HW/SW versions, controller upper /
  lower voltage / current / temperature, motor magnetic / wire / steel / reduction
  ratio, battery current and trip / total energy, crank RPM / torque, meter
  diagnostics, gear start).
- 1 binary sensor always on: `connected` (BLE link state).
- 1 dev binary sensor gated by `expose_dev_sensors`: `brake` (STATS 0x2A bit 5,
  hardware behaviour not user-verified).
- 4 selects: `gear` (3 or 5 options depending on mode), `mode` (3 / 5, hidden on
  bikes pinned to 3-gear), `speed_limit` (6 km/h / 25 km/h / No limit), `speed_unit`
  (km/h / mph).
- 8 stable switches: `motor` (Power), `light`, `auto_shutdown`, `speaker` (Horn),
  `key_sound`, `throttle`, `slow_mode_on_boot`, `bluetooth` (BLE link master switch).
- 8 experimental switches: `cruise`, `start_mode`, `insensitivity`, `show_total_km`,
  `auto_screen_off`, `ring`, `double_speed`, `bike_guard`. Created `disabled_by_default`
  except `bike_guard`. See Experimental controls.
- 3 numbers: `guard_time` (visible), `brightness` and `boost` (`disabled_by_default`).
- 1 button: `pair_watch` (`disabled_by_default`).

### Screenshots

The component in Home Assistant, shown as the device page split into its four
entity-category cards:

![Controls: power, light, gear, speed limit](images/ha-controls.png)
![Sensors: SOC, voltage, motor temperature, speed, trip and total distance](images/ha-sensors.png)
![Configuration: auto shutdown, bluetooth, gear count, horn, key sound, slow mode, speed unit, throttle](images/ha-configuration.png)
![Diagnostic: battery capacity, BLE link state, motor capacity, wheel diameter, uptime](images/ha-diagnostic.png)

---

## Part 1 - Integrator (YAML)

### Minimum config

This component uses the ESPHome sub-device API, so it needs **ESPHome 2025.7.0 or
newer**. Pin it with `esphome: { min_version: 2025.7.0 }` so an older install fails
fast instead of erroring deep in code generation.

Two declarations per bike. Replace the MAC with the bike's MAC.

```yaml
external_components:
  - source: github://dzikus/esphome-fiido-bms
    components: [fiido_bms]

esp32_ble_tracker:

ble_client:
  - id: ble_c11
    mac_address: XX:XX:XX:XX:XX:XX

fiido_bms:
  - id: hub_c11
    ble_client_id: ble_c11

sensor:
  - platform: fiido_bms
    fiido_bms_id: hub_c11

binary_sensor:
  - platform: fiido_bms
    fiido_bms_id: hub_c11

select:
  - platform: fiido_bms
    fiido_bms_id: hub_c11

switch:
  - platform: fiido_bms
    fiido_bms_id: hub_c11
```

That gives you all default entities, named in English, with default icons and
restore modes. Every individual entity can be customised; see **Override per-entity**
below.

### Hub options

Set on the `fiido_bms:` entry, not on the platforms.

| Option                | Type     | Default | Effect                                                                                          |
|-----------------------|----------|---------|-------------------------------------------------------------------------------------------------|
| `ble_client_id`       | id       | -       | Required. Points to the `ble_client` entry for this bike's MAC.                                 |
| `startup_delay`       | time     | `0s`    | Hub waits this long after boot before its first poll. Auto-derived from `hub_index` if omitted. |
| `update_interval_on`  | time     | `3s`    | Burst rotation period while motor controller is ON (bit 7 ADDR 0x27 set).                       |
| `update_interval_off` | time     | `15s`   | Burst rotation period while motor controller is OFF. Fast enough to catch a physical power-on.  |
| `idle_disconnect`     | time     | `15min` | After motor has been OFF this long with no pending writes, the BLE link is dropped.             |
| `expose_dev_sensors`  | bool     | `false` | When true, dev sensors and dev binary sensors are created (disabled in HA).                     |
| `ui_gear_mode_3`      | bool     | `false` | HA UI only: hides the `mode` select and shrinks `gear` to 4 options. Does not change BMS state. |
| `enforce_gear_mode_3` | bool     | `false` | Runtime: writes mode 3 to BMS when STATS reports 5-gear while the motor controller is ON (60s cooldown, ble_user_enabled). |
| `update_interval`     | time     | `1s`    | PollingComponent baseline tick. The component runs an adaptive gate on top.                     |

The auto-offset behaviour spreads multiple hubs evenly: hub N out of M starts
`(N * update_interval_on) / M` after boot, so two bikes on one ESP32 do not poll the
radio at the exact same millisecond.

### Entities (sensor)

All sensors are scalar floats. Sensors marked **dev** are gated by
`expose_dev_sensors`. ADDR is the BMS register the value lives in; offset (when given)
is the byte offset inside the STATS poll payload `[0..52]`.

**Always on** (created regardless of `expose_dev_sensors`):

| Key                       | Default name           | Unit  | Source       | Notes |
|---------------------------|------------------------|-------|--------------|-------|
| `battery_voltage`         | Battery Voltage        | V     | BATTERY 0x80 | 2B BE / 10 |
| `battery_capacity`        | Battery Capacity       | Ah    | BATTERY 0x7E | 2B BE / 10 |
| `motor_wheel_diameter`    | Motor Wheel Diameter   | in    | MOTOR 0x9C   |     |
| `motor_temperature`       | Motor Temperature      | C     | MOTOR        |     |
| `motor_capacity`          | Motor Capacity         | W     | MOTOR        |     |
| `startup_time`            | Uptime                 | s     | ENERGY 0xD3  | seconds since BMS power-up |
| `bicycle_speed`           | Speed                  | km/h  | STATS 0x23   | 2B BE / 10 |
| `current_kilometers`      | Trip Distance          | km    | STATS 0x21   | 2B BE / 10 |
| `total_kilometers`        | Total Distance         | km    | STATS 0x1F   | 4B BE / 10 |
| `battery_soc`             | Battery SOC            | %     | STATS 0x24   | 1B, matches the bike display bars |

**Dev** (created only with `expose_dev_sensors: true`, then `disabled_by_default`):

| Key                       | Default name             | Unit  | Source       |
|---------------------------|--------------------------|-------|--------------|
| `battery_current`         | Battery Current          | A     | BATTERY 0x85 |
| `battery_current_voltage` | Battery Current Voltage  | V     | BATTERY      |
| `battery_manufacturer`    | Battery Manufacturer     | -     | BATTERY      |
| `battery_hw_version`      | Battery HW Version       | -     | BATTERY      |
| `battery_sw_version`      | Battery SW Version       | -     | BATTERY      |
| `ctrl_upper_voltage`      | Controller Upper Voltage | V     | CTRL         |
| `ctrl_lower_voltage`      | Controller Lower Voltage | V     | CTRL         |
| `ctrl_current`            | Controller Current       | A     | CTRL         |
| `ctrl_temperature`        | Controller Temperature   | C     | CTRL         |
| `ctrl_hw_version`         | Controller HW Version    | -     | CTRL         |
| `ctrl_sw_version`         | Controller SW Version    | -     | CTRL         |
| `ctrl_version`            | Controller Version       | -     | CTRL         |
| `ctrl_manufacturer`       | Controller Manufacturer  | -     | CTRL         |
| `motor_version`           | Motor Version            | -     | MOTOR        |
| `motor_magnetic`          | Motor Magnetic           | -     | MOTOR        |
| `motor_wire_count`        | Motor Wire Count         | -     | MOTOR        |
| `motor_steel_count`       | Motor Steel Count        | -     | MOTOR        |
| `motor_reduction_ratio`   | Motor Reduction Ratio    | -     | MOTOR        |
| `crank_torque`            | Crank Torque             | Nm    | ENERGY       |
| `crank_rpm`               | Crank RPM                | rpm   | ENERGY       |
| `this_take_energy`        | Trip Energy              | Wh    | ENERGY       |
| `total_take_energy`       | Total Energy             | Wh    | ENERGY       |
| `bicycle_gear_start`      | Gear Start               | -     | STATS        |
| `meter_hw_version`        | Meter HW Version         | -     | METER        |
| `meter_sw_version`        | Meter SW Version         | -     | METER        |
| `meter_mode_data`         | Meter Mode Data          | -     | METER        |

### Entities (binary_sensor)

Same `expose_dev_sensors` gate as the sensor platform: dev entries are skipped
entirely when the flag is false, and created with `disabled_by_default: true` when
it is true.

**Always on** (created regardless of `expose_dev_sensors`):

| Key         | Default name   | Source             | Notes                          |
|-------------|----------------|--------------------|--------------------------------|
| `connected` | BLE Connected  | BLE link state     | diagnostic category            |

**Dev** (created only with `expose_dev_sensors: true`, then `disabled_by_default`):

| Key     | Default name | Source              | Notes                                                          |
|---------|--------------|---------------------|----------------------------------------------------------------|
| `brake` | Brake        | STATS 0x2A bit 5    | hardware behaviour not user-verified on C11 / M1; do not rely on it |

### Entities (select)

| Key           | Default name | Options                            | Source / write                                       |
|---------------|--------------|------------------------------------|------------------------------------------------------|
| `gear`        | Gear         | OFF / eco / sport / turbo (3-gear) | STATS 0x26, WRITE L0 ADDR 0x26 (1B raw)              |
|               |              | OFF / eco / normal / sport / turbo / turbo+ (5-gear) |                                    |
| `mode`        | Gear Count   | 3 / 5                              | STATS 0x25 nibble, WRITE J0 ADDR 0x25 (upper nibble = max_gear, lower preserved) |
| `speed_limit` | Speed Limit  | 6 km/h / 25 km/h / No limit        | STATS 0x27 bit 5 + ADDR 0x3C value (separate poll, two WRITE frames in order)    |
| `speed_unit`  | Speed Unit   | km/h / mph                         | STATS 0x28 bit 7, WRITE L0 ADDR 0x28 (read-modify-write) |

Note on `mode`: bikes with `ui_gear_mode_3: true` do not expose this entity. The
`gear` select shrinks to a 4-option list in that case. The `mode` UI is purely
cosmetic; physical BMS state can still be flipped to 5 by other apps. Use
`enforce_gear_mode_3: true` to also pin the BMS itself.

### Entities (switch)

All switches default to `RESTORE_DEFAULT_ON` except `motor`, `light`, `ring`,
`double_speed`, and `bike_guard`, which default to `RESTORE_DEFAULT_OFF`. Restore mode
is overridable per entity.

| Key                  | Default name        | Source              | Write                                |
|----------------------|---------------------|---------------------|--------------------------------------|
| `motor`              | Power               | STATS 0x27 bit 7    | WRITE L0 ADDR 0x27 (R-M-W, bit 7)    |
| `light`              | Light               | STATS 0x27 bit 3    | WRITE L0 ADDR 0x27 (R-M-W, bit 3, rejected if motor is OFF) |
| `auto_shutdown`      | Auto Shutdown       | local (HA-restored) | gates the automatic motor power-off after 15 min without activity (speed, gear or light change). Does not affect `idle_disconnect` |
| `speaker`            | Horn                | STATS 0x38 bits 3:2 | WRITE L0 ADDR 0x38 (R-M-W, ON = 00, OFF = 01) |
| `key_sound`          | Key Sound           | STATS 0x2C bit 4    | WRITE L0 ADDR 0x2C (R-M-W, **inverted**: bit 4 = 0 means ON) |
| `throttle`           | Throttle            | STATS 0x2B bit 1    | WRITE L0 ADDR 0x2B (R-M-W, **inverted**: bit 1 = 0 means active) |
| `slow_mode_on_boot`  | Slow Mode on Boot   | STATS 0x2C bit 6    | WRITE L0 ADDR 0x2C (R-M-W, bit 6 = 1 forces 6 km/h limit on next power-up) |
| `bluetooth`          | Bluetooth           | local               | master switch for the BLE link itself |
| `cruise`             | Cruise Control      | STATS 0x27 bit 6    | WRITE L0 ADDR 0x27 (R-M-W, bit 6)    |
| `start_mode`         | Start Mode          | STATS 0x27 bit 1    | WRITE L0 ADDR 0x27 (R-M-W, bit 1)    |
| `insensitivity`      | Insensitivity       | STATS 0x27 bit 0    | WRITE L0 ADDR 0x27 (R-M-W, bit 0)    |
| `show_total_km`      | Show Total Km       | STATS 0x28 bit 6    | WRITE L0 ADDR 0x28 (R-M-W, bit 6)    |
| `auto_screen_off`    | Auto Screen Off     | STATS 0x39 bit 3    | WRITE J0 ADDR 0x39 (R-M-W, bit 3, low 5 bits only) |
| `ring`               | Ring                | STATS 0x39 bit 1    | WRITE J0 ADDR 0x39 (R-M-W, bit 1, low 5 bits only) |
| `double_speed`       | Double Speed        | STATS 0x2B bit 5    | WRITE L0 ADDR 0x2B (R-M-W, bit 5)    |
| `bike_guard`         | Bike Guard          | STATS 0x2B bit 6    | WRITE L0 ADDR 0x2B (R-M-W, bit 6)    |

The eight rows from `cruise` down are experimental and capability-gated (see
Experimental controls). All except `bike_guard` are created `disabled_by_default`.

### Entities (number)

Add a `number:` platform block for the hub (same shape as `switch:`) to expose these.
All three use BOX input mode and the config entity category. Real hardware ranges are
unknown, so each spans the full byte (0 - 255, step 1).

| Key          | Default name       | Unit | Range   | Source / write                            |
|--------------|--------------------|------|---------|-------------------------------------------|
| `guard_time` | Guard Time         | s    | 0 - 255 | DISPLAY 0x58, WRITE J0 ADDR 0x58 (1B raw) |
| `brightness` | Display Brightness | -    | 0 - 255 | DISPLAY 0x57, WRITE J0 ADDR 0x57 (1B raw) |
| `boost`      | Boost              | -    | 0 - 255 | BOOST 0x52, WRITE L0 ADDR 0x52 (1B raw)   |

`brightness` and `boost` are experimental and created `disabled_by_default`;
`guard_time` is visible. See Experimental controls.

### Entities (button)

Add a `button:` platform block for the hub (same shape as `switch:`) to expose this.

| Key          | Default name | Write                                          |
|--------------|--------------|------------------------------------------------|
| `pair_watch` | Pair Watch   | WRITE J0 ADDR 0x09 (6-byte ESP32 BLE address)  |

Pressing it sends the ESP32's own BLE address to ADDR 0x09, the register the app uses
to pair a proximity-unlock companion. Experimental, `disabled_by_default`, and
unverified on C11 / M1. See Experimental controls.

### Experimental controls

`cruise`, `start_mode`, `insensitivity`, `show_total_km`, `auto_screen_off`, `ring`,
`double_speed`, `bike_guard`, the numbers `brightness`, `boost`, `guard_time`, and the
`pair_watch` button drive registers that exist in the protocol but are not confirmed to
have any effect on the C11 Pro or M1 Pro 2025. Those bikes report the matching
capability as unsupported, and toggling the controls produced no observable change in
testing. The BMS still latches the written bit and returns it on the next read, which
is not proof the feature works.

They are capability-gated: other Fiido models that report the capability as supported
may honour them. Because they are unverified, every switch above except `bike_guard`,
plus `brightness`, `boost`, and `pair_watch`, is created `disabled_by_default` and
stays hidden in HA until you enable it per entity. `bike_guard` and `guard_time` are
created visible, but `bike_guard` ON is incomplete: the app performs an extra unlock
step the component does not send. If any of these work on your bike, a report or PR is
welcome.

### Override per-entity

Every key on every platform accepts the normal ESPHome entity config. Override the
name, icon, category, restore mode, or any other entity field directly under the key:

```yaml
sensor:
  - platform: fiido_bms
    fiido_bms_id: hub_c11
    device_id: dev_c11
    battery_voltage:
      name: "C11 Pack Voltage"
      icon: "mdi:battery-charging-high"
    battery_soc:
      name: "C11 Charge"

switch:
  - platform: fiido_bms
    fiido_bms_id: hub_c11
    device_id: dev_c11
    motor:
      name: "C11 Bike Power"
      restore_mode: ALWAYS_OFF
    light:
      icon: "mdi:lightbulb-on"
```

Schema defaults are injected before validation, so omitted fields keep their
defaults. If you do not set `name`, the default in the tables above is used.

### Two bikes on one ESP32

Two `ble_client` entries and two `fiido_bms` hubs is all that is needed. The hub
auto-offset spreads the polls, and ESP32 supports up to 3 concurrent BLE client
connections out of the box (max 9 with `max_connections` raised in `esp32_ble_tracker`).

```yaml
ble_client:
  - id: ble_c11
    mac_address: XX:XX:XX:XX:XX:XX
  - id: ble_m1
    mac_address: XX:XX:XX:XX:XX:XX

fiido_bms:
  - id: hub_c11
    ble_client_id: ble_c11
    ui_gear_mode_3: true          # HA UI 3-gear only
    enforce_gear_mode_3: true     # also force BMS to 3-gear (revert external 5-gear change)
  - id: hub_m1
    ble_client_id: ble_m1
```

Each platform then needs one entry per hub. Use `device_id` to put entities under a
separate sub-device in HA:

```yaml
esphome:
  devices:
    - id: dev_c11
      name: "Fiido C11 Pro"
    - id: dev_m1
      name: "Fiido M1 PRO 2025"

sensor:
  - platform: fiido_bms
    fiido_bms_id: hub_c11
    device_id: dev_c11
  - platform: fiido_bms
    fiido_bms_id: hub_m1
    device_id: dev_m1
```

### Charging behaviour

- **C11 Pro**: BMS shuts the BLE radio off completely while the charger is plugged
  in. The hub will fail to connect; the entity `connected` goes to OFF. Unplug the
  charger to bring the link back.
- **M1 Pro 2025**: BMS stays on BLE while charging, but no register reports the
  charge state. `battery_voltage` reads nominal 48.0 V, `battery_current` reads 0.0
  A regardless. Do not use these to detect charging.

### App vs ESPHome

The bike's BMS only accepts one BLE central at a time. While ESPHome is connected,
the official app cannot pair. To use the app:

1. Disable the `bluetooth` switch on the bike's HA device, or power the ESP32 off.
2. Connect with the app, make your changes, disconnect from the app.
3. Re-enable the `bluetooth` switch (or power the ESP32 back on).

ESPHome will reconnect and start polling again on the next tick.

---

## Part 2 - Extender (Architecture)

### Component layout

```
components/fiido_bms/
  __init__.py                  hub config + schema, auto-offset, dev gating
  sensor.py                    sensor platform: 36 keys (10 always on, 26 dev), schema + to_code
  binary_sensor.py             binary_sensor platform: 2 keys (1 always on, 1 dev)
  select.py                    4 select classes + platform
  switch.py                    16 switch classes + platform (8 stable, 8 experimental)
  number.py                    3 number classes + platform (guard_time, brightness, boost)
  button.py                    1 button class + platform (pair_watch)

  fiido_protocol.{h,cpp}       pure C++: CRC XOR, frame builders, validate, POLL_TABLE
  fiido_bms.{h,cpp}            FiidoBMSHub: BLE client + PollingComponent + state machine

  fiido_bool_switch.h          all 16 switches via two templates + one-line subclasses:
                               FiidoBoolSwitch<Setter>             write_state only
                               FiidoBoolSwitchWithRestore<Setter>  setup() restore + defer
                               motor / light / speaker / key_sound / throttle / slow_mode
                                 and the 8 experimental switches are write-only; the bit
                                 + ADDR live in the hub setter
                               bluetooth / auto_shutdown use the with-restore template
                                 (local state, re-applied on boot)
  fiido_number.h               3 numbers via one template (brightness / boost / guard_time)
  fiido_button.h               pair_watch button

  fiido_gear_select.{h,cpp}         ADDR 0x26, count-aware (3 vs 5)
  fiido_mode_select.{h,cpp}         ADDR 0x25 nibble-packed
  fiido_speed_limit_select.{h,cpp}  ADDR 0x3C + bit 5 ADDR 0x27 pair
  fiido_speed_unit_select.{h,cpp}   bit 7 ADDR 0x28
```

`fiido_protocol.{h,cpp}` is pure C++ with no ESPHome dependencies and is what the
PlatformIO unit tests link against. Everything else needs the ESPHome runtime.

### Polling model

`FiidoBMSHub` inherits `PollingComponent` with a fixed 1-second baseline. On every
tick `update()` checks a gate:

```
if (now - last_burst_ms_) < desired_interval_ms_:
    return
last_burst_ms_ = now
send_burst_poll_()
```

`desired_interval_ms_` flips between `update_interval_on_ms_` (default 3s, motor on)
and `update_interval_off_ms_` (default 15s, motor off) inside `parse_stats_`, based
on bit 7 of ADDR 0x27. The flip is one-way per STATS frame; the gate decides when
the next burst actually fires.

A burst walks the whole `POLL_TABLE` (9 entries) scheduled 5 ms apart in time via
`set_timeout("burst", 5ms)`:

```
BATTERY -> CTRL -> MOTOR -> ENERGY -> STATS -> METER -> SPEEDLIM -> BOOST -> DISPLAY
```

5 ms is the empirically established sweet spot; anything below ~3 ms makes the BMS
drop frames. Polls whose group is disabled (no consumer sensor gated by
`expose_dev_sensors`, or a number entity that is not configured) are skipped at burst
time; a safety counter bounds the skip loop so a fully disabled rotation cannot spin
forever.

After every successful WRITE the hub sets `force_poll_stats_ = true`, cancels the
in-flight burst, and re-enters burst rotation starting with STATS so the WRITE's
visible effect (a flipped bit) shows up in HA within one burst step.

### BLE lifecycle state machine

```
                   motor_off_since >= idle_disconnect
                       AND pending_writes empty
[CONNECTED] -------------------------------------------------> [DISCONNECTED]
     ^                                                                |
     |                                                                |
     |  STATS, motor on                                               |
     |                                                                |
[PROBING] <-----------------------------------------------------------/
     |                            disconnected_since >= PERIODIC_PROBE
     |                                  (or HA-driven WRITE)
     |
     |--- STATS, motor off, no pending --> set_enabled(false) --> [DISCONNECTED]
     |
     \--- PROBE_WINDOW expired, not connected, no pending --> [DISCONNECTED]
```

| Transition                         | Trigger                                              | Effect                                                                  |
|------------------------------------|------------------------------------------------------|-------------------------------------------------------------------------|
| `CONNECTED -> DISCONNECTED`        | `now - motor_off_since_ms_ >= idle_disconnect_ms_` and no pending WRITE | `set_enabled(false)`, BLE link dropped                                  |
| `DISCONNECTED -> PROBING`          | `now - disconnected_since_ms_ >= PERIODIC_PROBE_MS` (5 min) | `set_enabled(true)`, scan + connect                                     |
| `PROBING -> CONNECTED`             | STATS arrives with bit 7 ADDR 0x27 set               | stay connected, run burst polls                                         |
| `PROBING -> DISCONNECTED`          | STATS arrives with bit 7 ADDR 0x27 clear and no pending | `set_enabled(false)`                                                    |
| `PROBING -> DISCONNECTED` (timeout)| `PROBE_WINDOW_MS` (60s) elapsed, still not linked, no pending | `set_enabled(false)`                                                    |
| any `-> PROBING`                   | HA writes a control while link is down               | `enqueue_pending_write_(fn)` + `ensure_enabled_for_write_()` + `set_enabled(true)` |
| WRITE drained                      | STATS valid after re-connect                         | `dispatch_pending_writes_()` runs every enqueued lambda                 |

None of these transitions is gated by `auto_shutdown` - that switch controls a
different mechanism (the automatic motor power-off, see the switch table above).
The BLE link is released on `idle_disconnect` regardless of it.

The `bluetooth` switch is a hard kill: turning it OFF
clears the pending-writes queue, cancels the pending timeouts, calls
`parent_->set_enabled(false)`, and any subsequent WRITE setter rejects the change
(`motor`/`light`/`speaker` re-publish the inverted state, others log + return).

### State cache and read-modify-write

The bike's WRITE protocol replaces an entire byte, so toggling a single bit needs
the latest copy of that byte first. The hub keeps a one-byte cache per relevant
address, populated from the STATS poll:

| ADDR | Offset in STATS payload | What lives there                              | Used by                                                   |
|------|-------------------------|-----------------------------------------------|-----------------------------------------------------------|
| 0x25 | payload[32]             | gear range, nibble-packed                     | `set_gear_mode`                                           |
| 0x27 | payload[34]             | bit 7 = motor, bit 6 = cruise, bit 5 = speed_limit_en, bit 3 = light, bit 1 = start_mode, bit 0 = insensitivity | `set_motor_enable`, `set_light_enable`, `set_speed_limit`, `set_cruise_enable`, `set_start_mode_enable`, `set_insensitivity_enable` |
| 0x28 | payload[35]             | bit 7 = speed unit (1 = mph), bit 6 = show_total_km, other UI flags | `set_speed_unit`, `set_show_total_km_enable`             |
| 0x2B | payload[38]             | bit 1 = throttle (inverted), bit 5 = double_speed, bit 6 = bike_guard | `set_throttle_enable`, `set_double_speed_enable`, `set_bike_guard_enable` |
| 0x2C | payload[39]             | bits 3:2 = gear way, bit 4 = key_sound (inverted), bit 6 = slow_mode_on_boot | `set_key_sound_enable`, `set_slow_mode_enable`            |
| 0x38 | payload[51]             | bits 3:2 = speaker (binary on these bikes), other flags | `set_speaker_enable`                                      |
| 0x39 | payload[52]             | bit 3 = auto_screen_off, bit 1 = ring; only bits 4..0 cached, write masks bits 7..5 and uses the J0 (0xFF) frame | `set_auto_screen_off_enable`, `set_ring_enable`          |
| 0x3C | separate poll (SPEEDLIM) | speed limit value in km/h                    | `set_speed_limit` (paired with bit 5 ADDR 0x27)           |
| 0x52 | separate poll (BOOST)   | PAS boost level                               | `set_boost`                                              |
| 0x57, 0x58 | separate poll (DISPLAY) | brightness (0x57), guard time (0x58)      | `set_brightness`, `set_guard_time`                       |

Each cache has a `_valid` flag. Setters reject writes (and re-publish the old state
to HA) when their cache byte is not yet valid; the next STATS frame populates it.
This is also why the first action after boot is sometimes deferred by one or two
seconds: the cache must be primed first.

If a WRITE arrives while disconnected, the setter wraps itself in a lambda and
pushes it to `pending_writes_`. The lifecycle state machine forces a re-connect,
and `dispatch_pending_writes_` runs the queue once STATS has come back valid.

### Frame format

```
POLL (read):            [0x46][0x64][0x55][len ][addr][CRC]      total: 6 bytes
WRITE J0:               [0x46][0x64][0xFF][plen][addr][...p ][CRC]
WRITE L0 (most):        [0x46][0x64][0xAA][plen][addr][...p ][CRC]
NOTIFY:                 [0x46][0x64][0xAA][plen][addr][...p ][CRC]
```

- Byte 0..1: `'F' 'd'`, the Fiido signature.
- Byte 2: frame type. `0x55` = poll, `0xFF` = WRITE J0, `0xAA` = WRITE L0 / NOTIFY.
- Byte 3: `len`. For poll frames this is how many bytes the BMS should send back;
  for WRITE / NOTIFY it is the payload length (frame length minus 6).
- Byte 4: address (register).
- Bytes 5..n-2: payload (WRITE / NOTIFY only).
- Last byte: XOR of all preceding bytes. `compute_crc(buf, len-1)` in
  `fiido_protocol.cpp`.

WRITE frames are fire-and-forget. The BMS does not return a NOTIFY for a WRITE
(the only exception is ADDR 0x25 mode change, which echoes back). Verification is
empirical: queue a STATS poll afterwards (`force_poll_stats_`) and read the bit
back from the next NOTIFY.

The full poll rotation:

| Name      | Addr | Len | Tx                       | Provides                                       |
|-----------|------|-----|--------------------------|------------------------------------------------|
| HANDSHAKE | 0x0D | 13  | `46 64 55 0D 0D 77`      | memory test pattern (sent once after connect)  |
| BATTERY   | 0x7B | 13  | `46 64 55 0D 7B 01`      | HW/SW/capacity/voltage/current/manufacturer    |
| CTRL      | 0xAF | 12  | `46 64 55 0C AF D4`      | controller HW/SW/upper/lower/current/temp      |
| MOTOR     | 0x96 | 12  | `46 64 55 0C 96 ED`      | motor version/wheel/temp/capacity              |
| ENERGY    | 0xC8 | 12  | `46 64 55 0C C8 B3`      | torque/RPM/trip/total energy/uptime            |
| STATS     | 0x05 | 53  | `46 64 55 35 05 47`      | speed/km/gear/SOC + every flag byte 0x05..0x39 |
| METER     | 0x60 | 13  | `46 64 55 0D 60 1A`      | meter HW/SW/mode                               |
| SPEEDLIM  | 0x3C | 1   | `46 64 55 01 3C 4A`      | current speed-limit value in km/h              |
| BOOST     | 0x52 | 1   | `46 64 55 01 52 24`      | PAS boost level                                |
| DISPLAY   | 0x57 | 2   | `46 64 55 02 57 22`      | display brightness (0x57) + guard time (0x58)  |

STATS and SPEEDLIM are always issued; CTRL and METER are skipped from the rotation
when `expose_dev_sensors` is false; BOOST and DISPLAY only run when the matching
number entity (`boost`, or `brightness` / `guard_time`) is configured.

### Adding a new entity

The component is built so that each new register-backed control follows the same
shape. Walking through a new switch on, say, bit 0 ADDR 0x39:

1. **Declare the cache** (if the byte is not already cached). In `fiido_bms.h`
   add a `uint8_t addr_39_cache_{0};` and `bool addr_39_valid_{false};` to the
   protected section. Direct field access is used inside the class; no public
   accessors are needed.

2. **Populate the cache** from `parse_stats_` in `fiido_bms.cpp`. Find the byte
   in the STATS payload (`payload[off]` where `off` is `ADDR - 0x05`), assign
   to `addr_39_cache_`, set `addr_39_valid_ = true`, and `publish_state` to the
   switch entity using the relevant bit.

3. **Write the setter** in `fiido_bms.cpp` alongside the other `set_X_enable`
   methods. Pattern:

   ```
   void FiidoBMSHub::set_my_thing_enable(bool on) {
     if (!ble_user_enabled_) {
       if (my_thing_switch_) my_thing_switch_->publish_state(!on);
       return;
     }
     if (!addr_39_valid_) {
       enqueue_pending_write_([this, on]() { set_my_thing_enable(on); });
       ensure_enabled_for_write_();
       return;
     }
     uint8_t b = addr_39_cache_;
     b = on ? (b | 0x01) : (b & ~0x01);
     // ADDR 0x39 latches only via the J0 (0xFF) frame and only bits 4..0 are valid.
     b &= 0x1F;
     if (send_raw_write(FRAME_TYPE_WRITE_J0, 0x39, std::vector<uint8_t>{b})) {
       addr_39_cache_ = b;
       force_poll_stats_ = true;
     }
   }
   ```

   Invert the polarity (`on ? & ~0x01 : | 0x01`) when the bit is inverted at the
   BMS (key_sound, throttle). Most single-bit writes reuse the shared
   `write_flag_bit_(addr, mask, on, &cache, valid, name)` helper, which applies the
   ADDR 0x39 mask and J0 frame automatically; the expanded form above shows the steps.

4. **Add a switch class**: add one line to `fiido_bool_switch.h`:
   `class FiidoMyThingSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_my_thing_enable> {};`.
   Use `FiidoBoolSwitchWithRestore<...>` instead only for local state that must
   re-apply its restored value on boot (the bluetooth and auto_shutdown switches).

5. **Register in `switch.py`**: import the class as `FiidoMyThingSwitch`, add it
   to the `SWITCHES` table with a config key, hub setter name, restore mode,
   icon, entity category, and default name.

6. **Hub wiring in `fiido_bms.h`**: forward-declare `class FiidoMyThingSwitch;`,
   add `void set_my_thing_switch(FiidoMyThingSwitch *sw) { my_thing_switch_ = sw; }`
   and `FiidoMyThingSwitch *my_thing_switch_{nullptr};` in the protected section.
   `fiido_bms.cpp` already includes `fiido_bool_switch.h`, which defines the class.

7. **Unit test the parse path** in `tests/test_protocol/test_main.cpp`: extend the
   STATS fixture to set bit 0 of ADDR 0x39, build a frame, decode it, assert the
   cache value.

For a new sensor the shape is the same minus steps 3-6: add it to `SENSORS` in
`sensor.py`, add `set_my_sensor` and the member pointer in `fiido_bms.h`, publish
from the corresponding `parse_*` in `fiido_bms.cpp`. Add it to `SENSOR_POLL_GROUP`
so the burst rotation enables the right poll when the sensor is declared, and add
to `DEV_SENSOR_KEYS` if it should be off by default.

### Testing

Unit tests under `tests/test_protocol/` build with PlatformIO + Unity. They link
only `fiido_protocol.{h,cpp}` and run on the host (no ESP32 required). 43 tests
cover CRC, the poll and write frame builders, validate, and the decode of every
poll's payload via static fixtures in `fixtures.h`.

```
pio test -d tests -e native
```

---

## Constraints and quirks

| Constraint                                                                                 | Effect / workaround                                                                                                                |
|--------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------|
| C11 charging cuts BLE entirely                                                             | `connected` goes OFF while the charger is plugged in. Unplug to restore the link.                                                  |
| M1 charging is invisible on BLE                                                            | No register reports charge current / voltage delta. Do not try to detect charging from BMS state.                                  |
| Official app and the component share the BLE link                                          | Only one central at a time. Use the `bluetooth` switch to release the link before pairing with the app.                            |
| `slow_mode_on_boot` (bit 6 ADDR 0x2C) has an instant side-effect on ADDR 0x3C              | BMS rewrites the speed-limit value on the same WRITE: ON forces 6 km/h, OFF restores the user choice. The component only writes bit 6; do not also write 0x3C in the same burst. |
| Speaker (bits 3:2 ADDR 0x38) is binary in firmware                                         | Only bits = 00 (audible) and bits = 01 (silent) have a physical effect. Other values collapse to silent.                           |
| Key sound (bit 4 ADDR 0x2C) is inverted                                                    | bit = 0 means audible, bit = 1 means silent. Setter applies the inversion; the entity reads "ON" when the bike beeps.              |
| Throttle (bit 1 ADDR 0x2B) is inverted                                                     | bit = 0 means handle is active, bit = 1 means disabled. Same shape as key sound.                                                   |
| WRITE is fire-and-forget                                                                   | No NOTIFY confirms a WRITE (except ADDR 0x25 mode change). Verify by force-polling STATS afterwards and checking the bit.          |
| Bike will not sleep while the BLE link is held                                             | The BMS only enters low-power state after the central disconnects. With `auto_shutdown` OFF the component never drops the link, so the bike keeps draining standby current indefinitely. Leave `auto_shutdown` ON unless you have an external reason to keep the link up. |
| Experimental controls are capability-gated                                                 | `cruise`, `start_mode`, `insensitivity`, `show_total_km`, `auto_screen_off`, `ring`, `double_speed`, `bike_guard`, `brightness`, `boost`, `guard_time`, and `pair_watch` write real registers, but the C11 / M1 report them as unsupported and toggling them had no observable effect. They ship `disabled_by_default` (except `bike_guard` and `guard_time`). See Experimental controls. |
| Frame CRC is a plain XOR, so a corrupted frame can still validate                          | STATS samples outside plausible bounds are dropped and the last good value kept: total <= 200000 km, trip <= 1000 km, speed <= 100 km/h, SOC <= 100%, motor temperature -40..125 C. |
| A fragmented BLE stream can reject every frame                                             | Invalid and unhandled-address NOTIFY frames are logged at most once per 5 s per category, each line carrying the count dropped since the previous log, so the log cannot flood. |
