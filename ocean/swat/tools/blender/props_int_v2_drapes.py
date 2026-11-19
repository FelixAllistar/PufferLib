# Briar Court interior props v2: floor-length pinch-pleat drapes stacked
# either side of a guest-room window, hung from a ceiling traverse track.
# Reference: John Margolies, "Fallsview Room 293, Ellenville, New York"
# (LOC LCCN2017713322, public domain).
#
#   blender -b --factory-startup --python-exit-code 1 -P props_int_v2_drapes.py -- OUT_DIR [--render PNG]
#
# Mount: wall piece on the facade, origin on the interior wall plane above the
# window centre at y = 2.5 m (local y = world y - 2.5), front faces local +Z,
# placed with yaw = pi so local +x is world west. The window opening spans
# local x -0.98..+0.98; the wide (west) stack overlaps its frame edge, the
# narrow (east) stack is pushed into the 0.1 m corner by the party wall
# (local x >= -1.08).
# Route clearance (in-game iteration 3, tools/props_interior_clearance.py):
# officers post beside the entrance door 0.3-0.5 m off the facade, so the
# wide stack is kept to local x 0.86..1.06 (+ hem flare) and 0.13 m deep.
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

MOUNT_Y = 2.5
P = dict(
    top=2.74,          # Fabric top under the ceiling track (world y).
    hem=0.025,         # Hem above the carpet.
    folds_w=5, folds_e=3,
    amp=.062,          # Fold depth (half) in the body.
    head=.10,          # Pinch-pleat header height.
    flare=.08,         # Extra stack width at the hem.
    standoff=.06,      # Track centre from the wall.
    hue=(0.90, 0.64, 0.38), shade=(0.58, 0.30, 0.14),   # Gold damask light/dark.
    hue_b=(0.62, 0.62, 0.34), shade_b=(0.30, 0.31, 0.13), # Avocado.
)
reset()
rng = np.random.default_rng(1966)

# Jacquard figure from Poly Haven floral_jacquard (CC0), recoloured: its grey
# luminance drives a two-tone damask. Two colourways (harvest gold for rooms
# 101/103, avocado for 102/104) share the geometry.
src = polyhaven("floral_jacquard", "diff", "1k", size=512)
nrm = polyhaven("floral_jacquard", "nor_gl", "1k", size=512)
w, h = src.size
lum = np.array(src.pixels[:], dtype=np.float32).reshape(h, w, 4)[:, :, :3].mean(axis=2)
lum = (lum - lum.min()) / (lum.max() - lum.min() + 1e-6)
metal = mat("drape_track", (.78, .76, .72), .35, .8)

def damask(name, hue, shade, seed):
    t = np.clip((lum - .15) / .7, 0, 1)[..., None]
    rgb = np.array(shade) * (1 - t) + np.array(hue) * t
    # Sun-fading and grime: a soft low-frequency stain breaks the repeat.
    stain = periodic_noise(np.random.default_rng(seed), w, 3, 3)[..., None]
    rgb *= .88 + .2 * stain
    img = bpy.data.images.new(name, w, h)
    img.pixels.foreach_set(np.dstack([np.clip(rgb, 0, 1), np.ones((h, w))]).astype(np.float32).ravel())
    img.file_format = 'PNG'; img.pack()
    return mat(name, image=img, normal=nrm, normal_strength=.6, rough=.85)

def stack(part, x0, x1, folds, seed, anchor, cloth):
    """One gathered drape stack between local x0..x1, front sheet + back."""
    r = np.random.default_rng(seed)
    nx, ny = folds * 8, 26
    jitter = r.uniform(.75, 1.25, folds + 1)
    bm = bmesh.new(); uvl = bm.loops.layers.uv.new("UVMap")
    top = P["top"] - MOUNT_Y; hem = P["hem"] - MOUNT_Y
    cx = anchor  # The flare spreads away from this x (a corner stack keeps its wall edge).
    verts = []
    arc = np.zeros((nx + 1, ny + 1))
    for j in range(ny + 1):
        v = j / ny                                      # 0 hem .. 1 top
        y = hem + (top - hem) * v
        body = 1 - min(1, max(0, (v - (1 - P["head"] / (top - hem))) / .02))
        flare = 1 + P["flare"] * (1 - v) ** 2 / max(1e-3, (x1 - x0))
        row = []
        for i in range(nx + 1):
            u = i / nx
            k = min(folds, int(u * folds))
            a = P["amp"] * (jitter[k] if body > .5 else .55)
            ph = u * folds * 2 * math.pi + body * .45 * math.sin(2.3 * v * math.pi + 1.7 * k)
            x = cx + (x0 + (x1 - x0) * u - cx) * flare + body * .003 * math.sin(1.9 * v * math.pi + k)
            # Rounded folds: |sin| profile, sharper creases toward the header.
            prof = abs(math.sin(ph / 2)) ** (0.55 if body > .5 else 0.4)
            z = P["standoff"] - .02 + a * prof + .008 * math.sin(3.1 * v * math.pi + k)
            row.append(bm.verts.new(E(x, y, z)))
        verts.append(row)
    for j in range(ny):
        for i in range(nx):
            f = bm.faces.new((verts[j][i], verts[j][i + 1], verts[j + 1][i + 1], verts[j + 1][i]))
            f.smooth = True
            for l, (ii, jj) in zip(f.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
                # Fabric repeat ~0.32 m across the flat width (fold arc ~1.6x).
                l[uvl].uv = (ii / nx * (x1 - x0) * 1.7 / .5, (hem + (top - hem) * jj / ny) / .5)
    bm.normal_update()
    # Front faces must point into the room (+Z local = Blender -Y).
    bm.faces.ensure_lookup_table()
    if bm.faces[0].normal.y > 0:
        bmesh.ops.reverse_faces(bm, faces=list(bm.faces))
    geom = bmesh.ops.duplicate(bm, geom=list(bm.faces))
    bmesh.ops.reverse_faces(bm, faces=[g for g in geom["geom"] if isinstance(g, bmesh.types.BMFace)])
    # Copy UVs onto the duplicated back faces (duplicate keeps loop data).
    part_add_raw(part, bm, cloth)

def part_add_raw(part, bm, m):
    """Append bm to part keeping its own UVs."""
    idx = part.mat_index(m)
    uvl = bm.loops.layers.uv.active
    vmap = {v: part.bm.verts.new(v.co) for v in bm.verts}
    for f in bm.faces:
        nf = part.bm.faces.new([vmap[v] for v in f.verts]); nf.material_index = idx; nf.smooth = f.smooth
        for l, lo in zip(nf.loops, f.loops):
            l[part.uv].uv = lo[uvl].uv
    bm.free()

def drapes(name, cloth):
    part = Part()
    stack(part, 0.86, 1.06, P["folds_w"] - 2, 11, .96, cloth)
    stack(part, -1.07, -0.86, P["folds_e"], 12, -1.07, cloth)
    # Ceiling traverse track with end brackets.
    part.add(bm_box((-.005, P["top"] - MOUNT_Y + .025, P["standoff"]), (1.065, .012, .018), bevel=.003), metal)
    for x in (1.05, -1.06):
        part.add(bm_box((x, P["top"] - MOUNT_Y + .02, P["standoff"] / 2), (.012, .012, P["standoff"] / 2)), metal)
    return export(name, part)

ob_b = drapes("drapes_avocado", damask("drape_avocado", P["hue_b"], P["shade_b"], 1967))
bpy.data.objects.remove(ob_b)
ob = drapes("drapes", damask("drape_gold", P["hue"], P["shade"], 1966))

# Preview: facade wall with a bright window opening, matched to the reference
# (camera ~4.5 m back, eye height, slightly left of the window axis).
if RENDER:
    ob.location = E(0, MOUNT_Y, 0)
    wall = mat("_w", (.62, .55, .45), .9)
    glass = mat("_glass", (1, 1, 1), emission=(1.0, .97, .9))
    ctx = Part()
    ctx.add(bm_box((1.75, 1.4, -.03), (0.75, 1.4, .03)), wall)
    ctx.add(bm_box((-1.45, 1.4, -.03), (0.4, 1.4, .03)), wall)
    ctx.add(bm_box((0, .45, -.03), (1.05, .45, .03)), wall)
    ctx.add(bm_box((0, 2.48, -.03), (1.05, .32, .03)), wall)
    ctx.add(bm_box((0, 1.53, -.06), (1.0, .64, .01)), glass)
    build_object("_ctx", ctx)
    preview({"cam": (0.4, 1.45, 5.2), "target": (0.1, 1.35, 0.0), "fov": 40,
             "room": {"box": (-1.08, 3.0, 0.0, 6.0, 2.8), "walls": ["left", "right", "ceiling"],
                      "wall": (.62, .55, .45), "floor": (.35, .16, .07)},
             "lights": [((0, 1.5, .35), 140, (1, .95, .88), .9), ((-.5, 2.4, 3.5), 40, (1, .9, .78), .5)],
             "ambient": .35, "sky": (.8, .78, .74)})
