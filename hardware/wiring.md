# Wiring

## Power: the upstream design runs on 12 V
Upstream's build instructions (`SplitFlapInstructions.pdf`, §2) use **12 V 28BYJ-48 steppers**. The 5 V version will not work with these PCBs. Each unit PCB carries:
- an **L7805**, which makes 5 V for the Nano and the hall sensor;
- a **ULN2003A**, which switches 12 V to the stepper.

Upstream's measurements per unit at 12 V:
- 13 mA idle;
- 186 mA while flipping.

For **9 units + ESP32**:
- about 0.3 A idle;
- **about 1.8 A with every unit flipping at once**.

Use a **12 V ≥ 3 A** supply. Optionally add a 1000 µF / 25 V electrolytic across 12 V/GND at the power input.

## Bus
Units daisy-chain through the 4-pin JST-XH sockets (U7/U8, 70 mm cables). Each cable carries 12 V, GND, SDA and SCL. Check the silkscreen for the pin order.

```
 12V PSU ──► Unit 0 ──XH-4──► Unit 1 ──XH-4──► … ──► Unit 8
             (ESP32 on the ESP-01S socket)
```

## Option A (recommended): ESP32 on the first unit's PCB
The **first** unit's PCB is fully populated (upstream §3.3.1). It has:
- an **AMS1117-3.3** regulator;
- a **BSS138** bidirectional level shifter (Q1, Q2);
- **10 kΩ pull-ups** on both sides;
- an **ESP-01S socket** (U2).

The ESP32 takes the ESP-01's place through 4 jumper wires into that socket, so no extra level shifter is needed.

| ESP-01S socket pin | Upstream use | → ESP32 DevKit pin |
|---|---|---|
| VCC (3.3 V) | ESP-01 power | **3V3** |
| GND | | **GND** |
| TXD (GPIO1) | I2C **SDA** (`Wire.begin(1, 3)`) | **GPIO21** |
| RXD (GPIO3) | I2C **SCL** | **GPIO22** |

ESP-01S pinout (component side, antenna pointing away from you): `GND, GPIO2, GPIO0, RXD` / `TXD, EN, RST, VCC`. **Check this against the silkscreen of your PCB before powering up.**

- The AMS1117 is rated 1 A, which is enough for the ESP32's ~500 mA Wi-Fi peaks.
- **While flashing over USB, disconnect the 3V3 jumper** (keep GND, SDA, SCL). Otherwise the DevKit's regulator and the AMS1117 back-feed each other.
- Leave CH_PD/EN, RST, GPIO0 and GPIO2 unconnected. They were for the ESP-01.

## Option B: no upstream PCB for the first unit
Use a BSS138 module (for example the 4-channel "I2C level shifter" boards):

```
ESP32 3V3  ── LV        HV ── 5 V (from a unit's 7805 / Nano 5V pin)
ESP32 GND  ── GND      GND ── bus GND
ESP32 G21  ── LV1      HV1 ── bus SDA (Nano A4)
ESP32 G22  ── LV2      HV2 ── bus SCL (Nano A5)
```

The ESP32 is **not 5 V tolerant**. Never connect SDA/SCL straight to the 5 V bus: the Nanos' `Wire` enables internal pull-ups to 5 V. Power the ESP32 from a 5 V → 3.3 V source, or from its USB port.

## Unit addresses (DIP switch)
Each unit reads its 4-position DIP switch at power-up. Switch 1 is the most significant bit, and ON = 1. The I2C address is **0x10 + DIP**. Unit 0 is the **leftmost** flap.

| Unit (left → right) | DIP 1 2 3 4 | Address | Shows |
|---|---|---|---|
| 0 | off off off off | 0x10 | label 1st letter |
| 1 | off off off ON | 0x11 | label 2nd letter |
| 2 | off off ON off | 0x12 | digit |
| 3 | off off ON ON | 0x13 | digit |
| 4 | off ON off off | 0x14 | digit |
| 5 | off ON off ON | 0x15 | digit |
| 6 | off ON ON off | 0x16 | digit |
| 7 | off ON ON ON | 0x17 | digit |
| 8 | ON off off off | 0x18 | digit |

> This firmware's addresses differ from upstream's, which started at 0x00 (the I2C general-call address). Units must run **this** repo's unit firmware.

## Bring-up checklist (Phase 7)
1. Print the parts (see `hardware/stl/README.md`). Build **one unit** first.
2. Flash `pio run -e offset_tool -t upload`. In `pio device monitor`, type an offset (start with `100`) and press Enter.
3. Flash `pio run -e unit -t upload` with DIP = 0000. After power-up the unit homes. The **blank** flap should show. If it shows a neighbouring flap instead, re-run step 2 with a larger or smaller offset. About 45 steps = one flap.
4. **Check the suspected `calibrate()` inversion** (`firmware/unit/src/main.cpp`, TODO in `calibrate()`):
   - Power up with the magnet parked right at the sensor. Does the drum back out and do a full turn, or stop immediately?
   - Power up away from the magnet. Does it travel ~50 steps first, then search?
   - Note what you see. Change the code only if homing is actually wrong.
5. Optional: uncomment `#define test` in the unit firmware. The unit should show `" ", Z, A, U, N, ?, 0, 1, 2, 9` in that order.
6. Wire the ESP32 (Option A or B). Flash the master: `pio run -e master -t upload && pio run -e master -t uploadfs`. Then:
   - The serial monitor (115200) should print `scan: 1/9 units answered: 0x10=ok …`.
   - Type text in the serial monitor and it should appear on the flaps.
7. Add the units one by one with DIP 0001, 0010, … Check each address in the scan (`!scan` on the serial console, or the Status page).
8. Full 9-unit test with every unit moving at once: show `88888888` → `        ` → `88888888`. Measure the 12 V rail under load. **Each unit's 7805 needs ≥ 7 V in**, so the rail must not sag below about 11 V.
9. 24-hour soak test:
   - Leave it cycling.
   - Afterwards, check that the Status page shows `last reset: power_on` and that uptime is about 24 h.
   - The serial log should show no `[bus] timeout` lines.
