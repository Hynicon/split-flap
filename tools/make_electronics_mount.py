"""Electronics tray for the back cover: holds the ESP32, the buck converter and the level shifter.

None of these boards has screw holes, so a printed tray grips each one by its edges with snap
clips, and the tray is screwed with 4 × M3×8 into heat-set inserts in 4 new bosses on the floor of
the back cover's left piece (the one with the switch and jack holes in its end). The inserts are
the same M3 inserts the case uses (4.0 mm hole).

    pip install trimesh numpy manifold3d
    python tools/make_electronics_mount.py

Writes, next to the existing case files (hardware/stl, git-ignored):
    ElectronicsTray/ElectronicsTray.stl                 print flat, as written
    BackCover/BackCoverLeft_mount.stl                   10 units
    Case <n>-unit/BackCoverLeft_<n>unit_mount.stl       9, 8 and 7 units
    ElectronicsTray/preview_*.png                       top and side views, to check the fit

Board sizes are at the top: measure yours with calipers and change them if they differ. Positions
are measured from the inside of the back cover's end wall (dx) and bottom wall (dy), which are the
same in every case length.
"""
from pathlib import Path

import manifold3d as mf
import numpy as np
import trimesh

STL = Path(__file__).resolve().parent.parent / "hardware" / "stl"

# ---- boards (mm). length x width as seen from above; "held" edges are the ones the clips grip.
ESP32 = dict(x=28.2, y=51.8, pcb=1.6)   # DOIT ESP32 DevKit V1, 30 pins, pins pointing down; USB end at +y
BUCK = dict(x=17.0, y=22.0, pcb=1.2)    # MP1584 module; solder pads at the four corners
SHIFTER = dict(x=13.5, y=16.0, pcb=1.6)  # 4-channel BSS138 module; pin rows along its 16 mm edges

# ---- back cover geometry (left piece, in its STL frame; same for every case length)
END_WALL_X = 14.55   # inside face of the end wall with the switch and jack
BOTTOM_WALL_Y = 38.4  # inside face of the bottom wall
FLOOR_Z = 3.0         # top of the floor; the rim is at 23.0
RIM_Z = 23.0

# ---- tray
TRAY_DX, TRAY_DY = 24.0, 12.0   # tray corner, from the end wall and bottom wall
TRAY = (66.0, 76.0)             # size; narrow enough to stay clear of the 7-unit piece's joint notch
PLATE = 2.0                     # plate thickness
BOSS_H = 7.5                    # boss height above the floor: room for wires under the tray
BOSS_D, INSERT_D, INSERT_DEPTH = 8.0, 4.0, 6.5
SCREW_D = 3.4
BOSSES = [(5, 5), (48, 5), (5, 71), (48, 71)]  # tray coordinates: left column, and below/above the ESP32

LIFT = 1.5      # boards sit this high on ledges, so solder joints and pins clear the plate
CLEAR = 0.3     # gap around each board
WALL = 1.0      # clip thickness
SLOT = 1.2      # gap behind a flexing clip
LIP_FIXED, LIP_FLEX = 1.0, 0.6  # how far the lips reach over the board
LEDGE = 2.5     # how far the ledges reach under the board

SEG = 48


def box(x0, x1, y0, y1, z0, z1):
    return mf.Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cyl(x, y, z0, z1, d):
    return mf.Manifold.cylinder(z1 - z0, d / 2, d / 2, SEG).translate([x, y, z0])


def union(parts):
    out = None
    for p in parts:
        out = p if out is None else out + p
    return out


def holder(x0, y0, board, held="x", fixed_side="min", fixed_span=None, flex_span=8.0, fixed_corners=False):
    """Clips for one board whose lower-left corner is at (x0, y0) on the plate.

    held: the axis whose two edges are gripped ("x": the edges at x0 and x0 + board x).
    The fixed side has a rigid wall with a lip; the other side has a flexing bar that is cut free
    from the plate except at its ends, with a smaller lip in its middle that snaps over the board.
    Returns (solids to add, solids to cut)."""
    bx, by, pcb = board["x"], board["y"], board["pcb"]
    add, cut = [], []
    lip_z0 = PLATE + LIFT + pcb + 0.2     # underside of the lips
    top = lip_z0 + 1.0
    # a window under the board for pins and solder joints, and ledges along the held edges
    cut.append(box(x0 + 0.01, x0 + bx - 0.01, y0 + 0.01, y0 + by - 0.01, -1, PLATE + 1))
    a0, a1 = (x0, x0 + bx) if held == "x" else (y0, y0 + by)      # held axis
    b0, b1 = (y0, y0 + by) if held == "x" else (x0, x0 + bx)      # along the edge

    def rect(p0, p1, q0, q1, z0, z1):  # p along the held axis, q along the edge
        return box(p0, p1, q0, q1, z0, z1) if held == "x" else box(q0, q1, p0, p1, z0, z1)

    for side in ("min", "max"):
        edge = a0 if side == "min" else a1
        inward = 1 if side == "min" else -1
        outer = edge - inward * CLEAR          # the clip's inner face
        if side == fixed_side:
            spans = []
            if fixed_corners:  # two short hooks at the corners, leaving the middle open (USB plug)
                spans = [(b0 - CLEAR, b0 + 5), (b1 - 5, b1 + CLEAR)]
            else:
                span = fixed_span or (b1 - b0)
                mid = (b0 + b1) / 2
                spans = [(mid - span / 2, mid + span / 2)]
            for q0, q1 in spans:
                w0, w1 = sorted((outer, outer - inward * WALL))
                add.append(rect(w0, w1, q0, q1, 0, top))
                l0, l1 = sorted((outer, outer + inward * LIP_FIXED))
                add.append(rect(l0, l1, q0, q1, lip_z0, top))
            # ledge under the board along this edge
            g0, g1 = sorted((edge - inward * CLEAR, edge + inward * LEDGE))
            add.append(rect(g0, g1, b0, b1, 0, PLATE + LIFT))
        else:
            # flexing bar: cut free from the plate along its length; its ends stay joined
            q0, q1 = b0 - 2, b1 + 2
            w0, w1 = sorted((outer, outer - inward * WALL))
            s0, s1 = sorted((outer - inward * WALL, outer - inward * (WALL + SLOT)))
            cut.append(rect(s0, s1, q0 + 1.5, q1 - 1.5, -1, PLATE + 1))          # slot behind the bar
            gap0, gap1 = sorted((outer, outer + inward * 0.6))
            cut.append(rect(gap0, gap1, q0 + 1.5, q1 - 1.5, -1, PLATE + 1))      # slot in front of it
            add.append(rect(w0, w1, q0, q1, 0, top))
            mid = (b0 + b1) / 2
            # lip with a sloped top, so pressing the board down pushes the bar out of the way
            # (stepped in 0.25 mm layers: full reach at the bottom, shrinking towards the top)
            steps = 4
            for i in range(steps):
                reach = LIP_FLEX * (steps - i) / steps
                l0, l1 = sorted((outer, outer + inward * reach))
                z0 = lip_z0 + (top - lip_z0) * i / steps
                z1 = lip_z0 + (top - lip_z0) * (i + 1) / steps
                add.append(rect(l0, l1, mid - flex_span / 2, mid + flex_span / 2, z0, z1))
            # ledge under the board, kept apart from the bar
            g0, g1 = sorted((edge + inward * 0.4, edge + inward * LEDGE))
            add.append(rect(g0, g1, b0, b1, 0, PLATE + LIFT))
    return add, cut


def tray():
    plate = box(0, TRAY[0], 0, TRAY[1], 0, PLATE)
    adds, cuts = [], []
    for x0, y0, board, kw in [
        (9.0, 12.0, BUCK, dict(held="x", fixed_side="min", fixed_span=10.0, flex_span=8.0)),
        (11.0, 44.0, SHIFTER, dict(held="x", fixed_side="min", flex_span=6.0)),
        (34.0, 12.0, ESP32, dict(held="y", fixed_side="max", fixed_corners=True, flex_span=10.0)),
    ]:
        a, c = holder(x0, y0, board, **kw)
        adds += a
        cuts += c
    body = (plate - union(cuts)) + union(adds)
    for bx, by in BOSSES:
        body = body - cyl(bx, by, -1, 20, SCREW_D)
    return body


def bosses_for(cover_x0, cover_y0):
    """Bosses with insert holes, in the back cover's frame."""
    parts, holes = [], []
    for bx, by in BOSSES:
        x, y = cover_x0 + bx, cover_y0 + by
        parts.append(cyl(x, y, FLOOR_Z - 0.5, FLOOR_Z + BOSS_H, BOSS_D))
        holes.append(cyl(x, y, FLOOR_Z + BOSS_H - INSERT_DEPTH, FLOOR_Z + BOSS_H + 1, INSERT_D))
    return union(parts), union(holes)


def to_manifold(t):
    return mf.Manifold(mf.Mesh(vert_properties=np.asarray(t.vertices, np.float32),
                               tri_verts=np.asarray(t.faces, np.uint32)))


def to_trimesh(m):
    g = m.to_mesh()
    return trimesh.Trimesh(g.vert_properties[:, :3], g.tri_verts)


def preview(cover, tray_placed, path):
    """Top view of the cover's floor with the tray, coloured by height."""
    from PIL import Image
    scene = cover + tray_placed
    t = to_trimesh(scene)
    res = 0.25
    b = t.bounds
    xs = np.arange(b[0][0], min(b[1][0], END_WALL_X + 140), res)
    ys = np.arange(b[0][1], b[1][1], res)
    X, Y = np.meshgrid(xs, ys)
    orig = np.stack([X.ravel(), Y.ravel(), np.full(X.size, 40.0)], 1)
    d = np.tile([0, 0, -1.0], (X.size, 1))
    loc, idx, _ = t.ray.intersects_location(orig, d, multiple_hits=False)
    h = np.full(X.size, np.nan)
    h[idx] = loc[:, 2]
    h = h.reshape(X.shape)
    img = np.where(np.isnan(h), 255, 40 + np.clip(h / RIM_Z, 0, 1) * 200).astype(np.uint8)
    Image.fromarray(img[::-1]).resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(path)


def main():
    out = STL / "ElectronicsTray"
    out.mkdir(parents=True, exist_ok=True)
    t = tray()
    to_trimesh(t).export(out / "ElectronicsTray.stl")
    x0, y0 = END_WALL_X + TRAY_DX, BOTTOM_WALL_Y + TRAY_DY
    placed = t.translate([x0, y0, FLOOR_Z + BOSS_H])
    top = to_trimesh(t).bounds[1][2] + FLOOR_Z + BOSS_H
    print(f"tray {TRAY[0]:.0f} x {TRAY[1]:.0f} mm; highest point {top:.1f} mm (rim at {RIM_Z})")
    assert top < RIM_Z - 1.0, "the boards would touch the middle frame"

    covers = [(STL / "BackCover" / "BackCoverLeft.stl", STL / "BackCover" / "BackCoverLeft_mount.stl")]
    for n in (9, 8, 7):
        src = STL / f"Case {n}-unit" / f"BackCoverLeft_{n}unit.stl"
        covers.append((src, src.with_name(src.stem + "_mount.stl")))
    for src, dst in covers:
        if not src.exists():
            print(f"skipped {src.relative_to(STL)} (run tools/make_case.py first)")
            continue
        src_mesh = trimesh.load(src)
        # every boss must stand on the floor (the 7-unit piece has a notch where it joins the centre)
        for bx, by in BOSSES:
            for ox, oy in [(0, 0), (BOSS_D / 2, 0), (-BOSS_D / 2, 0), (0, BOSS_D / 2), (0, -BOSS_D / 2)]:
                hit = src_mesh.ray.intersects_location([[x0 + bx + ox, y0 + by + oy, RIM_Z + 5]], [[0, 0, -1]])[0]
                assert len(hit) and hit[:, 2].max() > FLOOR_Z - 0.2, f"boss at {bx},{by} is not on the floor of {src.name}"
        m = to_manifold(src_mesh)
        add, holes = bosses_for(x0, y0)
        cover = (m + add) - holes
        mesh = to_trimesh(cover)
        mesh = max(mesh.split(only_watertight=False), key=lambda p: p.volume)
        mesh.export(dst)
        print(f"{dst.relative_to(STL)}: watertight={mesh.is_watertight}")
        if "9unit" in dst.name:
            preview(cover, placed, out / "preview_9unit.png")
        if "7unit" in dst.name:
            preview(cover, placed, out / "preview_7unit.png")


if __name__ == "__main__":
    main()
