# ESPHome Fiido BMS architecture

How the component is structured, how it polls and writes, and how to add a new
sensor, binary sensor, select, switch, number or button. Configuration and entities
are in [README.md](README.md), the wire protocol in [PROTOCOL.md](PROTOCOL.md).

## Component layout

```
components/fiido_bms/
  __init__.py                  hub config + schema, model entity sets, auto-offset, dev gating
  sensor.py                    sensor platform: 36 keys (10 always on, 26 dev), schema + to_code
  binary_sensor.py             binary_sensor platform: 3 keys (2 always on, 1 dev)
  select.py                    4 select classes + platform
  switch.py                    16 switch classes + platform (9 stable, 7 dev)
  number.py                    3 number classes + platform, all dev
  button.py                    1 button class + platform, dev (pair_watch)

  fiido_protocol.{h,cpp}       pure C++: CRC XOR, frame builders, validate, POLL_TABLE
  fiido_state.{h,cpp}          pure C++: lifecycle, write gate, pending queue, burst
                               cadence, register cache, speed limit plan, gear clamp
  fiido_model.h                pure C++: Model enum, GATT profiles, per-model traits
  fiido_link.{h,cpp}           GATT handles by UUID, notify subscription, writes
  fiido_bms.{h,cpp}            FiidoBMSHub: BLE client + PollingComponent + state machine

  fiido_bool_switch.h          all 16 switches via two templates + one-line subclasses:
                               FiidoBoolSwitch<Setter>             write_state only
                               FiidoBoolSwitchWithRestore<Setter>  setup() restore + defer
                               motor / light / speaker / key_sound / throttle / slow_mode
                                 / bike_guard and the 7 dev switches are write-only; the
                                 bit + ADDR live in the hub setter
                               bluetooth / auto_shutdown use the with-restore template
                                 (local state, re-applied on boot)
  fiido_number.h               3 numbers via one template (brightness / boost / guard_time)
  fiido_button.h               pair_watch button

  fiido_gear_select.{h,cpp}         ADDR 0x26, count-aware (3 vs 5)
  fiido_mode_select.{h,cpp}         ADDR 0x25 nibble-packed
  fiido_speed_limit_select.{h,cpp}  ADDR 0x3C + bit 5 ADDR 0x27 pair
  fiido_speed_unit_select.{h,cpp}   bit 7 ADDR 0x28
```

`fiido_protocol.{h,cpp}`, `fiido_state.{h,cpp}` and `fiido_model.h` are pure C++ with
no ESPHome dependencies and are what the PlatformIO unit tests build against.
Everything else needs the ESPHome runtime.

## Polling model

`FiidoBMSHub` inherits `PollingComponent` with a fixed 1-second baseline. On every
tick `update()` checks a gate:

```
interval = desired_interval_ms_
slot     = (now - startup_delay_ms_ % interval) / interval
if slot == last_burst_slot_ or (now - last_burst_ms_) < interval:
    return
last_burst_slot_ = slot
last_burst_ms_   = now
send_burst_poll_()
```

Two gates. The slot fixes the phase to a clock both hubs read, so their
`startup_delay` separation holds for the whole uptime instead of decaying into the
random phase ESPHome gives each component's poller tick. The elapsed check keeps a
minimum spacing, which matters because a write-verification poll bypasses both gates
and can land just before a slot boundary.

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
drop frames. Polls that no entity on the hub reads are skipped at burst time (see
the note under the poll table in [Frame format](#frame-format)). A counter limits the skip loop to one pass over
`POLL_TABLE`.

After every successful WRITE the hub sets `force_poll_stats_ = true`, cancels the
in-flight burst, and re-enters burst rotation starting with STATS so the WRITE's
visible effect (a flipped bit) shows up in HA within one burst step.

## BLE lifecycle state machine

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
| `CONNECTED -> DISCONNECTED` (silent link) | no valid STATS since the connection opened or since the last STATS for `silent_link_ms_` (`Silent link timeout` in `dump_config`), also before READY | `set_enabled(false)`; queued writes are dropped with a warning |
| any `-> DISCONNECTED` (GATT)       | services resolved, but the configured profile's service or characteristics are missing | `set_enabled(false)` and warning status. If the bike offers the other known profile instead: queued writes cleared, later writes rejected, no probing until the `bluetooth` switch is cycled or the node restarts |
| any `-> PROBING`                   | HA writes a control while link is down               | `enqueue_pending_write_(fn)` + `ensure_enabled_for_write_()` + `set_enabled(true)` |
| WRITE drained                      | STATS valid after re-connect                         | `dispatch_pending_writes_()` runs every enqueued lambda                 |

None of these transitions is gated by `auto_shutdown` - that switch controls a
different mechanism (the automatic motor power-off, see the
[switch table](README.md#entities-switch)).
The BLE link is released on `idle_disconnect` regardless of it.

The `bluetooth` switch is a hard kill: turning it OFF
clears the pending-writes queue, cancels the pending timeouts, calls
`parent_->set_enabled(false)`, and any subsequent WRITE setter rejects the change
(`motor`/`light`/`speaker` re-publish the inverted state, others log + return).

## State cache and read-modify-write

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

## Frame format

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

STATS is always issued. Every other poll runs only on a hub that builds an entity
reading it:

| Poll                                | Enabled by                               |
|-------------------------------------|------------------------------------------|
| BATTERY, CTRL, MOTOR, ENERGY, METER | a sensor fed by that poll                |
| SPEEDLIM                            | the `speed_limit` select                 |
| BOOST                               | the `boost` number                       |
| DISPLAY                             | the `brightness` or `guard_time` number  |

CTRL and METER feed dev sensors only and drop out with `expose_dev_sensors: false`. A
`model: air` hub without `expose_dev_sensors` sends STATS and BATTERY only. HANDSHAKE
goes out once per connection, outside the rotation.

## Adding a new entity

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
     if (send_raw_write(FrameType::WriteJ0, 0x39, std::vector<uint8_t>{b})) {
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

7. **Unit test the parse path** in `tests/test_protocol/test_decode.cpp` and register
   it in `run_decode_tests()`: extend the STATS fixture to set bit 0 of ADDR 0x39,
   build a frame, decode it, assert the cache value.

For a new sensor the shape is the same minus steps 3-6: add it to `SENSORS` in
`sensor.py`, add `set_my_sensor` and the member pointer in `fiido_bms.h`, publish
from the corresponding `parse_*` in `fiido_bms.cpp`. Add it to `SENSOR_POLL_GROUP`
so the burst rotation enables the right poll when the sensor is declared, and add
to `DEV_SENSOR_KEYS` if it should be off by default.

## Testing

Unit tests under `tests/test_protocol/` build with PlatformIO + Unity. They link
`fiido_protocol.{h,cpp}` and `fiido_state.{h,cpp}`, include the header-only
`fiido_model.h`, and run on the host (no ESP32 required). 144 tests cover CRC, the
poll and write frame builders, validate, the decode of every poll's payload via
static fixtures in `fixtures.h`, the state decisions (lifecycle, write gate, pending
queue, burst cadence, speed limit plan, gear clamping) and the model table: UUID
format, the profile each model uses, and which known profile a bike offers instead
of the configured one.

```
pio test -d tests -e native
```
