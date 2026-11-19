# Briar Court surroundings: presentation-only backdrop, roadside dressing and
# ground decal mask for the motel mission.
#
#   blender -b --factory-startup --python-exit-code 1 \
#     -P ocean/swat/tools/blender/motel_surroundings.py -- [OUT_DIR]
#
# OUT_DIR defaults to ocean/swat/assets/environment/motel_backdrop.
# Everything is generated from code with fixed seeds (no external inputs), so
# reruns with Blender 4.2 reproduce the same files. All content is original and
# dedicated CC0 (see LICENSE_PROVENANCE.txt written next to the outputs).
#
# Outputs (engine metres, +Y up; Blender meshes are authored in engine
# coordinates and converted once by the glTF exporter's +Y-up switch):
#   backdrop.glb        far terrain ring, hills, tree lines, scrub, neighbour
#                       silhouettes (gas station, diner, houses, warehouse,
#                       water tower, billboards) and far utility poles/wires.
#                       Vertex colour x one tiling grain map, one material.
#   near_props.glb      lot curbs, road sidewalk, streetlights, lot pole lights,
#                       near utility poles/wires, hydrant. Same material.
#   road_extension.glb  the highway continued from x=+-56 to the far terrain:
#                       material 0 asphalt (the engine binds the existing CC0
#                       asphalt maps), material 1 painted lines (grain x colour).
#   ground_decals.png   1280x2048 RGBA straight-alpha mask sampled by the
#                       lighting shader on ground-level surfaces: bay paint,
#                       arrows, cracks, sealed cracks, patches, oil, tyre wear,
#                       edge dust. Layout rows are fixed (see DECAL_LAYOUT).
#   (grain.png, a 256x256 tiling near-white grain, is embedded in the GLBs.)
#   manifest.json       counts, bounds and file hashes for review.
#
# The motel ground envelope is x -64..64, z -48..52, flat top y=-0.08. Nothing
# here is added to the simulation: no collision, navigation or perception.
import bpy, bmesh, sys, os, math, json, zlib, struct, hashlib, random
import numpy as np

G = -0.08                      # authoritative ground top
ENV = (-64.0, 64.0, -48.0, 52.0)
ROAD_Z = (36.0, 44.0)

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(here))
import motel_court_spec as C     # Briar Court court layout (stalls, island, gravel zones)
OUT = os.path.abspath(argv[0]) if argv else os.path.normpath(os.path.join(here, "..", "..", "assets", "environment", "motel_backdrop"))
os.makedirs(OUT, exist_ok=True)

# --------------------------------------------------------------------------
# small deterministic helpers
def h2(ix, iz, seed):
    n = (ix * 374761393 + iz * 668265263 + seed * 2147483647) & 0xffffffff
    n = ((n ^ (n >> 13)) * 1274126177) & 0xffffffff
    return ((n ^ (n >> 16)) & 0xffff) / 65535.0

def vnoise(x, z, seed):
    ix, iz = math.floor(x), math.floor(z); fx, fz = x - ix, z - iz
    sx, sz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
    a, b = h2(ix, iz, seed), h2(ix + 1, iz, seed)
    c, d = h2(ix, iz + 1, seed), h2(ix + 1, iz + 1, seed)
    return (a + (b - a) * sx) * (1 - sz) + (c + (d - c) * sx) * sz

def fbm(x, z, seed, octaves=4):
    s, amp, tot = 0.0, 1.0, 0.0
    for o in range(octaves):
        s += vnoise(x, z, seed + o * 17) * amp; tot += amp; x *= 2.03; z *= 2.03; amp *= 0.5
    return s / tot

def smooth(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0); return t * t * (3 - 2 * t)

def env_distance(x, z):
    """Chebyshev distance outside the playable ground envelope (0 inside)."""
    dx = max(ENV[0] - x, 0.0, x - ENV[1]); dz = max(ENV[2] - z, 0.0, z - ENV[3])
    return max(dx, dz)

# --------------------------------------------------------------------------
# PNG writer (rows top-first, as the engine uploads them: row 0 = v 0)
def write_png(path, arr):
    arr = np.ascontiguousarray(arr.astype(np.uint8)); h, w, c = arr.shape
    ctype = {3: 2, 4: 6}[c]
    raw = b"".join(b"\x00" + arr[y].tobytes() for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)

def np_value_noise(shape, cells, rng, tile=True):
    """Tiling bicubic-ish value noise on an HxW grid with `cells` lattice cells."""
    h, w = shape; lat = rng.random((cells, cells))
    ys = np.arange(h) * cells / h; xs = np.arange(w) * cells / w
    y0 = np.floor(ys).astype(int); x0 = np.floor(xs).astype(int)
    fy = ys - y0; fx = xs - x0; fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
    y1 = (y0 + 1) % cells; x1 = (x0 + 1) % cells; y0 %= cells; x0 %= cells
    a = lat[y0][:, x0]; b = lat[y0][:, x1]; c = lat[y1][:, x0]; d = lat[y1][:, x1]
    fx = fx[None, :]; fy = fy[:, None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

# --------------------------------------------------------------------------
# grain texture: tiling, near white, so vertex colours read as authored
def make_grain():
    rng = np.random.default_rng(1101)
    n = np.zeros((256, 256))
    for cells, amp in ((8, .45), (32, .3), (128, .25)):
        n += np_value_noise((256, 256), cells, rng) * amp
    speck = rng.random((256, 256))
    v = 0.86 + 0.14 * n - 0.10 * (speck > 0.985)
    rgb = np.stack([v * 1.0, v * 0.995, v * 0.985], -1)
    write_png(os.path.join(OUT, "grain.png"), np.clip(rgb * 255 + 0.5, 0, 255))

# --------------------------------------------------------------------------
# geometry builder: engine coordinates, per-corner colours and world UVs
class Builder:
    def __init__(self, name): self.name = name; self.v = []; self.f = []; self.col = []; self.smooth = []
    def face(self, pts, color, smooth=False):
        base = len(self.v); self.v.extend(pts)
        self.f.append(tuple(range(base, base + len(pts))))
        cols = color if isinstance(color[0], (tuple, list)) else [color] * len(pts)
        self.col.append([tuple(c) + ((1.0,) if len(c) == 3 else ()) for c in cols]); self.smooth.append(smooth)
    def quad(self, a, b, c, d, color, smooth=False): self.face([a, b, c, d], color, smooth)
    def box(self, cx, y0, cz, sx, sy, sz, color, yaw=0.0, top=None, shrink_top=0.0, bottom=True):
        """Axis box from y0 up sy, centred cx/cz, rotated by yaw (radians about +Y)."""
        hx, hz = sx / 2, sz / 2; tx, tz = hx - shrink_top, hz - shrink_top
        cs, sn = math.cos(yaw), math.sin(yaw)
        def P(x, y, z): return (cx + x * cs + z * sn, y, cz - x * sn + z * cs)
        b = [P(-hx, y0, -hz), P(hx, y0, -hz), P(hx, y0, hz), P(-hx, y0, hz)]
        t = [P(-tx, y0 + sy, -tz), P(tx, y0 + sy, -tz), P(tx, y0 + sy, tz), P(-tx, y0 + sy, tz)]
        tc = top or color
        self.quad(t[3], t[2], t[1], t[0], tc)            # top (+Y), CCW seen from above
        if bottom: self.quad(b[0], b[1], b[2], b[3], color)   # bottom (skipped for things on the ground)
        self.quad(b[0], t[0], t[1], b[1], color)         # -Z side
        self.quad(b[1], t[1], t[2], b[2], color)         # +X
        self.quad(b[2], t[2], t[3], b[3], color)         # +Z
        self.quad(b[3], t[3], t[0], b[0], color)         # -X
    def prism(self, x, y0, z, r0, r1, height, sides, color, cap=True, lean=(0, 0)):
        ring0 = [(x + r0 * math.cos(2 * math.pi * i / sides), y0, z + r0 * math.sin(2 * math.pi * i / sides)) for i in range(sides)]
        ring1 = [(x + lean[0] + r1 * math.cos(2 * math.pi * i / sides), y0 + height, z + lean[1] + r1 * math.sin(2 * math.pi * i / sides)) for i in range(sides)]
        for i in range(sides):
            j = (i + 1) % sides; self.quad(ring0[j], ring0[i], ring1[i], ring1[j], color, True)
        if cap: self.face(ring1[::-1], color)
    def blob(self, x, y, z, rx, ry, rz, color, rng, subdiv=1, jitter=0.18, shade=(0.8, 1.08)):
        """Low-poly icosphere canopy/bush; colour darkens towards the bottom."""
        t = (1 + 5 ** .5) / 2
        verts = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
        faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
                 (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        verts = [tuple(c / math.sqrt(sum(k * k for k in v)) for c in v) for v in verts]
        for _ in range(subdiv):
            cache = {}; nf = []
            def mid(a, b):
                key = (min(a, b), max(a, b))
                if key not in cache:
                    m = [(verts[a][k] + verts[b][k]) / 2 for k in range(3)]; l = math.sqrt(sum(c * c for c in m))
                    verts.append(tuple(c / l for c in m)); cache[key] = len(verts) - 1
                return cache[key]
            for a, b, c in faces:
                ab, bc, ca = mid(a, b), mid(b, c), mid(c, a); nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
            faces = nf
        js = [1 + (rng.random() * 2 - 1) * jitter for _ in verts]
        pts = [(x + v[0] * rx * j, y + v[1] * ry * j, z + v[2] * rz * j) for v, j in zip(verts, js)]
        for a, b, c in faces:
            cols = []
            for k in (a, b, c):
                s = shade[0] + (shade[1] - shade[0]) * (verts[k][1] * 0.5 + 0.5)
                cols.append(tuple(min(1.0, ch * s) for ch in color))
            self.face([pts[a], pts[b], pts[c]], cols, True)
    def wire(self, a, b, sag, width, color, segments=10):
        """Catenary-ish wire as a thin square tube, sagging `sag` at midspan."""
        prev = None
        for i in range(segments + 1):
            t = i / segments
            p = [a[k] + (b[k] - a[k]) * t for k in range(3)]; p[1] -= sag * 4 * t * (1 - t)
            if prev is not None:
                dx, dz = p[0] - prev[0], p[2] - prev[2]; l = math.hypot(dx, dz) or 1
                sx, sz = -dz / l * width / 2, dx / l * width / 2; hw = width / 2
                q0 = [(prev[0] + sx, prev[1] + hw, prev[2] + sz), (prev[0] - sx, prev[1] + hw, prev[2] - sz),
                      (prev[0] - sx, prev[1] - hw, prev[2] - sz), (prev[0] + sx, prev[1] - hw, prev[2] + sz)]
                q1 = [(p[0] + sx, p[1] + hw, p[2] + sz), (p[0] - sx, p[1] + hw, p[2] - sz),
                      (p[0] - sx, p[1] - hw, p[2] - sz), (p[0] + sx, p[1] - hw, p[2] + sz)]
                for k in range(4):
                    m = (k + 1) % 4; self.quad(q0[k], q1[k], q1[m], q0[m], color)
            prev = p
    def count(self): return sum(len(f) - 2 for f in self.f), len(self.v)

def to_object(b, material):
    """Engine (X,Y,Z) -> Blender (X,-Z,Y); the +Y-up exporter maps it back."""
    me = bpy.data.meshes.new(b.name)
    me.from_pydata([(x, -z, y) for x, y, z in b.v], [], b.f); me.update()
    ca = me.color_attributes.new("Col", 'BYTE_COLOR', 'CORNER')
    uv = me.uv_layers.new(name="UVMap")
    cols = [c for fc in b.col for c in fc]
    loops = me.loops; polys = me.polygons
    flat_cols = np.array(cols, dtype=np.float32).reshape(-1)
    ca.data.foreach_set("color", flat_cols)
    # World-projected grain UVs: dominant normal axis, 4 m per tile.
    uvs = np.zeros((len(loops), 2), dtype=np.float32)
    for p in polys:
        n = p.normal; ax = max(range(3), key=lambda k: abs(n[k]))
        for li in p.loop_indices:
            co = me.vertices[loops[li].vertex_index].co
            if ax == 2: u, v = co.x, co.y
            elif ax == 0: u, v = co.y, co.z
            else: u, v = co.x, co.z
            uvs[li] = (u / 4.0, v / 4.0)
    uv.data.foreach_set("uv", uvs.reshape(-1))
    me.polygons.foreach_set("use_smooth", b.smooth)
    me.materials.append(material)
    # Weld coincident corners: smooth canopies/terrain then share vertices and
    # the exporter only splits where normals, colours or UVs really differ.
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(b.name, me); bpy.context.scene.collection.objects.link(ob)
    return ob

def image_material(name, png, roughness, use_vertex_color=True):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = roughness
    img = bpy.data.images.load(png); img.pack()
    tex = nt.nodes.new("ShaderNodeTexImage"); tex.image = img
    if use_vertex_color:
        vc = nt.nodes.new("ShaderNodeVertexColor"); vc.layer_name = "Col"
        mul = nt.nodes.new("ShaderNodeMix"); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'
        mul.inputs["Factor"].default_value = 1.0
        nt.links.new(tex.outputs["Color"], mul.inputs["A"]); nt.links.new(vc.outputs["Color"], mul.inputs["B"])
        nt.links.new(mul.outputs["Result"], bsdf.inputs["Base Color"])
    else:
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return m

def export(path, objects):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects: o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_yup=True,
                              export_vertex_color='ACTIVE', export_normals=True, export_texcoords=True,
                              export_materials='EXPORT', export_image_format='AUTO', export_extras=False,
                              export_cameras=False, export_lights=False, export_animations=False)
    for o in objects: bpy.data.objects.remove(o, do_unlink=True)

# --------------------------------------------------------------------------
# colour palette (display-referred values; the shader decodes them like albedo)
SAND = (0.62, 0.53, 0.39); SAND_DARK = (0.52, 0.44, 0.33); SCRUB = (0.44, 0.42, 0.31)
OLIVE = (0.29, 0.31, 0.19); JUNIPER = (0.20, 0.25, 0.17); COTTON = (0.31, 0.35, 0.20)
BARK = (0.33, 0.27, 0.21); WOOD_POLE = (0.40, 0.33, 0.26); GALV = (0.62, 0.64, 0.64)
CONCRETE = (0.57, 0.56, 0.53); CONCRETE_OLD = (0.50, 0.49, 0.46); DARK = (0.12, 0.13, 0.14)
GLASS = (0.16, 0.22, 0.27); WHITE = (0.80, 0.79, 0.76); RED = (0.66, 0.16, 0.13); YELLOW = (0.86, 0.70, 0.22)
STUCCO = (0.72, 0.66, 0.55); BLUEGREY = (0.45, 0.52, 0.56); RUST = (0.52, 0.33, 0.22); TEAL = (0.20, 0.42, 0.44)

def tint(c, k): return tuple(min(1.0, ch * k) for ch in c)

# --------------------------------------------------------------------------
# far terrain ring
def far_terrain_height(x, z):
    d = env_distance(x, z)
    if d <= 0: return G - 0.005
    if 36.0 < z < 44.0: return G - 0.3      # hidden dip under the highway extension (no depth fight)
    road = smooth(10.0, 28.0, abs(z - 40.0))     # keep the highway corridor level
    und = (fbm(x / 90.0, z / 90.0, 7) - 0.45) * 5.0 + (fbm(x / 23.0, z / 23.0, 9) - 0.5) * 1.1
    return G - 0.005 + max(und, -0.6) * smooth(0.0, 70.0, d) * road

def far_terrain_color(x, z):
    n = fbm(x / 40.0, z / 40.0, 21); m = fbm(x / 9.0, z / 9.0, 23)
    c = [SAND[k] + (SAND_DARK[k] - SAND[k]) * smooth(0.35, 0.7, n) for k in range(3)]
    s = smooth(0.55, 0.8, m) * 0.6
    c = [c[k] + (SCRUB[k] - c[k]) * s for k in range(3)]
    return tuple(c)

def build_far_terrain(b):
    def lines(lo, hi):
        vals = {lo, hi}
        for v in range(-900, 901, 20 if True else 50):
            if (abs(v) <= 200 or v % 50 == 0) and not (lo < v < hi): vals.add(float(v))
        return sorted(v for v in vals if -900 <= v <= 900)
    xs = lines(ENV[0], ENV[1]); zs = lines(ENV[2], ENV[3])
    # z lines also follow the road edges so the extension sits on level ground
    zs = sorted(set(zs) | {ROAD_Z[0] + 16, ROAD_Z[1] + 12, ROAD_Z[0] - 0.05, ROAD_Z[1] + 0.05, 40.0})
    H = {}; C = {}
    for x in xs:
        for z in zs: H[(x, z)] = far_terrain_height(x, z); C[(x, z)] = far_terrain_color(x, z)
    for i in range(len(xs) - 1):
        for j in range(len(zs) - 1):
            x0, x1, z0, z1 = xs[i], xs[i + 1], zs[j], zs[j + 1]
            if x0 >= ENV[0] and x1 <= ENV[1] and z0 >= ENV[2] and z1 <= ENV[3]: continue
            pts = [(x0, H[(x0, z1)], z1), (x1, H[(x1, z1)], z1), (x1, H[(x1, z0)], z0), (x0, H[(x0, z0)], z0)]
            b.quad(*pts, [C[(p[0], p[2])] for p in pts], True)

# --------------------------------------------------------------------------
# hills: two rings, a near low rolling band and a far ridge line
def build_hills(b):
    def ring(r0, r1, segs, seed, hmin, hmax, rings, color_lo, color_hi, gaps=None):
        prof = [(0.0, 0.0), (0.18, 0.35), (0.42, 0.85), (0.6, 1.0), (0.8, 0.75), (1.0, 0.45)]
        grid = []
        for i in range(segs):
            a = 2 * math.pi * i / segs
            hn = fbm(math.cos(a) * 3 + 11, math.sin(a) * 3 + 5, seed, 5)
            h = hmin + (hmax - hmin) * smooth(0.25, 0.8, hn)
            if gaps and gaps(a): h *= 0.25
            rr = []
            for t, k in prof:
                r = r0 + (r1 - r0) * t + (fbm(math.cos(a) * 7, math.sin(a) * 7, seed + 3) - 0.5) * 60 * t
                y = G + h * k * (0.85 + 0.3 * fbm(a * 9, t * 3, seed + 5)) - 0.5
                x, z = math.cos(a) * r, math.sin(a) * r
                rr.append((x, y, z))
            grid.append(rr)
        for i in range(segs):
            j = (i + 1) % segs
            for k in range(len(prof) - 1):
                pts = [grid[i][k], grid[j][k], grid[j][k + 1], grid[i][k + 1]]
                cols = []
                for p in pts:
                    t = smooth(0, hmax, p[1] - G); n = fbm(p[0] / 70, p[2] / 70, seed + 9)
                    c = [color_lo[m] + (color_hi[m] - color_lo[m]) * t for m in range(3)]
                    cols.append(tint(c, 0.9 + 0.2 * n))
                b.quad(pts[0], pts[1], pts[2], pts[3], cols, True)
    # near band: rolling desert hills, parted where the highway runs east/west
    def road_gap(a):
        return abs(math.sin(a)) < 0.10
    ring(300, 430, 96, 41, 6, 22, 6, (0.52, 0.45, 0.34), (0.44, 0.41, 0.31), road_gap)
    # far ridge: mesas and mountains; faces are hazed further by fog
    ring(560, 900, 120, 57, 30, 95, 6, (0.46, 0.41, 0.34), (0.38, 0.37, 0.36))

# --------------------------------------------------------------------------
def tree(b, rng, x, z, kind, scale=1.0):
    y = far_terrain_height(x, z) if env_distance(x, z) > 0 else G
    if kind == 0:   # cottonwood/elm: a lumpy crown of several low-poly masses
        h = (4.5 + rng.random() * 3.5) * scale; r = (2.0 + rng.random() * 1.4) * scale
        b.prism(x, y - 0.2, z, 0.22 * scale, 0.14 * scale, h * 0.6, 5, BARK, cap=False, lean=(rng.uniform(-.4, .4), rng.uniform(-.4, .4)))
        c = tint(COTTON if rng.random() < .6 else OLIVE, 0.85 + rng.random() * 0.3)
        for k in range(4):
            ox, oz, oy = rng.uniform(-.55, .55) * r, rng.uniform(-.55, .55) * r, rng.uniform(-.25, .3) * r
            rr = r * rng.uniform(0.55, 0.8)
            b.blob(x + ox, y + h * 0.66 + oy, z + oz, rr, rr * 0.8, rr, tint(c, rng.uniform(0.88, 1.08)), rng, 0, 0.3, (0.62, 1.05))
    elif kind == 1:  # juniper/cypress: tall narrow cone
        h = (3.5 + rng.random() * 3.0) * scale; r = (1.0 + rng.random() * 0.6) * scale
        b.prism(x, y - 0.2, z, 0.15 * scale, 0.12 * scale, 0.8, 5, BARK, cap=False)
        c = tint(JUNIPER, 0.85 + rng.random() * 0.3)
        b.blob(x, y + h * 0.42, z, r, h * 0.45, r, c, rng, 0, 0.2, (0.62, 1.05))
        b.blob(x, y + h * 0.75, z, r * 0.65, h * 0.3, r * 0.65, tint(c, 1.05), rng, 0, 0.2, (0.62, 1.05))
    else:            # mesquite/scrub bush
        r = (0.7 + rng.random() * 1.1) * scale
        b.blob(x, y + r * 0.3, z, r, r * 0.55, r, tint(OLIVE if rng.random() < .5 else SCRUB, 0.8 + rng.random() * 0.3), rng, 0, 0.3, (0.6, 1.0))

def build_vegetation(trees, scrub):
    rng = random.Random(4242)
    def ok(x, z, margin=4.0):
        if env_distance(x, z) < margin: return False
        if abs(z - 40) < 12 and abs(x) > 50: return False          # highway corridor
        for bx0, bx1, bz0, bz1 in LOTS:
            if bx0 - 3 < x < bx1 + 3 and bz0 - 3 < z < bz1 + 3: return False
        return True
    # tree line behind the motel and along both sides, in clumps
    clumps = []
    for i in range(26):
        clumps.append((-260 + i * 20 + rng.uniform(-6, 6), -78 - rng.uniform(0, 45)))
    for i in range(10):
        clumps.append((rng.choice((-1, 1)) * rng.uniform(85, 150), rng.uniform(-120, 20)))
    for i in range(10):
        clumps.append((rng.uniform(-220, 220), rng.uniform(110, 200)))
    for cx, cz in clumps:
        for k in range(rng.randint(2, 5)):
            x, z = cx + rng.gauss(0, 7), cz + rng.gauss(0, 5)
            if ok(x, z): tree(trees, rng, x, z, 0 if rng.random() < 0.6 else 1)
    # windbreak rows along the far side of the highway and behind the gas station
    for i in range(16):
        x = 120 + i * 9 + rng.uniform(-1, 1)
        if ok(x, 66): tree(trees, rng, x, 66 + rng.uniform(-1, 1), 1, 1.1)
    for i in range(9):
        x = -110 + i * 8; tree(trees, rng, x, 112 + rng.uniform(-1, 1), 0)
    # scattered scrub outside the envelope, denser near it to break the edge
    for i in range(520):
        x = rng.uniform(-340, 340); z = rng.uniform(-260, 300)
        if not ok(x, z, 1.0): continue
        d = env_distance(x, z)
        if rng.random() > 1.0 - min(d, 160) / 220: continue
        tree(scrub, rng, x, z, 2, 1.0 + d / 120)

# --------------------------------------------------------------------------
# neighbour silhouettes. Each lot is outside the playable envelope.
LOTS = [(-100, -52, 60, 98),    # gas station
        (58, 100, 60, 90),      # diner
        (-40, 40, 118, 160),    # houses
        (140, 205, -40, 30),    # warehouse
        (-130, -96, -14, 14),   # closed shop west
        (-245, -215, -175, -145)]  # water tower

def gable_house(b, x, z, w, d, h, wall, roof, yaw=0.0):
    b.box(x, G - 0.1, z, w, h + 0.1, d, wall, yaw)
    cs, sn = math.cos(yaw), math.sin(yaw)
    def P(px, py, pz): return (x + px * cs + pz * sn, G + py, z - px * sn + pz * cs)
    hw, hd, rh = w / 2 + 0.4, d / 2 + 0.4, h + w * 0.28
    a, bb, c, dd = P(-hw, h, -hd), P(hw, h, -hd), P(hw, h, hd), P(-hw, h, hd)
    r0, r1 = P(0, rh, -hd), P(0, rh, hd)
    b.face([dd, r1, r0, a], roof); b.face([r0, r1, c, bb], tint(roof, 0.85))
    b.face([r0, bb, a], wall); b.face([r1, dd, c], wall)
    # door and two windows on the +Z face (towards the road)
    fz = d / 2 + 0.02
    for px, pw, ph, py, col in ((-w * 0.25, 0.9, 2.0, 0, DARK), (w * 0.18, 1.4, 1.1, 1.0, GLASS)):
        q = [P(px - pw / 2, py, fz), P(px + pw / 2, py, fz), P(px + pw / 2, py + ph, fz), P(px - pw / 2, py + ph, fz)]
        b.face(q, col)

def build_neighbours(b):
    rng = random.Random(777)
    # --- gas station (-100..-52, 60..98)
    gx, gz = -76.0, 76.0
    b.box(gx, G - 0.1, gz, 46, 0.13, 36, CONCRETE_OLD)                       # apron
    b.box(gx, 5.0, gz + 4, 24, 0.9, 13, WHITE, top=(0.75, 0.75, 0.74))       # canopy
    b.box(gx, 5.0, gz + 4, 24.06, 0.32, 13.06, RED)                          # fascia stripe
    for cx in (-9, 9):
        for cz in (-4.5, 4.5):
            b.box(gx + cx, G, gz + 4 + cz, 0.45, 5.1, 0.45, WHITE)
    for cx in (-6, 6):                                                        # pump islands
        b.box(gx + cx, G, gz + 4, 1.4, 0.18, 7.5, CONCRETE)
        for cz in (-2, 2):
            b.box(gx + cx, G + 0.18, gz + 4 + cz, 0.75, 1.8, 0.5, RED, top=WHITE)
    b.box(gx, G - 0.1, gz - 11, 16, 4.3, 9, STUCCO)                          # kiosk
    b.box(gx, G + 4.2, gz - 11, 16.4, 0.5, 9.4, RED)
    b.box(gx, G + 0.4, gz - 6.45, 12, 2.2, 0.1, GLASS)                       # storefront glass
    b.box(gx - 21, G, gz + 14, 0.5, 11.5, 0.5, GALV)                         # price sign pole
    b.box(gx - 21, G + 8.5, gz + 14, 3.6, 3.0, 0.5, WHITE)
    b.box(gx - 21, G + 10.7, gz + 14, 3.66, 0.9, 0.56, RED)
    b.box(gx - 21, G + 8.8, gz + 14, 2.8, 1.6, 0.56, DARK)
    # --- diner (58..100, 60..90)
    dx, dz = 79.0, 76.0
    b.box(dx, G - 0.1, dz, 40, 0.12, 28, CONCRETE_OLD)
    b.box(dx, G - 0.1, dz - 4, 26, 4.4, 11, (0.78, 0.80, 0.80))
    b.box(dx, G + 1.0, dz + 1.56, 22, 1.8, 0.1, GLASS)                       # window band
    b.box(dx, G + 3.4, dz - 4, 26.3, 0.35, 11.3, TEAL)                        # trim band
    b.box(dx, G + 4.2, dz - 4, 27, 0.35, 12, (0.55, 0.57, 0.58))              # roof cap
    b.box(dx + 9, G + 4.3, dz - 6, 2.5, 1.2, 2.0, GALV)                      # rooftop unit
    for px in (-6, 6): b.box(dx + px - 8, G + 4.4, dz - 2, 0.25, 3.2, 0.25, GALV)
    b.box(dx - 8, G + 7.4, dz - 2, 14, 2.6, 0.5, (0.85, 0.82, 0.70))          # roof sign
    b.box(dx - 8, G + 7.7, dz - 1.72, 12.5, 2.0, 0.1, RED)
    # --- houses across the highway (-40..40, 118..160)
    walls = [STUCCO, (0.74, 0.70, 0.60), (0.66, 0.70, 0.72), (0.80, 0.76, 0.68), (0.70, 0.64, 0.55)]
    roofs = [(0.36, 0.30, 0.27), (0.42, 0.36, 0.30), (0.30, 0.31, 0.33), (0.50, 0.33, 0.25)]
    for i in range(5):
        x = -32 + i * 16 + rng.uniform(-2, 2); z = 130 + rng.uniform(-4, 6)
        gable_house(b, x, z, rng.uniform(9, 12), rng.uniform(7, 9), rng.uniform(2.7, 3.2), walls[i % 5], roofs[i % 4])
        b.box(x + 7, G, z + 5, 0.15, 1.1, 10, (0.55, 0.47, 0.38))           # side fence
    # --- warehouse (140..205, -40..30)
    wx, wz = 172.0, -5.0
    b.box(wx, G - 0.1, wz, 52, 9.0, 32, BLUEGREY)
    b.box(wx, G + 8.9, wz, 52.6, 0.6, 32.6, (0.40, 0.45, 0.48))
    for k in range(4):
        b.box(wx - 26.05, G, wz - 10 + k * 6.5, 0.1, 4.2, 4.0, (0.36, 0.38, 0.40))
    b.box(wx - 34, G - 0.1, wz, 16, 0.12, 36, CONCRETE_OLD)
    # --- closed roadside shop to the west (-130..-96, -14..14)
    b.box(-113, G - 0.1, 0, 14, 4.0, 10, (0.72, 0.62, 0.50))
    b.box(-113, G + 3.9, 0, 14.6, 0.7, 10.6, RUST)
    b.box(-106.0, G + 0.2, 0, 0.1, 2.4, 6, (0.40, 0.36, 0.30))               # boarded window
    # --- water tower (-245..-215, -175..-145)
    tx, tz = -230.0, -160.0
    for lx, lz in ((-4, -4), (4, -4), (4, 4), (-4, 4)):
        b.prism(tx + lx, G, tz + lz, 0.35, 0.3, 26, 6, GALV, cap=False, lean=(-lx * 0.45, -lz * 0.45))
    b.prism(tx, G + 25.5, tz, 6.5, 6.5, 8.5, 16, (0.78, 0.80, 0.80))
    b.prism(tx, G + 34, tz, 6.5, 0.5, 2.8, 16, (0.70, 0.72, 0.72))
    # --- billboards along the highway, facing it
    for bx, bz, face in ((112, 54, WHITE), (-160, 26, YELLOW), (260, 52, (0.30, 0.55, 0.62))):
        for px in (-4, 4): b.box(bx + px, G, bz, 0.35, 7.5, 0.35, GALV)
        b.box(bx, G + 6.0, bz, 12.4, 4.6, 0.4, (0.25, 0.25, 0.26))
        b.box(bx, G + 6.2, bz + (0.22 if bz < 40 else -0.22), 12.0, 4.2, 0.05, face)
        b.box(bx, G + 6.2, bz + (0.24 if bz < 40 else -0.24), 6.0, 1.4, 0.05, tint(face, 0.45))
    # --- farm buildings behind the tree line (north)
    for fx, fz, w, d, hgt in ((-60, -230, 22, 12, 6.5), (40, -260, 16, 10, 5.0)):
        gable_house(b, fx, fz, w, d, hgt, (0.55, 0.28, 0.22), (0.45, 0.46, 0.47), 0.2)
    b.prism(0, G, -240, 3.5, 3.5, 16, 12, GALV); b.prism(0, G + 16, -240, 3.5, 0.3, 2.5, 12, GALV)
    b.prism(9, G, -244, 3.0, 3.0, 13, 12, tint(GALV, 0.95)); b.prism(9, G + 13, -244, 3.0, 0.3, 2.2, 12, GALV)

# --------------------------------------------------------------------------
# utility poles and wires along the far verge of the highway
POLE_Z = 47.2
def utility_pole(b, x, z, transformer=False):
    b.prism(x, G - 0.3, z, 0.15, 0.11, 11.0, 6, WOOD_POLE, cap=True)
    y = G + 10.1
    b.box(x, y, z, 0.1, 0.12, 2.4, WOOD_POLE)            # crossarm (along Z? no: across the road line)
    for dz in (-1.05, 0.0, 1.05):
        b.prism(x, y + 0.12, z + dz, 0.05, 0.04, 0.16, 5, (0.32, 0.42, 0.40))
    b.box(x, G + 8.6, z, 0.08, 0.08, 0.5, GALV)           # neutral bracket
    if transformer:
        b.prism(x + 0.32, G + 8.0, z, 0.26, 0.26, 0.95, 8, (0.55, 0.58, 0.58))
    return [(x, y + 0.28, z + dz) for dz in (-1.05, 0.0, 1.05)] + [(x, G + 8.6, z + 0.25), (x, G + 6.6, z - 0.18)]

def build_utility(near, far):
    xs = list(range(-880, 881, 40))
    tops = {}
    for x in xs:
        b = near if abs(x) <= 64 else far
        tops[x] = utility_pole(b, float(x), POLE_Z, transformer=(x == 20 or x == -260))
        if abs(x) <= 64: b.box(float(x), G + 6.6, POLE_Z - 0.1, 0.06, 0.06, 0.2, DARK)
    for a, c in zip(xs, xs[1:]):
        if max(abs(a), abs(c)) > 400: continue
        b = near if min(abs(a), abs(c)) <= 64 else far
        for k, (pa, pc) in enumerate(zip(tops[a], tops[c])):
            b.wire(pa, pc, 0.55 + 0.1 * k, 0.03 if k < 3 else 0.022, (0.16, 0.16, 0.16), 8 if max(abs(a), abs(c)) <= 120 else 5)

# --------------------------------------------------------------------------
# roadside dressing inside the envelope: curbs, sidewalk, lights, hydrant
def build_near(b):
    curb = CONCRETE; top = G + 0.07; low = G - 0.2
    def curb_run(x0, z0, x1, z1, w=0.18):
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2; l = math.hypot(x1 - x0, z1 - z0); yaw = math.atan2(-(z1 - z0), x1 - x0)
        # split into 3 m pieces with tiny joints so long runs shade like cast curbs
        n = max(1, int(round(l / 3.0)))
        for i in range(n):
            t = (i + 0.5) / n; px, pz = x0 + (x1 - x0) * t, z0 + (z1 - z0) * t
            b.box(px, low, pz, l / n - 0.012, top - low, w, tint(curb, 0.94 + 0.08 * h2(i, int(x0 + z0), 3)), yaw, shrink_top=0.015, bottom=False)
    # lot perimeter on the sand side of the asphalt edge
    curb_run(-24.09, -24.09, 24.09, -24.09)
    curb_run(-24.09, -24.09, -24.09, 24.09); curb_run(24.09, -24.09, 24.09, 24.09)
    curb_run(-24.09, 24.09, -3.0, 24.09); curb_run(3.0, 24.09, 24.09, 24.09)
    # driveway curbs
    curb_run(-3.09, 24.0, -3.09, 34.5); curb_run(3.09, 24.0, 3.09, 34.5)
    # sidewalk slabs between the sand and the road on the motel side
    for side in (-1, 1):
        x = 3.2
        while x < 56.0:
            w = min(1.5, 56.0 - x)
            c = tint(CONCRETE, 0.95 + 0.07 * h2(int(x * 3), 0, 11 + side))
            b.box(side * (x + w / 2), G - 0.12, 35.3, w - 0.014, 0.145, 1.386, c, shrink_top=0.004, bottom=False)
            x += 1.5
    # cobra-head streetlights on the sidewalk, arms over the highway
    for x in (-44.0, -20.0, 20.0, 44.0):
        z = 34.95
        b.box(x, G - 0.2, z, 0.55, 0.32, 0.55, CONCRETE_OLD)
        b.prism(x, G + 0.1, z, 0.12, 0.075, 8.6, 8, GALV)
        y = G + 8.5
        for i in range(6):  # arm, stepped curve towards +Z
            t0, t1 = i / 6, (i + 1) / 6
            z0, z1 = z + 2.3 * t0, z + 2.3 * t1; y0, y1 = y + 0.5 * math.sin(t0 * 1.6), y + 0.5 * math.sin(t1 * 1.6)
            b.box(x, min(y0, y1) - 0.03, (z0 + z1) / 2, 0.08, abs(y1 - y0) + 0.08, z1 - z0 + 0.02, GALV)
        b.box(x, y + 0.33, z + 2.75, 0.42, 0.18, 0.95, (0.55, 0.57, 0.57), shrink_top=0.08)   # head
        b.box(x, y + 0.31, z + 2.75, 0.30, 0.03, 0.6, (0.88, 0.86, 0.78))                       # lens
    # lot pole lights: steel pole on a concrete pier, shoebox heads facing in
    for x, z, heads in ((-24.55, -24.55, 1), (24.55, -24.55, 1), (-24.55, 24.6, 1), (24.55, 24.6, 1), (-24.55, 0.0, 2), (24.55, -14.0, 2)):
        b.prism(x, G - 0.2, z, 0.24, 0.22, 0.75, 8, CONCRETE_OLD)
        b.box(x, G + 0.55, z, 0.14, 7.3, 0.14, (0.30, 0.32, 0.33))
        dirs = [(-math.copysign(1, x), 0)] if heads == 2 else [(-math.copysign(1, x) * 0.7071, -math.copysign(1, z) * 0.7071)]
        if heads == 2: dirs.append((-math.copysign(1, x), 0)); dirs = [(-math.copysign(1, x), -0.35), (-math.copysign(1, x), 0.35)]
        for dx, dz in dirs:
            l = math.hypot(dx, dz); dx, dz = dx / l, dz / l; yaw = math.atan2(-dz, dx)
            b.box(x + dx * 0.45, G + 7.6, z + dz * 0.45, 0.8, 0.06, 0.06, (0.30, 0.32, 0.33), yaw)
            b.box(x + dx * 1.0, G + 7.45, z + dz * 1.0, 0.75, 0.2, 0.5, (0.28, 0.30, 0.31), yaw)
            b.box(x + dx * 1.0, G + 7.43, z + dz * 1.0, 0.6, 0.03, 0.38, (0.86, 0.85, 0.80), yaw)
    # hydrant at the sidewalk edge
    b.prism(5.6, G + 0.02, 34.9, 0.13, 0.12, 0.62, 8, RED)
    b.prism(5.6, G + 0.64, 34.9, 0.13, 0.04, 0.12, 8, RED)
    b.box(5.6, G + 0.38, 34.9, 0.42, 0.1, 0.1, RED)

# --------------------------------------------------------------------------
# highway extension beyond the authored road (x=+-56) to the far terrain
def build_road_extension():
    asphalt = Builder("road_extension_asphalt"); paint = Builder("road_extension_paint")
    # cross-section strips: (z0, z1, kind); only the centre line breaks for dashes
    strips = [(36.0, 36.45, None), (36.45, 36.55, WHITE), (36.55, 39.94, None), (40.06, 43.45, None),
              (43.45, 43.55, WHITE), (43.55, 44.0, None)]
    for side in (-1, 1):
        # Inside the envelope the stub ramps up to 3.5 cm over the authored soil
        # in its first 0.6 m (no depth fight with the soil at range); past the
        # envelope it eases back down onto the terrain's hidden dip.
        def yof(x):
            pts = [(56.0, 0.004), (56.6, 0.035), (64.0, 0.035), (66.0, 0.0), (1e9, 0.0)]
            for (xa, ya), (xb, yb) in zip(pts, pts[1:]):
                if x <= xb: return G + ya + (yb - ya) * (x - xa) / (xb - xa)
        breaks = [56.0, 56.6, 64.0, 66.0] + list(range(128, 900, 64)) + [900.0]
        def piece(builder, a, c, z0, z1, col):
            p0, p1 = side * a, side * c
            pts = [(p0, yof(a), z1), (p1, yof(c), z1), (p1, yof(c), z0), (p0, yof(a), z0)]
            if side < 0: pts = [pts[1], pts[0], pts[3], pts[2]]
            builder.quad(*pts, col)
        for a, c in zip(breaks, breaks[1:]):
            for z0, z1, kind in strips:
                if kind is None: piece(asphalt, a, c, z0, z1, (1.0, 1.0, 1.0))
                else: piece(paint, a, c, z0, z1, tint(WHITE, 0.92))
        # centre: 3 m yellow dashes / 3 m gaps continuing the authored road's phase (dash 54.5..57.5)
        x = 56.0
        while x < 900.0:
            phase = (x - 54.5) % 6.0
            on = phase < 3.0; nxt = min(900.0, x + ((3.0 - phase) if on else (6.0 - phase)))
            for b_ in (56.6, 64.0, 66.0):
                if x < b_ < nxt: nxt = b_
            if on: piece(paint, x, nxt, 39.94, 40.06, tint(YELLOW, 0.92))
            else: piece(asphalt, x, nxt, 39.94, 40.06, (1.0, 1.0, 1.0))
            x = nxt
    return asphalt, paint

def asphalt_uv_object(b, material):
    ob = to_object(b, material)
    me = ob.data; uv = me.uv_layers["UVMap"]
    uvs = np.zeros((len(me.loops), 2), dtype=np.float32)
    for li, loop in enumerate(me.loops):
        co = me.vertices[loop.vertex_index].co
        uvs[li] = (co.x / 2.1, co.y / 2.1)       # same 2.1 m tile as the authored lot; Blender y = -engine z
    uv.data.foreach_set("uv", uvs.reshape(-1))
    return ob

# --------------------------------------------------------------------------
# ground decal mask (lot + driveway, and both halves of the authored road)
DECAL_W, DECAL_H = 1280, 2048
DECAL_LAYOUT = {
    "lot":   {"rows": [0, 1600],    "x": [-24.0, 24.0], "z": [-24.0, 36.0]},
    "roadW": {"rows": [1600, 1824], "x": [-56.0, 0.0],  "z": [36.0, 44.0]},
    "roadE": {"rows": [1824, 2048], "x": [0.0, 56.0],   "z": [36.0, 44.0]},
}

class Canvas:
    def __init__(self):
        self.rgb = np.zeros((DECAL_H, DECAL_W, 3), np.float32); self.rgb[:] = 0.3
        self.a = np.zeros((DECAL_H, DECAL_W), np.float32)
        self.regions = []
        for name, r in DECAL_LAYOUT.items():
            r0, r1 = r["rows"]; xs = r["x"]; zs = r["z"]
            px = (xs[1] - xs[0]) / DECAL_W; pz = (zs[1] - zs[0]) / (r1 - r0)
            X = xs[0] + (np.arange(DECAL_W) + 0.5) * px; Z = zs[0] + (np.arange(r1 - r0) + 0.5) * pz
            self.regions.append((r0, r1, xs, zs, X, Z, px, pz))
    def paint(self, bbox, fn, color, alpha=1.0):
        """fn(X[None,:], Z[:,None]) -> coverage 0..1 over the world bbox (x0,x1,z0,z1)."""
        for r0, r1, xs, zs, X, Z, px, pz in self.regions:
            x0, x1, z0, z1 = bbox
            if x1 < xs[0] or x0 > xs[1] or z1 < zs[0] or z0 > zs[1]: continue
            i0 = max(0, int((x0 - xs[0]) / px) - 1); i1 = min(DECAL_W, int((x1 - xs[0]) / px) + 2)
            j0 = max(0, int((z0 - zs[0]) / pz) - 1); j1 = min(r1 - r0, int((z1 - zs[0]) / pz) + 2)
            if i1 <= i0 or j1 <= j0: continue
            Xg, Zg = np.broadcast_arrays(X[None, i0:i1], Z[j0:j1, None])
            m = np.clip(fn(Xg, Zg, px), 0, 1) * alpha
            if np.ndim(m) == 0: continue
            sl = (slice(r0 + j0, r0 + j1), slice(i0, i1))
            a = self.a[sl]; out = m + a * (1 - m)
            col = np.asarray(color, np.float32)
            num = col * m[..., None] + self.rgb[sl] * (a * (1 - m))[..., None]
            self.rgb[sl] = np.where(out[..., None] > 1e-5, num / np.maximum(out, 1e-5)[..., None], self.rgb[sl])
            self.a[sl] = out
    # primitives (all world-space, antialiased over one texel)
    def segment(self, a, b, width, color, alpha=1.0):
        ax, az = a; bx, bz = b; r = width / 2
        def fn(X, Z, px):
            dx, dz = bx - ax, bz - az; l2 = dx * dx + dz * dz or 1e-9
            t = np.clip(((X - ax) * dx + (Z - az) * dz) / l2, 0, 1)
            d = np.hypot(X - ax - t * dx, Z - az - t * dz)
            return np.clip((r - d) / (1.5 * px) + 0.5, 0, 1)
        self.paint((min(ax, bx) - r - .1, max(ax, bx) + r + .1, min(az, bz) - r - .1, max(az, bz) + r + .1), fn, color, alpha)
    def rect(self, x0, x1, z0, z1, color, alpha=1.0, noise=None):
        def fn(X, Z, px):
            m = np.clip(np.minimum.reduce([X - x0, x1 - X, Z - z0, z1 - Z]) / px + 0.5, 0, 1)
            return m * (noise(X, Z) if noise else 1.0)
        self.paint((x0, x1, z0, z1), fn, color, alpha)
    def polygon(self, pts, color, alpha=1.0):
        xs = [p[0] for p in pts]; zs = [p[1] for p in pts]
        def fn(X, Z, px):
            inside = np.zeros(np.broadcast(X, Z).shape, bool); n = len(pts)
            for i in range(n):
                x1, z1 = pts[i]; x2, z2 = pts[(i + 1) % n]
                cond = ((z1 > Z) != (z2 > Z)) & (X < (x2 - x1) * (Z - z1) / ((z2 - z1) or 1e-9) + x1)
                inside ^= cond
            return inside.astype(np.float32)
        self.paint((min(xs), max(xs), min(zs), max(zs)), fn, color, alpha)
    def blob(self, cx, cz, r, color, alpha, seed, squash=1.0):
        def fn(X, Z, px):
            ang = np.arctan2(Z - cz, X - cx); d = np.hypot((X - cx), (Z - cz) / squash)
            wob = 1 + 0.28 * np.sin(ang * 3 + seed) + 0.16 * np.sin(ang * 7 + seed * 2.3) + 0.08 * np.sin(ang * 13 + seed * 5.1)
            core = np.clip(1 - d / (r * wob), 0, 1)
            return np.clip(core * 2.2, 0, 1) ** 0.8 * (0.6 + 0.4 * core)
        self.paint((cx - r * 1.6, cx + r * 1.6, cz - r * 1.6 * squash, cz + r * 1.6 * squash), fn, color, alpha)

def crack(cv, rng, x, z, length, ang, width=0.06, color=(0.07, 0.07, 0.07), alpha=0.55, branch=True, bounds=None):
    step = 0.22; n = max(2, int(length / step)); p = (x, z)
    for i in range(n):
        ang += rng.gauss(0, 0.32); q = (p[0] + math.cos(ang) * step, p[1] + math.sin(ang) * step)
        if bounds and not (bounds[0] < q[0] < bounds[1] and bounds[2] < q[1] < bounds[3]): break
        w = width * (0.55 + 0.45 * math.sin(math.pi * i / n))
        cv.segment(p, q, w, color, alpha); p = q
        if branch and rng.random() < 0.06 and length > 2:
            crack(cv, rng, p[0], p[1], length * rng.uniform(0.2, 0.45), ang + rng.choice((-1, 1)) * rng.uniform(0.6, 1.3),
                  width * 0.7, color, alpha * 0.9, False, bounds)

def alligator(cv, rng, cx, cz, r, alpha=0.55):
    pts = [(cx + rng.uniform(-r, r), cz + rng.uniform(-r, r)) for _ in range(int(r * r * 5))]
    for i, a in enumerate(pts):
        near = sorted(pts, key=lambda p: (p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2)[1:4]
        for b in near:
            if math.hypot(b[0] - cx, b[1] - cz) > r: continue
            cv.segment(a, b, 0.05, (0.08, 0.08, 0.08), alpha * 0.8 * (1 - math.hypot(a[0] - cx, a[1] - cz) / (r * 1.3)))

def build_decals():
    cv = Canvas(); rng = random.Random(9090); nrng = np.random.default_rng(9091)
    LOT = (-24.0, 24.0, -24.0, 24.0)
    # low-frequency wear: sun-bleached and darker zones (breaks up the tiled asphalt)
    def wear(X, Z, px):
        n = np.sin(X * 0.19 + 1.3) * np.sin(Z * 0.23 + 0.4) + 0.6 * np.sin(X * 0.071 - Z * 0.093 + 2.0) + 0.35 * np.sin(X * 0.53 + Z * 0.41)
        return np.clip(n * 0.5, 0, 1)
    cv.paint((-24, 24, -24, 36), wear, (0.52, 0.51, 0.49), 0.20)
    def wear_dark(X, Z, px):
        n = np.sin(X * 0.13 - 0.7) * np.sin(Z * 0.17 + 2.1) + 0.5 * np.sin(X * 0.31 + Z * 0.27 + 1.0)
        return np.clip(n * 0.6 - 0.1, 0, 1)
    cv.paint((-24, 24, -24, 36), wear_dark, (0.13, 0.13, 0.14), 0.22)
    cv.paint((-56, 56, 36, 44), wear, (0.50, 0.49, 0.47), 0.16)
    # tyre-polished lanes: driveway into the aisle, and the highway wheel paths
    for x0 in (-1.6, 1.6):
        cv.paint((x0 - 1, x0 + 1, 7, 36), lambda X, Z, px, x0=x0: np.clip(1 - np.abs(X - x0) / 0.8, 0, 1) * 0.8, (0.11, 0.11, 0.12), 0.35)
    # court aisles (tools/motel_court_spec.py): the cross aisle and the two side aisles
    za = (C.WALK + C.STALL_D, C.WALK + C.STALL_D + C.AISLE)
    cv.paint((-9, 9, za[0], za[1]), lambda X, Z, px: np.clip(1 - np.abs(Z - (za[0] + za[1]) / 2) / 1.6, 0, 1) * np.clip((8.6 - np.abs(X)) / 3, 0, 1), (0.12, 0.12, 0.13), 0.28)
    for xc in (-4.9, 4.9):
        cv.paint((xc - 2, xc + 2, za[0], 24), lambda X, Z, px, xc=xc: np.clip(1 - np.abs(X - xc) / 1.4, 0, 1), (0.12, 0.12, 0.13), 0.24)
    for zc in (37.6, 39.0, 41.0, 42.4):
        cv.paint((-56, 56, zc - 0.6, zc + 0.6), lambda X, Z, px, zc=zc: np.clip(1 - np.abs(Z - zc) / 0.5, 0, 1), (0.10, 0.10, 0.11), 0.30)
    # edge dust: sand blown onto the asphalt along every edge facing open ground
    dust = (0.52, 0.47, 0.38)
    def edge(X, Z, px):
        d = np.minimum.reduce([X + 24, 24 - X, Z + 24, np.where(np.abs(X) < 3, 99, 24 - Z)])
        dd = np.where((np.abs(X) < 3.0) & (Z > 23), np.minimum(np.abs(X + 3), np.abs(3 - X)), d)
        noise = 0.55 + 0.45 * np.sin(X * 2.3 + Z * 1.7) * np.sin(X * 0.9 - Z * 3.1)
        return np.clip(1 - dd / 1.4, 0, 1) ** 1.5 * noise
    cv.paint((-24, 24, -24, 36), edge, dust, 0.5)
    def road_edge(X, Z, px):
        d = np.minimum(Z - 36, 44 - Z)
        noise = 0.5 + 0.5 * np.sin(X * 1.9 + Z * 2.7) * np.sin(X * 0.6 - Z * 1.3)
        return np.clip(1 - d / 0.9, 0, 1) ** 1.4 * noise
    cv.paint((-56, 56, 36, 44), road_edge, dust, 0.42)
    # patches: darker newer asphalt, slightly rotated rectangles
    patches = [(-18, -15, 5, 3, 0.1), (14, 12, 4, 2.5, -0.05), (-6, 20, 3, 6, 0.0), (19, -19, 2.5, 2.5, 0.2), (-21, 2, 2, 4, 0.0),
               (0.0, 30.0, 2.2, 3.2, 0.0), (-31, 39.5, 5, 2.6, 0.0), (22, 41.5, 3.5, 2.4, 0.0), (44, 38.2, 2.8, 2.0, 0.08), (-47, 42.0, 2.2, 1.6, 0.0)]
    for cx, cz, w, d, yaw in patches:
        cs, sn = math.cos(yaw), math.sin(yaw)
        pts = [(cx + px * cs - pz * sn, cz + px * sn + pz * cs) for px, pz in ((-w / 2, -d / 2), (w / 2, -d / 2), (w / 2, d / 2), (-w / 2, d / 2))]
        cv.polygon(pts, (0.15, 0.15, 0.16), 0.42)
        for i in range(4):  # sealed seam around the patch
            cv.segment(pts[i], pts[(i + 1) % 4], 0.05, (0.05, 0.05, 0.05), 0.75)
    # parking paint (worn white): 2.7 x 5.5 m stalls from tools/motel_court_spec.py
    paint = (0.80, 0.79, 0.75)
    def stripe(a, b, w=0.11, alpha=0.86):
        n = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) / 0.5))
        for i in range(n):  # segment-wise alpha for scuffed paint
            t0, t1 = i / n, (i + 1) / n
            p0 = (a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0); p1 = (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1)
            cv.segment(p0, p1, w, paint, alpha * (0.62 + 0.38 * rng.random()))
    for a, b in C.stall_lines(): stripe(a, b)
    # accessible stall: the rear row's east end, by Room 104 (blue field, white symbol)
    ax0, ax1, az0, az1 = C.rear_stalls()[-1]; acx = (ax0 + ax1) / 2
    cv.rect(ax0 + 0.08, ax1 - 0.08, az0 + 0.1, az1 - 0.1, (0.17, 0.29, 0.48), 0.42)
    cv.blob(acx, 4.6, 0.12, paint, 0.85, 1.0)
    cv.segment((acx, 4.35), (acx - 0.1, 3.75), 0.11, paint, 0.85)
    cv.segment((acx - 0.1, 3.75), (acx + 0.4, 3.65), 0.1, paint, 0.85)
    cv.segment((acx + 0.4, 3.65), (acx + 0.55, 3.2), 0.1, paint, 0.85)
    def ring(X, Z, px):
        d = np.hypot(X - (acx - 0.15), Z - 3.55); return np.clip((0.06 - np.abs(d - 0.42)) / px + 0.5, 0, 1) * (Z > 3.45)
    cv.paint((acx - 0.7, acx + 0.4, 3.4, 4.1), ring, paint, 0.85)
    # central concrete island (painted kerb, weathered concrete, yellow noses)
    ix0, ix1, iz0, iz1 = C.ISLAND
    cv.rect(ix0, ix1, iz0, iz1, (0.60, 0.58, 0.54), 0.92, noise=lambda X, Z: 0.85 + 0.15 * np.sin(X * 7.1) * np.sin(Z * 5.3))
    for zc in (iz0, iz1):
        cv.rect(ix0, ix1, zc - 0.35, zc + 0.35, (0.82, 0.68, 0.20), 0.75)
    for (x0, z0), (x1, z1) in (((ix0, iz0), (ix0, iz1)), ((ix1, iz0), (ix1, iz1)), ((ix0, iz0), (ix1, iz0)), ((ix0, iz1), (ix1, iz1))):
        cv.segment((x0, z0), (x1, z1), 0.09, (0.86, 0.85, 0.80), 0.8)
    # corner planters and the gravel service yard / strips behind the wings
    def gravel(seed):
        def fn(X, Z):
            n = 0.78 + 0.22 * np.sin(X * 3.7 + seed) * np.sin(Z * 4.1 - seed) + 0.1 * np.sin(X * 11.3 + Z * 9.7)
            return np.clip(n, 0, 1)
        return fn
    for k, (x0, x1, z0, z1) in enumerate(C.GRAVEL_ZONES):
        cv.rect(x0, x1, z0, z1, (0.56, 0.50, 0.40), 0.88, noise=gravel(k))
        for i in range(int((x1 - x0) * (z1 - z0) / 6)):   # pebbles and tyre ruts
            cv.blob(rng.uniform(x0, x1), rng.uniform(z0, z1), rng.uniform(0.05, 0.16), (0.40, 0.36, 0.30), 0.5, i)
    for k, (x0, x1, z0, z1) in enumerate(C.PLANTERS):
        cv.rect(x0, x1, z0, z1, (0.50, 0.43, 0.33), 0.95, noise=gravel(10 + k))
        for (a, b) in (((x0, z0), (x1, z0)), ((x1, z0), (x1, z1)), ((x1, z1), (x0, z1)), ((x0, z1), (x0, z0))):
            cv.segment(a, b, 0.16, (0.78, 0.77, 0.73), 0.9)   # painted concrete kerb
    # direction arrows at the driveway and the stop bar at the highway
    def arrow(x, z, dz, alpha=0.8):
        s = 1 if dz > 0 else -1
        cv.rect(x - 0.1, x + 0.1, min(z, z + s * 2.2), max(z, z + s * 2.2), paint, alpha)
        tip = z + s * 3.1; base = z + s * 2.1
        cv.polygon([(x - 0.45, base), (x + 0.45, base), (x, tip)], paint, alpha)
    arrow(-1.5, 30.5, -1); arrow(1.5, 27.0, 1)
    cv.rect(0.05, 2.9, 34.9, 35.35, paint, 0.82)
    # continue the authored road lines from |x| 52 to 56 (authored lines stop at 52)
    for s in (-1, 1):
        cv.rect(min(s * 52, s * 56), max(s * 52, s * 56), 36.45, 36.55, paint, 0.9)
        cv.rect(min(s * 52, s * 56), max(s * 52, s * 56), 43.45, 43.55, paint, 0.9)
        cv.rect(min(s * 54.5, s * 56), max(s * 54.5, s * 56), 39.94, 40.06, (0.86, 0.70, 0.22), 0.9)
    # oil stains in bays, and drips along the aisle
    bays = [((x0 + x1) / 2, (z0 + z1) / 2) for (x0, x1, z0, z1) in C.stalls()]
    for i, (x, z) in enumerate(bays):
        if rng.random() < 0.25: continue
        cv.blob(x + rng.uniform(-0.3, 0.3), z + rng.uniform(-0.6, 0.6), rng.uniform(0.25, 0.65), (0.05, 0.045, 0.04), rng.uniform(0.35, 0.6), rng.uniform(0, 6), rng.uniform(0.7, 1.4))
        if rng.random() < 0.5:
            cv.blob(x + rng.uniform(-0.6, 0.6), z + rng.uniform(-1.0, 1.0), rng.uniform(0.1, 0.22), (0.04, 0.04, 0.035), 0.55, rng.uniform(0, 6))
    for i in range(18):
        cv.blob(rng.uniform(-3, 3), rng.uniform(8, 34), rng.uniform(0.06, 0.18), (0.05, 0.05, 0.045), 0.45, i)
    for i in range(30):
        cv.blob(rng.uniform(-55, 55), rng.uniform(37.2, 42.8), rng.uniform(0.08, 0.3), (0.05, 0.05, 0.045), 0.35, i * 1.7, 1.6)
    # cracks: long ones from the lot edges inwards, random interior cracks, and tar-sealed meanders
    for i in range(46):
        side = rng.randrange(4); t = rng.uniform(-23, 23)
        x, z, ang = [(t, -23.9, math.pi / 2), (t, 23.9, -math.pi / 2), (-23.9, t, 0.0), (23.9, t, math.pi)][side]
        if side == 1 and abs(x) < 3.5: continue
        crack(cv, rng, x, z, rng.uniform(1.0, 6.0), ang + rng.gauss(0, 0.5), bounds=LOT)
    for i in range(38):
        crack(cv, rng, rng.uniform(-22, 22), rng.uniform(-22, 22), rng.uniform(0.8, 4.5), rng.uniform(0, 2 * math.pi), 0.05, alpha=0.5, bounds=LOT)
    for i in range(7):
        crack(cv, rng, rng.uniform(-20, 20), rng.uniform(-20, 20), rng.uniform(5, 14), rng.uniform(0, 2 * math.pi), 0.07, (0.03, 0.03, 0.03), 0.62, False, LOT)
    for cx, cz, r in ((-19, -20, 2.4), (20, 21, 2.0), (-12, 22, 1.6), (1.0, 33.0, 1.3)):
        alligator(cv, rng, cx, cz, r)
    ROAD = (-56, 56, 36, 44)
    for i in range(44):
        side = rng.randrange(2); x = rng.uniform(-55, 55)
        crack(cv, rng, x, 36.1 if side == 0 else 43.9, rng.uniform(0.6, 3.0), (math.pi / 2 if side == 0 else -math.pi / 2) + rng.gauss(0, 0.6), bounds=ROAD)
    for i in range(12):
        crack(cv, rng, rng.uniform(-54, 54), rng.uniform(37, 43), rng.uniform(3, 10), rng.gauss(0, 0.25) + rng.choice((0, math.pi)), 0.065, (0.03, 0.03, 0.03), 0.6, False, ROAD)
    for i in range(10):  # transverse cracks across the lanes
        x = rng.uniform(-54, 54); crack(cv, rng, x, 36.2, 7.6, math.pi / 2 + rng.gauss(0, 0.08), 0.05, alpha=0.5, branch=False, bounds=ROAD)
    for cx, cz, r in ((-36, 42.3, 1.2), (30, 37.5, 1.0)):
        alligator(cv, rng, cx, cz, r)
    # Only asphalt receives marks: clear the sand either side of the driveway.
    r0, r1, xs, zs, X, Z, px, pz = cv.regions[0]
    Xg, Zg = np.broadcast_arrays(X[None, :], Z[:, None])
    asphalt = (Zg <= 24.0) | (np.abs(Xg) <= 3.0)
    cv.a[r0:r1] *= asphalt
    out = np.concatenate([np.clip(cv.rgb, 0, 1), cv.a[..., None]], -1)
    write_png(os.path.join(OUT, "ground_decals.png"), np.round(out * 255))
    return float(cv.a.mean())

# --------------------------------------------------------------------------
def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    make_grain()
    grain = image_material("backdrop_grain", os.path.join(OUT, "grain.png"), 0.92)
    stats = {}
    # backdrop
    terrain = Builder("backdrop_terrain"); build_far_terrain(terrain)
    hills = Builder("backdrop_hills"); build_hills(hills)
    trees = Builder("backdrop_trees"); scrub = Builder("backdrop_scrub"); build_vegetation(trees, scrub)
    neigh = Builder("backdrop_neighbours"); build_neighbours(neigh)
    near = Builder("near_props"); far_util = Builder("backdrop_utility"); build_utility(near, far_util)
    build_near(near)
    parts = [terrain, hills, trees, scrub, neigh, far_util]
    for b in parts + [near]:
        tris, verts = b.count(); stats[b.name] = {"triangles": tris, "corners": verts}
    export(os.path.join(OUT, "backdrop.glb"), [to_object(b, grain) for b in parts])
    export(os.path.join(OUT, "near_props.glb"), [to_object(near, grain)])
    # road extension: asphalt strips use a 4x4 placeholder albedo; the engine binds the CC0 asphalt maps
    proxy = os.path.join(OUT, "asphalt_proxy.png"); write_png(proxy, np.full((4, 4, 3), 92))
    asphalt_mat = image_material("road_extension_asphalt", proxy, 0.85, use_vertex_color=False)
    road, paint = build_road_extension()
    for b in (road, paint):
        tris, verts = b.count(); stats[b.name] = {"triangles": tris, "corners": verts}
    export(os.path.join(OUT, "road_extension.glb"), [asphalt_uv_object(road, asphalt_mat), to_object(paint, grain)])
    os.remove(proxy)
    coverage = build_decals()
    for f in ("backdrop.glb", "near_props.glb", "road_extension.glb"):
        data = open(os.path.join(OUT, f), "rb").read(); n = struct.unpack("<I", data[12:16])[0]; j = json.loads(data[20:20 + n])
        for m in j["meshes"]:
            for prim in m["primitives"]:
                count = j["accessors"][prim["attributes"]["POSITION"]]["count"]
                assert count < 65536, (f, m.get("name"), count)   # Raylib 5.5 draws with 16-bit indices
                stats.setdefault(m.get("name"), {})["exported_vertices"] = count
    files = {}
    os.remove(os.path.join(OUT, "grain.png"))   # embedded in the GLBs; not a separate runtime file
    for f in ("backdrop.glb", "near_props.glb", "road_extension.glb", "ground_decals.png"):
        p = os.path.join(OUT, f); files[f] = {"bytes": os.path.getsize(p), "sha256": hashlib.sha256(open(p, "rb").read()).hexdigest()}
    manifest = {"generator": "ocean/swat/tools/blender/motel_surroundings.py", "blender": bpy.app.version_string,
                "license": "CC0-1.0 (original procedural work; no external sources)", "units": "metres", "up": "+Y",
                "ground_top_y": G, "playable_envelope_xz": ENV, "presentation_only": True,
                "collision": False, "navigation": False, "perception": False,
                "decal_layout": DECAL_LAYOUT, "decal_size": [DECAL_W, DECAL_H], "decal_mean_alpha": round(coverage, 4),
                "meshes": stats, "files": files}
    json.dump(manifest, open(os.path.join(OUT, "manifest.json"), "w"), indent=1, sort_keys=True)
    print(json.dumps(stats, indent=1))

main()
