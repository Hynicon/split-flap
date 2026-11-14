"""Shorten the upstream 10-unit Assembly.step to 9, 8 and 7 units.

Same cuts as tools/make_case.py, applied to the B-rep solids in assembly
coordinates: each removed unit is a 50.4 mm slab, and everything right of a
slab moves 50.4 mm left. The single modelled unit sits in the leftmost window
and does not move.

    pip install build123d
    python tools/make_assembly.py

Reads hardware/stl/Assembly_10unit.step (the original upstream file) and writes
hardware/stl/Assembly_<n>unit.step, plus Assembly.step (the 9-unit version).
"""
import shutil
from pathlib import Path

from build123d import Align, Box, Compound, Pos, export_step, import_step

PITCH = 50.4
STL = Path(__file__).resolve().parent.parent / "hardware" / "stl"
SRC = STL / "Assembly_10unit.step"
# Assembly-frame x where each removable slab starts, per case group
# (the print-frame cuts from make_case.py, moved into assembly coordinates).
LEFT_1 = {"HousingFrame": 23.3, "FrameFront": 28.3, "FrameBack": 9.7}
LEFT_2 = {"HousingFrame": 81.0, "FrameFront": 81.0, "FrameBack": 81.0}
RIGHT_1 = {"HousingFrame": 311.0, "FrameFront": 311.0, "FrameBack": 311.0}
VARIANTS = {9: [LEFT_1], 8: [LEFT_1, RIGHT_1], 7: [LEFT_1, LEFT_2, RIGHT_1]}
BIG = 2000


def slab(x0, x1):
    return Pos(x0, -BIG / 2, -BIG / 2) * Box(x1 - x0, BIG, BIG, align=Align.MIN)


def shorten(solid, cuts):
    """Remove [c, c + PITCH] for each c in cuts and close the gaps."""
    bb = solid.bounding_box()
    hits = [c for c in cuts if c < bb.max.X and c + PITCH > bb.min.X]
    shift = -PITCH * sum(c + PITCH <= bb.min.X for c in cuts)
    if not hits:
        return Pos(shift, 0, 0) * solid
    edges = [-BIG] + [x for c in hits for x in (c, c + PITCH)] + [BIG]
    out = None
    for i in range(0, len(edges), 2):
        piece = Pos(shift - PITCH * (i // 2), 0, 0) * (solid & slab(edges[i], edges[i + 1]))
        out = piece if out is None else out + piece
    solids = out.clean().solids()
    assert len(solids) == 1, f"expected one solid, got {len(solids)}"
    return solids[0]


def build(asm, tables):
    children = []
    for child in asm.children:
        if child.label in LEFT_1:
            cuts = sorted(t[child.label] for t in tables)
            # Sorted left to right: Left, Center, Right.
            solids = sorted(child.solids(), key=lambda s: s.bounding_box().min.X)
            parts = [shorten(s, cuts) for s in solids]
            for p, side in zip(parts, ("Left", "Center", "Right")):
                p.label = f"{child.label}{side}"
            group = Compound(children=parts, label=child.label)
            group.color = child.color
            children.append(group)
        else:
            children.append(child)
    return Compound(children=children, label=asm.label)


def main():
    for n, tables in VARIANTS.items():
        dst = STL / f"Assembly_{n}unit.step"
        # Re-import each time: build() re-parents the unchanged children.
        out = build(import_step(SRC), tables)
        export_step(out, dst)
        bb = out.bounding_box()
        print(f"wrote {dst.name}: {bb.max.X - bb.min.X:.1f} mm wide (10-unit is 513.2)")
    shutil.copyfile(STL / "Assembly_9unit.step", STL / "Assembly.step")


if __name__ == "__main__":
    main()
