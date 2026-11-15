# Briar Court interior dressing pack, recipe v1.
#
# Deterministic Blender 4.2 build of the presentation-only interior art in
# ocean/swat/assets/environment/motel_interior_v1/: bed replacement looks that
# stay inside the existing bed-frame and mattress collider bounds, bathroom
# fixtures, guest clutter, reception/laundry dressing, and four tileable
# surface maps (carpet, wallpaper, bathroom tile, popcorn ceiling). Everything is original:
# geometry is built from bmesh primitives, textures are baked from seeded
# numpy noise. No downloads, no external images, no supplied scripts run.
#
#   blender -b --factory-startup --python-exit-code 1 \
#       -P ocean/swat/tools/blender/motel_interior_v1.py -- OUT_DIR
#
# Authoring convention: every helper takes ENGINE coordinates (metres, +Y up,
# +Z toward the motel's front court) and converts once to Blender Z-up
# (x, -z, y); the glTF exporter's +Y-up conversion maps them back exactly.
# Each GLB origin is the mount documented in motel_interior_art.h.
import bpy, bmesh, math, os, sys, json, hashlib
import numpy as np
from mathutils import Vector, Matrix

OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "motel_interior_v1"
os.makedirs(OUT, exist_ok=True)
TEX = 256

def E(x, y, z):
    return Vector((x, -z, y))

# --------------------------------------------------------------- textures
def periodic_noise(rng, n, scale, octaves=4, persistence=.55):
    """Tileable fractal noise: filtered white noise in the Fourier domain."""
    out = np.zeros((n, n))
    amp = 1.0
    fy = np.fft.fftfreq(n)[:, None] * n
    fx = np.fft.fftfreq(n)[None, :] * n
    r = np.sqrt(fx * fx + fy * fy)
    for o in range(octaves):
        f = scale * (2 ** o)
        spec = np.fft.fft2(rng.standard_normal((n, n)))
        filt = np.exp(-(r / f) ** 2)
        layer = np.real(np.fft.ifft2(spec * filt))
        layer /= (np.abs(layer).max() + 1e-9)
        out += amp * layer
        amp *= persistence
    out -= out.min(); out /= (out.max() + 1e-9)
    return out

def to_image(name, rgb, alpha=None):
    n = rgb.shape[0]
    img = bpy.data.images.new(name, n, rgb.shape[1], alpha=alpha is not None)
    a = np.ones(rgb.shape[:2]) if alpha is None else alpha
    px = np.dstack([np.clip(rgb, 0, 1), a]).astype(np.float32)
    # Blender rows start at the bottom; flip so array row 0 is the image top.
    img.pixels.foreach_set(px[::-1].ravel())
    img.file_format = 'PNG'
    img.pack()
    return img

def save_png(img, path):
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()

def srgb(c):
    return np.array(c, dtype=float) / 255.0

def tex_carpet(seed=11, n=512):
    """Worn burgundy/navy motel carpet: repeating medallion lattice, 0.5 m tile."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    base = srgb((92, 34, 40))
    navy = srgb((36, 44, 66))
    gold = srgb((118, 86, 50))
    teal = srgb((44, 86, 84))
    # 2 x 2 diamond lattice per texture (texture spans 1 m -> 0.5 m motif).
    u = (x * 2) % 1 - .5; v = (y * 2) % 1 - .5
    d = np.abs(u) + np.abs(v)
    ring = np.exp(-((d - .36) / .03) ** 2)
    medal = .8 * np.exp(-((np.sqrt(u * u + v * v) - .14) / .016) ** 2)
    dot = np.exp(-(u * u + v * v) / .002)
    cross = np.exp(-(np.minimum(np.abs(u), np.abs(v)) / .015) ** 2) * (d > .42)
    col = np.ones((n, n, 3)) * base
    col = col * (1 - ring[..., None]) + navy * ring[..., None]
    col = col * (1 - medal[..., None]) + gold * medal[..., None]
    col = col * (1 - dot[..., None]) + teal * dot[..., None]
    col = col * (1 - .7 * cross[..., None]) + navy * .7 * cross[..., None]
    # Pile speckle, traffic wear and a few tileable stains.
    speck = rng.random((n, n))
    col *= (.86 + .2 * speck)[..., None]
    wear = periodic_noise(rng, n, 3)
    col = col * (1 - .35 * wear[..., None]) + srgb((120, 100, 86)) * .35 * wear[..., None]
    stain = periodic_noise(rng, n, 5) ** 6
    col *= (1 - .55 * stain)[..., None]
    return col

def tex_wallpaper(seed=23, n=512):
    """Yellowed 1970s stripe/damask paper, 0.6 m repeat, with water bloom."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    cream = srgb((196, 178, 132))
    olive = srgb((142, 130, 84))
    rust = srgb((150, 96, 64))
    # Three vertical stripe bands per texture width (texture spans 0.6 m).
    s = (x * 3) % 1
    stripe = ((s > .08) & (s < .14)) | ((s > .86) & (s < .92))
    col = np.ones((n, n, 3)) * cream
    col[stripe] = olive
    # Stylised damask motif between stripes: lobed rings.
    u = s - .5; v = (y * 3) % 1 - .5
    ang = np.arctan2(v, u); rad = np.sqrt(u * u + v * v)
    lobe = .22 + .06 * np.cos(4 * ang)
    motif = np.exp(-((rad - lobe) / .018) ** 2) + np.exp(-(rad / .05) ** 2)
    motif = np.clip(motif, 0, 1)
    col = col * (1 - .38 * motif[..., None]) + rust * .38 * motif[..., None]
    fade = periodic_noise(rng, n, 2)
    col = col * (.9 + .12 * fade[..., None])
    bloom = periodic_noise(rng, n, 3) ** 7
    col = col * (1 - .25 * bloom[..., None]) + srgb((120, 92, 52)) * .25 * bloom[..., None]
    col *= (.95 + .08 * rng.random((n, n)))[..., None]
    return col

def tex_tile(seed=31, n=512):
    """Small square bathroom tile with grimy grout, 4 x 4 tiles = 0.6 m."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    u = (x * 4) % 1; v = (y * 4) % 1
    grout = (u < .04) | (v < .04)
    tile_id = (np.floor(x * 4) + 7 * np.floor(y * 4)).astype(int)
    tint = rng.random(64)[tile_id % 64]
    col = np.ones((n, n, 3)) * srgb((178, 186, 172))
    col *= (.94 + .08 * tint)[..., None]
    col[grout] = srgb((96, 92, 78))
    dirt = periodic_noise(rng, n, 4) ** 3
    col *= (1 - .3 * dirt)[..., None]
    return col

def tex_ceiling(seed=37, n=512):
    """Popcorn ceiling: off-white stipple with low-frequency nicotine yellowing."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    col = np.ones((n, n, 3)) * srgb((214, 206, 186))
    stipple = periodic_noise(rng, n, 90, 2)
    col *= (.82 + .3 * stipple)[..., None]
    smoke = periodic_noise(rng, n, 2)
    col = col * (1 - .3 * smoke[..., None]) + srgb((176, 150, 98)) * .3 * smoke[..., None]
    # No discrete rings: the map repeats every ~0.9 m and they would tile visibly.
    return col

def tex_quilt(seed, base, flower, leaf, n=TEX):
    """Quilted floral bedspread, one texture repeat = 0.5 m."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    col = np.ones((n, n, 3)) * srgb(base)
    cx = rng.random(28); cy = rng.random(28); rr = .03 + .03 * rng.random(28)
    for i in range(28):
        dx = (x - cx[i] + .5) % 1 - .5; dy = (y - cy[i] + .5) % 1 - .5
        ang = np.arctan2(dy, dx); rad = np.sqrt(dx * dx + dy * dy)
        petal = rad < rr[i] * (.7 + .3 * np.cos(5 * ang + i))
        leafm = (rad < rr[i] * 1.6) & (rad > rr[i]) & (np.cos(2 * ang + i) > .6)
        col[leafm] = srgb(leaf)
        col[petal] = srgb(flower) * (.85 + .15 * np.cos(i))
    # Diagonal quilting stitches: darker valleys every 1/4 tile.
    q1 = np.abs(((x + y) * 4) % 1 - .5); q2 = np.abs(((x - y) * 4) % 1 - .5)
    valley = np.exp(-((.5 - np.maximum(q1, q2)) / .03) ** 2)
    col *= (1 - .35 * valley)[..., None]
    col *= (.92 + .1 * periodic_noise(rng, n, 4))[..., None]
    return col

def tex_wood(seed=41, n=TEX, tone=(96, 62, 38)):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    warp = periodic_noise(rng, n, 3)
    grain = np.sin((x * 28 + warp * 3) * 2 * np.pi) * .5 + .5
    fine = periodic_noise(rng, n, 40, 2)
    col = np.ones((n, n, 3)) * srgb(tone)
    col *= (.78 + .22 * grain[..., None] + .1 * fine[..., None])
    return col

def tex_print(seed=53, n=TEX):
    """Faded desert sunset print for the second framed picture."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    sky = np.clip(1 - y / .6, 0, 1)[..., None]
    col = srgb((214, 120, 70)) * sky + srgb((236, 196, 128)) * (1 - sky)
    sun = np.exp(-(((x - .62) ** 2 + (y - .42) ** 2) / .004))
    col = col * (1 - sun[..., None]) + srgb((250, 230, 170)) * sun[..., None]
    hills = .52 + .06 * np.sin(x * 9 + 1) + .03 * np.sin(x * 23)
    mesa = .6 + .1 * (np.abs(x - .25) < .12)
    ground = (y > hills)
    col[ground] = srgb((110, 66, 52))
    col[y > mesa] = srgb((70, 44, 38))
    cactus = (np.abs(x - .8) < .012) & (y > .5) | (np.abs(x - .78) < .03) & (np.abs(y - .6) < .01)
    col[cactus & (y < .85)] = srgb((40, 52, 36))
    col = col * .82 + srgb((200, 180, 140)) * .18  # sun-faded
    col *= (.95 + .08 * rng.random((n, n)))[..., None]
    return col

def tex_cardboard(seed=61, n=256, logo=True):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    col = np.ones((n, n, 3)) * srgb((176, 140, 96))
    col *= (.9 + .12 * periodic_noise(rng, n, 8))[..., None]
    if logo:
        r = np.sqrt((x - .5) ** 2 + (y - .5) ** 2)
        col[(r > .22) & (r < .26)] = srgb((170, 40, 36))
        col[(np.abs(x - .5) < .16) & (np.abs(y - .5) < .03)] = srgb((170, 40, 36))
    grease = periodic_noise(rng, n, 3) ** 4
    col *= (1 - .4 * grease)[..., None]
    return col

def tex_label(seed, n=256, band=(180, 30, 30)):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    col = np.ones((n, n, 3)) * srgb((214, 200, 160))
    col[(y > .35) & (y < .65)] = srgb(band)
    col[(y > .45) & (y < .55) & (np.abs(x % .25 - .12) < .05)] = srgb((240, 230, 200))
    return col

# --------------------------------------------------------------- materials
MATS = {}
def mat(name, color=(1, 1, 1), rough=.7, metal=0., image=None, alpha=1.0):
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*[c for c in color], alpha)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if image is not None:
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = image
        nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    MATS[name] = m
    return m

def lin(c):
    """sRGB 0-255 to linear factor for glTF baseColorFactor."""
    return tuple(((v / 255) ** 2.2) for v in c)

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

    def add_bm(self, src, m, uv_scale=1.0, uv_mode="box"):
        idx = self.mat_index(m)
        lo = [min(v.co[i] for v in src.verts) for i in range(3)]
        hi = [max(v.co[i] for v in src.verts) for i in range(3)]
        vmap = {}
        for v in src.verts:
            vmap[v] = self.bm.verts.new(v.co)
        for f in src.faces:
            try:
                nf = self.bm.faces.new([vmap[v] for v in f.verts])
            except ValueError:
                continue
            nf.material_index = idx
            nf.smooth = f.smooth
            n = f.normal
            for l in nf.loops:
                co = l.vert.co
                ax = max(range(3), key=lambda i: abs(n[i]))
                if uv_mode == "fit":  # front (Blender XZ) plane to 0..1
                    l[self.uv].uv = ((co.x - lo[0]) / (hi[0] - lo[0] + 1e-9), (co.z - lo[2]) / (hi[2] - lo[2] + 1e-9))
                    continue
                if uv_mode == "top":
                    uv = (co.x, co.y)
                elif ax == 0:
                    uv = (co.y * (1 if n.x > 0 else -1), co.z)
                elif ax == 1:
                    uv = (co.x * (-1 if n.y > 0 else 1), co.z)
                else:
                    uv = (co.x, co.y * (1 if n.z > 0 else -1))
                l[self.uv].uv = (uv[0] * uv_scale, uv[1] * uv_scale)
        src.free()

def bm_box(c, h, bevel=0.0, seg=1):
    """Engine-space centre c and half extents h."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector((2 * h[0], 2 * h[2], 2 * h[1])), verts=bm.verts)
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=seg, affect='EDGES', profile=.5)
    bmesh.ops.translate(bm, vec=E(*c), verts=bm.verts)
    return bm

def bm_cyl(c, r, height, seg=16, r2=None, axis='y', cap=True):
    """Cylinder/cone centred at engine c along engine axis."""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=seg,
                          radius1=r, radius2=r if r2 is None else r2, depth=height)
    for f in bm.faces:
        f.smooth = len(f.verts) == 4
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

def bm_grid(nx, nz, fn, smooth=True, double=False):
    """Height-field sheet: fn(i/nx, j/nz) -> engine (x, y, z)."""
    bm = bmesh.new()
    vs = [[bm.verts.new(E(*fn(i / nx, j / nz))) for j in range(nz + 1)] for i in range(nx + 1)]
    for i in range(nx):
        for j in range(nz):
            f = bm.faces.new((vs[i][j], vs[i][j + 1], vs[i + 1][j + 1], vs[i + 1][j]))
            f.smooth = smooth
    bm.normal_update()
    if double:
        # Thin back face so both sides render with culling enabled.
        geom = bmesh.ops.duplicate(bm, geom=list(bm.faces))
        dup = [g for g in geom["geom"] if isinstance(g, bmesh.types.BMFace)]
        bmesh.ops.reverse_faces(bm, faces=dup)
    return bm

def rotate_bm(bm, yaw, about=(0, 0, 0)):
    """Engine yaw (raylib MatrixRotateY convention) about engine point."""
    # Engine yaw about +Y equals Blender rotation about +Z by the same angle.
    bmesh.ops.rotate(bm, verts=bm.verts, cent=E(*about), matrix=Matrix.Rotation(yaw, 3, 'Z'))
    return bm

def soften(bm, rng, amount):
    for v in bm.verts:
        v.co += Vector(rng.normal(0, amount, 3))
    return bm

def canonical_indices(path):
    """The glTF exporter's index order (and choice among byte-identical
    duplicate vertices) is not stable run to run. Canonicalise both so the GLB
    bytes are reproducible; the rendered geometry is unchanged."""
    import struct
    data = bytearray(open(path, "rb").read())
    n = struct.unpack("<I", data[12:16])[0]
    j = json.loads(data[20:20 + n])
    base = 20 + n + 8
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
            count = attrs[0][2]
            first = {}
            remap = []
            for v in range(count):
                key = b"".join(bytes(data[o + v * w:o + (v + 1) * w]) for o, w, _, _ in attrs)
                remap.append(first.setdefault(key, v))
            o, w, c, t = view(prim["indices"])
            fmt = {5123: "<H", 5125: "<I", 5121: "<B"}[t]; step = struct.calcsize(fmt)
            idx = [remap[struct.unpack_from(fmt, data, o + k * step)[0]] for k in range(c)]
            # Rotate each triangle to start at its smallest index (winding is
            # kept), then sort triangles: draw order of an opaque mesh is moot.
            tris = []
            for k in range(0, c, 3):
                tri = idx[k:k + 3]; r = tri.index(min(tri))
                tris.append(tuple(tri[r:] + tri[:r]))
            flat = [v for tri in sorted(tris) for v in tri]
            for k, v in enumerate(flat):
                struct.pack_into(fmt, data, o + k * step, v)
    open(path, "wb").write(bytes(data))

def export(name, part, meta):
    me = bpy.data.meshes.new(name)
    # Triangulate here with fixed rules: the exporter's own n-gon split is
    # not byte-stable between runs.
    bmesh.ops.triangulate(part.bm, faces=list(part.bm.faces), quad_method='FIXED', ngon_method='EAR_CLIP')
    part.bm.normal_update()
    part.bm.to_mesh(me)
    part.bm.free()
    for m in part.mats:
        me.materials.append(m)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
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
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    lo = [min(b[i] for b in bb) for i in range(3)]; hi = [max(b[i] for b in bb) for i in range(3)]
    # Report engine-space bounds (x, y, z) = (bx, bz, -by).
    meta[name] = {"triangles": tris, "materials": len(part.mats),
                  "min": [round(lo[0], 4), round(lo[2], 4), round(-hi[1], 4)],
                  "max": [round(hi[0], 4), round(hi[2], 4), round(-lo[1], 4)]}
    bpy.data.objects.remove(ob)

# --------------------------------------------------------------- scene
bpy.ops.wm.read_factory_settings(use_empty=True)
# Blender dithers float->8-bit image conversion by default; keep bytes reproducible.
bpy.context.scene.render.dither_intensity = 0.0
rng = np.random.default_rng(1977)
meta = {}

img_quilt_a = to_image("quilt_rose", tex_quilt(101, (104, 40, 44), (206, 150, 96), (70, 92, 60)))
img_quilt_b = to_image("quilt_teal", tex_quilt(202, (50, 92, 96), (214, 128, 64), (140, 120, 70)))
img_wood = to_image("veneer", tex_wood(41))
img_print = to_image("desert_print", tex_print(53))
img_card = to_image("pizza_box", tex_cardboard(61))
img_bag = to_image("paper_bag", tex_cardboard(62, logo=False))
img_beer = to_image("beer_label", tex_label(71))
img_soap = to_image("detergent_label", tex_label(72, band=(220, 120, 30)))
def tex_fabric(seed, tone, weave=64, n=TEX):
    r = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    w = .5 + .5 * np.sin(x * weave * 2 * np.pi) * np.sin(y * weave * 2 * np.pi)
    col = np.ones((n, n, 3)) * srgb(tone)
    col *= (.8 + .12 * w + .2 * periodic_noise(r, n, 6))[..., None]
    return col
img_denim = to_image("denim", tex_fabric(81, (52, 72, 110), 90))
img_tee = to_image("tee", tex_fabric(82, (150, 54, 38), 40))

M_wood = mat("veneer_wood", image=img_wood, rough=.55)
M_wood_dark = mat("dark_wood", lin((70, 46, 30)), rough=.6)
M_vinyl = mat("padded_vinyl", lin((88, 58, 46)), rough=.45)
M_sheet = mat("off_white_sheet", lin((222, 214, 196)), rough=.9)
M_quilt_a = mat("quilt_rose", image=img_quilt_a, rough=.95)
M_quilt_b = mat("quilt_teal", image=img_quilt_b, rough=.95)
M_towel = mat("towel_white", lin((230, 226, 214)), rough=1.)
M_glass_brown = mat("bottle_brown", lin((70, 36, 12)), rough=.15)
M_beer = mat("beer_label", image=img_beer, rough=.5)
M_alu = mat("can_aluminium", lin((180, 40, 36)), rough=.3, metal=.8)
M_chrome = mat("chrome", lin((200, 200, 205)), rough=.15, metal=1.)
M_mirror = mat("mirror_glass", lin((58, 66, 70)), rough=.06, metal=.3)
M_plastic_grey = mat("bin_plastic", lin((94, 98, 92)), rough=.6)
M_paper = mat("crumpled_paper", lin((226, 222, 210)), rough=.95)
M_denim = mat("denim", image=img_denim, rough=1.)
M_shirt = mat("tee_shirt", image=img_tee, rough=1.)
M_shoe = mat("sneaker", lin((60, 60, 64)), rough=.8)
M_sole = mat("sneaker_sole", lin((214, 210, 200)), rough=.8)
M_card = mat("pizza_box", image=img_card, rough=.9)
M_bag = mat("paper_bag", image=img_bag, rough=.95)
M_foam = mat("foam_clamshell", lin((236, 234, 226)), rough=.8)
M_cup = mat("soda_cup", lin((200, 40, 40)), rough=.5)
M_print = mat("desert_print", image=img_print, rough=.8)
M_frame_gold = mat("gilt_frame", lin((150, 116, 60)), rough=.4, metal=.6)
M_curtain = mat("shower_curtain", lin((176, 196, 194)), rough=.6)
M_mat = mat("bath_mat", lin((84, 120, 140)), rough=1.)
M_bell = mat("brass_bell", lin((190, 150, 70)), rough=.25, metal=1.)
M_black = mat("black_plastic", lin((24, 24, 26)), rough=.5)
M_page = mat("ledger_page", lin((230, 224, 200)), rough=.9)
M_ledger = mat("ledger_cover", lin((80, 26, 26)), rough=.6)
M_brochure = mat("brochures", lin((60, 120, 170)), rough=.7)
M_clock_face = mat("clock_face", lin((236, 232, 220)), rough=.6)
M_basket = mat("laundry_basket", lin((70, 110, 160)), rough=.6)
M_soap = mat("detergent_label", image=img_soap, rough=.5)
M_detergent = mat("detergent_bottle", lin((220, 120, 30)), rough=.4)

# ---- 1. bed frame: platform, padded headboard, low footboard.
# Origin = bed-frame instance origin; stays inside x +-0.526, z +-1.068, y 0..1.095.
p = Part()
p.add_bm(bm_box((0, .26, 0), (.51, .15, 1.04), bevel=.012), M_wood, 1.5)            # side rails / box base
for sx in (-.46, .46):
    for sz in (-1.0, 1.0):
        p.add_bm(bm_box((sx, .06, sz), (.035, .06, .035)), M_wood_dark)                  # legs
p.add_bm(bm_box((0, .70, -1.035), (.52, .40, .028), bevel=.01), M_wood, 1.5)          # headboard backer
p.add_bm(bm_box((0, .78, -1.0), (.47, .24, .022), bevel=.02, seg=3), M_vinyl)         # padded panel
for i in range(5):
    p.add_bm(bm_sphere((-.36 + .18 * i, .78, -.976), .012, seg=8, rings=4), M_wood_dark) # buttons
p.add_bm(bm_box((0, .46, 1.04), (.52, .21, .024), bevel=.01), M_wood, 1.5)            # footboard
p.add_bm(bm_box((0, .675, 1.04), (.522, .012, .024), bevel=.004), M_wood_dark)        # footboard cap
export("bed_frame", p, meta)

# ---- 2. bedding: mattress, quilted spread with drape, sheet fold, pillows.
# Origin = mattress instance origin (scale 1); collider top at local y .422.
def bedding(name, quilt, seed, rumple):
    r = np.random.default_rng(seed)
    p = Part()
    p.add_bm(bm_box((0, .18, .02), (.46, .15, .98), bevel=.03, seg=2), M_sheet)   # mattress
    top = .345
    def spread(u, v):
        # u across (0..1), v head->foot (0..1). Flat top, curved side drape.
        x = (u - .5) * 1.0
        z = -.62 + v * 1.66
        edge = max(0.0, abs(x) - .44) / .06
        y = top + .03 - .2 * min(edge, 1.0) ** 1.5
        x = math.copysign(min(abs(x), .47 + .025 * min(edge, 1.0)), x)
        if v > .97:  # fold over the foot
            y -= (v - .97) / .03 * .15
            z = 1.0 + .02
        w = rumple * (math.sin(u * 17 + seed) * math.sin(v * 11 + seed * .3) +
                      .5 * math.sin(u * 5 + v * 9))
        return (x, y + w * (1 - min(edge, 1.0)), z)
    p.add_bm(bm_grid(24, 40, spread), quilt, 2.0, uv_mode="top")
    # Turned-down sheet band across the head end of the spread.
    p.add_bm(bm_box((0, top + .045, -.6), (.47, .012, .06), bevel=.01, seg=2), M_sheet)
    for i, sx in enumerate((-.22, .22)):
        pil = bm_sphere((sx, top + .1, -.76), 1, scale=(.21, .075, .17), seg=16, rings=10)
        soften(pil, r, .006)
        rotate_bm(pil, (r.random() - .5) * .25, (sx, 0, -.76))
        p.add_bm(pil, M_sheet)
    export(name, p, meta)
bedding("bed_bedding_rose", M_quilt_a, 3, .006)
bedding("bed_bedding_teal", M_quilt_b, 7, .01)

# ---- 3. folded towel pair (sits on the spread at the foot).
p = Part()
for i, (dx, dy) in enumerate(((0, 0), (.01, .045))):
    p.add_bm(bm_box((dx, .022 + dy, 0), (.17, .021, .11), bevel=.015, seg=2), M_towel)
export("towel_pair", p, meta)

# ---- 4. beer bottles + crushed can (desk end).
p = Part()
def bottle(x, z, tilt=0.0):
    b = bmesh.new()
    parts = [bm_cyl((x, .085, z), .031, .17, 14), bm_cyl((x, .195, z), .031, .05, 14, r2=.012),
             bm_cyl((x, .245, z), .012, .05, 10)]
    return parts
for x, z in ((0, 0), (.08, .04), (-.05, .07)):
    a, b, c = bottle(x, z)
    p.add_bm(a, M_glass_brown); p.add_bm(b, M_glass_brown); p.add_bm(c, M_glass_brown)
    p.add_bm(bm_cyl((x, .08, z), .0318, .07, 14, cap=False), M_beer, 4.5)
can = bm_cyl((0, 0, 0), .033, .1, 14, axis='x'); soften(can, rng, .004)
bmesh.ops.scale(can, vec=Vector((1, 1, .8)), verts=can.verts)
bmesh.ops.translate(can, vec=E(.02, .028, -.09), verts=can.verts)
p.add_bm(can, M_alu)
export("desk_bottles", p, meta)

# ---- 5. wastebasket with crumpled paper (also reused in bathrooms).
p = Part()
bin_ = bm_cyl((0, .14, 0), .125, .28, 20, r2=.15, cap=False)
inner = bm_cyl((0, .14, 0), .118, .27, 20, r2=.143, cap=False)
bmesh.ops.reverse_faces(inner, faces=list(inner.faces))
p.add_bm(bin_, M_plastic_grey); p.add_bm(inner, M_plastic_grey)
p.add_bm(bm_cyl((0, .006, 0), .125, .012, 20), M_plastic_grey)
for i in range(5):
    a = i * 1.3
    ball = bm_sphere((.06 * math.cos(a), .24 + .02 * (i % 2), .06 * math.sin(a)), .045, seg=8, rings=6)
    soften(ball, rng, .01)
    p.add_bm(ball, M_paper)
export("wastebasket", p, meta)

# ---- 6. floor clothes: jeans, tee, sneakers (flat, < 0.12 m).
p = Part()
def cloth(cx, cz, w, l, yaw, seed, amp=.012):
    r = np.random.default_rng(seed)
    ph = r.random(4) * 6
    def f(u, v):
        x = (u - .5) * w; z = (v - .5) * l
        y = .006 + amp * (1.5 + math.sin(u * 9 + ph[0]) * math.cos(v * 7 + ph[1]) +
                          .6 * math.sin(u * 23 + v * 13 + ph[2]))
        y *= min(1, 4 * min(u, 1 - u, v, 1 - v) + .2)
        return (x, y, z)
    g = bm_grid(10, 12, f, double=True)
    rotate_bm(g, yaw)
    bmesh.ops.translate(g, vec=E(cx, 0, cz), verts=g.verts)
    return g
p.add_bm(cloth(0, 0, .42, .95, .3, 5, .02), M_denim, 3)
p.add_bm(cloth(.18, -.35, .55, .5, -.6, 9, .028), M_shirt, 3)
for i, (sx, sz, yaw) in enumerate(((-.3, .5, .2), (-.18, .58, .5))):
    s = bm_box((0, .045, 0), (.05, .04, .14), bevel=.025, seg=2)
    sole = bm_box((0, .012, 0), (.052, .012, .145), bevel=.008)
    for g in (s, sole):
        rotate_bm(g, yaw); bmesh.ops.translate(g, vec=E(sx, 0, sz), verts=g.verts)
    p.add_bm(s, M_shoe); p.add_bm(sole, M_sole)
export("floor_clothes", p, meta)

# ---- 7. takeout: paper bag, foam clamshell, soda cup with straw.
p = Part()
bag = bm_box((0, .14, 0), (.09, .14, .055))
bmesh.ops.subdivide_edges(bag, edges=list(bag.edges), cuts=3, use_grid_fill=True)
for v in bag.verts:
    h = v.co.z / .28  # Blender Z = engine height
    v.co.x *= 1 - .12 * max(0, h - .75) / .25
    v.co += Vector(rng.normal(0, .004 + .006 * h, 3))
p.add_bm(bag, M_bag, 3)
p.add_bm(bm_box((.2, .035, .02), (.1, .035, .09), bevel=.015, seg=2), M_foam)
p.add_bm(bm_cyl((-.17, .085, .05), .045, .17, 16, r2=.035), M_cup)
p.add_bm(bm_cyl((-.17, .19, .05), .004, .1, 6), M_sole)
export("takeout", p, meta)

# ---- 8. pizza box (closed, slightly bowed lid).
p = Part()
p.add_bm(bm_box((0, .022, 0), (.2, .022, .2), bevel=.003), M_card, 2.5, uv_mode="top")
export("pizza_box", p, meta)

# ---- 9. second framed print (desert sunset), origin = wall contact centre, faces +Z.
p = Part()
p.add_bm(bm_box((0, 0, .012), (.34, .25, .006)), M_print, 1.0, uv_mode="fit")
for (c, h) in (((0, .265, .016), (.37, .018, .016)), ((0, -.265, .016), (.37, .018, .016)),
               ((-.355, 0, .016), (.018, .25, .016)), ((.355, 0, .016), (.018, .25, .016))):
    p.add_bm(bm_box(c, h, bevel=.004), M_frame_gold)
# Remap print UVs to the full image on the front face.
export("wall_print_desert", p, meta)

# ---- 10. bathroom mirror cabinet, origin = wall contact centre, faces +Z.
p = Part()
p.add_bm(bm_box((0, 0, .04), (.25, .34, .04), bevel=.004), M_towel)
p.add_bm(bm_box((0, 0, .0815), (.235, .325, .0015)), M_mirror)
p.add_bm(bm_box((0, -.39, .06), (.24, .008, .06)), M_chrome)                          # glass shelf
p.add_bm(bm_cyl((-.12, -.34, .07), .025, .09, 12), M_plastic_grey)                    # tooth cup
p.add_bm(bm_cyl((.1, -.35, .06), .02, .07, 12), M_detergent)                          # lotion
export("bath_mirror", p, meta)

# ---- 11. shower rod + bunched curtain. Origin = rod midpoint at wall-to-wall
# span along local Z (length 1.80 m). Curtain bunched toward local -Z end.
p = Part()
p.add_bm(bm_cyl((0, 0, 0), .012, 1.80, 12, axis='z'), M_chrome)
for i in range(9):
    p.add_bm(bm_cyl((0, -.02, -.86 + i * .07), .02, .006, 8, r2=.02, axis='z'), M_chrome)
def curtain(u, v):
    z = -.88 + u * .58
    x = .05 * math.sin(u * 2 * math.pi * 6) * (1 - .3 * v)
    y = -.04 - v * 1.25
    return (x + .02 * v, y, z)
p.add_bm(bm_grid(36, 10, curtain, double=True), M_curtain, 1.5)
export("shower_curtain", p, meta)

# ---- 12. towel rail with hanging towel, origin = wall contact, faces +Z.
p = Part()
p.add_bm(bm_cyl((0, 0, .06), .01, .56, 10, axis='x'), M_chrome)
for sx in (-.28, .28):
    p.add_bm(bm_cyl((sx, 0, .03), .014, .06, 10, axis='z'), M_chrome)
def towel(u, v):
    x = (u - .5) * .42
    if v < .5:
        return (x, -v * .9 + .01, .075 + .012 * math.sin(u * 13))
    return (x, -(1 - v) * .9 + .01, .045 + .012 * math.sin(u * 11))
p.add_bm(bm_grid(10, 16, towel, double=True), M_towel)
export("towel_rail", p, meta)

# ---- 13. bath mat.
p = Part()
p.add_bm(bm_box((0, .006, 0), (.3, .006, .22), bevel=.005), M_mat)
export("bath_mat", p, meta)

# ---- 14. reception counter kit: bell, open register + pen, brochure stand.
p = Part()
p.add_bm(bm_cyl((.35, .006, .1), .05, .012, 16), M_black)
bell = bm_sphere((.35, .02, .1), .045, scale=(1, .9, 1), seg=16, rings=8)
bmesh.ops.delete(bell, geom=[v for v in bell.verts if v.co.z < E(0, .02, 0).z - 1e-4], context='VERTS')
p.add_bm(bell, M_bell)
p.add_bm(bm_cyl((.35, .068, .1), .006, .012, 8), M_bell)
for side in (-1, 1):
    pg = bm_box((side * .14, .012, 0), (.135, .01, .2))
    rotate_bm(pg, 0); p.add_bm(pg, M_page)
p.add_bm(bm_box((0, .003, 0), (.29, .003, .21)), M_ledger)
p.add_bm(bm_cyl((.08, .03, .05), .004, .14, 6, axis='x'), M_black)
p.add_bm(bm_box((-.45, .1, -.05), (.1, .1, .04), bevel=.004), M_black)
for i in range(3):
    p.add_bm(bm_box((-.51 + i * .06, .14, -.04), (.025, .07, .03)), M_brochure)
export("counter_kit", p, meta)

# ---- 15. wall clock, origin = wall contact, faces +Z.
p = Part()
p.add_bm(bm_cyl((0, 0, .02), .16, .04, 32, axis='z'), M_black)
p.add_bm(bm_cyl((0, 0, .041), .145, .002, 32, axis='z'), M_clock_face)
for i in range(12):
    a = i * math.pi / 6
    p.add_bm(bm_box((.125 * math.sin(a), .125 * math.cos(a), .043), (.006, .012, .001)), M_black)
hand = bm_box((0, .05, .045), (.004, .05, .001)); rotate_bm(hand, 0); p.add_bm(hand, M_black)
h2 = bm_box((.035, -.02, .046), (.035, .004, .001)); p.add_bm(h2, M_black)
export("wall_clock", p, meta)

# ---- 16. laundry basket of linen, floor.
p = Part()
outer = bm_box((0, .14, 0), (.27, .14, .2), bevel=.04, seg=2)
bmesh.ops.delete(outer, geom=[f for f in outer.faces if f.normal.z > .9], context='FACES')
p.add_bm(outer, M_basket)
inn = bm_box((0, .145, 0), (.25, .135, .18), bevel=.03, seg=2)
bmesh.ops.delete(inn, geom=[f for f in inn.faces if f.normal.z > .9], context='FACES')
bmesh.ops.reverse_faces(inn, faces=list(inn.faces)); p.add_bm(inn, M_basket)
for i in range(4):
    lump = bm_sphere((-.12 + .08 * i, .25 + .02 * (i % 2), -.05 + .04 * (i % 3)), .11, scale=(1, .5, .9), seg=10, rings=6)
    soften(lump, rng, .012); p.add_bm(lump, M_towel if i % 2 else M_sheet)
export("laundry_basket", p, meta)

# ---- 17. detergent jug, sits on washer.
p = Part()
p.add_bm(bm_box((0, .14, 0), (.09, .14, .06), bevel=.02, seg=2), M_soap, 3)
p.add_bm(bm_cyl((.04, .3, 0), .03, .04, 12), M_detergent)
export("detergent", p, meta)

# ---- surface maps (separate PNGs, loaded by environment_art.c).
for name, arr in (("carpet_burgundy_medallion_512", tex_carpet()),
                  ("wallpaper_stripe_damask_512", tex_wallpaper()),
                  ("bath_tile_grout_512", tex_tile()),
                  ("ceiling_popcorn_stained_512", tex_ceiling())):
    img = to_image(name, arr)
    save_png(img, os.path.join(OUT, name + ".png"))

with open(os.path.join(OUT, "build_report.json"), "w") as f:
    json.dump({"recipe": "motel_interior_v1", "blender": bpy.app.version_string, "assets": meta}, f, indent=1, sort_keys=True)
print(json.dumps(meta, indent=1))
