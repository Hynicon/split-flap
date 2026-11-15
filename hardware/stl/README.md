# Printed parts

The model pack comes from Printables (https://www.printables.com/model/69464-split-flap-display), by David Königsmann. The maintained fork keeps a backup of it at `upstream/jonnybooker/3dModels/Models.zip`, and that backup has been extracted here.

Everything in this folder except this README is **git-ignored**. Check the model's license before committing or redistributing the files. To re-create the folder, unzip that archive here again, or download the pack from Printables by hand (the site blocks automated downloads).

## What to print for 7, 8, 9 or 10 units
The upstream case holds 10 units, so the 10-unit build prints every upstream part unchanged. Each shorter version cuts whole 50.4 mm units out of the Left and/or Right case parts. The three **Center** parts (`MiddleFrameCenter`, `FrontCoverCenter`, `BackCoverCenter`) are always printed unchanged.

| Units | Width | Left parts | Right parts | Assembly |
|---|---|---|---|---|
| 10 | ~513 mm | upstream `*Left`, unchanged | upstream `*Right`, unchanged | `Assembly_10unit.step` |
| 9 | ~463 mm | `Case 9-unit/*Left_9unit` (−1 unit) | upstream `*Right`, unchanged | `Assembly_9unit.step` |
| 8 | ~412 mm | `Case 8-unit/*Left_8unit` (−1) | `Case 8-unit/*Right_8unit` (−1) | `Assembly_8unit.step` |
| 7 | ~362 mm | `Case 7-unit/*Left_7unit` (−2) | `Case 7-unit/*Right_7unit` (−1) | `Assembly_7unit.step` |

Each layer (`MiddleFrame`, `FrontCover`, `BackCover`) has its own Left and Right part. The cuts avoid the screw bosses and the wall-hanging keyholes, and the screw holes still line up between layers. The 8-unit case keeps the Center joint in the middle of the display.

- `Unit/`: one of each per-unit part per unit (7, 8, 9 or 10).
- Every case part, including the upstream 10-unit ones (≤ 241 mm), fits a 256 mm bed.
- `Assembly.step` is the 9-unit version. The original 10-unit file is kept as `Assembly_10unit.step`.
- To regenerate the case parts, run `python tools/make_case.py`. To regenerate the assemblies, run `python tools/make_assembly.py`.
- Build guide: `python tools/serve_guide.py` opens `hardware/guide/` in the browser, with step-by-step 3D diagrams of the drum, the unit and the case, wiring drawings, and per-unit progress ticks. Add `--lan` to open it from a phone or tablet on the same network too. Its models come from `python tools/make_guide_models.py` (after `make_assembly.py`), and its photos from upstream's PDF via `python tools/make_guide_images.py` (needs `pip install pymupdf`).
- Electronics: `ElectronicsTray/ElectronicsTray.stl` holds the ESP32 (30-pin DevKit V1), the MP1584 buck converter and the BSS138 level shifter with snap clips, and screws onto 4 bosses in the back cover's Left piece. Print that piece from its `_mount` file instead (`BackCover/BackCoverLeft_mount.stl` for 10 units, `Case <n>-unit/BackCoverLeft_<n>unit_mount.stl` otherwise), and add 4 M3 inserts and 4 M3×8. Run `python tools/make_electronics_mount.py` to make them (after `make_case.py`); board sizes are at the top of the script. The tray is this project's own design, so its STL is committed; the `_mount` covers derive from upstream and stay git-ignored.
- Firmware: set `units` in the master's settings to match (the default is 9).
- Flaps: one full set of 45 per unit. This build uses a **Portuguese set**: 31 flaps come from upstream (**`Flaps 1-9` … `Flaps 37-45`**) and 14 from **`Flaps PT/`**. Take these from `Flaps PT/` instead of upstream:

  | Flap | Front (top half) | Back (bottom half) | Replaces upstream |
  |---|---|---|---|
  | 1 | Facebook logo | blank | `Flaps 1-9/Flap1` |
  | 14 | L | M (narrowed) | `Flaps 10-18/Flap14` |
  | 15 | M (narrowed) | N | `Flap15` |
  | 24 | V | W (narrowed) | `Flaps 19-27/Flap24` |
  | 25 | W (narrowed) | X | `Flap25` |
  | 28 | Z | Ã | `Flap28` |
  | 29 | Ã | Ç | `Flap29` |
  | 30 | Ç | É | `Flap30` |
  | 31 | É | 0 | `Flap31` |
  | 41 | 9 | Á | `Flaps 37-45/Flap41` |
  | 42 | Á | . | `Flap42` |
  | 43 | (blank: the `.` sits wholly below the split) | TikTok logo | `Flap43` |
  | 44 | TikTok logo | Instagram logo | `Flap44` |
  | 45 | Instagram logo | Facebook logo | `Flap45` |

  Don't print upstream's flaps 28–31 at all (neither `Flaps 28-36/` nor `Flap 28-36 no umlaute/`). The Z, 0, 9 and `.` halves on the new flaps are copied from upstream's, and so are the A, C and E under the accents, so they match the rest of the set. Upstream's M and W are squeezed sideways (same height) to the width of A, O and V: next to the hinge pins the flap is only 39 mm wide, and upstream's W (41 mm) ran past its edge, with M (38.4 mm) leaving a 0.3 mm rim. To regenerate them, run `python tools/make_flaps_pt.py` (needs `pip install trimesh numpy shapely manifold3d mapbox_earcut`).
- Flaps in OrcaSlicer / Bambu Studio: the upstream `.3mf` plates are PrusaSlicer projects, and Orca loads only their geometry, so the white letters lose their filament. Print the five plates in **`Flaps PT/`** instead: `Flap1-9_pt_orca`, `Flap10-18_pt_orca`, `Flap19-27_pt_orca`, `Flap28-36_pt_orca` and `Flap37-45_pt_orca`. They are the whole set with the new flaps already swapped in, and every flap has its letter parts on filament 2. If you've already printed upstream's flaps, `Flaps_PT_orca` holds just the 14 new ones. (`Flaps Orca/`, made by `python tools/flaps_to_orca.py`, has upstream's original set.)

The final flap order must match `FLAP_GLYPHS` in `firmware/common/charset.h`:

```
(blank) A B C D E F G H I J K L M N O P Q R S T U V W X Y Z Ã Ç É 0 1 2 3 4 5 6 7 8 9 Á . [TikTok] [Instagram] [Facebook]
```

Each flap carries the top half of one character on its front and the bottom half of the previous one on its back: flap *n* shows the top of character *n*−2 and the bottom of character *n*−1 (counting the blank as 0, wrapping at 45). Other accents (`õ ê í ó ú à â ô`) are folded to the plain letter by the firmware.
