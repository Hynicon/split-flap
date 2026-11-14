"""Extract the build guide's photos from upstream's assembly instructions.

Reads upstream/split-flap/Instructions/SplitFlapInstructions.pdf (David
Königsmann's PDF, part of the upstream repo) and writes the figures the guide
shows into hardware/guide/img/. Like the models, they are git-ignored; the
guide hides any picture that is missing.

    pip install pymupdf
    python tools/make_guide_images.py
"""
from pathlib import Path

import pymupdf

ROOT = Path(__file__).resolve().parent.parent
PDF = ROOT / "upstream" / "split-flap" / "Instructions" / "SplitFlapInstructions.pdf"
OUT = ROOT / "hardware" / "guide" / "img"

# (page number, image index on that page) -> file name, in the PDF's order.
PARTS = ["drum-inner", "drum-outer", "unit-frame",
         "mf-left", "mf-center", "mf-right",
         "fc-left", "fc-center", "fc-right",
         "bc-left", "bc-center", "bc-right"]
FIGS = {(10, i): f"part-{name}" for i, name in enumerate(PARTS)}
FIGS.update({
    (9, 0): "pcb-bottom",
    (9, 1): "pcb-top",
    (13, 0): "drum-inserts",
    (14, 0): "drum-first-flap",
    (15, 0): "cables",
    (16, 0): "unit-top",
    (17, 0): "unit-cables",
    (17, 1): "unit-pcb",
    (18, 0): "units-on-frame",
    (18, 1): "mf-inserts",
})


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    doc = pymupdf.open(PDF)
    for (page, idx), name in FIGS.items():
        xref = doc[page - 1].get_images(full=True)[idx][0]
        img = doc.extract_image(xref)
        dst = OUT / f"{name}.{img['ext']}"
        dst.write_bytes(img["image"])
        print(f"{dst.name}: {img['width']}x{img['height']}")


if __name__ == "__main__":
    main()
