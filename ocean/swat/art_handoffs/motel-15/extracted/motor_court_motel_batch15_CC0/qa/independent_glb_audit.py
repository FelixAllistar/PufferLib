"""Independent core-glTF numerical/material audit. No game runtime validation."""
import hashlib,io,json,struct
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
DTYPES = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bounds_record(a):
    b = a.get("aabb_blender_z_up") or a.get("bounds_m_source_xyz") or a.get("source_bounds_z_up")
    return np.array([b["min"], b["max"]] if isinstance(b, dict) else b, dtype=float)


def source_to_gltf_bounds(bounds):
    lo, hi = bounds
    return np.array([[lo[0], lo[2], -hi[1]], [hi[0], hi[2], -lo[1]]])


def node_matrix(n):
    if "matrix" in n:
        return np.array(n["matrix"], dtype=float).reshape(4, 4).T
    x, y, z, w = n.get("rotation", [0, 0, 0, 1])
    out = np.eye(4)
    out[:3, :3] = np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)],
    ]) @ np.diag(n.get("scale", [1, 1, 1]))
    out[:3, 3] = n.get("translation", [0, 0, 0])
    return out


def inspect_glb(path, a, batch):
    raw = path.read_bytes()
    errors = []
    def check(ok, message):
        if not ok:
            errors.append(message)
    magic, version, length = struct.unpack_from("<III", raw)
    check(magic == 0x46546C67 and version == 2 and length == len(raw), "Invalid GLB header or length")
    chunks = {}
    cursor = 12
    while cursor < len(raw):
        size, kind = struct.unpack_from("<II", raw, cursor)
        check(cursor+8+size <= len(raw) and size % 4 == 0, "Invalid chunk length")
        chunks[kind] = raw[cursor+8:cursor+8+size]
        cursor += 8+size
    doc = json.loads(chunks[0x4E4F534A])
    binary = chunks.get(0x004E4942, b"")
    check(doc.get("asset", {}).get("version") == "2.0", "Not glTF 2.0")
    check(not doc.get("extensionsRequired"), "Required glTF extensions present")
    uris = [r["uri"] for key in ("buffers", "images") for r in doc.get(key, []) if "uri" in r]
    check(not uris, "External/data URI dependency present")
    check(len(doc.get("buffers", [])) == 1, "Expected a single embedded buffer")
    check(doc["buffers"][0]["byteLength"] <= len(binary), "Buffer exceeds embedded data")
    for view in doc.get("bufferViews", []):
        check(view.get("buffer", 0) == 0 and view.get("byteOffset", 0)+view["byteLength"] <= len(binary), "Invalid bufferView bounds")
    def accessor(i):
        ac = doc["accessors"][i]
        if "sparse" in ac:
            raise ValueError("Sparse accessor unsupported by this audit")
        view = doc["bufferViews"][ac["bufferView"]]
        dtype = np.dtype(DTYPES[ac["componentType"]])
        width = WIDTHS[ac["type"]]
        start = view.get("byteOffset", 0)+ac.get("byteOffset", 0)
        stride = view.get("byteStride", width*dtype.itemsize)
        end = start+(ac["count"]-1)*stride+width*dtype.itemsize
        if end > view.get("byteOffset", 0)+view["byteLength"]:
            raise ValueError("Accessor exceeds bufferView")
        return np.ndarray((ac["count"], width), dtype=dtype, buffer=binary, offset=start, strides=(stride, dtype.itemsize))
    for image in doc.get("images", []):
        check(image.get("mimeType") == "image/png" and "bufferView" in image, "Image is not embedded PNG")
        if "bufferView" not in image:
            continue
        view = doc["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        png = binary[start:start+view["byteLength"]]
        with Image.open(io.BytesIO(png)) as im:
            im.verify()
    mats = doc.get("materials", [])
    images = doc.get("images", [])
    textures = doc.get("textures", [])
    for tx in textures:
        check(0 <= tx.get("source", -1) < len(images), "Invalid texture source")
    for mat in mats:
        pbr = mat.get("pbrMetallicRoughness", {})
        check(not mat.get("extensions"), "Material extension present")
        for key in ("metallicFactor", "roughnessFactor"):
            value = pbr.get(key, 1)
            check(np.isfinite(value) and 0 <= value <= 1, "Invalid PBR scalar")
        col = pbr.get("baseColorFactor", [1, 1, 1, 1])
        check(np.isfinite(col).all() and all(0 <= c <= 1 for c in col), "Invalid base color factor")
        if "baseColorTexture" in pbr:
            check(0 <= pbr["baseColorTexture"]["index"] < len(textures), "Invalid base color texture index")
            check(col == [1, 1, 1, 1], "Textured material also has a tint factor")
        check(mat.get("alphaMode", "OPAQUE") == "OPAQUE", "Nonopaque material present")
    positions = []
    triangles = 0
    primitive_count = 0
    degenerate = 0
    node_ids = []
    node_extras = []
    nodes = doc.get("nodes", [])
    def visit(ni, parent, ancestors):
        nonlocal triangles, primitive_count, degenerate
        if ni in ancestors:
            raise ValueError("Cyclic node hierarchy")
        n = nodes[ni]
        transform = parent @ node_matrix(n)
        extras = n.get("extras", {})
        if "mesh" in n:
            node_ids.append(extras.get("asset_id"))
            node_extras.append(extras)
            check(extras.get("license") == "CC0-1.0", "Mesh node lacks CC0 license")
            check(extras.get("units") == "metres", "Mesh node lacks metre unit metadata")
            collision_text = str(extras.get("collision_recommendation", ""))
            check(extras.get("render_only") is True or "render_only_by_default" in collision_text, "Mesh node lacks render-only default")
            for prim in doc["meshes"][n["mesh"]]["primitives"]:
                primitive_count += 1
                check(prim.get("mode", 4) == 4, "Nontriangle primitive present")
                check(0 <= prim.get("material", -1) < len(mats), "Invalid primitive material")
                attr = prim["attributes"]
                check("NORMAL" in attr and "TEXCOORD_0" in attr, "Normals or UV0 missing")
                pts = accessor(attr["POSITION"]).astype(float)
                for key, ai in attr.items():
                    values = accessor(ai)
                    check(len(values) == len(pts) and np.isfinite(values).all(), "Invalid attribute values/count: "+key)
                inds = accessor(prim["indices"]).reshape(-1) if "indices" in prim else np.arange(len(pts))
                check(len(inds) % 3 == 0 and np.min(inds) >= 0 and np.max(inds) < len(pts), "Invalid triangle indices")
                tris = pts[inds.reshape(-1, 3)]
                area2 = np.linalg.norm(np.cross(tris[:, 1]-tris[:, 0], tris[:, 2]-tris[:, 0]), axis=1)
                degenerate += int((area2 < 1e-14).sum())
                triangles += len(inds)//3
                homogeneous = np.column_stack([pts, np.ones(len(pts))])
                positions.append((homogeneous @ transform.T)[:, :3])
        for child in n.get("children", []):
            visit(child, transform, ancestors | {ni})
    scene = doc["scenes"][doc.get("scene", 0)]
    check(scene.get("extras", {}).get("batch") == batch, "Inherited or missing scene batch provenance")
    for ni in scene.get("nodes", []):
        visit(ni, np.eye(4), set())
    pts = np.concatenate(positions)
    bounds = np.array([pts.min(axis=0), pts.max(axis=0)])
    expected = source_to_gltf_bounds(bounds_record(a))
    max_error = float(np.max(np.abs(bounds-expected)))
    check(max_error <= 1e-4, "Exported bounds differ from converted source bounds")
    explicit = a.get("aabb_gltf_y_up") or a.get("optional_coarse_aabb_y_up")
    if explicit:
        check(np.max(np.abs(bounds-np.array([explicit["min"], explicit["max"]]))) <= 1e-4, "Explicit glTF AABB mismatch")
    aid = a.get("asset_id", a.get("id"))
    check(node_ids == [aid], "Manifest and node asset IDs disagree")
    check(triangles == a.get("triangles_exported", a.get("triangles")), "Manifest triangle count mismatch")
    check(len(mats) == a.get("material_count"), "Manifest material count mismatch")
    check(len(raw) == a.get("bytes", a.get("file_bytes")), "Manifest byte size mismatch")
    digest = hashlib.sha256(raw).hexdigest()
    if "sha256" in a:
        check(digest == a["sha256"], "Manifest SHA-256 mismatch")
    check(not doc.get("animations") and not doc.get("skins"), "Animation or skin data present")
    check(not doc.get("cameras"), "Presentation camera in asset export")
    check(degenerate == 0, "Degenerate triangles detected")
    mounting=a.get("mounting","floor")
    return {
        "asset_id": aid, "library_id": batch+"/"+aid,
        "batch_id": batch, "file": path.relative_to(ROOT).as_posix(),
        "source_manifest": batch+"/manifest.json", "source_manifest_asset_id": aid,
        "label": a.get("title", a.get("label", a.get("description", aid))),
        "license": "CC0-1.0", "units": "metres", "mounting": mounting,
        "bounds_gltf_y_up_m": {"min": bounds[0].tolist(), "max": bounds[1].tolist()},
        "dimensions_gltf_xyz_m": (bounds[1]-bounds[0]).tolist(),
        "triangles": triangles, "material_count": len(mats), "primitive_count": primitive_count,
        "embedded_image_count": len(images), "double_sided_material_count": sum(m.get("doubleSided", False) for m in mats),
        "mesh_count": len(doc.get("meshes", [])), "file_bytes": len(raw), "sha256": digest,
        "render_only_default": True, "colliders_supplied": False,
        "audit_status": "passed" if not errors else "failed", "audit_errors": errors,
        "maximum_bounds_error_m": max_error,
    }



if __name__=='__main__':
 m=json.loads((ROOT/'manifest.json').read_text());rows=[]
 for a in m['assets']:rows.append(inspect_glb(ROOT/a['file'],a,'15_motor_court_motel'))
 errors=[r['asset_id']+': '+e for r in rows for e in r['audit_errors']]
 report={'status':'passed' if not errors else 'failed','scope':'Independent core glTF bounds, indices, materials, embedded images, node metadata, normals/UVs and degeneracy. Not engine QA.','errors':errors,'assets':rows}
 (ROOT/'qa/independent_glb_report.json').write_text(json.dumps(report,indent=2));print(report['status'],errors)
 if errors:raise SystemExit(1)
