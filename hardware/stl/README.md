# Printed parts

The model pack comes from Printables (https://www.printables.com/model/69464-split-flap-display), by David Königsmann. The maintained fork keeps a backup of it at `upstream/jonnybooker/3dModels/Models.zip`, and that backup has been extracted here.

Everything in this folder except this README is **git-ignored**. Check the model's license before committing or redistributing the files. To re-create the folder, unzip that archive here again, or download the pack from Printables by hand (the site blocks automated downloads).

## What to print for 9 units
- `Unit/`: 9 × each per-unit part.
- `MiddleFrame/`, `FrontCover/`, `BackCover/`: the upstream case is sized for 10 units, so shorten it or leave one position empty. See the PDF in this folder.
- Flaps: 9 full sets of 45. Use **`Flaps 1-9` … `Flaps 37-45`**, but take flaps 28–31 from **`Flap 28-36 no umlaute/`** (`Flap28_no_um` … `Flap31_no_um`) instead of `Flaps 28-36/`. That gives `$ & #` at positions 27–29 instead of `Ä Ö Ü`, matching the firmware.

The final flap order must match `FLAP_GLYPHS` in `firmware/common/charset.h`:

```
(blank) A B C D E F G H I J K L M N O P Q R S T U V W X Y Z $ & # 0 1 2 3 4 5 6 7 8 9 : . - ? !
```

There are no accented flaps: the firmware folds Portuguese and other accented text to plain letters (`ã→A`, `ç→C`, `é→E`).
