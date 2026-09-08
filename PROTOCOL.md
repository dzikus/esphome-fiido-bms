# Fiido BMS BLE protocol

Reconstructed from a live C11 Pro and M1 Pro 2025.
Kept in step with `components/fiido_bms/fiido_protocol.h`.

## Transport

Service `00010203-0405-0607-0809-0A0B0C0DFFE0`:

| characteristic | handle | direction |
|---|---|---|
| `…FFE1` | 0x12 | notify, bike to host |
| `…FFE2` | 0x10 | write, host to bike |

Writes go out as write-without-response. Negotiated MTU is 247, and no frame in
this protocol comes near it.

## Frame layout

```
poll:    46 64 55 <len> <addr> <crc>
write:   46 64 <type> <payload_len> <addr> <payload...> <crc>
notify:  46 64 AA <payload_len> <addr> <payload...> <crc>
```

- `46 64` is `"Fd"`.
- `<type>` is `AA` for an L0 write and `FF` for a J0 write. A notify carries
  `AA` in the same position, which means something else.
- `<crc>` is the XOR of every preceding byte.
- A poll's `<len>` is how many bytes the answer will carry, and it has to match
  what the parser expects. `poll_len()` asserts that at compile time.

Writes are fire and forget. The BMS sends no notify for a write; confirmation
comes from a forced STATS poll.

### Which write type

ADDR 0x39 latches only under J0. An L0 write to it is acknowledged and dropped,
which reads as success. `compute_masked_write()` picks the type from the address.

## Polls

| name | addr | payload | carries |
|---|---|---|---|
| STATS | 0x05 | 53 | speed, distance, gear, SOC, status flags (0x05..0x39) |
| METER | 0x60 | 13 | display HW/SW, mode |
| SPEEDLIM | 0x3C | 1 | speed limit value in km/h |
| BOOST | 0x52 | 1 | PAS boost level |
| DISPLAY | 0x57 | 2 | brightness and guard time |
| BATTERY | 0x7B | 13 | capacity, voltage, current, manufacturer |
| MOTOR | 0x96 | 12 | version, wheel, temperature, rated power |
| CTRL | 0xAF | 12 | controller HW/SW, voltages, current, temperature |
| ENERGY | 0xC8 | 12 | crank torque and rpm, energy, uptime |

A handshake poll of ADDR 0x0D goes out once per connection.

## STATS payload

Offsets are into the payload, which starts at ADDR 0x05, so a one-byte field at
ADDR `XX` sits at `XX - 0x05`. A multi-byte big-endian field is named by the ADDR
of its **last** byte and therefore begins that many bytes earlier: total distance
0x1F is 4B at offset 23 (0x1C..0x1F), trip 0x21 is 2B at 27 (0x20..0x21), speed
0x23 is 2B at 29 (0x22..0x23), all in tenths.

The same rule holds outside STATS. Battery voltage 0x80 is payload[4..5] of the
BATTERY poll, capacity 0x7E is payload[2..3], uptime 0xD3 is payload[10..11] of
ENERGY. The constants in `fiido_protocol.h` are the offset of the first byte, so
a field's ADDR and its constant differ by the width minus one.

| offset | addr | meaning |
|---|---|---|
| 31 | 0x24 | SOC in percent |
| 32 | 0x25 | gear count as a nibble pair, not a plain number |
| 33 | 0x26 | current gear |
| 34 | 0x27 | bit 7 controller, 6 cruise, 5 speed limit, 3 light, 1 start mode, 0 insensitivity |
| 35 | 0x28 | bit 7 speed unit (1 = mph), 6 show total km |
| 37 | 0x2A | bit 5 brake |
| 38 | 0x2B | bit 6 bike guard, 5 double speed, 1 throttle (inverted) |
| 39 | 0x2C | bit 7 PAS limit, 6 slow mode on boot, 4 key sound (inverted) |
| 51 | 0x38 | bits 3:2 speaker (00 = audible) |
| 52 | 0x39 | bit 3 auto screen off, 1 ring; bits 7:5 are written as zero |

The checksum is a plain XOR, so a corrupted frame can still validate. Distance,
speed and SOC are therefore range-checked before they reach an entity, and a
sample outside its bound is dropped rather than published.

## Speed limit

Setting the limit takes two writes:

1. ADDR 0x2C bit 7, the PAS cap, if it differs from the target.
2. after 50 ms, ADDR 0x3C with the value and ADDR 0x27 bit 5 with the flag.

Clearing the PAS bit makes the BMS write 0x3C = 25 by itself, so phase two has
to land after that. Setting the cap needs no delay.

Readable combinations are value 100 with the flag off, or 6 and 25 with the
flag on. Anything else is the resting state the BMS re-arms after a ride and is
ignored rather than published.

## Telemetry these bikes do not provide

Battery current (0x85), the battery current-voltage field (0x83), the CTRL block
apart from its version and manufacturer bytes, and every ENERGY field except
uptime (0xD3) read zero on both the C11 Pro and the M1 Pro 2025. That holds at
rest, under load, and across the whole state-of-charge range. Battery voltage at
0x80 reads the pack's nameplate 48.0 V and never moves off it. None of this is a
decode error and no write arms any of it.

The capability bytes point at the reason. Both bikes clear the bits for a torque
transducer (0x2D bit 1), a Hall sensor in the pack (0x30 bit 5) and a CAN link to
the battery (0x33 bit 4), which are the parts that would have to exist for crank
torque at 0xC9 and pack current at 0x85 to carry anything. The mapping is not
exact: the energy-meter bit at 0x2D bit 0 differs between the two bikes and both
still report zero trip and total energy.

SOC at 0x24 is the battery reading to use. One byte, plain percent, and it tracks
the bars on the bike's own display.

## Behaviour the BMS imposes

- The controller has to be on before a gear or gear-count write is accepted.
- Bit 3 of ADDR 0x27, the light, survives an off/on cycle, so the bike would
  come back on with the lamp lit. The component clears it on the falling edge
  of the controller.
- A read-back that latches does not prove the function works. Several bits
  persist on hardware with no support for them. ADDR 0x2D..0x34 carry the
  capability bits that say which functions the bike declares support for.
