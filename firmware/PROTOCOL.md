# Master ↔ Unit I2C protocol

The protocol is unchanged from upstream except for the address base and the optional re-home byte. The constants live in `firmware/common/protocol.h` and `firmware/common/charset.h`.

## Bus
- Speed: 50 kHz (long wires between units). The master is the ESP32, with SDA = GPIO21 and SCL = GPIO22 by default. It connects to the 5 V bus through a BSS138 level shifter (see `hardware/wiring.md`).
- **Unit address = `0x10 + DIP`**, where DIP is the 4-position switch read as a binary number (switch 1 = MSB, ON = 1). That gives addresses `0x10`–`0x1F`.
  Upstream used `0x00 + DIP`, so the first unit sat on the I2C general-call address. Firmware from upstream and from this repo cannot be mixed on one bus.
- Unit *i* (counting from the left, starting at 0) must have DIP = *i*.

## Master → unit (write)

| Byte | Meaning |
|---|---|
| 0 | Flap index `0..44` (see table below). Values ≥ 45 are ignored. |
| 1 | Speed in RPM `1..12` (clamped by the unit). |
| 2 | *Optional.* `0xFE` = re-home against the hall sensor first, then move to the flap in byte 0. Any other value is ignored. |

If the target index is lower than the current one, the drum turns all the way round and re-homes on the way (the motor only turns one direction).

## Unit → master (read, 1 byte)
- `1`: the drum is rotating or homing, or a received move is still pending (for example, held back by the 2 s overheating timeout). Upstream reported `0` in that last case.
- `0`: idle.
- No answer (`Wire.read()` returns `-1`): the unit is asleep or missing. The master sends an empty write to wake it, then retries. A unit asleep in AVR power-down wakes on an address match.

## Flap table (`FLAP_GLYPHS`)

| Index | Glyph |
|---|---|
| 0 | blank |
| 1–26 | `A`–`Z` |
| 27–29 | `$ & #` |
| 30–39 | `0`–`9` |
| 40–44 | `: . - ? !` |

On the master, text is folded before display: lowercase becomes uppercase, Latin-1 accents become their base letter (`ã→A`, `ç→C`, `é→E`), and any other character becomes blank.

## Timing
- The unit enforces `OVERHEATINGTIMEOUT` (2 s) between moves.
- A full revolution at 12 RPM takes about 5 s. The master waits until every unit reports idle before it sends the next frame, and gives up after a timeout.
