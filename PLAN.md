# Split-Flap Social Follower Counter — Build Plan for Claude Code

A mechanical split-flap display that shows **TikTok, Instagram and Facebook follower counts**, cycling between platforms.
Based on David Königsmann's open-source split-flap display:

- Code: https://github.com/davidkingsman/split-flap (GPL-3.0 — our firmware is a derivative, so it stays GPL-3.0)
- 3D files: https://www.printables.com/model/69464-split-flap-display (download manually — Printables blocks automated fetches)
- Maintained fork (read before starting, may already fix bugs listed below): https://github.com/JonnyBooker/split-flap

> **For Claude Code:** work phase by phase, in order. Each phase ends with a "Done when" check — do not start the next phase until it passes. Items marked **VERIFY** are facts that were not confirmed against official docs when this plan was written; check them before relying on them and update this file if they're wrong. Never commit secrets (Wi-Fi passwords, API tokens, client secrets).

---

## 1. How the original works (summary of upstream code)

| Piece | Upstream | Notes |
|---|---|---|
| Master | ESP-01 (ESP8266), `ESPMaster/` | Web UI on port 80, talks to units over I2C (`Wire.begin(1, 3)` = TX/RX pins) |
| Unit (one per character) | Arduino Nano on custom PCB, `unit/unit.ino` | 28BYJ-48 stepper via ULN2003, KY003 hall sensor + magnet for homing, 4-pos DIP switch = I2C address 0–15 |
| Flaps | 45 per unit | Master table `letters[]`: `' '`, `A–Z` (1–26), `$ & #` (27–29), `0–9` (30–39), `: . - ? !` (40–44) |
| Protocol | master → unit: 2 bytes `{flapIndex, speedRPM}`; unit → master: 1 byte `currentlyrotating` | Master waits until all units idle before sending a new message |
| Homing | `calibrate()` spins until hall LOW, then moves `calOffset` steps (stored in EEPROM addr 0, set with `EEPROM_Write_Offset.ino`, ~100) | Motor only turns one direction; going "backwards" = full revolution with re-home |
| Limits | `UNITSAMOUNT 10`, max 16 units, `OVERHEATINGTIMEOUT 2` s between moves, speed 1–12 RPM | |

Known upstream problems to handle:
1. First unit uses I2C address **0x00 = general-call address** — fragile.
2. Unit's debug table says `Ä Ö Ü` at 27–29, master says `$ & #` — glyphs actually depend on the printed flaps.
3. `calibrate()` "move out of magnet first" branch appears inverted (runs when hall reads HIGH). **VERIFY on hardware before changing.**
4. ESP-01 has almost no RAM/pins and no proper debug serial (serial pins are used for I2C). Not suitable for HTTPS API calls.

---

## 2. Target design (what we're building)

### Hardware changes vs upstream
- **Replace ESP-01 master with an ESP32** (ESP32 DevKit or ESP32-C3 SuperMini, ~$3–5). Reasons: enough RAM for 3 TLS connections, real debug serial, free pins. Units and PCBs stay unchanged.
- **I2C level shifting:** ESP32 is 3.3 V and **not 5 V tolerant**; Nanos' `Wire` enables internal pull-ups to 5 V. Use a BSS138 bi-directional level shifter (~$0.50) between ESP32 and the bus, *or* disable AVR internal pull-ups in unit firmware and use external 4.7 kΩ pull-ups to 3.3 V. Pick the level shifter (safer).
- **Unit count: 9** (configurable, decided 2026-10-16): 2 label units + 7 digit units.
- **Power:** 5 V ≥ 3 A supply. 9 × 28BYJ-48 can draw ~2.2 A when all move at once. Add a 1000 µF cap on the 5 V rail.
- **Flaps:** the default 45-flap set (blank, A–Z, `$ & #`, 0–9, `: . - ? !`). There are no accented flaps. Text input is folded instead: Portuguese and other Latin-1 accents become the base letter (`ã→A`, `ç→C`), and anything without a flap becomes blank.

### Display layout (9 units)
```
[ L ][ L ][ d ][ d ][ d ][ d ][ d ][ d ][ d ]
  ^ platform label: TT = TikTok, IG = Instagram, FB = Facebook
```
- Count right-aligned in the digit units, leading blanks (not zeros).
- If the count doesn't fit: abbreviate using the `.` flap and `K`/`M`/`B` letters, e.g. `12.34M`, `987.6K`, with at most 2 decimals and never wider than the digit units. Must never show a wrong-looking number, so always round down.
- `LABEL_WIDTH = 1` → `T`, `I`, `F` + 8 digits (or 8 units with 7 digits).
- Error state for a platform: show label + `-------` (dashes) only if there has **never** been a good value; otherwise keep showing the last good value.

### Behaviour
- Poll each enabled platform every `POLL_MINUTES` (default 15). Cache last good value + timestamp.
- Cycle display between enabled platforms every `CYCLE_SECONDS` (default 60; minimum 20 — a full flap revolution is ~5 s at 12 RPM plus the 2 s overheat timeout).
- Modes: `cycle` (default), `single:<platform>`, `text` (manual message, keeps upstream feature), `off` (all blank).
- Optional: "celebrate" — when a count crosses a milestone (every 1,000 / custom), flash the display (blank → number) once.

---

## 3. Platform APIs (official only — no scraping)

All three require an account type and token setup **done once by the user**; the device then refreshes tokens itself.

### Instagram — needs a Professional (Business or Creator) account
- API: **Instagram API with Instagram Login**.
- Count: `GET https://graph.instagram.com/me?fields=username,followers_count&access_token=TOKEN`
- Token: long-lived, ~60 days. Refresh: `GET https://graph.instagram.com/refresh_access_token?grant_type=ig_refresh_token&access_token=TOKEN` (token must be ≥24 h old and not expired). Device refreshes every ~30 days.
- App can stay in Development mode if the user's IG account is added as a tester/role on the Meta app — no app review needed for own account. **VERIFY** scope name (`instagram_business_basic`) and current API version in Meta docs.

### Facebook — Pages only (personal profile follower counts are not available via API)
- Count: `GET https://graph.facebook.com/vXX.X/{PAGE_ID}?fields=name,followers_count&access_token=PAGE_TOKEN`
- Token: Page token derived from a **long-lived user token** has no expiry (invalidated if the admin changes password or revokes the app). Device does not refresh; on `OAuthException`/code 190 → show error + flag "re-auth needed" in web UI.
- **VERIFY** current Graph API version and required permission (likely `pages_read_engagement`).

### TikTok — hardest
- API: TikTok for Developers, Login Kit + `user.info.stats` scope (follower_count moved there from `user.info.basic` in 2024).
- Count: `GET https://open.tiktokapis.com/v2/user/info/?fields=display_name,follower_count` with header `Authorization: Bearer ACCESS_TOKEN`
- Tokens: access token ~24 h, refresh token ~365 days. Refresh: `POST https://open.tiktokapis.com/v2/oauth/token/` form `client_key, client_secret, grant_type=refresh_token, refresh_token`. Device refreshes when access token is <2 h from expiry; store the **new** refresh token each time.
- **VERIFY:** whether a Sandbox app can authorize the user's own account without public app review; whether redirect URIs must be HTTPS (if so, use a GitHub Pages page that just displays the `code` query parameter); whether PKCE is required.
- If TikTok approval turns out impossible: ship with TikTok disabled; leave a `manual` source where the user types the count in the web UI. Do not scrape.

### Token bootstrap
- `tools/auth/` — a small **Python** CLI (`python -m auth instagram|facebook|tiktok`) that runs the one-time OAuth flow on the user's PC, exchanges for long-lived tokens, and **POSTs them to the device** (`POST http://splitflap.local/api/tokens`) — tokens never get written to a file in the repo.
- Client secrets read from environment variables or a git-ignored `tools/auth/.env`.

---

## 4. Repository layout

```
split-flap-social/
├─ PLAN.md                     (this file)
├─ README.md                   build + setup guide for humans
├─ LICENSE                     GPL-3.0 (inherited)
├─ .gitignore                  .pio/, .env, secrets.h, *.local.*
├─ platformio.ini              envs: master (esp32), unit (nanoatmega328), native (tests)
├─ firmware/
│  ├─ unit/                    ported from upstream unit/unit.ino
│  ├─ offset_tool/             ported EEPROM_Write_Offset
│  └─ master/
│     ├─ src/main.cpp
│     ├─ src/display/          FlapBus (I2C), charset, layout/formatting
│     ├─ src/sources/          Source interface + instagram/facebook/tiktok/manual
│     ├─ src/store/            config + token store (NVS/Preferences)
│     ├─ src/web/              AsyncWebServer routes + JSON API
│     └─ data/                 web UI (index.html, app.js, style.css) → LittleFS
├─ test/                       native unit tests (Unity)
│  └─ fixtures/                sample API JSON responses
├─ tools/auth/                 token bootstrap CLI (Python)
└─ hardware/
   ├─ stl/                     user drops Printables files here (not committed if license forbids redistribution — check)
   ├─ pcb/                     upstream Gerbers
   └─ wiring.md                ESP32 ↔ level shifter ↔ bus diagram, pinout
```

---

## 5. Phases

### Phase 0 — Project setup
1. Clone upstream into `upstream/` (read-only reference, git-ignored) and skim JonnyBooker fork's changes.
2. Create the layout above, `platformio.ini` with three envs:
   - `unit`: `platform = atmelavr`, `board = nanoatmega328` (note: some clones need `nanoatmega328new` bootloader)
   - `master`: `platform = espressif32`, board per user's choice (`esp32dev` or `esp32-c3-devkitm-1`), `board_build.filesystem = littlefs`
   - `native`: for tests
3. Libraries (pin versions): ESPAsyncWebServer + AsyncTCP (maintained forks, e.g. `mathieucarbou/ESPAsyncWebServer`), ArduinoJson v7, WiFiManager (tzapu) for Wi-Fi setup.

**Done when:** `pio run -e unit` and `pio run -e master` both compile (master can be an empty sketch).

### Phase 1 — Unit firmware port
1. Port `unit.ino` to `firmware/unit/src/main.cpp` with **minimal** changes:
   - I2C address = `0x10 + dip` (avoid 0x00 general call). Master uses the same base. Document in README.
   - Single shared `charset.h` used by both master and unit (fixes table mismatch). Ask the user which glyphs their printed flaps have at 27–29; default `$ & #`.
   - Keep protocol (2-byte command, 1-byte status) unchanged.
   - Add a 3rd optional command byte `0xFE` = "re-home now" (master uses it after errors).
2. Do not change `calibrate()` logic yet — log the suspected inversion as a TODO for hardware testing in Phase 7.
3. Port offset tool.

**Done when:** compiles; protocol documented in `firmware/PROTOCOL.md`.

### Phase 2 — Master: display driver
1. `FlapBus`: `begin(sda, scl, freq=50kHz)`, `isMoving(addr)` (handles -1 / wake-up like upstream), `send(addr, flapIndex, rpm)`, `showString(str)` → waits for idle (with timeout + error, unlike upstream's infinite loop), maps chars via `charset.h`, skips unknown chars as blank.
2. `I2C scan` at boot → log which unit addresses answered; expose in `/api/status`.
3. Serial debug at 115200 (ESP32 has a real USB serial now).

**Done when:** with a real bus, typing text in a temporary serial command shows it on the units.

### Phase 3 — Formatting (pure logic, test-driven)
`layout.h/.cpp`, no Arduino dependencies:
- `String formatCount(uint64_t n, uint8_t width)` → right-aligned digits, or abbreviated `K`/`M`/`B` with `.`, always rounding **down**, never exceeding `width`.
- `String composeFrame(Platform p, std::optional<uint64_t> count, Config c)` → full string for all units (label + count / dashes).

Tests (`pio test -e native`) must cover: 0, 9, 1234567 (width 7), 9999999, 10000000 → `10.00M`/fits width, 999999 at width 6, 1234 at width 3, rounding-down edge cases (`9999` at width 4 vs 3), label width 1 vs 2, missing count → dashes.
> Correction (implemented in `test/test_layout`): the `.` is a flap too. That means `1.2K` and `9.9K` are 4 characters and can't fit in width 3. The rule "never exceed width" wins, so 1234 and 9999 at width 3 both give ` 1K`/` 9K`, rounded down and never `10K`.

**Done when:** all native tests pass.

### Phase 4 — Data sources
1. Interface:
   ```cpp
   struct FetchResult { bool ok; uint64_t followers; String error; bool authExpired; };
   class Source { public: virtual const char* id() = 0; virtual FetchResult fetch() = 0; virtual bool maintainToken() { return true; } };
   ```
2. Implement `InstagramSource`, `FacebookSource`, `TikTokSource`, `ManualSource`.
   - HTTPS via `WiFiClientSecure` with the ESP-IDF **certificate bundle** (no `setInsecure()`).
   - Parse with ArduinoJson using a filter document (only `followers_count` / `data.user.follower_count`) to save RAM.
   - Map API error shapes (Meta `error.code == 190` → authExpired; TikTok `error.code != "ok"`).
   - Exponential backoff on failure (15 → 30 → 60 min, cap 2 h); respect HTTP 429.
3. `TokenStore` on NVS (`Preferences`): per platform `access_token`, `refresh_token`, `expires_at`, `page_id`, `client_key/secret` (TikTok). Never print full tokens to serial or web — mask to last 4 chars.
4. Token maintenance task: IG refresh at age ≥30 days; TikTok refresh at <2 h to expiry; persist new tokens atomically (write then commit).
5. Parsing logic split into pure functions (`parseInstagram(json)`, etc.) so they're tested natively against `test/fixtures/*.json` (success, expired token, rate limit, malformed).

**Done when:** native parser tests pass; on device, each enabled platform logs a real follower count.

### Phase 5 — Scheduler & modes
- Non-blocking main loop (no `delay()` chains): poll timer, cycle timer, token maintenance timer, web server.
- Display updates only when the frame string changes (like upstream `writtenLast`).
- Modes `cycle`, `single`, `text`, `off`; optional quiet hours (no flipping at night — the motors are noisy) with NTP time.
- Milestone celebration (optional, off by default).

**Done when:** device cycles T/I/F with real numbers for an hour with no resets (`esp_reset_reason` logged at boot).

### Phase 6 — Web UI, setup & bootstrap tool
1. Wi-Fi via WiFiManager captive portal (`SplitFlap-Setup` AP) — no hard-coded credentials. mDNS `splitflap.local`.
2. Pages: Status (each platform: last count, last fetch time, error, token expiry), Settings (enabled platforms, intervals, label width, speed, quiet hours, mode, manual text, manual count), Tokens (paste/clear per platform, masked display).
3. JSON API: `GET /api/status`, `GET/POST /api/config`, `POST /api/tokens`, `POST /api/rehome`, `POST /api/show` (manual text).
4. Simple shared-secret header (`X-Admin-Key`, set during setup) on all POST endpoints — the device holds tokens.
5. `tools/auth` Python CLI as described in §3. Include step-by-step README sections for creating the Meta app and TikTok app, with screenshots placeholders.

**Done when:** a fresh device can be set up end-to-end from README only (Wi-Fi → tokens via CLI → counts showing).

### Phase 7 — Hardware bring-up checklist (user + Claude Code together)
1. Print parts per Printables instructions (PETG or PLA). Put STLs in `hardware/stl/`.
2. Build **one unit first**: flash unit firmware, set DIP = 0000 (→ 0x10), run offset tool until the blank flap shows after every home. Test the suspected `calibrate()` inversion here; fix only if observed.
3. Wire ESP32 → level shifter → unit 1; verify I2C scan finds 0x10; send test characters (calibration sequence from upstream `#define test`).
4. Add units one by one (DIP 0001, 0010, …); confirm each address in scan.
5. Full 8-unit test with all moving at once — check supply voltage doesn't sag below 4.75 V.
6. 24-hour soak test.

### Phase 8 — Docs & cleanup
- README: BOM with prices, wiring diagram (`hardware/wiring.md`), flashing steps, API setup per platform, troubleshooting (unit not found, wrong flap after home, token expired, TikTok not approved).
- Credit upstream author, keep GPL-3.0 notices.

---

## 6. Bill of materials (8 units, approximate AliExpress prices)

| Item | Qty | ~Cost |
|---|---|---|
| Arduino Nano clone | 8 | $24 |
| 28BYJ-48 stepper (+ULN2003 if not on PCB) | 8 | $12 |
| KY003 hall sensor + 4 mm magnet | 8 | $4 |
| 4-pos DIP switch | 8 | $2 |
| Unit PCB (upstream Gerbers, JLCPCB 10 pcs) or perfboard | 8 | $5–10 + shipping |
| ESP32 / ESP32-C3 SuperMini | 1 | $4 |
| BSS138 level shifter | 1 | $0.50 |
| 5 V 3 A power supply + barrel jack + 1000 µF cap | 1 | $7 |
| Filament + screws/wire | — | $10 |
| **Total** | | **~$70–75** |

Cost-saving option (bigger code change, decide before Phase 1): drop the per-unit Nanos and drive all ULN2003s from one ESP32 via shift registers / I2C expanders (PCF8575 / MCP23017), as in Morgan Manly's ESP32-C3 split-flap design. Saves ~$25 but replaces the unit firmware and PCB.

---

## 7. Open questions for the user (ask before Phase 1)
1. How many units — 8 (1 label + 7 digits) or 9 (2-letter labels + 7 digits)?
2. Which ESP32 board do you have/will buy?
3. Are your accounts: Instagram Professional? Facebook **Page** (not personal profile)?
4. Which glyphs are printed on flaps 27–29 of your Printables files?
5. Nano-per-unit (as designed) or the cheaper shared-controller variant?
