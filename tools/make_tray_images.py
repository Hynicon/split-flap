"""Pictures of the electronics tray for the build guide, in the style of upstream's part renders.

Renders, from hardware/stl (after tools/make_electronics_mount.py):
    hardware/guide/img/part-bc-left-mount.png   the back cover's Left piece with the 4 bosses
    hardware/guide/img/part-tray.png            the tray on its own
    hardware/guide/img/tray-in-cover.png        the tray screwed in, with the three boards

    pip install trimesh numpy pillow rtree embreex
    python tools/make_tray_images.py

It is a small ray-cast renderer (orthographic, flat shading, dark outlines) so it needs no GPU.
"""
from pathlib import Path

import numpy as np
import trimesh
from PIL import Image

import make_electronics_mount as em

OUT = em.STL.parent / "guide" / "img"
GREY = (0.86, 0.86, 0.86)
AMBER = (0.93, 0.70, 0.16)
PCB = (0.16, 0.42, 0.26)
BLUE_PCB = (0.15, 0.30, 0.62)
METAL = (0.78, 0.79, 0.80)
BLACK = (0.12, 0.12, 0.13)


def render(parts, view, width=1100, focus=None, light=(-0.45, 0.35, 0.82)):
    """parts: [(trimesh, rgb)]. view: direction the camera looks along. Returns a PIL image."""
    scene = trimesh.util.concatenate([m for m, _ in parts])
    colors = np.concatenate([np.tile(c, (len(m.faces), 1)) for m, c in parts])
    d = np.asarray(view, float)
    d /= np.linalg.norm(d)
    up = np.array([0.0, 1.0, 0.0])
    u = np.cross(d, up)   # screen right
    u /= np.linalg.norm(u)
    v = np.cross(u, d)    # screen up
    box = focus if focus is not None else scene.bounds
    corners = np.array([[x, y, z] for x in box[:, 0] for y in box[:, 1] for z in box[:, 2]])
    pu, pv = corners @ u, corners @ v
    pad = 0.04 * max(np.ptp(pu), np.ptp(pv))
    u0, u1, v0, v1 = pu.min() - pad, pu.max() + pad, pv.min() - pad, pv.max() + pad
    ss = 2  # supersampling
    w = width * ss
    h = int(round(w * (v1 - v0) / (u1 - u0)))
    U, V = np.meshgrid(np.linspace(u0, u1, w), np.linspace(v1, v0, h))
    far = np.abs(scene.bounds).max() * 4
    origins = U.ravel()[:, None] * u + V.ravel()[:, None] * v - d * far
    dirs = np.tile(d, (len(origins), 1))
    tri, ray, loc = scene.ray.intersects_id(origins, dirs, multiple_hits=False, return_locations=True)
    n = scene.face_normals[tri]
    n = np.where((n @ d)[:, None] > 0, -n, n)  # facing the camera
    l = np.asarray(light, float)
    l /= np.linalg.norm(l)
    shade = 0.42 + 0.48 * np.clip(n @ l, 0, 1) + 0.10 * np.clip(-(n @ d), 0, 1)
    rgb = np.ones((h * w, 3))
    rgb[ray] = colors[tri] * shade[:, None]
    depth = np.full(h * w, np.nan)
    depth[ray] = (loc - origins[ray]) @ d
    normals = np.zeros((h * w, 3))
    normals[ray] = n
    rgb, depth, normals = rgb.reshape(h, w, 3), depth.reshape(h, w), normals.reshape(h, w, 3)
    # outlines: where depth jumps or the surface turns sharply
    edge = np.zeros((h, w), bool)
    step = np.ptp(pu) / w
    for axis in (0, 1):
        dd = np.abs(np.diff(depth, axis=axis))
        nn = (np.diff(normals, axis=axis) ** 2).sum(-1)
        hit = ~np.isnan(depth)
        silhouette = np.diff(hit.astype(np.int8), axis=axis) != 0  # part on one side, background on the other
        e = (np.nan_to_num(dd) > 12 * step) | (nn > 0.35) | silhouette
        if axis == 0:
            edge[1:] |= e
        else:
            edge[:, 1:] |= e
    rgb[edge] *= 0.35
    img = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8))
    return img.resize((width, h // ss), Image.LANCZOS)


def box(x0, x1, y0, y1, z0, z1):
    return trimesh.creation.box(bounds=[[x0, y0, z0], [x1, y1, z1]])


def boards(x0, y0, z):
    """Simple stand-ins for the three boards, sitting in their holders (tray placed at x0, y0, z)."""
    zb = z + em.PLATE + em.LIFT
    parts = []
    # ESP32: holder at (34, 12), USB end at +y; the WROOM module at the antenna end (-y)
    ex, ey = x0 + 34, y0 + 12
    e = em.ESP32
    parts.append((box(ex, ex + e["x"], ey, ey + e["y"], zb, zb + e["pcb"]), PCB))
    t = zb + e["pcb"]
    parts.append((box(ex + 5, ex + e["x"] - 5, ey + 1, ey + 26, t, t + 3.2), METAL))
    parts.append((box(ex + e["x"] / 2 - 4, ex + e["x"] / 2 + 4, ey + e["y"] - 5, ey + e["y"] + 0.8, t, t + 3.0), METAL))
    for side in (0, 1):  # header rows under the board
        hx = ex + 1.27 + side * (e["x"] - 2.54) - 1.27
        parts.append((box(hx, hx + 2.54, ey + 7, ey + 7 + 38.1, zb - 2.5, zb), BLACK))
    # MP1584 at (9, 12): inductor and chip on top
    bx, by = x0 + 9, y0 + 12
    b = em.BUCK
    parts.append((box(bx, bx + b["x"], by, by + b["y"], zb, zb + b["pcb"]), BLUE_PCB))
    t = zb + b["pcb"]
    parts.append((box(bx + 4, bx + 11, by + 8, by + 15, t, t + 3.5), BLACK))
    # level shifter at (11, 44): pin rows along its 16 mm edges (y = 44 and 60)
    sx, sy = x0 + 11, y0 + 44
    s = em.SHIFTER
    parts.append((box(sx, sx + s["x"], sy, sy + s["y"], zb, zb + s["pcb"]), BLUE_PCB))
    return parts


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    tray = trimesh.load(em.STL / "ElectronicsTray" / "ElectronicsTray.stl")
    cover = trimesh.load(em.STL / "BackCover" / "BackCoverLeft_mount.stl")
    view = (0.18, 0.42, -1.0)  # from above the open side, a little towards the bottom wall

    render([(cover, GREY)], view).save(OUT / "part-bc-left-mount.png")
    render([(tray, AMBER)], (0.35, 0.55, -1.0), width=800).save(OUT / "part-tray.png")

    x0, y0 = em.END_WALL_X + em.TRAY_DX, em.BOTTOM_WALL_Y + em.TRAY_DY
    z = em.FLOOR_Z + em.BOSS_H
    placed = tray.copy()
    placed.apply_translation([x0, y0, z])
    parts = [(cover, GREY), (placed, AMBER)] + boards(x0, y0, z)
    focus = np.array([[cover.bounds[0][0], cover.bounds[0][1], 0], [em.END_WALL_X + 120, cover.bounds[1][1], em.RIM_Z]])
    render(parts, view, width=1000, focus=focus).save(OUT / "tray-in-cover.png")
    for name in ("part-bc-left-mount.png", "part-tray.png", "tray-in-cover.png"):
        im = Image.open(OUT / name)
        print(f"{name}: {im.size[0]}x{im.size[1]}")


if __name__ == "__main__":
    main()
