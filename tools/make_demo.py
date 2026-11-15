"""Build the app demo: the phone app with a virtual display, for the build guide's site.

Copies the app (firmware/master/data) into hardware/guide/demo/ and adds tools/demo/demo.js, which
answers the app's API calls in the browser and flips a virtual board. The display and the server
never load demo.js. The output is git-ignored; it is published with the guide.

    python tools/make_demo.py
    python tools/serve_guide.py      # then open /demo/
"""
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "firmware" / "master" / "data"
OUT = ROOT / "hardware" / "guide" / "demo"
COPY = ["app.js", "style.css", "icon.svg", "icon-180.png", "icon-192.png"]


def main() -> None:
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    for name in COPY:
        shutil.copy2(APP / name, OUT / name)
    shutil.copy2(ROOT / "tools" / "demo" / "demo.js", OUT / "demo.js")
    html = (APP / "index.html").read_text(encoding="utf-8")
    for old, new in [
        ('<script src="app.js"></script>', '<script src="demo.js"></script>\n  <script src="app.js"></script>'),
        ('  <link rel="manifest" href="manifest.webmanifest">\n', ""),  # no home-screen install for the demo
        ("<title>", '<meta name="description" content="Try the split-flap display\'s phone app with a virtual display.">\n  <title>'),
    ]:
        assert old in html, old
        html = html.replace(old, new, 1)
    (OUT / "index.html").write_text(html, encoding="utf-8")
    size = sum(f.stat().st_size for f in OUT.iterdir()) // 1024
    print(f"{OUT.relative_to(ROOT)}: {len(list(OUT.iterdir()))} files, {size} KB")


if __name__ == "__main__":
    main()
