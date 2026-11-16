"""Add your own build photos to the guide. See hardware/guide/PHOTOS.md for the shot list.

Name each photo after its step (pcb5.jpg, pcb5-2.jpg, hero.jpg) and put it in
hardware/guide/photos-inbox/. This turns it upright, shrinks it, strips the location and camera
details, saves it in hardware/guide/photos/ and lists it in photos/photos.json, which the guide
reads. The originals move to photos-inbox/done/.

    python tools/add_photos.py           # add what's in the inbox, then list the steps still without a photo
    python tools/add_photos.py --list    # only list
"""
import json
import re
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parent.parent
GUIDE = ROOT / "hardware" / "guide"
INBOX = GUIDE / "photos-inbox"
OUT = GUIDE / "photos"
INDEX = OUT / "photos.json"
MAX_SIDE = 1600
NAME = re.compile(r"^([a-z]+\d*)(?:-(\d+))?\.(jpe?g|png|heic|heif|webp)$", re.I)

try:  # iPhone photos are HEIC; Pillow reads them only with this plugin
    from pillow_heif import register_heif_opener
    register_heif_opener()
    HEIC = True
except ImportError:
    HEIC = False


def slots() -> list[str]:
    """Step ids and photo slots in the guide, in page order."""
    html = (GUIDE / "index.html").read_text(encoding="utf-8")
    return [a or b for a, b in re.findall(r'<li><input type="checkbox" id="(\w+)">|data-photos="(\w+)"', html)]


def shot_list() -> dict[str, str]:
    md = (GUIDE / "PHOTOS.md").read_text(encoding="utf-8")
    return {m[0]: m[1] for m in re.findall(r"^\| \**`(\w+)`\** \| (.+?) \|$", md, re.M)}


def convert(src: Path, dest_stem: Path) -> Path:
    with Image.open(src) as im:
        im = ImageOps.exif_transpose(im)  # phones store "rotate me" in the metadata we're about to drop
        icc = im.info.get("icc_profile")  # colour profile only: keeps colours right, holds nothing personal
        keep_png = src.suffix.lower() == ".png"  # screenshots stay sharp as PNG
        im = im.convert("RGBA" if keep_png and im.mode in ("RGBA", "LA", "P") else "RGB")
        im.thumbnail((MAX_SIDE, MAX_SIDE), Image.LANCZOS)
        dest = dest_stem.with_suffix(".png" if keep_png else ".jpg")
        for old in OUT.glob(dest_stem.name + ".*"):
            if old.suffix in (".jpg", ".png"):
                old.unlink()
        if keep_png:
            im.save(dest, optimize=True, icc_profile=icc)
        else:
            im.save(dest, quality=82, optimize=True, progressive=True, icc_profile=icc)
    with Image.open(dest) as check:  # make sure nothing like GPS survived
        assert not check.getexif(), f"{dest.name} still has metadata"
        assert not {"exif", "xmp", "XML:com.adobe.xmp"} & set(check.info), f"{dest.name} still has metadata"
    return dest


def main() -> None:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # the shot list has − and × in it
    keys = slots()
    index: dict[str, list[dict]] = json.loads(INDEX.read_text(encoding="utf-8")) if INDEX.exists() else {}
    OUT.mkdir(parents=True, exist_ok=True)
    added = 0
    if "--list" not in sys.argv and INBOX.exists():
        done = INBOX / "done"
        for src in sorted(p for p in INBOX.iterdir() if p.is_file()):
            m = NAME.match(src.name)
            if not m:
                print(f"  skipped {src.name}: name it after a step, like pcb5.jpg or pcb5-2.jpg")
                continue
            key, n, ext = m[1].lower(), m[2], m[3].lower()
            if key not in keys:
                print(f"  skipped {src.name}: there's no step called {key} (see the shot list in hardware/guide/PHOTOS.md)")
                continue
            if ext in ("heic", "heif") and not HEIC:
                print(f"  skipped {src.name}: HEIC needs   pip install pillow-heif   (or set the phone to save JPEG)")
                continue
            dest = convert(src, OUT / (key if not n or n == "1" else f"{key}-{n}"))
            entries = index.setdefault(key, [])
            if not any(e["file"] == dest.name for e in entries):
                entries.append({"file": dest.name, "caption": "", "alt": ""})
            for e in entries:  # a photo re-added as PNG instead of JPG (or back)
                if Path(e["file"]).stem == dest.stem:
                    e["file"] = dest.name
            done.mkdir(exist_ok=True)
            shutil.move(str(src), done / src.name)
            print(f"  added {dest.relative_to(ROOT)} ({dest.stat().st_size // 1024} KB)")
            added += 1

    # Drop entries whose file was deleted, sort each step's photos, keep steps in page order.
    tidy = {}
    for key in keys:
        entries = [e for e in index.get(key, []) if (OUT / e["file"]).exists()]
        if entries:
            entries.sort(key=lambda e: int(m[1]) if (m := re.search(r"-(\d+)$", Path(e["file"]).stem)) else 1)
            tidy[key] = entries
    INDEX.write_text(json.dumps(tidy, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    shots = shot_list()
    missing = [k for k in shots if k not in tidy]
    print(f"\n{added} added. {len(tidy)} of {len(shots)} shots done.")
    if missing:
        print("Still wanted:")
        for k in missing:
            print(f"  {k:6} {re.sub(r'[*`]', '', shots[k])}")
    no_alt = [e["file"] for v in tidy.values() for e in v if not e.get("alt")]
    if no_alt:
        print(f"\nTip: give these an 'alt' description in {INDEX.relative_to(ROOT)}: {', '.join(no_alt)}")


if __name__ == "__main__":
    main()
