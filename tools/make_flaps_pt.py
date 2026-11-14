"""Make the replacement flaps for the Portuguese flap set.

Upstream's 45 flaps show
    (blank) A-Z $ & # 0-9 : . - ? !
This set swaps the symbols for Portuguese accents and three logos:
    (blank) A-Z Ã Ç É 0-9 Á . [TikTok] [Instagram] [Facebook]
so blank, A-Z, 0-9 and '.' keep their positions. Upstream's M and W are also narrowed,
because they run past the flap's edge next to the hinge pins. 14 of the 45 flaps change.

Each flap carries the top half of one glyph on its front and the bottom half of the
previous glyph on its back: flap n (1-45) shows the top of glyph n-2 and the bottom of
glyph n-1 (indices wrap at 45). The letters are 0.4 mm white inlays in the 1 mm flap.

Halves that stay the same (Z, 0, 9, '.') are copied from upstream's flaps, and so are the
A, C and E under the accents; only the accent marks (blocky, like upstream's Ä Ö Ü dots)
and the logos are drawn new.

    pip install trimesh numpy shapely manifold3d mapbox_earcut
    python tools/make_flaps_pt.py

Writes hardware/stl/Flaps PT/: FlapN_pt.stl (black flap), FlapN_pt_1.stl / _2.stl
(white front / back letters), Flaps_PT_orca.3mf (just the 14 new flaps) and
Flap1-9_pt_orca.3mf ... Flap37-45_pt_orca.3mf (the whole set, upstream's plates with the new
flaps swapped in). The Orca / Bambu Studio plates have the letters on filament 2.
"""
import re
import sys
from pathlib import Path

import manifold3d as mf
import numpy as np
import trimesh
from shapely import affinity
from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import unary_union

from flaps_to_orca import read_prusa, write_bambu

STL = Path(__file__).resolve().parent.parent / "hardware" / "stl"
OUT = STL / "Flaps PT"
# Upstream's full plates (the README's choice for 28-36), rewritten with the new flaps swapped in.
PLATES = ["Flaps 1-9/Flap1-9.3mf", "Flaps 10-18/Flap10-18.3mf", "Flaps 19-27/Flap19-27.3mf",
          "Flap 28-36 no umlaute/Flap28-36_no_Umlaut.3mf", "Flaps 37-45/Flap37-45.3mf"]

TT, IG, FB = "[tiktok]", "[instagram]", "[facebook]"
OLD = list(" ABCDEFGHIJKLMNOPQRSTUVWXYZ$&#0123456789:.-?!")
NEW = list(" ABCDEFGHIJKLMNOPQRSTUVWXYZÃÇÉ0123456789Á.") + [TT, IG, FB]
ACCENTED = {"Ã": ("A", "tilde"), "Ç": ("C", "cedilla"), "É": ("E", "acute"), "Á": ("A", "acute")}
N = 45
assert len(OLD) == len(NEW) == N

CAP = 26.85        # upstream cap half-height (mm above / below the split)
ACCENT_GAP = 1.0   # between cap top and accent, as upstream's Ä Ö Ü
EDGE = 33.4        # nothing past this distance from the split (upstream's dots end at 32.85)
# Widest a glyph may be either side of the middle. Between its hinge pins the flap is only
# ±19.5 mm, and upstream's M (±19.2) and W (±20.6) run to or past that edge; A O Q V are ±17.8.
HALF_WIDTH = 17.8
FRONT_Z, BACK_Z = (0.6, 1.0), (0.0, 0.4)


# ---- upstream flaps, in one frame ----
# Odd upstream flaps: hinge edge at y=0, flap y 0..35, z 0..1; the z>0.5 face holds the
# top half of a glyph at (u, v) = (x, y), the z<0.5 face the bottom half at (u, v) = (x, -y).
# Even ones are the same flap turned over: (x, y, z) -> (x, -y - 1.3, 1 - z).
TURN = np.array([[1, 0, 0, 0], [0, -1, 0, -1.3], [0, 0, -1, 1], [0, 0, 0, 1]], float)


def upstream_parts(n):
    """Letter meshes of upstream flap n (as the README tells you to print it), in the odd frame."""
    if 28 <= n <= 31:
        d = STL / "Flap 28-36 no umlaute"
        files = sorted(d.glob(f"Flap{n}_[0-9]_no_um.stl"))
    else:
        lo = (n - 1) // 9 * 9 + 1
        d = STL / f"Flaps {lo}-{lo + 8}"
        files = sorted(p for p in d.glob(f"Flap{n}[._]*.stl") if p.name != f"Flap{n}.stl")
    meshes = [trimesh.load(f) for f in files]
    if n % 2 == 0:
        for m in meshes:
            m.apply_transform(TURN)
    return meshes


def footprint(meshes, flip):
    tris = [Polygon(t) for m in meshes for t in m.vertices[m.faces][:, :, :2]]
    g = unary_union([t for t in tris if t.area > 1e-9])
    return affinity.scale(g, 1, -1, origin=(0, 0)) if flip else g


def upstream_glyph(k):
    """Whole upstream glyph k in (u, v): top from flap k+2, bottom from flap k+1."""
    top = [m for m in upstream_parts((k + 1) % N + 1) if m.centroid[2] > 0.5]
    bot = [m for m in upstream_parts(k % N + 1) if m.centroid[2] < 0.5]
    return unary_union([footprint(top, False), footprint(bot, True)]).buffer(0)


# ---- accents, drawn in the blocky style of upstream's Ä Ö Ü dots ----
def acute():
    """A slanted bar, 5.4 mm tall, about 4 mm thick."""
    return Polygon([(-4.4, 0), (0.8, 0), (5.0, 5.4), (-0.2, 5.4)])


def tilde():
    xs = np.linspace(-7.2, 7.2, 60)
    wave = LineString(list(zip(xs, 2.7 - 1.05 * np.sin(xs / 7.2 * np.pi))))
    return wave.buffer(1.65, cap_style="flat")


def cedilla():
    """A short stem down from the letter that curls right and back under itself (about 5.8 mm tall)."""
    a = np.linspace(np.pi / 2, -np.pi / 2, 24)
    path = [(0, 0.8)] + list(zip(1.55 * np.cos(a), -2.35 + 1.55 * np.sin(a))) + [(-2.4, -3.9)]
    return LineString(path).buffer(1.05, cap_style="flat", join_style="round")


MARKS = {"acute": acute, "tilde": tilde, "cedilla": cedilla}


def with_accent(letter, mark):
    """Mark centred on the letter: above it with a 1 mm gap, or (cedilla) hanging from its foot,
    overlapping it by 0.8 mm."""
    lx = (letter.bounds[0] + letter.bounds[2]) / 2
    y = letter.bounds[1] - 0.8 if mark == "cedilla" else CAP + ACCENT_GAP
    return unary_union([letter, affinity.translate(MARKS[mark](), lx, y)])


# ---- logos ----
R = HALF_WIDTH


def instagram():
    t, r = 3.4, 9.0
    frame = box(-R + r, -R + r, R - r, R - r).buffer(r, 64).difference(
        box(-R + r, -R + r, R - r, R - r).buffer(r - t, 64))
    lens = Point(0, 0).buffer(8.6, 64).difference(Point(0, 0).buffer(8.6 - t, 64))
    dot = Point(10.2, 10.2).buffer(2.1, 32)
    return unary_union([frame, lens, dot])


def tiktok():
    """The note: a ring at the bottom left, a stem, and a flag curling right off its top."""
    stem = box(1.2, -8.5, 6.8, 16.5)
    ring = Point(-2.8, -8.5).buffer(8.4, 96).difference(Point(-2.8, -8.5).buffer(3.8, 64))
    xs = np.linspace(6.8, 15.4, 40)
    t = (xs - 6.8) / 8.6
    upper = 16.5 - 7.0 * np.sqrt(1 - (1 - t) ** 2)        # concave top edge: stem top -> tip
    lower = 9.4 - 5.1 * t ** 0.8                          # bottom edge: stem -> tip
    flag = Polygon(list(zip(xs, upper)) + list(zip(xs[::-1], lower[::-1])))
    note = unary_union([stem, ring, flag.buffer(0)])
    x0, y0, x1, y1 = note.bounds
    return affinity.translate(note, -(x0 + x1) / 2, -(y0 + y1) / 2)


def facebook():
    stem = box(0.6, -R - 2, 6.8, 6.0)
    bar = box(0.6, 6.8, 12.4, 11.4)
    top = unary_union([box(0.6, 5.0, 6.8, 11.4), bar])
    corner = box(0.6, 5.0, 7.0, 11.4)        # round the top-left corner of the f
    top = top.difference(corner).union(Point(7.0, 5.0).buffer(6.4, 64).intersection(corner))
    cross = box(-4.4, -3.4, 11.4, 1.4)
    f = unary_union([stem, top, cross])
    return Point(0, 0).buffer(R, 128).difference(f)


# ---- flaps ----
def narrow(g):
    """Squeeze a glyph sideways (height unchanged, so the split stays put) to HALF_WIDTH."""
    w = max(-g.bounds[0], g.bounds[2])
    return affinity.scale(g, HALF_WIDTH / w, 1, origin=(0, 0)) if w > HALF_WIDTH + 0.01 else g


def glyph_shapes():
    """(shapes, changed): every glyph's 2D shape, and the indices that differ from upstream's."""
    shapes, changed = {}, set()
    for k, ch in enumerate(NEW):
        if ch == OLD[k] or ch in OLD:
            shapes[k] = upstream_glyph(OLD.index(ch))
        elif ch in ACCENTED:
            base, mark = ACCENTED[ch]
            shapes[k] = with_accent(upstream_glyph(OLD.index(base)), mark)
        elif ch == IG:
            shapes[k] = instagram()
        elif ch == FB:
            shapes[k] = facebook()
        elif ch == TT:
            shapes[k] = tiktok()
        wide = shapes[k]
        shapes[k] = narrow(wide)
        if ch != OLD[k] or shapes[k] is not wide:
            changed.add(k)
    return shapes, changed


def extrude(geom, z):
    """Manifold slab of a 2D shape between z[0] and z[1], or None if empty."""
    # close hairline gaps between upstream's triangles, then open pinch points where two strokes
    # touch at a corner (they would leave an edge shared by four faces)
    geom = geom.buffer(0.01, join_style="mitre").buffer(-0.01, join_style="mitre")
    geom = geom.buffer(-0.02, join_style="mitre").buffer(0.02, join_style="mitre").simplify(0.002)
    polys = [g for g in getattr(geom, "geoms", [geom]) if g.area > 0.01]
    rings = [list(r.coords)[:-1] for p in polys for r in [p.exterior, *p.interiors]]
    if not rings:
        return None
    slab = mf.CrossSection(rings, mf.FillRule.EvenOdd).extrude(z[1] - z[0]).translate([0, 0, z[0]])
    return slab


def to_trimesh(m):
    out = m.to_mesh()
    return trimesh.Trimesh(out.vert_properties[:, :3], out.tri_verts)


def to_manifold(t):
    return mf.Manifold(mf.Mesh(vert_properties=np.asarray(t.vertices, np.float32),
                               tri_verts=np.asarray(t.faces, np.uint32)))


def flap(n, shapes, blank):
    """Body and letter meshes of flap n in the odd upstream frame."""
    top, bot = shapes[(n - 2) % N], shapes[(n - 1) % N]
    # 0.05 mm short of the hinge edge, so no corner of a letter touches it at a single point
    front = top.intersection(box(-21, 0.05, 21, EDGE))
    back = affinity.scale(bot.intersection(box(-21, -EDGE, 21, -0.05)), 1, -1, origin=(0, 0))
    letters = [m for m in (extrude(front, FRONT_Z), extrude(back, BACK_Z)) if m is not None]
    b = to_manifold(blank)
    for m in letters:
        b = b - m
    body = to_trimesh(b)
    letters = [to_trimesh(m) for m in letters]
    return body, letters


def changed_flaps(changed):
    return [n for n in range(1, N + 1) if (n - 2) % N in changed or (n - 1) % N in changed]


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    OUT.mkdir(exist_ok=True)
    for old in OUT.glob("Flap*_pt*.stl"):
        old.unlink()
    blank = trimesh.load(STL / "Flap_blank_do_not_print.stl")
    shapes, changed = glyph_shapes()
    plate, new = [], {}
    for i, n in enumerate(changed_flaps(changed)):
        body, letters = flap(n, shapes, blank)
        body.export(OUT / f"Flap{n}_pt.stl")
        parts = [(f"Flap{n}_pt", 1, body)]
        for j, m in enumerate(letters, 1):
            m.export(OUT / f"Flap{n}_pt_{j}.stl")
            parts.append((f"Flap{n}_pt_{j}", 2, m))
        # centre on the flap, 4 x 4 grid around the middle of a 256 mm bed
        c = blank.bounds.mean(axis=0)
        x, y = 128 + (i % 4 - 1.5) * 49, 128 + (1.5 - i // 4) * 41
        new[n] = [(name, ext, [tuple(f"{v:.5f}" for v in p) for p in m.vertices - c],
                   [tuple(int(i) for i in f) for f in m.faces]) for name, ext, m in parts]
        plate.append((f"Flap{n}_pt", f"1 0 0 0 1 0 0 0 1 {x} {y} {c[2]:g}", new[n]))
        top, bot = NEW[(n - 2) % N], NEW[(n - 1) % N]
        print(f"Flap {n}: front = top of {top!r}, back = bottom of {bot!r}")
    write_bambu(plate, OUT / "Flaps_PT_orca.3mf", "Flaps PT")
    print(f"{len(plate)} flaps -> {OUT}")
    # the whole set as five Orca plates, each new flap where upstream's was
    for src in PLATES:
        flaps = []
        for name, transform, parts in read_prusa(STL / src):
            n = int(re.match(r"Flap(\d+)", name).group(1))
            flaps.append((f"Flap{n}_pt", transform, new[n]) if n in new else (name, transform, parts))
        dst = OUT / f"{Path(src).stem.removesuffix('_no_Umlaut')}_pt_orca.3mf"
        write_bambu(flaps, dst, dst.stem)
        print(f"{dst.name}: {sum(1 for f in flaps if f[0].endswith('_pt'))} new of {len(flaps)}")


if __name__ == "__main__":
    main()
