"""Convert the upstream PrusaSlicer flap 3MFs into Bambu Studio / OrcaSlicer projects.

The upstream files keep each flap as one object with 2-3 volumes: the black flap
(extruder 1) and the white letter halves inlaid 0.4 mm into its top and bottom
(extruder 2). OrcaSlicer only reads their geometry and drops the extruder
assignments. This rewrites each file in the Bambu 3MF layout, where every flap is
an object with parts, and the parts already assigned: flap = filament 1, letter =
filament 2.

    python tools/flaps_to_orca.py

Writes hardware/stl/Flaps Orca/<name>_orca.3mf. Standard library only.
"""
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

STL = Path(__file__).resolve().parent.parent / "hardware" / "stl"
OUT = STL / "Flaps Orca"
NS = {"m": "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"}
IDENTITY = "1 0 0 0 1 0 0 0 1 0 0 0"

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
 <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
 <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
</Types>
"""
RELS = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>
"""
MODEL_HEAD = """<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" \
xmlns:BambuStudio="http://schemas.bambulab.com/package/2021" \
xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p">
 <metadata name="BambuStudio:3mfVersion">1</metadata>
"""


def read_prusa(path):
    """Yield (name, item transform, [(part name, extruder, vertices, triangles)])."""
    with zipfile.ZipFile(path) as z:
        model = ET.fromstring(z.read("3D/3dmodel.model"))
        config = ET.fromstring(z.read("Metadata/Slic3r_PE_model.config"))
    meshes = {o.get("id"): o.find("m:mesh", NS) for o in model.find("m:resources", NS)}
    items = {i.get("objectid"): i.get("transform") for i in model.find("m:build", NS)}
    for obj in config.findall("object"):
        oid = obj.get("id")
        name = obj.find("metadata[@key='name']").get("value").removesuffix(".stl")
        mesh = meshes[oid]
        verts = [(v.get("x"), v.get("y"), v.get("z")) for v in mesh.find("m:vertices", NS)]
        tris = [(int(t.get("v1")), int(t.get("v2")), int(t.get("v3"))) for t in mesh.find("m:triangles", NS)]
        parts = []
        for vol in obj.findall("volume"):
            first, last = int(vol.get("firstid")), int(vol.get("lastid"))
            meta = {m.get("key"): m.get("value") for m in vol.findall("metadata")}
            sub = tris[first:last + 1]
            used = sorted({i for t in sub for i in t})
            remap = {old: new for new, old in enumerate(used)}
            parts.append((meta["name"].removesuffix(".stl_1").replace(".stl", ""),
                          int(meta.get("extruder", "1")),
                          [verts[i] for i in used],
                          [tuple(remap[i] for i in t) for t in sub]))
        yield name, items[oid], parts


def uuid(kind, n):
    return f"{kind:04x}{n:04x}-61cb-4c03-9d28-80fed5dfa1dc"


def mesh_xml(oid, verts, tris):
    v = "".join(f'<vertex x="{x}" y="{y}" z="{z}"/>' for x, y, z in verts)
    t = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in tris)
    return (f'  <object id="{oid}" p:UUID="{uuid(1, oid)}" type="model"><mesh><vertices>{v}</vertices>'
            f'<triangles>{t}</triangles></mesh></object>\n')


def write_bambu(flaps, dst, title):
    """Same package layout Bambu Studio writes: one 3D/Objects/object_<n>.model per flap."""
    files, res, build, cfg, plate = {}, [], [], [], []
    pid = 0
    for n, (name, transform, parts) in enumerate(flaps, start=1):
        oid = 1000 + n
        meshes, comps, part_cfg = [], [], []
        for pname, ext, verts, tris in parts:
            pid += 1
            meshes.append(mesh_xml(pid, verts, tris))
            comps.append(f'<component p:path="/3D/Objects/object_{n}.model" objectid="{pid}" '
                         f'p:UUID="{uuid(2, pid)}" transform="{IDENTITY}"/>')
            part_cfg.append(
                f'    <part id="{pid}" subtype="normal_part">\n'
                f'      <metadata key="name" value="{escape(pname)}"/>\n'
                f'      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n'
                f'      <metadata key="extruder" value="{ext}"/>\n'
                f'    </part>\n')
        files[f"3D/Objects/object_{n}.model"] = (
            MODEL_HEAD + f' <resources>\n{"".join(meshes)} </resources>\n <build/>\n</model>\n')
        res.append(f'  <object id="{oid}" p:UUID="{uuid(3, n)}" type="model">'
                   f'<components>{"".join(comps)}</components></object>\n')
        build.append(f'  <item objectid="{oid}" p:UUID="{uuid(4, n)}" transform="{transform}" printable="1"/>\n')
        cfg.append(f'  <object id="{oid}">\n    <metadata key="name" value="{escape(name)}"/>\n'
                   f'    <metadata key="extruder" value="1"/>\n{"".join(part_cfg)}  </object>\n')
        plate.append(f'    <model_instance>\n      <metadata key="object_id" value="{oid}"/>\n'
                     f'      <metadata key="instance_id" value="0"/>\n'
                     f'      <metadata key="identify_id" value="{100 + n}"/>\n    </model_instance>\n')
    files["3D/3dmodel.model"] = (
        MODEL_HEAD + ' <metadata name="Application">BambuStudio-02.08.02.61</metadata>\n'
        f' <metadata name="Title">{escape(title)}</metadata>\n'
        f' <resources>\n{"".join(res)} </resources>\n'
        f' <build p:UUID="{uuid(5, 0)}">\n{"".join(build)} </build>\n</model>\n')
    files["3D/_rels/3dmodel.model.rels"] = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
        + "".join(f' <Relationship Target="/3D/Objects/object_{n}.model" Id="rel-{n}" '
                  'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n'
                  for n in range(1, len(flaps) + 1))
        + '</Relationships>\n')
    files["Metadata/model_settings.config"] = (
        '<?xml version="1.0" encoding="UTF-8"?>\n<config>\n' + "".join(cfg)
        + '  <plate>\n    <metadata key="plater_id" value="1"/>\n    <metadata key="plater_name" value=""/>\n'
        '    <metadata key="locked" value="false"/>\n' + "".join(plate)
        + '  </plate>\n  <assemble>\n  </assemble>\n</config>\n')
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        z.writestr("_rels/.rels", RELS)
        for name, text in files.items():
            z.writestr(name, text)


def main():
    OUT.mkdir(exist_ok=True)
    for src in sorted(p for p in STL.glob("Flap*/*.3mf") if p.parent != OUT):
        flaps = list(read_prusa(src))
        dst = OUT / f"{src.stem}_orca.3mf"
        write_bambu(flaps, dst, src.stem)
        letters = sum(1 for _, _, parts in flaps for p in parts if p[1] == 2)
        print(f"{dst.name}: {len(flaps)} flaps, {letters} letter parts on filament 2")


if __name__ == "__main__":
    main()
