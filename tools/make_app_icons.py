"""Draw the app's home-screen icons (PNG) to match firmware/master/data/icon.svg.

    pip install pillow
    python tools/make_app_icons.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parent.parent / "firmware" / "master" / "data"
BG, AMBER, WHITE = "#16181a", "#e9b61a", "#f4f3ee"


def icon(size: int) -> Image.Image:
    scale = 4  # draw large, then shrink, for smooth edges
    s = size * scale / 512
    img = Image.new("RGB", (size * scale, size * scale), BG)
    d = ImageDraw.Draw(img)
    box = lambda x, y, w, h: [x * s, y * s, (x + w) * s, (y + h) * s]
    d.rounded_rectangle(box(116, 116, 124, 280), radius=14 * s, fill=AMBER)
    d.rounded_rectangle(box(272, 116, 124, 280), radius=14 * s, fill=WHITE)
    d.rectangle(box(100, 248, 312, 16), fill=BG)
    return img.resize((size, size), Image.LANCZOS)


for size in (180, 192, 512):
    path = OUT / f"icon-{size}.png"
    icon(size).save(path, optimize=True)
    print(f"{path.name}: {path.stat().st_size} bytes")
