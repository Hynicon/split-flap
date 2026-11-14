# Split-Flap Social Follower Counter

A mechanical split-flap display with 9 units. It shows your **TikTok, Instagram and Facebook** follower counts and cycles between them:

```
[tt][ ][ ][ ][ ][1][2][3][4]     tt = the TikTok logo flap
[ig][ ][ ][ ][4][8][2][1][3]     ig = the Instagram logo flap
[fb][ ][ ][ ][1][.][2][5][M]     fb = the Facebook logo flap (counts that don't fit are abbreviated, always rounded down)
```

It is based on David Königsmann's open-source [split-flap display](https://github.com/davidkingsman/split-flap) and its 3D models on [Printables](https://www.printables.com/model/69464-split-flap-display). It also draws on the maintained fork by [JonnyBooker](https://github.com/JonnyBooker/split-flap). Changes from upstream:
- an **ESP32** master instead of the ESP-01;
- data from the platforms' **official APIs** only, with no scraping;
- a web UI;
- unit I2C addresses starting at **0x10**;
- a Portuguese flap set: `Ã Ç É Á` and **TikTok, Instagram and Facebook logo flaps** instead of upstream's `$ & # : - ? !` (14 of the 45 flaps are reprinted, including a narrower M and W that fit the flap; see `hardware/stl/README.md`).

**Building one?** Follow the step-by-step [build guide](https://hynicon.github.io/split-flap-guide/), with 3D assembly diagrams, wiring drawings and a checklist.

Licensed **GPL-3.0**, same as upstream (see `LICENSE`).

---

## Contents
1. [Bill of materials](#bill-of-materials)
2. [Repository layout](#repository-layout)
3. [Building and flashing](#building-and-flashing)
4. [First-time setup](#first-time-setup)
5. [Connecting the platforms](#connecting-the-platforms)
6. [Using it](#using-it)
7. [The phone app](#the-phone-app)
8. [Remote app: use it from anywhere](#remote-app-use-it-from-anywhere)
9. [Troubleshooting](#troubleshooting)
10. [Development](#development)

## Bill of materials
These are for 9 units, at approximate AliExpress prices. Upstream's PDF (`upstream/split-flap/Instructions/`) has the full list, including screws, inserts and connectors.

| Item | Qty | ~Cost |
|---|---|---|
| Arduino Nano clone | 9 | $27 |
| **12 V** 28BYJ-48 stepper (5 V versions do **not** work with these PCBs) | 9 | $14 |
| KY-003 hall sensor (momentary) + 2×1 mm magnet | 9 | $5 |
| 4-position DIP switch | 9 | $2 |
| Unit PCB (upstream Gerbers in `hardware/pcb/`, 10 pcs) with ULN2003A, L7805, JST-XH | 9 | $10–15 + shipping |
| ESP32 DevKit (esp32dev) | 1 | $5 |
| BSS138 level-shifter module + 12 V → 5 V buck converter (replace the first-unit AMS1117, BSS138s and 10 kΩ resistors) | 1 each | $2 |
| 12 V ≥ 3 A power supply, DC jack, rocker switch, optional 1000 µF/25 V cap | 1 | $9 |
| Filament (~2.4 kg black, ~250 g white PLA/PETG), screws, inserts, wire | — | $35 |
| **Total** | | **~$110–115** |

## Repository layout
```
firmware/
  common/        charset.h (the 45-flap table + accent folding), protocol.h (I2C constants)
  unit/          Arduino Nano unit firmware (port of upstream unit.ino)
  offset_tool/   writes the homing offset to a unit's EEPROM
  master/        ESP32: display driver, API sources, scheduler, web UI (data/ → LittleFS)
  PROTOCOL.md    master ↔ unit protocol
server/         relay server for using the app from anywhere (Docker), with a fake display for testing
test/            native unit tests (formatting, parsers, config, rotation) + API fixtures
tools/auth/      one-time OAuth CLI that sends tokens to the device
tools/gen_cert_bundle.py   regenerates the TLS root-CA bundle
hardware/        wiring.md, PCB Gerbers, stl/ (models, git-ignored)
```

## Building and flashing
Install [PlatformIO Core](https://platformio.org/install/cli) (`pip install platformio`), or use the VS Code extension.

```sh
pio run                      # builds unit + master
pio test -e native           # host tests (needs a C++ compiler: MinGW-w64 on Windows)
```

**Units:** set each unit's DIP switch (see [hardware/wiring.md](hardware/wiring.md#unit-addresses-dip-switch)), then flash it:

```sh
pio run -e offset_tool -t upload && pio device monitor    # type the homing offset, e.g. 100, Enter
pio run -e unit -t upload
```

Some Nano clones use the old bootloader. If upload fails, change `board = nanoatmega328new` in `platformio.ini`, or the other way round.

**Master (ESP32):**

```sh
pio run -e master -t upload        # firmware
pio run -e master -t uploadfs      # web UI (LittleFS)
pio device monitor                 # 115200 baud
```

Wiring, power and the step-by-step hardware bring-up: **[hardware/wiring.md](hardware/wiring.md)**.

## First-time setup
1. Power on. The display shows `SETUP`, and the ESP32 opens a Wi-Fi network called **`SplitFlap-Setup`**.
2. Join that network with your phone or PC. A setup page opens; if it doesn't, browse to `192.168.4.1`.
3. Pick your Wi-Fi and enter the password. Also choose an **admin key** (8–64 characters). You need the key to change settings or send tokens, so keep it somewhere safe.
4. The device joins your Wi-Fi. Open **http://splitflap.local/**, or use its IP address if your network doesn't do mDNS (the serial log prints it).
5. Enter the admin key in the footer of the web page. The browser remembers it.

To change Wi-Fi or the admin key later, **hold the BOOT button while powering on**. That reopens the setup portal.

The portal is an open network, so once an admin key is set it **only** opens when BOOT is held. If your router is down when the device boots, it simply keeps retrying in the background and does not open the portal. Otherwise anyone nearby could set a new admin key.

## Connecting the platforms
Every platform needs a free developer app that **you** own. It stays in development/sandbox mode, because it only ever reads your own account. Each one is a one-time setup.

Put the app credentials in `tools/auth/.env`, copied from `.env.example`. That file is git-ignored. Then run the CLI from `tools/`:

```sh
cd tools
python -m auth instagram     # or facebook / tiktok
```

The CLI opens the official login page and exchanges the code for long-lived tokens. It shows your current follower count as a check, then sends the tokens to the device. It asks for the admin key. **Tokens are never written to disk.** Add `--dry-run` to test without sending, and `--device 192.168.1.50` if `splitflap.local` doesn't resolve.

You can also paste tokens into the web UI's **Tokens** tab.

> Security note: the CLI sends tokens to the device over plain HTTP on your local network, protected by the admin key. Only do this on a network you trust.

### Instagram: needs a Professional (Business or Creator) account
1. At https://developers.facebook.com/apps create an app, then add the **Instagram** product, and choose **API setup with Instagram login**. *(screenshot placeholder)*
2. Under *App roles → Roles*, add your Instagram account as an **Instagram Tester**. Then accept the invite in Instagram (*Settings → Apps and websites → Tester invites*).
3. In *Business login settings*, add the OAuth redirect URI `https://localhost/`. *(screenshot placeholder)*
4. Copy the **Instagram app ID** and **Instagram app secret** into `.env` as `IG_APP_ID` / `IG_APP_SECRET`.
5. Run `python -m auth instagram`:
   - Log in and approve.
   - The browser then lands on `https://localhost/?code=…`. The page fails to load, which is expected.
   - Copy the whole URL from the address bar and paste it into the terminal.

Shortcut: the dashboard's *Generate token* button for your tester account gives a long-lived token. Use `python -m auth instagram --token <token>`.

The device refreshes this ~60-day token by itself every 30 days. If it is offline for more than 60 days, run the CLI again.

### Facebook: Page followers only
The API does not expose follower counts for personal profiles, only for **Pages** you manage.
1. In the same or another Meta app, add **Facebook Login**. Set the valid OAuth redirect URI to `https://localhost/`.
2. Put the app ID and secret from *App settings → Basic* into `.env` as `FB_APP_ID` / `FB_APP_SECRET`.
3. Run `python -m auth facebook`. Approve `pages_show_list` + `pages_read_engagement` and **select your Page**, then paste the redirected URL. If you manage several Pages, the CLI asks which one, or you can pass `--page-id`.

Shortcut: get a user token from the [Graph API Explorer](https://developers.facebook.com/tools/explorer/) with those two permissions, then run `python -m auth facebook --token <user-token>`.

The CLI stores a **Page token derived from a long-lived user token**, which does not expire. It stops working if you change your Facebook password or remove the app. The Status page then shows *re-auth needed*, and running the CLI again fixes it.

### TikTok
1. At https://developers.tiktok.com create an app. Add **Login Kit** and pick the **Desktop** platform. Set the redirect URI to `http://127.0.0.1:8765/callback/`. *(screenshot placeholder)*
2. Add the scopes **`user.info.basic`** and **`user.info.stats`** (`follower_count` lives in `user.info.stats`).
3. Under **Sandbox**, add your TikTok account as a target user.
4. Put the client key and secret into `.env` as `TIKTOK_CLIENT_KEY` / `TIKTOK_CLIENT_SECRET`.
5. Run `python -m auth tiktok`. It logs in with PKCE and catches the redirect on the local port automatically. If that port is blocked, use `--paste`.

The device refreshes the 24-hour access token itself, using the refresh token (valid ~365 days) and your client key and secret. That's why those are stored on the device.

> **Not yet confirmed:** whether a sandbox app may grant `user.info.stats` to your own account without app review. If TikTok refuses, set TikTok's source to **Manual** in Settings and type the count in occasionally. This project never scrapes.

## Using it
- **Status** tab: shows each platform's count, last successful update, errors and token expiry, plus which units answered on the I2C bus. *Re-home all units* re-calibrates every drum.
- **Settings** tab:
  - which platforms are shown, and **API or Manual** source per platform;
  - mode: `cycle` (default, switches every 60 s), `single` platform, `text` message, or `off`;
  - fetch interval (default 15 min);
  - quiet hours (the motors are noisy), with a POSIX time zone such as `WET0WEST,M3.5.0/1,M10.5.0` for Lisbon or `<-03>3` for São Paulo;
  - flap speed;
  - unit count, label width, and whether the label is the platform's logo flap or its letters (logo by default);
  - milestone flash (off by default): when a count crosses a multiple of N, the display flips to blank and back.
- **Text mode:** the flaps have `A–Z 0–9 Ã Ç É Á .` and blank. Lowercase is shown as uppercase, and other accents are folded to the plain letter (`Você está aqui!` → `VOCE ESTA AQUI`, `Olá, coração` → `OLÁ  CORAÇÃO`). Any other character shows as blank. The logo flaps can't be typed.
- **Errors:** if a fetch fails, the display keeps the last good number and the Status page shows why. The digits stay blank only if a platform has *never* returned a value (there is no dash flap). Failed fetches back off 15 → 30 → 60 → 120 min, and HTTP 429 `Retry-After` is respected.
- **JSON API:** for scripting. POSTs need the `X-Admin-Key` header.
  ```sh
  curl http://splitflap.local/api/status
  curl -X POST -H "X-Admin-Key: $KEY" -d '{"text":"OLA MUNDO","align":"center"}' http://splitflap.local/api/show
  curl -X POST -H "X-Admin-Key: $KEY" -d '{"mode":"cycle"}' http://splitflap.local/api/config
  ```
  Endpoints are listed in `firmware/master/src/web/WebServer.h`.
- **Serial console** (115200): `!scan`, `!rehome`, `!status`, or type any text to show it.

## The phone app
The display serves a phone-sized app at **http://splitflap.local/** (the same app the remote server serves, below). Tabs along the bottom:
- **Show**: pick what the display shows (followers, one platform, random words, a message, clock, date, the mix, or blank), type a message to show at once, and keep up to 12 saved messages for one-tap use.
- **Words**: the random word list (up to 100). Add and remove words while it runs: the display shows every word once before repeating any. Change the interval here (20 s at the least).
- **Mix**: a rotation of up to 8 entries (followers, random word, message, clock, date), each with its own time.
- **Followers**: platforms on or off, counts, a hand-typed count instead of the API, tokens.
- **Settings**: text position, speed, clock and date format, quiet hours, time zone, device details, re-home.

The app tells you when a text won't fit or uses characters the flaps don't have. On the display's own page, changes need the **admin key** (Settings, at the bottom).

## Remote app: use it from anywhere
The display can sit on a network that nothing can connect in to, such as a phone hotspot in a food trailer. For that, a small server (`server/`) runs on a machine that *is* reachable, and the display connects out to it. The display still decides everything itself and keeps running if the server or the hotspot drops; the server only passes changes along and remembers what the display last reported.

```
phone ──HTTPS──► nginx-proxy ──► split-flap server (Docker)
                                      ▲  outbound connection, held open
                                      └────────── the display (ESP32)
```

**Set up the server** (Docker, behind [nginx-proxy](https://github.com/nginx-proxy/nginx-proxy)):
1. Copy `server/.env.example` to `server/.env` and fill it in:
   - `APP_PASSWORD`: the password for the app's sign-in page. It faces the internet, so make it long: 16 or more random characters. Five wrong passwords lock out the network they came from for five minutes. When many networks guess at once, each one is locked out after two wrong tries, except networks that have signed in successfully in the last 30 days. Phone carriers share addresses between many customers, so a stranger's guessing can occasionally lock out your phone's network for five minutes; staying signed in (sessions last 30 days) avoids that.
   - `DEVICE_TOKEN`: a random secret for the display, for example from `openssl rand -hex 24`.
   - `VIRTUAL_HOST`: the hostname nginx-proxy should route, e.g. `flap.example.com`. Set `LETSENCRYPT_HOST` too if you use acme-companion.
   - `PROXY_NETWORK`: the Docker network your nginx-proxy is on.
2. From the repository root: `docker compose -f server/docker-compose.yml up -d --build`
3. Open `https://<your hostname>/` and sign in. It says the display has not connected yet.

**Or with Portainer**, either way:
- **From this repository**: Stacks → Add stack → **Repository**. Repository URL `https://github.com/Hynicon/split-flap`, compose path `server/docker-compose.yml`. Add the four variables above under **Environment variables**, then deploy. To update later, open the stack and use **Pull and redeploy**.
- **Without git access from the server**: run `python server/make_bundle.py` on your PC. Then in Portainer, Images → **Build a new image**, name it `splitflap-server:latest`, choose **Upload** and pick `server/dist/splitflap-server.tar`. Then Stacks → Add stack → **Web editor**, paste `server/portainer-stack.yml` (not `docker-compose.yml`, which builds from the repository), and add the variables. To update later, build the image again and use **Update the stack**.

Don't publish a port: nginx-proxy reaches the container over the shared network.

The server must be reached over **HTTPS**: the display checks the certificate, and the password and token would otherwise travel in the clear. The display refuses an `http://` address unless it is on the local network (for testing against a server on your PC).

**Point the display at it**, either way:
- on the display's own page (same Wi-Fi): **Settings → App server**, enter `https://<your hostname>` and the device token; or
- in the setup portal (hold **BOOT** while powering on): the two "App server" fields, next to the Wi-Fi password. Use this to move the display to the hotspot and set the server in one go.

Within a few seconds the app shows **Display online**. On an iPhone use Share → **Add to Home Screen**; on Android use the browser menu → **Install app**.

What to expect:
- A change reaches the display in about a second. If the display is offline, changes wait on the server and are applied when it reconnects.
- The preview at the top is what the display last reported. Things the display changes by itself (the next random word, the clock) can take up to 25 seconds to show in the preview.
- The display's own page keeps working on its local network. Its admin key, server address and device token can only be changed there, not through the server.
- `python -m auth instagram --device https://<your hostname>` sends tokens through the server; give the app's password when it asks for the key. Tokens wait in the server's memory until the display collects them and are never written to its disk.
- Mobile data: the display makes a small request about every 25 seconds, in the order of 30 MB a day.

To try the app without hardware: run the server locally (`uvicorn app:app --no-proxy-headers` in `server/`, with `APP_PASSWORD`, `DEVICE_TOKEN` and `DATA_DIR` set) and `python server/fake_device.py http://127.0.0.1:8000 <DEVICE_TOKEN>`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Status shows a unit **missing** / scan says `0x1N=MISSING` | Check that unit's DIP switch (unit *N* = binary *N*), its XH-4 cable, and that it runs **this** repo's unit firmware (upstream used 0x00+). Re-scan with `!scan`. |
| No units answer at all | SDA/SCL are swapped, or there's no common GND, or the level shifter is missing (Option B), or the first-unit PCB isn't fully populated. See [wiring](hardware/wiring.md). |
| **Wrong flap after homing** (off by one or more) | Re-run the offset tool for that unit. About 45 steps per flap, so adjust in steps of ~45. |
| A drum keeps spinning, or the serial log shows `[bus] timeout … re-homing` | The magnet or hall sensor isn't detected. Check the sensor wiring and the magnet's distance and polarity. A unit gives up after 3 revolutions. See the `calibrate()` TODO in `firmware/unit/src/main.cpp`. |
| Units stutter or reset when all move | The supply is too weak or the 12 V rail sags. Each 7805 needs ≥ 7 V in. Use 12 V ≥ 3 A, add the 1000 µF cap, and keep wires short. |
| Platform shows **auth_expired / re-auth needed** | Run `python -m auth <platform>` again. Instagram: the device was offline for more than 60 days. Facebook: the password changed or the app was removed. TikTok: the refresh token expired (365 days) or was revoked. |
| TikTok: `no follower_count (is the user.info.stats scope granted?)` | Add `user.info.stats` to the app, re-run the CLI and approve the scope. If TikTok won't grant it, use the Manual source. |
| Instagram: `no followers_count` | The account must be Professional (Business/Creator). Switch it in the Instagram app settings. |
| **rate_limited** | Nothing to do: the device backs off on its own. Raise the fetch interval if it keeps happening. |
| `network error -1` and similar | Wi-Fi or DNS problem, or the clock isn't synced yet (TLS needs the time). Check the Status page. |
| Can't save settings: `missing or wrong X-Admin-Key` | Enter the admin key in the page footer. Forgot it? Hold BOOT at power-on and set a new one. |
| `splitflap.local` doesn't resolve | Use the IP address shown in the serial log, or in your router's client list. |
| Web page is blank / 404 | The filesystem wasn't uploaded. Run `pio run -e master -t uploadfs`. |
| Remote app says **Waiting for the display** | The display has no server address or token yet, or can't reach the server. Check both on the display's own page (Settings → App server). The serial log prints `[remote] connected to the relay` or `[remote] sync failed (code)`: 401 means the token differs from `DEVICE_TOKEN`; a negative code means no connection or a certificate the display doesn't trust (the server needs HTTPS with a public certificate). |
| Remote app says **Display offline** | The hotspot or the display's power is off. Changes you make wait and are applied when it reconnects. |
| Remote app: "The display refused a change" | The display checks every change itself; the message says what was wrong. The app then shows the settings as they really are. |

## Development
- `pio test -e native` runs the host tests in `test/`:
  - count formatting, frame composition and accent folding;
  - API response parsers against `test/fixtures/*.json`;
  - backoff and token-refresh policy;
  - config validation.
- `cd tools && python -m unittest discover -s auth/tests -t .` runs the CLI tests. They are offline.
- `py tools/gen_cert_bundle.py` regenerates the TLS root-CA bundle from `certifi` (needs `pip install certifi cryptography`). Do this once a year or so, then re-flash.
- Never commit secrets: `.env`, `secrets.h` and `*.local.*` are git-ignored.
- `PLAN.md` holds the build plan and the API research notes (what was verified, and when).

## Credits
- Split-flap mechanics, PCB, original firmware: **David Königsmann**, [davidkingsman/split-flap](https://github.com/davidkingsman/split-flap) (GPL-3.0), 3D models on [Printables](https://www.printables.com/model/69464-split-flap-display).
- Maintained fork and model backup: [JonnyBooker/split-flap](https://github.com/JonnyBooker/split-flap).
- This firmware is a derivative work and is also licensed under GPL-3.0.
