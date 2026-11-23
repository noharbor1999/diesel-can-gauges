"""Export can_case_v01: geometry-only 3MF (base + lid as two objects, print orientation, 256 plate),
STEP per part (assembled frame, for CAD) and STL per part (print orientation)."""
import os, sys, json, zipfile
import numpy as np, trimesh, cadquery as cq
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import can_case_v01 as C
EXP = os.path.join(HERE, "..", "exports"); P = lambda n: os.path.join(EXP, n)
PLATE = 256.0
parts = {"base": C.base(), "lid": C.lid()}
info = {"parts": {}}
meshes = {}
for k, s in parts.items():
    assert s.val().isValid() and len(s.solids().vals()) == 1, k
    cq.exporters.export(s, P(f"can_case_v01_{k}.step"))
    tmp = P(f"_tmp_{k}.stl"); cq.exporters.export(s, tmp, tolerance=0.02, angularTolerance=0.1)
    m = trimesh.load(tmp); os.remove(tmp)
    if k == "lid":   # outer face (z = 20) down: flip 180 deg about X
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))
    lo = m.bounds[0]; m.apply_translation(-lo)          # on the bed, corner at origin
    m.export(P(f"can_case_v01_{k}.stl"))
    meshes[k] = m
    ext = np.ptp(m.vertices, axis=0)
    info["parts"][k] = {"extent_mm": [round(float(e), 3) for e in ext], "watertight": bool(m.is_watertight),
                        "winding_ok": bool(m.is_winding_consistent), "volume_cm3": round(float(m.volume) / 1000, 3),
                        "brep_valid": bool(s.val().isValid()), "solids": len(s.solids().vals()), "faces": int(len(m.faces))}
# print-orientation sanity: base floor at z=0 has the big flat face; lid outer face (no counterbore rings) on the bed
for k, m in meshes.items():
    n, a = m.face_normals, m.area_faces
    bed = (m.triangles_center[:, 2] < 1e-3) & (n[:, 2] < -0.99)
    info["parts"][k]["bed_contact_mm2"] = round(float(a[bed].sum()), 1)
# layout: side by side, centred on the plate, 10 mm apart
gap = 10.0
wb, wl = meshes["base"].extents[0], meshes["lid"].extents[0]
x0 = (PLATE - (wb + gap + wl)) / 2
place = {"base": (x0, (PLATE - meshes["base"].extents[1]) / 2), "lid": (x0 + wb + gap, (PLATE - meshes["lid"].extents[1]) / 2)}
def mx(oid, mesh, name):
    v = "".join(f'<vertex x="{a:.4f}" y="{b:.4f}" z="{c:.4f}"/>' for a, b, c in mesh.vertices)
    t = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in mesh.faces)
    return f'<object id="{oid}" name="{name}" type="model"><mesh><vertices>{v}</vertices><triangles>{t}</triangles></mesh></object>'
objs, items = "", ""
for i, k in enumerate(("base", "lid"), start=1):
    m = meshes[k].copy(); m.apply_translation([place[k][0], place[k][1], 0])
    objs += mx(i, m, f"can_case_v01_{k}"); items += f'<item objectid="{i}"/>'
    info["parts"][k]["plate_xy_min_max"] = [[round(float(v), 2) for v in m.bounds[0][:2]], [round(float(v), 2) for v in m.bounds[1][:2]]]
model = ('<?xml version="1.0" encoding="UTF-8"?><model unit="millimeter" xml:lang="en-US" '
         'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02"><metadata name="Title">can_case_v01</metadata>'
         f'<resources>{objs}</resources><build>{items}</build></model>')
with zipfile.ZipFile(P("can_case_v01.3mf"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
               '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
               '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    z.writestr("_rels/.rels", '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
               '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    z.writestr("3D/3dmodel.model", model)
names = zipfile.ZipFile(P("can_case_v01.3mf")).namelist()
back = trimesh.load(P("can_case_v01.3mf"))
geoms = back.geometry if isinstance(back, trimesh.Scene) else {"one": back}
info["3mf"] = {"entries": names, "has_settings": any(n not in ("[Content_Types].xml", "_rels/.rels", "3D/3dmodel.model") for n in names),
               "objects": len(geoms), "readback_watertight": {k: bool(g.is_watertight) for k, g in geoms.items()},
               "all_on_plate": all(0 <= info["parts"][k]["plate_xy_min_max"][0][j] and info["parts"][k]["plate_xy_min_max"][1][j] <= PLATE for k in meshes for j in (0, 1))}
# assembled size check (modeled frame)
bb = parts["base"].union(parts["lid"]).val().BoundingBox()
info["assembled_outer_mm"] = [round(bb.xlen, 3), round(bb.ylen, 3), round(bb.zlen, 3)]
info["assembled_matches_50x35x20"] = all(abs(a - b) < 0.01 for a, b in zip(info["assembled_outer_mm"], (50, 35, 20)))
json.dump(info, open(P("can_case_v01_export.json"), "w"), indent=1); print(json.dumps(info, indent=1))
