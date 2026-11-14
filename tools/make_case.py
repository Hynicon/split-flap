"""Shorten the upstream 10-unit case to 9, 8 or 7 units.

The case repeats every 50.4 mm (one unit). Each removed unit is a 50.4 mm slab
cut out of a Left or Right case part, with the two halves rejoined. Center
parts are never changed. Cut positions were chosen where the geometry repeats
exactly and the removed slab holds no screw boss or wall-hanging keyhole:

    9 units: Left -1
    8 units: Left -1, Right -1   (Center piece stays centred)
    7 units: Left -2, Right -1

    pip install trimesh numpy manifold3d networkx
    python tools/make_case.py

Writes hardware/stl/Case <n>-unit/*_<n>unit.stl.
"""
from pathlib import Path

import manifold3d as mf
import numpy as np
import trimesh

PITCH = 50.4
STL = Path(__file__).resolve().parent.parent / "hardware" / "stl"
# x (in the part's own STL frame) where each removable slab starts.
LEFT_1 = {"MiddleFrame": 62.8, "FrontCover": 72.6, "BackCover": 54.0}
LEFT_2 = {"MiddleFrame": 120.5, "FrontCover": 125.3, "BackCover": 125.3}
RIGHT_1 = {"MiddleFrame": 75.6, "FrontCover": 70.7, "BackCover": 70.7}
# units -> {side: [cut tables]}
VARIANTS = {
    9: {"Left": [LEFT_1]},
    8: {"Left": [LEFT_1], "Right": [RIGHT_1]},
    7: {"Left": [LEFT_1, LEFT_2], "Right": [RIGHT_1]},
}


def to_manifold(t):
    return mf.Manifold(mf.Mesh(vert_properties=np.asarray(t.vertices, np.float32),
                               tri_verts=np.asarray(t.faces, np.uint32)))


def slab(x0, x1, b):
    size = [x1 - x0, b[1][1] - b[0][1] + 20, b[1][2] - b[0][2] + 20]
    return mf.Manifold.cube(size).translate([x0, b[0][1] - 10, b[0][2] - 10])


def shorten(src, cuts):
    """Remove [c, c + PITCH] for each c in cuts and close the gaps."""
    t = trimesh.load(src)
    m, b, e = to_manifold(t), t.bounds, 0.02
    edges = [b[0][0] - 5] + [x for c in sorted(cuts) for x in (c, c + PITCH)] + [b[1][0] + 5]
    out = None
    for i in range(0, len(edges), 2):
        x0, x1 = edges[i], edges[i + 1]
        piece = (m ^ slab(x0 - e, x1 + e, b)).translate([-PITCH * (i // 2), 0, 0])
        out = piece if out is None else out + piece
    g = out.to_mesh()
    mesh = trimesh.Trimesh(g.vert_properties[:, :3], g.tri_verts)
    # Drop zero-volume slivers left on the seams.
    return max(mesh.split(only_watertight=False), key=lambda p: p.volume)


def main():
    for n, sides in VARIANTS.items():
        out = STL / f"Case {n}-unit"
        out.mkdir(exist_ok=True)
        for side, tables in sides.items():
            for group in LEFT_1:
                src = STL / group / f"{group}{side}.stl"
                part = shorten(src, [t[group] for t in tables])
                dst = out / f"{src.stem}_{n}unit.stl"
                part.export(dst)
                print(f"{out.name}/{dst.name}: {part.extents[0]:.1f} mm wide, "
                      f"watertight={part.is_watertight}")


if __name__ == "__main__":
    main()
