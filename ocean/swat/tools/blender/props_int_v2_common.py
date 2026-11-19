# Shared helpers for the Briar Court interior props layer v2
# (assets/environment/motel_props_int_v2/, motel_props_interior_v2_data.h).
#
# Each prop script imports this module, builds its geometry from bmesh
# primitives in ENGINE coordinates (metres, +Y up, model front toward +Z)
# and calls export(). The exporter's +Y-up conversion maps Blender (x,-z,y)
# back exactly. Textures are CC0 Poly Haven maps fetched by polyhaven(),
# downscaled and embedded in the GLB, or small maps baked from seeded numpy
# noise. Output bytes are reproducible: fixed triangulation, canonical index
# order, no dithering.
#
#   blender -b --factory-startup --python-exit-code 1 -P props_int_v2_<prop>.py -- OUT_DIR [--render PNG] [--iter N]
#
# --render writes a Cycles CPU preview from the script's reference viewpoint
# (REF_VIEW) for the hill-climb against the reference photograph.
import bpy, bmesh, math, os, sys, json, hashlib, struct, urllib.request
import numpy as np
from mathutils import Vector, Matrix

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = ARGS[0] if ARGS and not ARGS[0].startswith("--") else "motel_props_int_v2"
RENDER = ARGS[ARGS.index("--render") + 1] if "--render" in ARGS else None
ITER = int(ARGS[ARGS.index("--iter") + 1]) if "--iter" in ARGS else 99
os.makedirs(OUT, exist_ok=True)
CACHE = os.environ.get("SWAT_PH_CACHE", os.path.expanduser("~/.cache/swat_polyhaven"))

def E(x, y, z):
    return Vector((x, -z, y))

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.dither_intensity = 0.0

MATS = {}

# --------------------------------------------------------------- textures
def periodic_noise(rng, n, scale, octaves=4, persistence=.55):
    out = np.zeros((n, n)); amp = 1.0
    fy = np.fft.fftfreq(n)[:, None] * n; fx = np.fft.fftfreq(n)[None, :] * n
    r = np.sqrt(fx * fx + fy * fy)
    for o in range(octaves):
        f = scale * (2 ** o)
        layer = np.real(np.fft.ifft2(np.fft.fft2(rng.standard_normal((n, n))) * np.exp(-(r / f) ** 2)))
        layer /= (np.abs(layer).max() + 1e-9); out += amp * layer; amp *= persistence
    out -= out.min(); out /= (out.max() + 1e-9)
    return out

def to_image(name, rgb, alpha=None):
    n = rgb.shape[0]
    img = bpy.data.images.new(name, rgb.shape[1], n, alpha=alpha is not None)
    a = np.ones(rgb.shape[:2]) if alpha is None else alpha
    px = np.dstack([np.clip(rgb, 0, 1), a]).astype(np.float32)
    img.pixels.foreach_set(px[::-1].ravel())
    img.file_format = 'PNG'
    img.pack()
    return img

def srgb(c):
    return np.array(c, dtype=float) / 255.0

def lin(c):
    return tuple(((v / 255) ** 2.2) for v in c)

def polyhaven(asset, kind="diff", res="1k", size=None):
    """CC0 Poly Haven texture map (diff / nor_gl / rough / arm), cached
    outside the repository, optionally downscaled, packed as JPEG."""
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, f"{asset}_{kind}_{res}.jpg")
    if not os.path.exists(path):
        url = f"https://dl.polyhaven.org/file/ph-assets/Textures/jpg/{res}/{asset}/{asset}_{kind}_{res}.jpg"
        req = urllib.request.Request(url, headers={"User-Agent": "swat-art-build/1.0"})
        data = urllib.request.urlopen(req, timeout=120).read()
        open(path, "wb").write(data)
    img = bpy.data.images.load(path)
    img.name = f"{asset}_{kind}"
    if kind != "diff":
        img.colorspace_settings.name = "Non-Color"
    if size and img.size[0] != size:
        img.scale(size, size)
        # Re-encode the scaled pixels as an embedded JPEG.
        tmp = os.path.join(CACHE, f"_{asset}_{kind}_{size}.jpg")
        sc = bpy.context.scene
        sc.render.image_settings.file_format = 'JPEG'; sc.render.image_settings.quality = 88
        img.save_render(tmp, scene=sc)
        bpy.data.images.remove(img)
        img = bpy.data.images.load(tmp); img.name = f"{asset}_{kind}_{size}"
        if kind != "diff":
            img.colorspace_settings.name = "Non-Color"
    img.pack()
    return img

def tint_image(src, name, mul=(1, 1, 1), add=(0, 0, 0), size=None):
    """Colour-graded copy of an image (multiply then add, sRGB-ish space)."""
    w, h = src.size
    px = np.array(src.pixels[:], dtype=np.float32).reshape(h, w, 4)
    rgb = px[:, :, :3] * np.array(mul, dtype=np.float32) + np.array(add, dtype=np.float32)
    img = bpy.data.images.new(name, w, h)
    out = np.dstack([np.clip(rgb, 0, 1), np.ones((h, w), np.float32)])
    img.pixels.foreach_set(out.ravel())
    img.file_format = 'PNG'; img.pack()
    return img

def mat(name, color=(1, 1, 1), rough=.7, metal=0., image=None, normal=None, normal_strength=1.0, rough_image=None, alpha=1.0, emission=None):
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, alpha)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if image is not None:
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = image
        if color != (1, 1, 1):
            mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'
            mix.inputs[0].default_value = 1.0
            nt.links.new(t.outputs["Color"], mix.inputs[6]); mix.inputs[7].default_value = (*color, 1)
            nt.links.new(mix.outputs[2], bsdf.inputs["Base Color"])
        else:
            nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    if normal is not None:
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = normal
        nm = nt.nodes.new("ShaderNodeNormalMap"); nm.inputs["Strength"].default_value = normal_strength
        nt.links.new(t.outputs["Color"], nm.inputs["Color"]); nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    if emission is not None:
        bsdf.inputs["Emission Color"].default_value = (*emission, 1)
        bsdf.inputs["Emission Strength"].default_value = 1.0
    MATS[name] = m
    return m

# --------------------------------------------------------------- geometry
class Part:
    """Accumulates bmesh geometry per material for one exported object."""
    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.mats = []

    def mat_index(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def add(self, src, m, uv_scale=1.0, uv_mode="box", uv_offset=(0, 0)):
        idx = self.mat_index(m)
        lo = [min(v.co[i] for v in src.verts) for i in range(3)]
        hi = [max(v.co[i] for v in src.verts) for i in range(3)]
        vmap = {v: self.bm.verts.new(v.co) for v in src.verts}
        src.normal_update()
        for f in src.faces:
            try:
                nf = self.bm.faces.new([vmap[v] for v in f.verts])
            except ValueError:
                continue
            nf.material_index = idx; nf.smooth = f.smooth
            n = f.normal
            for l in nf.loops:
                co = l.vert.co
                ax = max(range(3), key=lambda i: abs(n[i]))
                if uv_mode == "fit":
                    uv = ((co.x - lo[0]) / (hi[0] - lo[0] + 1e-9), (co.z - lo[2]) / (hi[2] - lo[2] + 1e-9))
                    l[self.uv].uv = (uv[0] + uv_offset[0], uv[1] + uv_offset[1]); continue
                if uv_mode == "fitside":  # Blender YZ plane to 0..1
                    uv = ((co.y - lo[1]) / (hi[1] - lo[1] + 1e-9), (co.z - lo[2]) / (hi[2] - lo[2] + 1e-9))
                    l[self.uv].uv = (uv[0] + uv_offset[0], uv[1] + uv_offset[1]); continue
                if uv_mode == "top":
                    uv = (co.x, co.y)
                elif ax == 0:
                    uv = (co.y * (1 if n.x > 0 else -1), co.z)
                elif ax == 1:
                    uv = (co.x * (-1 if n.y > 0 else 1), co.z)
                else:
                    uv = (co.x, co.y * (1 if n.z > 0 else -1))
                l[self.uv].uv = (uv[0] * uv_scale + uv_offset[0], uv[1] * uv_scale + uv_offset[1])
        src.free()

def bm_box(c, h, bevel=0.0, seg=1):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector((2 * h[0], 2 * h[2], 2 * h[1])), verts=bm.verts)
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=seg, affect='EDGES', profile=.5)
    bmesh.ops.translate(bm, vec=E(*c), verts=bm.verts)
    return bm

def bm_cyl(c, r, height, seg=16, r2=None, axis='y', cap=True, smooth=True):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=seg,
                          radius1=r, radius2=r if r2 is None else r2, depth=height)
    for f in bm.faces:
        f.smooth = smooth and len(f.verts) == 4
    if axis == 'x':
        bmesh.ops.rotate(bm, verts=bm.verts, cent=Vector(), matrix=Matrix.Rotation(math.pi / 2, 3, 'Y'))
    elif axis == 'z':
        bmesh.ops.rotate(bm, verts=bm.verts, cent=Vector(), matrix=Matrix.Rotation(math.pi / 2, 3, 'X'))
    bmesh.ops.translate(bm, vec=E(*c), verts=bm.verts)
    return bm

def bm_sphere(c, r, scale=(1, 1, 1), seg=12, rings=8):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1)
    for f in bm.faces:
        f.smooth = True
    bmesh.ops.scale(bm, vec=Vector((r * scale[0], r * scale[2], r * scale[1])), verts=bm.verts)
    bmesh.ops.translate(bm, vec=E(*c), verts=bm.verts)
    return bm

def bm_lathe(profile, c=(0, 0, 0), seg=24, cap_top=False, cap_bottom=False):
    """Surface of revolution about engine +Y. profile: [(radius, y), ...] bottom to top."""
    bm = bmesh.new()
    rings = []
    for (r, y) in profile:
        ring = []
        for k in range(seg):
            a = 2 * math.pi * k / seg
            ring.append(bm.verts.new(E(c[0] + r * math.cos(a), c[1] + y, c[2] + r * math.sin(a))))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(seg):
            f = bm.faces.new((rings[i][k], rings[i][(k + 1) % seg], rings[i + 1][(k + 1) % seg], rings[i + 1][k]))
            f.smooth = True
    if cap_bottom and profile[0][0] > 0:
        bm.faces.new(list(rings[0]))
    if cap_top and profile[-1][0] > 0:
        bm.faces.new(list(reversed(rings[-1])))
    bm.normal_update()
    # Outward normals: flip if the first side face points inward.
    f0 = bm.faces[0]
    cen = E(*c); mid = f0.calc_center_median()
    radial = Vector((mid.x - cen.x, mid.y - cen.y, 0))
    if radial.length > 1e-6 and f0.normal.dot(radial) < 0:
        bmesh.ops.reverse_faces(bm, faces=list(bm.faces))
    return bm

def bm_grid(nx, nz, fn, smooth=True, double=False):
    """Sheet: fn(u, v) -> engine (x, y, z)."""
    bm = bmesh.new()
    vs = [[bm.verts.new(E(*fn(i / nx, j / nz))) for j in range(nz + 1)] for i in range(nx + 1)]
    for i in range(nx):
        for j in range(nz):
            f = bm.faces.new((vs[i][j], vs[i][j + 1], vs[i + 1][j + 1], vs[i + 1][j])); f.smooth = smooth
    bm.normal_update()
    if double:
        geom = bmesh.ops.duplicate(bm, geom=list(bm.faces))
        dup = [g for g in geom["geom"] if isinstance(g, bmesh.types.BMFace)]
        bmesh.ops.reverse_faces(bm, faces=dup)
    return bm

def bm_tube(points, r, seg=8, cap=True):
    """Round tube along engine-space polyline points."""
    bm = bmesh.new()
    pts = [E(*p) for p in points]
    rings = []
    for i, p in enumerate(pts):
        d = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        up = Vector((0, 0, 1)) if abs(d.z) < .9 else Vector((1, 0, 0))
        a = d.cross(up).normalized(); b = d.cross(a)
        rings.append([bm.verts.new(p + r * (math.cos(2 * math.pi * k / seg) * a + math.sin(2 * math.pi * k / seg) * b)) for k in range(seg)])
    for i in range(len(rings) - 1):
        for k in range(seg):
            f = bm.faces.new((rings[i][k], rings[i][(k + 1) % seg], rings[i + 1][(k + 1) % seg], rings[i + 1][k])); f.smooth = True
    if cap:
        bm.faces.new(list(reversed(rings[0]))); bm.faces.new(list(rings[-1]))
    bm.normal_update()
    return bm

def rotate_bm(bm, yaw, about=(0, 0, 0)):
    bmesh.ops.rotate(bm, verts=bm.verts, cent=E(*about), matrix=Matrix.Rotation(yaw, 3, 'Z'))
    return bm

def tilt_bm(bm, angle, axis='x', about=(0, 0, 0)):
    """Rotate about an engine axis through `about`."""
    ax = {'x': Vector((1, 0, 0)), 'y': Vector((0, 0, 1)), 'z': Vector((0, -1, 0))}[axis]
    bmesh.ops.rotate(bm, verts=bm.verts, cent=E(*about), matrix=Matrix.Rotation(angle, 3, ax))
    return bm

def soften(bm, rng, amount):
    for v in bm.verts:
        v.co += Vector(rng.normal(0, amount, 3))
    return bm

# --------------------------------------------------------------- export
def canonical_indices(path):
    data = bytearray(open(path, "rb").read())
    n = struct.unpack("<I", data[12:16])[0]
    j = json.loads(data[20:20 + n]); base = 20 + n + 8
    size = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
    def view(i):
        a = j["accessors"][i]; bv = j["bufferViews"][a["bufferView"]]
        width = size[a["type"]] * {5126: 4, 5123: 2, 5125: 4, 5121: 1}[a["componentType"]]
        return base + bv.get("byteOffset", 0) + a.get("byteOffset", 0), width, a["count"], a["componentType"]
    for mesh in j["meshes"]:
        for prim in mesh["primitives"]:
            if "indices" not in prim:
                continue
            attrs = [view(i) for _, i in sorted(prim["attributes"].items())]
            count = attrs[0][2]; first = {}; remap = []
            for v in range(count):
                key = b"".join(bytes(data[o + v * w:o + (v + 1) * w]) for o, w, _, _ in attrs)
                remap.append(first.setdefault(key, v))
            o, w, c, t = view(prim["indices"])
            fmt = {5123: "<H", 5125: "<I", 5121: "<B"}[t]; step = struct.calcsize(fmt)
            idx = [remap[struct.unpack_from(fmt, data, o + k * step)[0]] for k in range(c)]
            tris = []
            for k in range(0, c, 3):
                tri = idx[k:k + 3]; r = tri.index(min(tri)); tris.append(tuple(tri[r:] + tri[:r]))
            flat = [v for tri in sorted(tris) for v in tri]
            for k, v in enumerate(flat):
                struct.pack_into(fmt, data, o + k * step, v)
    open(path, "wb").write(bytes(data))

def build_object(name, part):
    me = bpy.data.meshes.new(name)
    bmesh.ops.triangulate(part.bm, faces=list(part.bm.faces), quad_method='FIXED', ngon_method='EAR_CLIP')
    part.bm.normal_update(); part.bm.to_mesh(me); part.bm.free()
    for m in part.mats:
        me.materials.append(m)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob

def export(name, part, keep=True):
    """Write OUT/name.glb; returns the Blender object (kept for previews)."""
    ob = build_object(name, part)
    for o in bpy.context.scene.objects:
        o.select_set(o == ob)
    bpy.context.view_layer.objects.active = ob
    path = os.path.join(OUT, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True,
                              export_yup=True, export_apply=True, export_texcoords=True,
                              export_normals=True, export_materials='EXPORT',
                              export_image_format='AUTO', export_extras=False,
                              export_cameras=False, export_lights=False, export_animations=False)
    canonical_indices(path)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    lo = [min(b[i] for b in bb) for i in range(3)]; hi = [max(b[i] for b in bb) for i in range(3)]
    meta = {"file": name + ".glb", "triangles": tris, "materials": len(part.mats) if hasattr(part, "mats") else len(ob.data.materials),
            "bytes": os.path.getsize(path), "sha256": hashlib.sha256(open(path, "rb").read()).hexdigest()[:16],
            "min": [round(lo[0], 3), round(lo[2], 3), round(-hi[1], 3)],
            "max": [round(hi[0], 3), round(hi[2], 3), round(-lo[1], 3)]}
    print("EXPORT", json.dumps(meta))
    with open(os.path.join(OUT, name + ".json"), "w") as f:
        json.dump(meta, f, indent=1)
    if not keep:
        bpy.data.objects.remove(ob)
    return ob

# --------------------------------------------------------------- preview
def preview(view, ob=None):
    """Cycles CPU preview from an engine-space viewpoint:
    view = {cam:(x,y,z), target:(x,y,z), fov:deg (horizontal), room:{...}, sun:..., lights:[(pos,watts,color)]}"""
    if not RENDER:
        return
    sc = bpy.context.scene
    world = bpy.data.worlds.new("w"); sc.world = world; world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (*view.get("sky", (0.55, 0.5, 0.45)), 1); bg.inputs[1].default_value = view.get("ambient", 0.35)
    room = view.get("room")
    if room:
        # Simple room shell for context: floor, back/side walls, optional ceiling.
        wall = mat("_wall", room.get("wall", (0.55, 0.45, 0.3)), .85)
        floor = mat("_floor", room.get("floor", (0.25, 0.08, 0.07)), .95)
        shell = Part()
        x0, x1, z0, z1, h = room["box"]
        shell.add(bm_box(((x0 + x1) / 2, -.01, (z0 + z1) / 2), ((x1 - x0) / 2, .01, (z1 - z0) / 2)), floor)
        for w in room.get("walls", ["back", "left", "right"]):
            if w == "back": shell.add(bm_box(((x0 + x1) / 2, h / 2, z0 - .02), ((x1 - x0) / 2, h / 2, .02)), wall)
            if w == "front": shell.add(bm_box(((x0 + x1) / 2, h / 2, z1 + .02), ((x1 - x0) / 2, h / 2, .02)), wall)
            if w == "left": shell.add(bm_box((x0 - .02, h / 2, (z0 + z1) / 2), (.02, h / 2, (z1 - z0) / 2)), wall)
            if w == "right": shell.add(bm_box((x1 + .02, h / 2, (z0 + z1) / 2), (.02, h / 2, (z1 - z0) / 2)), wall)
            if w == "ceiling": shell.add(bm_box(((x0 + x1) / 2, h + .01, (z0 + z1) / 2), ((x1 - x0) / 2, .01, (z1 - z0) / 2)), mat("_ceil", (.7, .68, .6), .9))
        build_object("_room", shell)
    for i, (pos, watts, col, size) in enumerate(view.get("lights", [((0, 2.5, 1.5), 300, (1, .9, .75), .3)])):
        ld = bpy.data.lights.new(f"L{i}", 'POINT'); ld.energy = watts; ld.color = col; ld.shadow_soft_size = size
        lo = bpy.data.objects.new(f"L{i}", ld); lo.location = E(*pos); sc.collection.objects.link(lo)
    if "sun" in view:
        d, strength = view["sun"]
        ld = bpy.data.lights.new("sun", 'SUN'); ld.energy = strength; ld.angle = .1
        lo = bpy.data.objects.new("sun", ld); sc.collection.objects.link(lo)
        lo.rotation_euler = (E(*d)).to_track_quat('-Z', 'Y').to_euler()
    cd = bpy.data.cameras.new("cam"); cd.lens_unit = 'FOV'; cd.angle = math.radians(view.get("fov", 50))
    cam = bpy.data.objects.new("cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    cam.location = E(*view["cam"])
    cam.rotation_euler = (E(*view["target"]) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'
    sc.cycles.samples = view.get("samples", 48); sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 4
    sc.render.resolution_x, sc.render.resolution_y = view.get("res", (960, 540))
    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'AgX' if view.get("agx", True) else 'Standard'
    sc.view_settings.exposure = view.get("exposure", 0.0)
    sc.render.image_settings.file_format = 'PNG'
    sc.render.filepath = RENDER
    bpy.ops.render.render(write_still=True)
