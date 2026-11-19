# Shared helpers for the Briar Court exterior prop pass v2 (Blender 4.2).
# Each prop script builds one prop from code (deterministic, metres, Blender
# Z-up, prop front = +X), can render a reference-matched view with Cycles on
# the CPU, and exports a GLB for the engine (+Y up). See README in
# assets/environment/motel_props_ext_v2/.
import bpy, bmesh, math, os, sys, json, hashlib, random
from mathutils import Vector, Matrix, Euler

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", ".."))
ASSETS = os.path.join(REPO, "ocean", "swat", "assets", "environment", "motel_props_ext_v2")
# CC0 Poly Haven sources (1K diffuse JPEGs). fetch() downloads them into the
# cache once; md5s pin the exact files.
POLYHAVEN = {
    "dirty_concrete": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/dirty_concrete/dirty_concrete_diff_1k.jpg",
    "rust_coarse_01": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rust_coarse_01/rust_coarse_01_diff_1k.jpg",
    "palm_tree_bark": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/palm_tree_bark/palm_tree_bark_diff_1k.jpg",
    "rough_wood": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rough_wood/rough_wood_diff_1k.jpg",
    "brushed_concrete": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/brushed_concrete/brushed_concrete_diff_1k.jpg",
    "rusty_painted_metal": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rusty_painted_metal/rusty_painted_metal_diff_1k.jpg",
    "metal_plate": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/metal_plate/metal_plate_diff_1k.jpg",
}
CACHE = os.environ.get("PE2_CACHE", "/tmp/ph")

def args():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = {"render": None, "export": False, "params": {}}
    i = 0
    while i < len(a):
        if a[i] == "--render": out["render"] = a[i + 1]; i += 2
        elif a[i] == "--export": out["export"] = True; i += 1
        elif a[i] == "--set":  # --set key=jsonvalue (iteration overrides)
            k, v = a[i + 1].split("=", 1); out["params"][k] = json.loads(v); i += 2
        else: i += 1
    return out

def fetch(name):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, name + "_diff_1k.jpg")
    if not os.path.exists(path):
        import urllib.request
        urllib.request.urlretrieve(POLYHAVEN[name], path)
    return path

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    _images.clear()
    random.seed(1234)
    s = bpy.context.scene
    s.unit_settings.system = "METRIC"
    return s

_images = {}
def image(name, size=512, gray=False, gain=1.0, mean=None):
    """Processed copy of a Poly Haven texture, packed into the .blend so the
    exporter embeds it. gray+mean: neutral grain normalised to that mean, to be
    multiplied by vertex colour (paint) in the engine."""
    key = (name, size, gray, gain, mean)
    if key in _images: return _images[key]
    src = bpy.data.images.load(fetch(name))
    src.scale(size, size)
    px = list(src.pixels)
    n = size * size
    if gray or mean is not None:
        lum = [0.2126 * px[4*i] + 0.7152 * px[4*i+1] + 0.0722 * px[4*i+2] for i in range(n)]
        m = sum(lum) / n
        for i in range(n):
            if gray:
                v = lum[i]
                if mean is not None: v = mean + (v - m) * gain
                v = min(max(v, 0.0), 1.0)
                px[4*i] = px[4*i+1] = px[4*i+2] = v
            else:
                for c in range(3): px[4*i+c] = min(max(px[4*i+c] * gain, 0.0), 1.0)
    elif gain != 1.0:
        for i in range(n):
            for c in range(3): px[4*i+c] = min(px[4*i+c] * gain, 1.0)
    img = bpy.data.images.new("T_%s_%d%s" % (name, size, "_g" if gray else ""), size, size)
    img.pixels = px
    img.file_format = "JPEG"
    img.pack()
    bpy.data.images.remove(src)
    _images[key] = img
    return img

def material(name, tex=None, rough=0.8, metal=0.0, vcol=True, emission=None, alpha_clip=False):
    """Base colour = texture x vertex colour (engine: albedo map x COLOR_0)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree; n = nt.nodes; l = nt.links
    b = n["Principled BSDF"]
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    col = None
    if vcol:
        col = n.new("ShaderNodeVertexColor"); col.layer_name = "Col"
    if tex is not None:
        t = n.new("ShaderNodeTexImage"); t.image = tex
        if alpha_clip:
            l.new(t.outputs["Alpha"], b.inputs["Alpha"]); m.blend_method = "CLIP"
        if col is not None:
            mix = n.new("ShaderNodeMix"); mix.data_type = "RGBA"; mix.blend_type = "MULTIPLY"
            mix.inputs["Factor"].default_value = 1.0
            l.new(t.outputs["Color"], mix.inputs[6]); l.new(col.outputs["Color"], mix.inputs[7])
            l.new(mix.outputs[2], b.inputs["Base Color"])
        else:
            l.new(t.outputs["Color"], b.inputs["Base Color"])
    elif col is not None:
        l.new(col.outputs["Color"], b.inputs["Base Color"])
    if emission:
        b.inputs["Emission Color"].default_value = (*emission, 1); b.inputs["Emission Strength"].default_value = 1.0
    return m

def srgb(c):
    return tuple((x / 255.0) ** 2.2 if x > 1 else x for x in c)

# ---------------------------------------------------------------- geometry
def new_obj(name, bm, mat=None, color=(1, 1, 1), smooth=False):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    if mat is not None: me.materials.append(mat)
    paint(ob, color)
    if smooth:
        for p in me.polygons: p.use_smooth = True
    return ob

def paint(ob, color, noise=0.0, seed=0, grad=None):
    """Per-corner vertex colour (linear RGB). noise: per-vertex value jitter;
    grad=(z0,z1,k): darken towards z0 (dirt near the ground) by factor k."""
    me = ob.data
    if "Col" not in me.color_attributes:
        me.color_attributes.new("Col", "BYTE_COLOR", "CORNER")
    ca = me.color_attributes["Col"]
    rnd = random.Random(seed)
    vj = [1.0 + noise * (rnd.random() * 2 - 1) for _ in me.vertices]
    # Colours are display (sRGB) tints, as the engine reads COLOR_0.
    c = srgb(color) if any(x > 1 for x in color) else tuple(x ** 2.2 for x in color)
    for poly in me.polygons:
        for li in poly.loop_indices:
            v = me.loops[li].vertex_index
            f = vj[v]
            if grad:
                z = (ob.matrix_world @ me.vertices[v].co).z
                t = min(max((z - grad[0]) / (grad[1] - grad[0]), 0), 1)
                f *= grad[2] + (1 - grad[2]) * t
            ca.data[li].color = (min(c[0]*f,1), min(c[1]*f,1), min(c[2]*f,1), 1)
    me.color_attributes.active_color = ca

def box_uv(ob, size=1.0):
    """World-metre box projection UVs (deterministic)."""
    me = ob.data
    if not me.uv_layers: me.uv_layers.new(name="UV")
    uv = me.uv_layers.active.data
    mw = ob.matrix_world
    for p in me.polygons:
        nrm = (mw.to_3x3() @ p.normal)
        ax = max(range(3), key=lambda i: abs(nrm[i]))
        for li in p.loop_indices:
            co = mw @ me.vertices[me.loops[li].vertex_index].co
            if ax == 0: u, v = co.y, co.z
            elif ax == 1: u, v = co.x, co.z
            else: u, v = co.x, co.y
            uv[li].uv = (u / size, v / size)

def cyl_uv(ob, circ=1.0, height=1.0):
    me = ob.data
    if not me.uv_layers: me.uv_layers.new(name="UV")
    uv = me.uv_layers.active.data
    for p in me.polygons:
        cs = [me.vertices[me.loops[li].vertex_index].co for li in p.loop_indices]
        angs = [math.atan2(c.y, c.x) for c in cs]
        base = angs[0]
        for k, li in enumerate(p.loop_indices):
            a = angs[k]
            while a - base > math.pi: a -= 2 * math.pi
            while a - base < -math.pi: a += 2 * math.pi
            uv[li].uv = (a / (2 * math.pi) * circ, cs[k].z / height)

def bm_box(bm, cx, cy, cz, sx, sy, sz):
    r = bmesh.ops.create_cube(bm, size=1.0)
    for v in r["verts"]:
        v.co = Vector((cx + v.co.x * sx, cy + v.co.y * sy, cz + v.co.z * sz))
    return r["verts"]

def box(name, c, s, mat=None, color=(1, 1, 1), bevel=0.0, segs=1):
    bm = bmesh.new(); bm_box(bm, c[0], c[1], c[2], s[0], s[1], s[2])
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=segs, affect="EDGES", profile=0.5)
    return new_obj(name, bm, mat, color)

def cylinder(name, r1, r2, h, segs=16, mat=None, color=(1, 1, 1), loc=(0, 0, 0), rot=(0, 0, 0), cap=True, smooth=True):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=segs, radius1=r1, radius2=r2, depth=h)
    ob = new_obj(name, bm, mat, color, smooth)
    ob.location = loc; ob.rotation_euler = rot
    return ob

def lathe(name, profile, segs=16, mat=None, color=(1, 1, 1), smooth=True):
    """profile: [(r, z)] revolved about Z."""
    bm = bmesh.new(); rings = []
    for (r, z) in profile:
        ring = [bm.verts.new((r * math.cos(2*math.pi*i/segs), r * math.sin(2*math.pi*i/segs), z)) for i in range(segs)]
        rings.append(ring)
    for a, b in zip(rings, rings[1:]):
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new((a[i], a[j], b[j], b[i]))
    if profile[0][0] > 1e-4: bm.faces.new(list(reversed(rings[0])))
    if profile[-1][0] > 1e-4: bm.faces.new(rings[-1])
    bm.normal_update()
    return new_obj(name, bm, mat, color, smooth)

def tube(name, pts, radius, segs=6, mat=None, color=(1, 1, 1)):
    """Polyline tube (wires, rails)."""
    bm = bmesh.new(); rings = []
    for k, p in enumerate(pts):
        p = Vector(p)
        d = (Vector(pts[min(k+1, len(pts)-1)]) - Vector(pts[max(k-1, 0)])).normalized()
        up = Vector((0, 0, 1)) if abs(d.z) < 0.9 else Vector((1, 0, 0))
        a = d.cross(up).normalized(); b = d.cross(a).normalized()
        rings.append([bm.verts.new(p + radius * (math.cos(2*math.pi*i/segs) * a + math.sin(2*math.pi*i/segs) * b)) for i in range(segs)])
    for r0, r1 in zip(rings, rings[1:]):
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
    bm.normal_update()
    return new_obj(name, bm, mat, color, True)

def extrude_profile(name, pts, width, mat=None, color=(1, 1, 1), bevel=0.0, segs=2, y0=None):
    """Side silhouette polygon pts [(x,z)] extruded along Y (centred)."""
    bm = bmesh.new()
    y0 = -width / 2 if y0 is None else y0
    a = [bm.verts.new((x, y0, z)) for x, z in pts]
    b = [bm.verts.new((x, y0 + width, z)) for x, z in pts]
    bm.faces.new(a); bm.faces.new(list(reversed(b)))
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((a[j], a[i], b[i], b[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=segs, affect="EDGES", profile=0.5, clamp_overlap=True)
    return new_obj(name, bm, mat, color)

def boolean(target, cutter, op="DIFFERENCE"):
    m = target.modifiers.new("b", "BOOLEAN"); m.object = cutter; m.operation = op; m.solver = "EXACT"
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_apply(modifier="b")
    bpy.data.objects.remove(cutter)

def apply_all():
    for ob in bpy.context.scene.objects:
        if ob.type != "MESH": continue
        bpy.context.view_layer.objects.active = ob
        ob.select_set(True)
        for m in list(ob.modifiers): bpy.ops.object.modifier_apply(modifier=m.name)
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        ob.select_set(False)

def join_by_material(name):
    """One object per material keeps the GLB to a few meshes."""
    apply_all()
    groups = {}
    for ob in [o for o in bpy.context.scene.objects if o.type == "MESH" and not o.name.startswith("_")]:
        groups.setdefault(ob.data.materials[0].name if ob.data.materials else "", []).append(ob)
    out = []
    for k, (mat, obs) in enumerate(sorted(groups.items())):
        bpy.ops.object.select_all(action="DESELECT")
        for o in obs: o.select_set(True)
        bpy.context.view_layer.objects.active = obs[0]
        if len(obs) > 1: bpy.ops.object.join()
        obs[0].name = "%s_%s" % (name, mat)
        out.append(obs[0])
    return out

def triangles():
    return sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in bpy.context.scene.objects if o.type == "MESH" and not o.name.startswith("_"))

# ---------------------------------------------------------------- render
def look_at(cam, target):
    d = Vector(target) - cam.location
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()

def render(path, cam_loc, target, focal=35.0, sun=(45, 135), res=(960, 540), ground=(0.33, 0.31, 0.28), sky=(0.55, 0.68, 0.9), samples=24, sun_strength=5.5, sensor=36.0):
    """sun=(elevation_deg, azimuth_deg from +X towards +Y). Ground plane only
    for the render (prefixed '_', never exported)."""
    s = bpy.context.scene
    s.render.engine = "CYCLES"; s.cycles.device = "CPU"; s.cycles.samples = samples
    s.cycles.use_denoising = True; s.cycles.denoiser = "OPENIMAGEDENOISE"
    s.render.resolution_x, s.render.resolution_y = res; s.render.resolution_percentage = 100
    s.view_settings.view_transform = "Standard"
    s.cycles.max_bounces = 4
    if "_ground" not in bpy.data.objects:
        bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, 0)); g = bpy.context.object; g.name = "_ground"
        gm = bpy.data.materials.new("_ground"); gm.use_nodes = True
        gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*ground, 1)
        gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.95
        g.data.materials.append(gm)
    w = bpy.data.worlds.new("_w"); s.world = w; w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (*sky, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.9
    sd = bpy.data.lights.new("_sun", "SUN"); sd.energy = sun_strength; sd.angle = math.radians(1.5)
    so = bpy.data.objects.new("_sun", sd); s.collection.objects.link(so)
    el, az = math.radians(sun[0]), math.radians(sun[1])
    so.rotation_euler = Vector((-math.cos(el)*math.cos(az), -math.cos(el)*math.sin(az), -math.sin(el))).to_track_quat("-Z", "Y").to_euler()
    cd = bpy.data.cameras.new("_cam"); cd.lens = focal; cd.sensor_width = sensor
    co = bpy.data.objects.new("_cam", cd); s.collection.objects.link(co)
    co.location = Vector(cam_loc); look_at(co, target); s.camera = co
    s.render.filepath = path; s.render.image_settings.file_format = "JPEG"; s.render.image_settings.quality = 90
    bpy.ops.render.render(write_still=True)

def export(name, extra=None):
    os.makedirs(ASSETS, exist_ok=True)
    for o in list(bpy.data.objects):
        if o.name.startswith("_"): bpy.data.objects.remove(o)
    path = os.path.join(ASSETS, name + ".glb")
    # glTF COLOR_0 is linear, but the engine's lit shader linearises
    # texture x tint with pow(2.2): store display-encoded tints.
    for o in bpy.data.objects:
        if o.type != "MESH" or "Col" not in o.data.color_attributes: continue
        for d in o.data.color_attributes["Col"].data:
            c = d.color; d.color = (c[0] ** (1 / 2.2), c[1] ** (1 / 2.2), c[2] ** (1 / 2.2), c[3])
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_image_format="JPEG", export_jpeg_quality=85,
                              export_vertex_color="ACTIVE", export_all_vertex_colors=False,
                              export_normals=True, export_texcoords=True, export_materials="EXPORT",
                              export_extras=False, export_animations=False, export_skins=False, export_morph=False)
    print("PE2 export", path, "tris", triangles())
    return path

def finish(name, build, defaults, render_view):
    a = args()
    p = dict(defaults); p.update(a["params"])
    reset(); build(p)
    tris = triangles()
    print("PE2 %s triangles %d" % (name, tris))
    if a["render"]:
        render(a["render"], **render_view(p))
    if a["export"]:
        reset(); build(p); join_by_material(name); export(name)
