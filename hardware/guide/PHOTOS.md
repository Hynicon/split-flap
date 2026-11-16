# Photos for the build guide

The guide has a slot for photos at every step. A slot stays hidden until it has a photo, so you can add them a few at a time, as you build.

## How to add photos

1. Take the photo with your phone, landscape, in good light (a window or a desk lamp, no flash), on a plain background. Fill the frame with the part the step is about.
2. Name the file after the step it belongs to: `pcb5.jpg`. For more than one photo for a step, add `-2`, `-3`: `pcb5-2.jpg`. JPEG, PNG, HEIC and WebP all work.
3. Put the files in `hardware/guide/photos-inbox/`. That folder is not committed.
4. Run:

   ```
   python tools/add_photos.py
   ```

   It turns each photo the right way up, shrinks it to 1600 px wide, and **removes the location and camera details** your phone stores in it. It saves the result in `hardware/guide/photos/` and lists it in `photos/photos.json`. It moves the originals to `photos-inbox/done/` (also not committed; delete them when you like) and prints which steps still have no photo.
5. Optional: open `hardware/guide/photos/photos.json` and fill in `caption` (shown under the photo) and `alt` (read out by screen readers: say what the photo shows, for example "L7805 standing upright with its metal tab toward the Nano headers").
6. Preview with `python tools/serve_guide.py`, then commit and publish the guide.

A photo replaces an older photo in the same step if that one shows upstream's older boards (the stage 3 and case photos). The others stay alongside it.

Check each photo before committing it: the guide is public. Keep faces, screens with personal details and the inside of your home out of the frame.

## Shot list

**Bold** names are the ones that help most: parts that are easy to get the wrong way round.

| Name | What to show |
|---|---|
| `hero` | The finished display, from the front, showing a word. Shown at the top of the guide. |
| `parts` | Everything for one unit laid out: printed parts, motor, sensor, board, Nano, screws. |
| `pcb1` | Bottom side with the ULN2003A soldered, dot at the top left. Close up, so the pins are sharp. |
| `pcb2` | The two capacitors in the box marked 5V. |
| `pcb3` | Top side with the DIP switch and both header rows, ON edge visible. |
| `pcb4` | The four XH sockets fitted, from above. |
| **`pcb5`** | The L7805 standing upright, metal tab toward the Nano headers. Take a second shot from the side. |
| `pcb6` | Meter probes on U8's pins in continuity mode. |
| **`pcb7`** | The finished power lead: tagged wires, DC jack soldered, SDA and SCL taped. |
| `pcb8` | Meter reading 5 V on the Nano header's 5V hole. |
| **`dm9`** | A KY-003 with its cable soldered, the + − S labels readable. |
| `dm2` | The magnet held to the sensor with the LED lit. |
| `dm3` | The magnet glued in its pocket on FlapDrumInner, close up. |
| `dm4` | The two drum halves pushed together, inserts under the counterbored holes. |
| `dm6` | Flaps going in, and a second shot of the first letters forming. |
| `dm7` | The two M2×8 screws in place. |
| `dm8` | A finished drum. |
| `ut1` | M3 nuts in two opposite hex pockets. |
| `ut2` | The stepper screwed to the side wall, wires toward the round post. |
| `ut3` | The KY-003 on its mount. |
| `ut4` | Cables pressed into the channel, from underneath. |
| **`ut5`** | Your board on the PCB plate, U10 and U6 at the sensor end, both cables plugged in. |
| `ut6` | The drum on the shaft, from the side. |
| **`ut7`** | The Nano plugged in, USB end toward U10, the DIP set and the frame labelled. |
| `ft2` | A unit on the bench showing the blank flap after homing. |
| `ca1` | One layer's three pieces dry-fitted face down. |
| `ca2` | Inserts in the middle frame's outer rows. |
| `ca3` | A joint: the nut in its pocket and the M4×20 going in. |
| `ca4` | A unit held to the front of the middle frame, its board through a window. |
| `ca5` | The back of the middle frame with all units screwed on. |
| `ca6` | The units daisy-chained, U7 to U8. |
| `ca7` | Inserts in a front cover piece's back edge. |
| `ca8` | A front cover joint with its nut and M3×8. |
| `ca9` | The middle frame lowered onto the front cover. |
| `ca10` | The front cover screws from the back. |
| `ca11` | The back cover joined, with the switch and DC jack fitted. |
| `ca12` | The back cover closed, keyholes visible. |
| `es1` | The buck converter on 12 V with the meter reading 5.0 V, screwdriver on the trimmer. |
| `es2` | The finished wiring before it goes into the tray: switch, capacitor, joints insulated. |
| **`es6`** | The three boards clipped into the tray, and the tray screwed into the back cover. |
| `es5` | The serial monitor showing the bus scan (a screenshot is fine). |
| `al2` | The display showing all 8s during the load test. |
| `al3` | The finished display on the wall or a shelf. |
