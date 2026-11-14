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
| 27–29 | `Ã Ç É` |
| 30–39 | `0`–`9` |
| 40–44 | `Á .` TikTok Instagram Facebook |

Inside the master a frame is one byte per unit: ASCII, Latin-1 for `Ã Ç É Á` (0xC3 0xC7 0xC9 0xC1), and 0x01 / 0x02 / 0x03 for the Instagram, Facebook and TikTok logos (`FLAP_INSTAGRAM`, `FLAP_FACEBOOK`, `FLAP_TIKTOK`). `/api/status` sends it as UTF-8 with the logos as U+E000, U+E001 and U+E002.

Text is folded before display: lowercase becomes uppercase, `Ã Ç É Á` keep their own flaps, other Latin-1 accents become their base letter (`õ→O`, `ê→E`), and any other character (control codes included) becomes blank.

## Timing
- The unit enforces `OVERHEATINGTIMEOUT` (2 s) between moves.
- A full revolution at 12 RPM takes about 5 s. The master waits until every unit reports idle before it sends the next frame, and gives up after a timeout.

# Display ↔ relay server
The optional relay (`server/`) lets the app reach a display that nothing can connect in to. The display makes every connection; the server never calls the display. Display side: `firmware/master/src/net/Remote.cpp`. Server side: `server/app.py`.

## Request
`POST {remote_url}/api/device/sync`, with `Authorization: Bearer <remote_token>` and a JSON body:

| Field | Sent | Meaning |
|---|---|---|
| `results` | always | One `{"id": n}` per command received in the previous reply, with `"error": "..."` if the display refused it. This acknowledges them. |
| `status` | always | The same document as `GET /api/status`. |
| `config` | after boot, and whenever the settings changed | The same document as `GET /api/config` (no secrets). |

## Reply
`{"commands": [{"id": n, "type": "...", "body": {...}}, ...]}`. If no command is waiting, the server holds the request for up to 25 s and answers as soon as one arrives, so a change reaches the display in about a second.

| `type` | `body` | The display runs |
|---|---|---|
| `config` | a partial settings document | the same as `POST /api/config` |
| `show` | `{"text", "align"}` | the same as `POST /api/show` |
| `tokens` | a token document | the same as `POST /api/tokens` |
| `rehome` | `{}` | re-home all units |

## Rules
- Command ids only grow. A command stays in the reply until the display acknowledges it, so a lost reply is repeated; the display acknowledges an id it has already run without running it again.
- The display validates every command exactly as it does for its own web page. The server does not validate settings: it shows the display's reported `config` with the waiting changes laid over it.
- On any failure the display retries after 5 s, doubling up to 60 s, and keeps running on its stored settings.
- The server refuses to queue `admin_key`, `remote_url` or `remote_token`: those can only be changed on the display itself.
- `https://` URLs are verified against the embedded CA bundle. `http://` is accepted for a relay on the local network only; it sends the token in the clear.
