# Blender (4.2) headless converter: glTF prop -> Cretaceous .prop meshes + textures.
#   blender -b --factory-startup --disable-autoexec --python props_convert.py -- IN.gltf NAME OUTDIR
# Each mesh object becomes one variant (sets like rock_moss_set_01 split into
# NAME_a, NAME_b, ...). Every variant gets 3 LODs (decimated), is recentred on
# its footprint with the base at y=0, and is written Y-up.
#
# .prop layout (little endian):
#   u32 magic 'CPRP', u32 version (1), char[32] material, f32 radius, f32 height,
#   u32 nlod, then per LOD: u32 nvert, u32 nidx,
#   nvert x {f32 pos[3], f32 uv[2], i8 nrm[4], i8 tan[4] (w = bitangent sign)},
#   nidx x u32.
# Textures: MATERIAL_diff.jpg (sRGB albedo), MATERIAL_nor.jpg (OpenGL normal),
# MATERIAL_arm.jpg (R ao, G roughness, B metal).
import bpy, bmesh, sys, os, struct, shutil
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
src, name, out = argv[0], argv[1], argv[2]
LOD0 = {"rock": 4000, "wood": 3000}
os.makedirs(out, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src, merge_vertices=True)
objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
wood = any(k in name for k in ("trunk", "stump", "root", "bark", "log", "branch"))
target0 = LOD0["wood" if wood else "rock"]

# ---------------------------------------------------------------- textures
srcdir = os.path.dirname(src)
def find_tex(*keys):
    for root, _, files in os.walk(srcdir):
        for f in files:
            if all(k in f for k in keys): return os.path.join(root, f)
    return None
diff, nor, arm, rough, ao = find_tex("_diff"), find_tex("_nor_gl"), find_tex("_arm"), find_tex("_rough"), find_tex("_ao")
mat = name
shutil.copy(diff, os.path.join(out, mat + "_diff.jpg"))
shutil.copy(nor, os.path.join(out, mat + "_nor.jpg"))
if arm:
    shutil.copy(arm, os.path.join(out, mat + "_arm.jpg"))
else:
    def load(p, fill):
        if not p: return None
        im = bpy.data.images.load(p); a = np.empty(im.size[0] * im.size[1] * 4, np.float32); im.pixels.foreach_get(a)
        return im.size, a.reshape(-1, 4)
    r = load(rough, 0.8); o = load(ao, 1.0)
    size = (r or o)[0]
    px = np.ones((size[0] * size[1], 4), np.float32)
    px[:, 1] = r[1][:, 0] if r else 0.8
    if o and o[0] == size: px[:, 0] = o[1][:, 0]
    px[:, 2] = 0
    img = bpy.data.images.new(mat + "_arm", size[0], size[1]); img.pixels.foreach_set(px.ravel())
    img.filepath_raw = os.path.join(out, mat + "_arm.jpg"); img.file_format = "JPEG"; img.save()

# ---------------------------------------------------------------- meshes
def tri_count(o): return sum(len(p.vertices) - 2 for p in o.data.polygons)

def export_lod(o):
    me = o.data
    me.calc_tangents(uvmap=me.uv_layers.active.name)
    uv = me.uv_layers.active.data
    verts, index, lut = [], [], {}
    for poly in me.polygons:
        assert len(poly.loop_indices) == 3
        for li in poly.loop_indices:
            l = me.loops[li]; v = me.vertices[l.vertex_index].co
            p = (v.x, v.z, -v.y); n = (l.normal.x, l.normal.z, -l.normal.y); t = (l.tangent.x, l.tangent.z, -l.tangent.y)
            u = (uv[li].uv.x, 1.0 - uv[li].uv.y)
            key = (round(p[0], 4), round(p[1], 4), round(p[2], 4), round(u[0], 4), round(u[1], 4), round(n[0], 2), round(n[1], 2), round(n[2], 2))
            k = lut.get(key)
            if k is None:
                k = lut[key] = len(verts)
                verts.append((p, u, n, t, l.bitangent_sign))
            index.append(k)
    q = lambda x: max(-127, min(127, int(round(x * 127))))
    b = bytearray(struct.pack("<II", len(verts), len(index)))
    for p, u, n, t, s in verts:
        b += struct.pack("<5f8b", *p, *u, q(n[0]), q(n[1]), q(n[2]), 0, q(t[0]), q(t[1]), q(t[2]), 1 if s > 0 else -1)
    b += struct.pack("<%dI" % len(index), *index)
    return b, len(index) // 3

suffix = "abcdefghijklmnop"
variants = objs if len(objs) > 1 else objs[:1]
for vi, o in enumerate(variants):
    vname = name if len(variants) == 1 else name + "_" + suffix[vi]
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Recentre: footprint centre at origin, base at 0 (Blender Z is up).
    co = np.empty(len(o.data.vertices) * 3, np.float32); o.data.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
    c = (co.min(0) + co.max(0)) * 0.5; c[2] = co[:, 2].min()
    co -= c; o.data.vertices.foreach_set("co", co.ravel()); o.data.update()
    radius = float(np.sqrt((co[:, 0] ** 2 + co[:, 1] ** 2).max())); height = float(co[:, 2].max())
    # Triangulate once, then decimate copies.
    # Weld: some scans are triangle soup (every edge a border), which blocks decimation.
    bm = bmesh.new(); bm.from_mesh(o.data); bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bmesh.ops.triangulate(bm, faces=bm.faces); bm.to_mesh(o.data); bm.free()
    base = tri_count(o)
    # Budget by size: pebbles and debris are tiny, coastal shelves are huge.
    t0 = 600 if radius < 0.35 else 8000 if radius > 10 else target0
    blob = bytearray(); counts = []
    for frac in (1.0, 0.25, 0.06):
        target = max(40, int(t0 * frac))
        lo = o.copy(); lo.data = o.data.copy(); bpy.context.scene.collection.objects.link(lo)
        if base > target:
            m = lo.modifiers.new("dec", "DECIMATE"); m.ratio = target / base; m.use_collapse_triangulate = True
            bpy.ops.object.select_all(action="DESELECT"); lo.select_set(True); bpy.context.view_layer.objects.active = lo
            bpy.ops.object.modifier_apply(modifier="dec")
        bm = bmesh.new(); bm.from_mesh(lo.data); bmesh.ops.triangulate(bm, faces=bm.faces); bm.to_mesh(lo.data); bm.free()
        for p in lo.data.polygons: p.use_smooth = True
        b, nt = export_lod(lo); blob += b; counts.append(nt)
        bpy.data.objects.remove(lo)
    hdr = struct.pack("<II32sffI", 0x50525043, 1, mat.encode()[:31], radius, height, 3)
    open(os.path.join(out, vname + ".prop"), "wb").write(hdr + blob)
    print("props: %s src %d tris -> %s, r %.2f h %.2f" % (vname, base, counts, radius, height))
