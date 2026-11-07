# Blender (4.2) headless converter: animated glTF dinosaur -> Cretaceous .dino
#   blender -b --factory-startup --disable-autoexec --python dino_convert.py -- \
#       IN.glb SPECIES OUTDIR --length 3.0 [--lod0 24000] [--tex 2048] \
#       [--map idle=Idle_01,walk=Walk,...] [--skip-mat NAME,...]
#
# The glTF is parsed directly (nodes, skin, animations, accessors), so the
# skeleton and clips are exactly the file's; Blender is used only to decimate
# the LOD meshes and to read and resize images.
#
# Model space: Y up, the animal faces +x, feet on y = 0 in the idle pose, the
# origin between the feet, scaled so nose-to-tail length is --length metres.
# Clips are resampled to 30 Hz as local joint TRS; root motion is removed and
# reported as the clip's ground speed (for in-place clips, the speed is
# measured from the planted feet), so playback rate can follow the sim.
#
# .dino layout (little endian):
#   u32 magic 'CDNO', u32 version (1)
#   f32 length, f32 height, f32 bmin[3], f32 bmax[3]      (idle pose bounds)
#   u32 njoint, per joint: char name[32], i32 parent, f32 inv_bind[16] (column major),
#                          f32 rest[8] (T xyz, R xyzw, uniform S)
#   u32 nmat,   per material: char name[32] (texture prefix), u32 flags (1 double sided)
#   u32 nlod,   per LOD: u32 nvert, u32 nidx, u32 nsub, nsub x {u32 mat, u32 first, u32 count},
#               nvert x {f32 pos[3], f32 uv[2], i8 nrm[4], i8 tan[4] (w bitangent sign),
#                        u8 joint[4], u8 weight[4]}, nidx x u32
#   u32 nclip,  per clip: char name[32], f32 duration, f32 speed (m/s), u32 flags (1 loop),
#               u32 nframes, nframes x njoint x f32[8] (local T, R, S)
# Textures (written next to the .dino): SPECIES_MAT_diff.jpg (sRGB albedo),
# SPECIES_MAT_nor.jpg (OpenGL normal), SPECIES_MAT_arm.jpg (R ao, G roughness, B metal).
import bpy, sys, os, json, struct, math, tempfile
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, SPECIES, OUT = argv[0], argv[1], argv[2]
opt = {"--length": "3.0", "--lod0": "24000", "--tex": "2048", "--map": "", "--skip-mat": "", "--lods": "1.0,0.3,0.08"}
i = 3
while i < len(argv):
    opt[argv[i]] = argv[i + 1]; i += 2
LENGTH, LOD0, TEX = float(opt["--length"]), int(opt["--lod0"]), int(opt["--tex"])
LOD_FRAC = [float(x) for x in opt["--lods"].split(",")]
CLIPMAP = {}
for kv in filter(None, opt["--map"].split(",")):
    k, v = kv.split("="); CLIPMAP[v] = k
SKIP_MAT = set(filter(None, opt["--skip-mat"].split(",")))
SPEED_OVERRIDE = {kv.split("=")[0]: float(kv.split("=")[1]) for kv in filter(None, opt.get("--speed", "").split(","))}
HZ = 30.0
os.makedirs(OUT, exist_ok=True)

# ---------------------------------------------------------------- glTF parsing
def load_glb(path):
    data = open(path, "rb").read()
    magic, ver, total = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67, "not a GLB"
    off, js, binc = 12, None, None
    while off < min(total, len(data)):
        cl, ct = struct.unpack_from("<II", data, off); off += 8
        chunk = data[off:off + cl]; off += cl
        if ct == 0x4E4F534A: js = json.loads(chunk)
        elif ct == 0x004E4942: binc = chunk
    return js, binc

J, BIN = load_glb(SRC)
CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}

def view_bytes(bvi):
    bv = J["bufferViews"][bvi]; o = bv.get("byteOffset", 0)
    return BIN[o:o + bv["byteLength"]], bv.get("byteStride", 0)

def accessor(ai):
    a = J["accessors"][ai]; dt = np.dtype(CT[a["componentType"]]).newbyteorder("<"); nc = NC[a["type"]]; cnt = a["count"]
    if "bufferView" in a:
        buf, stride = view_bytes(a["bufferView"])
        stride = stride or dt.itemsize * nc
        arr = np.ndarray((cnt, nc), dtype=dt, buffer=buf, offset=a.get("byteOffset", 0), strides=(stride, dt.itemsize)).copy()
    else:
        arr = np.zeros((cnt, nc), dt)
    if "sparse" in a:
        sp = a["sparse"]; n = sp["count"]
        ib, _ = view_bytes(sp["indices"]["bufferView"])
        idx = np.frombuffer(ib, dtype=CT[sp["indices"]["componentType"]], count=n, offset=sp["indices"].get("byteOffset", 0))
        vb, _ = view_bytes(sp["values"]["bufferView"])
        vals = np.frombuffer(vb, dtype=dt, count=n * nc, offset=sp["values"].get("byteOffset", 0)).reshape(n, nc)
        arr[idx] = vals
    if a.get("normalized") and dt.kind in "iu":
        arr = arr.astype(np.float32) / float(np.iinfo(dt).max)
        if dt.kind == "i": arr = np.maximum(arr, -1.0)
    return arr

# ---------------------------------------------------------------- math
def quat_to_mat3(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])

def mat3_to_quat(m):
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2; w = 0.25 * s
        x = (m[2, 1] - m[1, 2]) / s; y = (m[0, 2] - m[2, 0]) / s; z = (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2; w = (m[2, 1] - m[1, 2]) / s
        x = 0.25 * s; y = (m[0, 1] + m[1, 0]) / s; z = (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2; w = (m[0, 2] - m[2, 0]) / s
        x = (m[0, 1] + m[1, 0]) / s; y = 0.25 * s; z = (m[1, 2] + m[2, 1]) / s
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2; w = (m[1, 0] - m[0, 1]) / s
        x = (m[0, 2] + m[2, 0]) / s; y = (m[1, 2] + m[2, 1]) / s; z = 0.25 * s
    q = np.array([x, y, z, w]); return q / np.linalg.norm(q)

def trs_mat(t, r, s):
    m = np.eye(4); m[:3, :3] = quat_to_mat3(r) * np.asarray(s)[None, :]; m[:3, 3] = t; return m

def decompose(m):
    """Similarity transform -> T, R (xyzw), uniform S."""
    a = m[:3, :3]; sc = np.linalg.norm(a, axis=0); s = float(np.mean(sc))
    r = a / sc[None, :]
    if np.linalg.det(r) < 0: r[:, 0] *= -1; s = s   # mirrored: rare, keep rotation proper
    return m[:3, 3].copy(), mat3_to_quat(r), s

def node_local(n):
    if "matrix" in n: return np.array(n["matrix"], dtype=np.float64).reshape(4, 4).T
    return trs_mat(n.get("translation", [0, 0, 0]), n.get("rotation", [0, 0, 0, 1]), n.get("scale", [1, 1, 1]))

def node_trs(n):
    if "matrix" in n: return decompose(node_local(n))[0], decompose(node_local(n))[1], np.ones(3) * decompose(node_local(n))[2]
    return np.array(n.get("translation", [0, 0, 0]), float), np.array(n.get("rotation", [0, 0, 0, 1]), float), np.array(n.get("scale", [1, 1, 1]), float)

nodes = J["nodes"]; NN = len(nodes)
parent = [-1] * NN
for ni, n in enumerate(nodes):
    for c in n.get("children", []): parent[c] = ni
LOC = [node_local(n) for n in nodes]
GLOB = [None] * NN
def glob(ni):
    if GLOB[ni] is None: GLOB[ni] = LOC[ni] if parent[ni] < 0 else glob(parent[ni]) @ LOC[ni]
    return GLOB[ni]

skin = J["skins"][0]
SJ = skin["joints"]                       # skin joint -> node
IBM = accessor(skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1).astype(np.float64) if "inverseBindMatrices" in skin else np.tile(np.eye(4), (len(SJ), 1, 1))
node_to_sj = {nd: k for k, nd in enumerate(SJ)}

# ---------------------------------------------------------------- meshes -> primitives in bind space
mats_json = J.get("materials", [])
def mat_skipped(mi):
    if mi is None: return True
    m = mats_json[mi]
    return m.get("alphaMode") == "BLEND" or m.get("name", "") in SKIP_MAT

prims = []   # dict(pos, nrm, tan, uv, joints(sj idx), weights, idx, mat)
for ni, n in enumerate(nodes):
    if "mesh" not in n: continue
    rigid_sj = None; X = np.eye(4)
    if "skin" not in n:
        # Rigid attachment: find the nearest ancestor that is a skin joint.
        q = parent[ni]; chain = LOC[ni]
        while q >= 0 and q not in node_to_sj: chain = LOC[q] @ chain; q = parent[q]
        if q < 0: print("dino: skipping static mesh node", n.get("name")); continue
        rigid_sj = node_to_sj[q]; X = np.linalg.inv(IBM[rigid_sj]) @ chain
    for pr in J["meshes"][n["mesh"]]["primitives"]:
        if pr.get("mode", 4) != 4 or mat_skipped(pr.get("material")): continue
        at = pr["attributes"]
        pos = accessor(at["POSITION"]).astype(np.float64)
        nrm = accessor(at["NORMAL"]).astype(np.float64) if "NORMAL" in at else None
        tan = accessor(at["TANGENT"]).astype(np.float64) if "TANGENT" in at else None
        uv = accessor(at["TEXCOORD_0"]).astype(np.float64) if "TEXCOORD_0" in at else np.zeros((len(pos), 2))
        idx = accessor(pr["indices"]).ravel().astype(np.int64) if "indices" in pr else np.arange(len(pos))
        if rigid_sj is None:
            jj = accessor(at["JOINTS_0"]).astype(np.int64); ww = accessor(at["WEIGHTS_0"]).astype(np.float64)
        else:
            jj = np.zeros((len(pos), 4), np.int64); jj[:, 0] = rigid_sj
            ww = np.zeros((len(pos), 4)); ww[:, 0] = 1
            pos = (X[:3, :3] @ pos.T).T + X[:3, 3]
            if nrm is not None: nrm = (X[:3, :3] @ nrm.T).T
            if tan is not None: tan[:, :3] = (X[:3, :3] @ tan[:, :3].T).T
        prims.append(dict(pos=pos, nrm=nrm, tan=tan, uv=uv, j=jj, w=ww, idx=idx, mat=pr["material"]))

def compute_normals(pos, idx):
    t = idx.reshape(-1, 3); fn = np.cross(pos[t[:, 1]] - pos[t[:, 0]], pos[t[:, 2]] - pos[t[:, 0]])
    n = np.zeros_like(pos)
    for k in range(3): np.add.at(n, t[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)

def compute_tangents(pos, nrm, uv, idx):
    t = idx.reshape(-1, 3); p0, p1, p2 = pos[t[:, 0]], pos[t[:, 1]], pos[t[:, 2]]
    u0, u1, u2 = uv[t[:, 0]], uv[t[:, 1]], uv[t[:, 2]]
    e1, e2, d1, d2 = p1 - p0, p2 - p0, u1 - u0, u2 - u0
    r = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]; r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    sdir = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / r[:, None]; tdir = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / r[:, None]
    T = np.zeros_like(pos); B = np.zeros_like(pos)
    for k in range(3): np.add.at(T, t[:, k], sdir); np.add.at(B, t[:, k], tdir)
    T = T - nrm * np.sum(nrm * T, axis=1, keepdims=True)
    T /= np.maximum(np.linalg.norm(T, axis=1, keepdims=True), 1e-12)
    w = np.where(np.sum(np.cross(nrm, T) * B, axis=1) < 0, -1.0, 1.0)
    return np.concatenate([T, w[:, None]], axis=1)

for p in prims:
    if p["nrm"] is None: p["nrm"] = compute_normals(p["pos"], p["idx"])
    p["nrm"] /= np.maximum(np.linalg.norm(p["nrm"], axis=1, keepdims=True), 1e-12)
    if p["tan"] is None: p["tan"] = compute_tangents(p["pos"], p["nrm"], p["uv"], p["idx"])

# ---------------------------------------------------------------- skeleton (pruned)
used = set()
for p in prims:
    for k in range(4):
        sel = p["w"][:, k] > 1e-4; used.update(np.unique(p["j"][sel, k]).tolist())
keep_nodes = set()
for sj in used:
    q = SJ[sj]
    while q >= 0:
        if q in node_to_sj: keep_nodes.add(q)
        q = parent[q]
roots = [nd for nd in keep_nodes if not any(a in keep_nodes for a in [parent[nd]] if a >= 0)]
order = []
def dfs(nd):
    if nd in keep_nodes: order.append(nd)
    for c in nodes[nd].get("children", []): dfs(c)
for nd in range(NN):
    if parent[nd] < 0: dfs(nd)
jindex = {nd: k for k, nd in enumerate(order)}
NJ = len(order)
assert NJ <= 255, "too many joints"
jparent = []; jpre = []   # pre: static transform folded in front of the joint's animated local
for nd in order:
    q = parent[nd]; pre = np.eye(4)
    while q >= 0 and q not in keep_nodes: pre = LOC[q] @ pre; q = parent[q]
    jparent.append(jindex[q] if q >= 0 else -1); jpre.append(pre)
jnames = [nodes[nd].get("name", "j%d" % k) for k, nd in enumerate(order)]
sj_to_j = np.zeros(len(SJ), np.int64)
for sj, nd in enumerate(SJ): sj_to_j[sj] = jindex.get(nd, 0)
ibm = np.array([IBM[node_to_sj[nd]] for nd in order])
print("dino: %d skin joints, %d kept, roots %s" % (len(SJ), NJ, [jnames[jindex[r]] for r in roots]))

# ---------------------------------------------------------------- animation sampling
def sample_channel(times, vals, interp, path, t):
    n = len(times)
    if interp == "CUBICSPLINE": vals = vals.reshape(n, 3, -1)[:, 1, :]
    if t <= times[0]: return vals[0]
    if t >= times[-1]: return vals[-1]
    k = int(np.searchsorted(times, t) - 1); k = max(0, min(n - 2, k))
    if interp == "STEP": return vals[k]
    f = (t - times[k]) / max(times[k + 1] - times[k], 1e-9)
    a, b = vals[k], vals[k + 1]
    if path == "rotation":
        if np.dot(a, b) < 0: b = -b
        r = a * (1 - f) + b * f; return r / np.linalg.norm(r)
    return a * (1 - f) + b * f

rest_trs = [node_trs(nodes[nd]) for nd in order]
# --rebind-static: joints that no clip animates take their bind pose
# (inverse of the inverse bind matrix) relative to their parent's bind pose.
# Some exports leave such leaf joints ("_end" bones) far from where the mesh
# was bound, which stretches the vertices weighted to them.
# --freeze REGEX: joints whose name matches are held at that bind pose in
# every clip (their own keys are ignored), e.g. a broken sail rig.
import re
FREEZE = re.compile(opt["--freeze"]) if opt.get("--freeze") else None
frozen = set()
ROT_ONLY = opt.get("--rot-only", "0") == "1"
if opt.get("--rebind-static", "0") == "1" or FREEZE:
    animated = set()
    for a_ in J.get("animations", []):
        for c_ in a_["channels"]: animated.add(c_["target"].get("node"))
    nfix = 0
    for j, nd in enumerate(order):
        fz = bool(FREEZE and FREEZE.search(jnames[j]))
        if fz: frozen.add(nd)
        if jparent[j] < 0 or (not fz and (nd in animated or opt.get("--rebind-static", "0") != "1")): continue
        Bp = np.linalg.inv(ibm[jparent[j]]); Bj = np.linalg.inv(ibm[j])
        L = np.linalg.inv(jpre[j]) @ np.linalg.inv(Bp) @ Bj
        t_, r_, s_ = decompose(L); rest_trs[j] = (t_, r_, np.ones(3) * s_); nfix += 1
    print("dino: rebound %d joints to their bind pose" % nfix)
def clip_locals(anim):
    ch = {}
    t0, t1 = 1e9, -1e9
    for c in anim["channels"]:
        nd = c["target"].get("node"); path = c["target"]["path"]
        if nd not in keep_nodes or nd in frozen or path not in ("translation", "rotation", "scale"): continue
        # --rot-only 1: only rotation keys below the root (some FBX exports
        # key bone translation and scale in the wrong units).
        if ROT_ONLY and path != "rotation" and jparent[jindex[nd]] >= 0: continue
        smp = anim["samplers"][c["sampler"]]
        tm = accessor(smp["input"]).ravel().astype(np.float64); vv = accessor(smp["output"]).astype(np.float64)
        ch[(jindex[nd], path)] = (tm, vv, smp.get("interpolation", "LINEAR"))
        t0 = min(t0, tm[0]); t1 = max(t1, tm[-1])
    if not ch: return None
    nf = max(2, int(round((t1 - t0) * HZ)) + 1)
    out = np.zeros((nf, NJ, 4, 4))
    for f in range(nf):
        t = min(t0 + f / HZ, t1)
        for j in range(NJ):
            T, R, S = rest_trs[j]
            if (j, "translation") in ch: T = sample_channel(*ch[(j, "translation")], "translation", t)
            if (j, "rotation") in ch: R = sample_channel(*ch[(j, "rotation")], "rotation", t)
            if (j, "scale") in ch: S = sample_channel(*ch[(j, "scale")], "scale", t)
            out[f, j] = trs_mat(T, R, S)
    return out, (t1 - t0)

def globals_of(locs, C):
    """locs (NJ,4,4) animated locals -> model-space globals with C in front of the roots."""
    G = np.zeros_like(locs)
    for j in range(NJ):
        L = jpre[j] @ locs[j]
        # Roots: jpre holds the static wrapper nodes (Sketchfab/FBX) up to the scene root.
        G[j] = C @ L if jparent[j] < 0 else G[jparent[j]] @ L
    return G

def skin_points(G, sel_prims):
    pts = []
    for p in sel_prims:
        S = np.einsum("jab,jbc->jac", G, ibm)          # (NJ,4,4)
        jj = sj_to_j[p["j"]]                            # (V,4)
        Mv = np.einsum("vk,vkab->vab", p["w"], S[jj])   # (V,4,4)
        pts.append(np.einsum("vab,vb->va", Mv[:, :3, :3], p["pos"]) + Mv[:, :3, 3])
    return np.concatenate(pts)

anims = J.get("animations", [])
def find_anim(name):
    for a in anims:
        if a.get("name") == name: return a
    return None
idle_name = next((v for v, k in CLIPMAP.items() if k == "idle"), None)
idle_anim = find_anim(idle_name) if idle_name else (anims[0] if anims else None)

# ---------------------------------------------------------------- model correction C
def foot_joints():
    if opt.get("--feet"):   # rigs with generic bone names: a regex for the foot joints
        import re as re_; rx = re_.compile(opt["--feet"]); return [j for j, nm in enumerate(jnames) if rx.search(nm)]
    fj = [j for j, nm in enumerate(jnames) if any(k in nm.lower() for k in ("toe", "foot", "metatars"))]
    return fj
I4 = np.eye(4)
rest_locs = np.array([trs_mat(*rest_trs[j]) for j in range(NJ)])
G0 = globals_of(rest_locs, I4)
pos_rest = skin_points(G0, prims)
jp = G0[:, :3, 3]
head = [j for j, nm in enumerate(jnames) if "head" in nm.lower()]
fwd = (jp[head[0]] - jp.mean(0)) if head else np.array([1.0, 0, 0])
if "--forward" in opt: fwd = np.array([float(x) for x in opt["--forward"].split()])
th = math.atan2(fwd[2], fwd[0])
Rc = np.eye(4); Rc[:3, :3] = np.array([[math.cos(th), 0, math.sin(th)], [0, 1, 0], [-math.sin(th), 0, math.cos(th)]])
pr_ = (Rc[:3, :3] @ pos_rest.T).T
scale = LENGTH / (pr_[:, 0].max() - pr_[:, 0].min())
Sc = np.diag([scale, scale, scale, 1.0])
C1 = Sc @ Rc
if idle_anim is not None:
    L_idle, _ = clip_locals(idle_anim); Gi = globals_of(L_idle[0], C1)
else:
    Gi = globals_of(rest_locs, C1)
pi_ = skin_points(Gi, prims)
if os.environ.get("DINO_DEBUG"):
    med = np.median(pi_, 0); dist = np.linalg.norm(pi_ - med, axis=1); far = dist > 4 * np.percentile(dist, 50)
    jall = np.concatenate([sj_to_j[p["j"]][np.arange(len(p["j"])), p["w"].argmax(1)] for p in prims])
    u_, c_ = np.unique(jall[far], return_counts=True)
    print("dino: debug: %d far vertices in the idle pose, dominant joints %s" % (far.sum(), [(jnames[x], int(n)) for x, n in zip(u_, c_)]))
fj = foot_joints()
feet = Gi[fj, :3, 3] if fj else pi_[np.argsort(pi_[:, 1])[:50]]
ox = float(np.mean(feet[:, 0])); oz = float(np.mean(feet[:, 2])) if fj else 0.0
oz = 0.5 * (pi_[:, 2].min() + pi_[:, 2].max())
Tc = np.eye(4); Tc[:3, 3] = [-ox, -pi_[:, 1].min(), -oz]
C = Tc @ C1
pi_ = pi_ + Tc[:3, 3]
bmin, bmax = pi_.min(0), pi_.max(0)
HEIGHT = float(bmax[1])
print("dino: length %.2f m, height %.2f m, bounds %s %s, scale %.5f, facing %.1f deg" % (LENGTH, HEIGHT, np.round(bmin, 2), np.round(bmax, 2), scale, math.degrees(th)))

def to_local8(G):
    """Model-space globals -> local T,R,S per joint (roots carry C)."""
    out = np.zeros((NJ, 8), np.float32)
    for j in range(NJ):
        L = G[j] if jparent[j] < 0 else np.linalg.inv(G[jparent[j]]) @ G[j]
        t, r, s = decompose(L); out[j, :3] = t; out[j, 3:7] = r; out[j, 7] = s
    return out

# ---------------------------------------------------------------- clips
clips = []
for a in anims:
    res = clip_locals(a)
    if res is None: continue
    L, dur = res; nf = len(L)
    Gs = np.array([globals_of(L[f], C) for f in range(nf)])
    nm = a.get("name", "clip%d" % len(clips))
    canon = CLIPMAP.get(nm, nm.lower().replace(" ", "_"))
    if dur < 0.1: continue
    # Root motion: drift of the skeleton centroid over the clip. Removed only
    # where the sim moves the body (locomotion, leaps, lunges); deaths and
    # knock-downs keep theirs.
    cen = Gs[:, :, :3, 3].mean(1)
    drift = cen[-1] - cen[0]; drift[1] = 0
    speed_rm = 0.0
    moving = any(k in canon for k in ("walk", "trot", "jog", "run", "sprint", "jump", "leap", "tackle", "charge", "swim"))
    if moving and np.linalg.norm(drift) > 0.05 * LENGTH and dur > 0:
        speed_rm = float(drift[0] / dur)
        k = (np.arange(nf) / (nf - 1))[:, None]
        Gs[:, :, 0, 3] -= drift[0] * k; Gs[:, :, 2, 3] -= drift[2] * k   # every global: locals below the root are unchanged
    # In-place stride speed: each frame the lowest foot joint is the planted
    # one; it slides backward at the ground speed.
    speed_ft = 0.0
    if moving and fj and nf > 4 and dur > 0:
        y = Gs[:, fj, 1, 3]; x = Gs[:, fj, 0, 3]
        v = []
        for f in range(1, nf - 1):
            j = int(np.argmin(y[f])); v.append(-(x[f + 1, j] - x[f - 1, j]) * HZ * 0.5)
        speed_ft = max(0.0, float(np.percentile(v, 75)))   # transitional frames pull the median down
        if os.environ.get("DINO_DEBUG"):
            for jx, j in enumerate(fj):
                print("   foot %-22s x %.2f..%.2f  y %.2f..%.2f" % (jnames[j], x[:, jx].min(), x[:, jx].max(), y[:, jx].min(), y[:, jx].max()))
            print("   planted v per frame", np.round(v, 1).tolist())
    if canon in SPEED_OVERRIDE: speed_rm, speed_ft = 0.0, SPEED_OVERRIDE[canon]
    speed = speed_rm + speed_ft
    if abs(speed) < 0.05 * LENGTH: speed = 0.0
    frames = np.array([to_local8(Gs[f]) for f in range(nf)], np.float32)
    # Loop if the last pose matches the first.
    dq = np.abs(np.sum(frames[0, :, 3:7] * frames[-1, :, 3:7], axis=1)).min()
    loop = dq > math.cos(math.radians(12) / 2) or any(k in canon for k in ("idle", "walk", "trot", "run", "jog", "sprint", "loop", "swim"))
    clips.append((canon[:31], dur, speed, 1 if loop else 0, frames))
    if canon == "attack" and head:   # when the head reaches furthest forward / the body drops lowest: the sim's hit_time
        hx = Gs[:, head[0], 0, 3]; hy = Gs[:, head[0], 1, 3]
        print("dino: attack: head furthest forward at %.2f s (x %.2f..%.2f), lowest at %.2f s" % (int(np.argmax(hx)) / HZ, hx.min(), hx.max(), int(np.argmin(hy)) / HZ))
    print("dino: clip %-14s <- %-16s %5.2fs %3d frames speed %5.2f m/s (root %.2f, feet %.2f) %s" % (canon, nm, dur, nf, speed, speed_rm, speed_ft, "loop" if loop else ""))
rest8 = to_local8(globals_of(rest_locs, C))

# ---------------------------------------------------------------- materials and textures
tmpdir = tempfile.mkdtemp(prefix="dino_img_", dir=OUT)
used_mats = sorted(set(p["mat"] for p in prims))
def tex_info(t):
    return J["textures"][t["index"]]["source"] if t else None
def load_image(src_index, label):
    if src_index is None: return None
    im = J["images"][src_index]
    data, _ = view_bytes(im["bufferView"])
    ext = ".png" if im.get("mimeType") == "image/png" else ".jpg"
    path = os.path.join(tmpdir, "%s%s" % (label, ext)); open(path, "wb").write(data)
    img = bpy.data.images.load(path); img.colorspace_settings.name = "Non-Color"
    return img
def pixels(img, size):
    if img.size[0] != size or img.size[1] != size: img.scale(size, size)
    a = np.empty(size * size * 4, np.float32); img.pixels.foreach_get(a)
    return a.reshape(size, size, 4)
def save(arr, path, srgb=False):
    h, w = arr.shape[:2]
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=False)
    img.colorspace_settings.name = "Non-Color"
    px = np.ones((h, w, 4), np.float32); px[:, :, :3] = np.clip(arr[:, :, :3], 0, 1)
    img.pixels.foreach_set(px.ravel())
    img.filepath_raw = path; img.file_format = "JPEG"
    img.save(filepath=path, quality=92); bpy.data.images.remove(img)

mat_names = []; mat_flags = []
for mi in used_mats:
    m = mats_json[mi]; name = "".join(ch if ch.isalnum() else "_" for ch in m.get("name", "m%d" % mi)).lower()
    sg = m.get("extensions", {}).get("KHR_materials_pbrSpecularGlossiness")
    mr = m.get("pbrMetallicRoughness", {})
    if sg:
        base_i, base_f = tex_info(sg.get("diffuseTexture")), sg.get("diffuseFactor", [1, 1, 1, 1])
        sg_i = tex_info(sg.get("specularGlossinessTexture")); gloss_f = sg.get("glossinessFactor", 1.0)
    else:
        base_i, base_f = tex_info(mr.get("baseColorTexture")), mr.get("baseColorFactor", [1, 1, 1, 1])
        mr_i = tex_info(mr.get("metallicRoughnessTexture"))
        if mr.get("metallicRoughnessTexture", {}).get("texCoord", 0) != 0: print("dino: warning: %s metal/rough uses texCoord %d" % (name, mr["metallicRoughnessTexture"]["texCoord"]))
    nor_i = tex_info(m.get("normalTexture")); ao_i = tex_info(m.get("occlusionTexture"))
    if m.get("normalTexture", {}).get("texCoord", 0) != 0: print("dino: warning: %s normal map uses texCoord %d" % (name, m["normalTexture"]["texCoord"]))
    imgs = {k: load_image(v, "%s_%s" % (name, k)) for k, v in (("base", base_i), ("nor", nor_i), ("ao", ao_i))}
    if sg: imgs["sg"] = load_image(sg_i, name + "_sg")
    else: imgs["mr"] = load_image(mr_i, name + "_mr")
    src_size = max([im.size[0] for im in imgs.values() if im] + [256])
    size = min(TEX, src_size)
    if src_size <= 1024 or len(prims) and sum(len(p["idx"]) for p in prims if p["mat"] == mi) < 0.1 * sum(len(p["idx"]) for p in prims):
        size = min(size, 1024)   # small parts (eyes, teeth, claws): 1K
    base = pixels(imgs["base"], size)[:, :, :3] if imgs["base"] else np.ones((size, size, 3)) * 0.5
    # glTF images are sRGB-encoded; the factor multiplies linear values.
    lin = np.where(base <= 0.04045, base / 12.92, ((base + 0.055) / 1.055) ** 2.4) * np.array(base_f[:3])[None, None, :]
    base = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(np.maximum(lin, 0), 1 / 2.4) - 0.055)
    nor = pixels(imgs["nor"], size) if imgs["nor"] else np.tile(np.array([0.5, 0.5, 1.0, 1.0]), (size, size, 1))
    arm = np.zeros((size, size, 3), np.float32)
    arm[:, :, 0] = pixels(imgs["ao"], size)[:, :, 0] if imgs["ao"] else 1.0
    if sg:
        gl = pixels(imgs["sg"], size)[:, :, 3] if imgs["sg"] else 1.0
        arm[:, :, 1] = 1.0 - gloss_f * gl; arm[:, :, 2] = 0.0
    else:
        mrp = pixels(imgs["mr"], size) if imgs["mr"] else None
        arm[:, :, 1] = (mrp[:, :, 1] if mrp is not None else 1.0) * mr.get("roughnessFactor", 1.0)
        arm[:, :, 2] = (mrp[:, :, 2] if mrp is not None else 1.0) * mr.get("metallicFactor", 1.0)
    # Blender pixel rows start at the bottom; glTF UV v=0 is the top row.
    # Write as an ordinary top-down image (save() handles orientation).
    pre = "%s_%s" % (SPECIES, name)
    save(base, os.path.join(OUT, pre + "_diff.jpg")); save(nor, os.path.join(OUT, pre + "_nor.jpg")); save(arm, os.path.join(OUT, pre + "_arm.jpg"))
    for im in imgs.values():
        if im: bpy.data.images.remove(im)
    mat_names.append(pre); mat_flags.append(1 if m.get("doubleSided") else 0)
    print("dino: material %s %dpx (%s)" % (pre, size, "spec-gloss" if sg else "metal-rough"))
for f in os.listdir(tmpdir): os.remove(os.path.join(tmpdir, f))
os.rmdir(tmpdir)
mat_slot = {mi: k for k, mi in enumerate(used_mats)}

# ---------------------------------------------------------------- LODs
def pack_vertices(pos, uv, nrm, tan, jj, ww):
    """Top-4 weights renormalised, quantised to u8 summing to 255."""
    order_ = np.argsort(-ww, axis=1)[:, :4]
    w4 = np.take_along_axis(ww, order_, 1); j4 = np.take_along_axis(jj, order_, 1)
    if w4.shape[1] < 4:
        w4 = np.pad(w4, ((0, 0), (0, 4 - w4.shape[1]))); j4 = np.pad(j4, ((0, 0), (0, 4 - j4.shape[1])))
    w4 = w4 / np.maximum(w4.sum(1, keepdims=True), 1e-9)
    q = np.floor(w4 * 255 + 0.5).astype(np.int64)
    q[:, 0] += 255 - q.sum(1)
    q = np.clip(q, 0, 255)
    j4 = np.where(q > 0, j4, 0)
    rec = np.zeros(len(pos), dtype=[("p", "<f4", 3), ("uv", "<f4", 2), ("n", "i1", 4), ("t", "i1", 4), ("j", "u1", 4), ("w", "u1", 4)])
    rec["p"] = pos; rec["uv"] = uv
    qn = lambda v: np.clip(np.round(v * 127), -127, 127).astype(np.int8)
    n4 = np.zeros((len(pos), 4)); n4[:, :3] = nrm; rec["n"] = qn(n4)
    t4 = np.zeros((len(pos), 4)); t4[:, :3] = tan[:, :3]; rec["t"] = qn(t4); rec["t"][:, 3] = np.where(tan[:, 3] < 0, -1, 1)
    rec["j"] = j4.astype(np.uint8); rec["w"] = q.astype(np.uint8)
    return rec

def lod_from_prims():
    """Original vertices (exact normals/tangents), grouped by material."""
    allv = []; subs = []; idx_all = []; base = 0
    for mi in used_mats:
        for p in prims:
            if p["mat"] != mi: continue
            jj = sj_to_j[p["j"]]
            allv.append(pack_vertices(p["pos"], p["uv"], p["nrm"], p["tan"], jj, p["w"]))
            idx_all.append(p["idx"] + base); base += len(p["pos"])
    # sub ranges in material order
    first = 0; k = 0
    for mi in used_mats:
        cnt = sum(len(p["idx"]) for p in prims if p["mat"] == mi)
        subs.append((mat_slot[mi], first, cnt)); first += cnt
    return np.concatenate(allv), np.concatenate(idx_all).astype(np.uint32), subs

def lod_decimated(ratio):
    """Weld, build a Blender mesh with joint vertex groups, decimate, read back."""
    P, UV, F, MI, W = [], [], [], [], []
    base = 0
    for p in prims:
        P.append(p["pos"]); UV.append(p["uv"]); F.append(p["idx"].reshape(-1, 3) + base); MI.append(np.full(len(p["idx"]) // 3, mat_slot[p["mat"]]))
        w = np.zeros((len(p["pos"]), NJ)); jj = sj_to_j[p["j"]]
        for k in range(4): np.add.at(w, (np.arange(len(p["pos"])), jj[:, k]), p["w"][:, k])
        W.append(w); base += len(p["pos"])
    P = np.concatenate(P); UV = np.concatenate(UV); F = np.concatenate(F); MI = np.concatenate(MI); W = np.concatenate(W)
    key = np.round(P / (np.ptp(P, 0).max() * 1e-6)).astype(np.int64)
    _, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    inv = inv.ravel(); Pw = P[first]; Ww = W[first]
    Fw = inv[F]
    good = (Fw[:, 0] != Fw[:, 1]) & (Fw[:, 1] != Fw[:, 2]) & (Fw[:, 0] != Fw[:, 2])
    Fw, MIw, Floops = Fw[good], MI[good], F[good]
    me = bpy.data.meshes.new("lod"); me.from_pydata(Pw.tolist(), [], Fw.tolist())
    for _ in range(len(mat_names)): me.materials.append(None)
    me.polygons.foreach_set("material_index", MIw.astype(np.int32))
    uvl = me.uv_layers.new(name="uv"); uvl.data.foreach_set("uv", UV[Floops.ravel()].astype(np.float32).ravel())
    ob = bpy.data.objects.new("lod", me); bpy.context.scene.collection.objects.link(ob)
    for j in range(NJ): ob.vertex_groups.new(name="j%d" % j)
    for j in range(NJ):
        sel = np.nonzero(Ww[:, j] > 1e-4)[0]
        g = ob.vertex_groups[j]
        for vi in sel: g.add([int(vi)], float(Ww[vi, j]), "REPLACE")
    bpy.context.view_layer.objects.active = ob; ob.select_set(True)
    if ratio < 0.999:
        m = ob.modifiers.new("dec", "DECIMATE"); m.ratio = ratio; m.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier="dec")
    me = ob.data
    for poly in me.polygons: poly.use_smooth = True
    me.calc_tangents(uvmap="uv")
    nv = len(me.vertices)
    wv = np.zeros((nv, NJ))
    for v in me.vertices:
        for g in v.groups: wv[v.index, g.group] = g.weight
    verts = {}; out = []; subs_idx = {s: [] for s in range(len(mat_names))}
    uvd = me.uv_layers["uv"].data
    for poly in me.polygons:
        tri = []
        for li in poly.loop_indices:
            l = me.loops[li]; vi = l.vertex_index; u = uvd[li].uv
            n = l.normal; t = l.tangent
            k = (vi, round(u[0], 5), round(u[1], 5), round(n[0], 2), round(n[1], 2), round(n[2], 2))
            if k not in verts:
                verts[k] = len(out); co = me.vertices[vi].co
                out.append((co[:], (u[0], u[1]), n[:], (t[0], t[1], t[2], l.bitangent_sign), vi))
            tri.append(verts[k])
        subs_idx[poly.material_index] += tri
    pos = np.array([o[0] for o in out]); uv = np.array([o[1] for o in out]); nrm = np.array([o[2] for o in out]); tan = np.array([o[3] for o in out])
    ww = wv[[o[4] for o in out]]
    jj = np.tile(np.arange(NJ), (len(out), 1))
    rec = pack_vertices(pos, uv, nrm, tan, jj, ww)
    idx = []; subs = []; first = 0
    for s in range(len(mat_names)):
        idx += subs_idx[s]; subs.append((s, first, len(subs_idx[s]))); first += len(subs_idx[s])
    bpy.data.objects.remove(ob); bpy.data.meshes.remove(me)
    return rec, np.array(idx, np.uint32), subs

total_tris = sum(len(p["idx"]) for p in prims) // 3
lods = []
for li, frac in enumerate(LOD_FRAC):
    target = int(min(LOD0, total_tris) * frac)
    if li == 0 and total_tris <= LOD0: lods.append(lod_from_prims())
    else: lods.append(lod_decimated(target / total_tris))
    print("dino: LOD%d %d tris, %d verts" % (li, len(lods[-1][1]) // 3, len(lods[-1][0])))

# ---------------------------------------------------------------- write
def name32(s): return s.encode()[:31].ljust(32, b"\0")
b = bytearray(struct.pack("<II", 0x4F4E4443, 1))
b += struct.pack("<ff3f3f", LENGTH, HEIGHT, *bmin.tolist(), *bmax.tolist())
b += struct.pack("<I", NJ)
for j in range(NJ):
    b += name32(jnames[j]) + struct.pack("<i", jparent[j]) + struct.pack("<16f", *ibm[j].T.ravel().tolist()) + struct.pack("<8f", *rest8[j].tolist())
b += struct.pack("<I", len(mat_names))
for nm, fl in zip(mat_names, mat_flags): b += name32(nm) + struct.pack("<I", fl)
b += struct.pack("<I", len(lods))
for rec, idx, subs in lods:
    b += struct.pack("<III", len(rec), len(idx), len(subs))
    for s in subs: b += struct.pack("<III", *s)
    b += rec.tobytes() + idx.astype("<u4").tobytes()
b += struct.pack("<I", len(clips))
for nm, dur, speed, flags, frames in clips:
    b += name32(nm) + struct.pack("<ffII", dur, speed, flags, len(frames)) + frames.astype("<f4").tobytes()
path = os.path.join(OUT, SPECIES + ".dino")
open(path, "wb").write(b)
print("dino: wrote %s (%.1f MB): %d joints, %d clips, LOD tris %s" % (path, len(b) / 1e6, NJ, len(clips), [len(l[1]) // 3 for l in lods]))
