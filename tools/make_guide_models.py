"""Export the case assemblies as the build guide's 3D models.

Reads hardware/stl/Assembly_<n>unit.step for 7, 8 and 9 units (from
tools/make_assembly.py) and the upstream 10-unit file, and writes
hardware/guide/models/split-flap-<n>.json: glTF with the mesh data embedded,
so the guide needs no other files. The 10-unit assembly goes through
make_assembly.build() with no cuts, so its case layers are split into
Left/Center/Right pieces like the other sizes.

    pip install build123d
    python tools/make_assembly.py
    python tools/make_guide_models.py
"""
import base64
import json
import struct
import tempfile
from pathlib import Path

from build123d import export_gltf, import_step

from make_assembly import SRC, STL, build

OUT = STL.parent / "guide" / "models"


def glb_to_json(glb, dst):
    """Rewrite a binary .glb as .gltf JSON with its buffer inlined as base64."""
    data = glb.read_bytes()
    off, doc, blob = 12, None, b""
    while off < len(data):
        length, kind = struct.unpack_from("<II", data, off)
        chunk = data[off + 8:off + 8 + length]
        off += 8 + length
        if kind == 0x4E4F534A:  # JSON
            doc = json.loads(chunk)
        elif kind == 0x004E4942:  # BIN
            blob = chunk
    doc["buffers"] = [{"byteLength": len(blob),
                       "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode()}]
    dst.write_text(json.dumps(doc, separators=(",", ":")))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for n in (7, 8, 9, 10):
            asm = build(import_step(SRC), []) if n == 10 else import_step(STL / f"Assembly_{n}unit.step")
            glb = Path(tmp) / f"{n}.glb"
            export_gltf(asm, str(glb), binary=True, linear_deflection=0.15, angular_deflection=0.4)
            dst = OUT / f"split-flap-{n}.json"
            glb_to_json(glb, dst)
            print(f"{dst.name}: {dst.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
