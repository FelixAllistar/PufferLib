#!/bin/bash
# Re-downloads the CC0 photo scans for the vegetation atlas and re-extracts
# them into ocean/cretaceous/assets/foliage/ (see SOURCES.md there):
#   sprites/<set>_<nn>.png    single leaves / fronds / sprays, RGBA, base at the
#                             bottom edge, tip at the top, sorted by size
#   sprites/<set>_<nn>_m.png  the same sprite's material: R, G its surface normal
#                             (OpenGL convention: R toward the sprite's right, G
#                             toward its tip), B the light it lets through (thin
#                             blade bright, veins and stems dark), A unused
#   bark_<name>.png           tileable bark albedo, 512x512
#   bark_<name>_n.png         its normal map (OpenGL convention), 512x512
# Needs curl, unzip and python3 with Pillow (PYTHON=python3.12 picks another
# interpreter). Run from the repository root:
#   bash ocean/cretaceous/tools/fetch_foliage.sh
# The renderer composites these into its procedural plant atlas and its leaf
# material atlas at startup and falls back to painted layers when the files are
# missing. Downloads are about 260 MB (kept in build/cret_foliage/dl, so a
# second run only re-extracts); the extracted files are about 40 MB.
set -e
OUT=ocean/cretaceous/assets/foliage
DL=build/cret_foliage/dl
PY=${PYTHON:-python3}
mkdir -p "$OUT/sprites" "$DL"
UA="cretaceous-foliage-fetch"

# ambientCG leaf scans (CC0), 2K JPG sets with Color, Opacity, NormalGL and,
# for some, Scattering (light through the leaf):
#   id  orientation  max_sprites  max_px
ACG="
LeafSet019 cw 3 512
LeafSet022 up 6 512
LeafSet018 up 3 512
LeafSet024 up 9 512
LeafSet013 up 6 512
LeafSet020 up 5 512
LeafSet001 up 6 512
LeafSet002 up 6 512
LeafSet004 up 6 512
Foliage001 up 9 1024
Foliage006 up 6 1024
LeafSet010 up 6 1024
LeafSet025 up 9 1024
LeafSet014 up 6 512
"
# Poly Haven plant model textures (CC0), leaf maps only (the meshes are not
# used): set  model  map-prefix  resolution  orientation  max_sprites  max_px
PH="
fern_02 fern_02 fern_02 4k up 8 1024
grass_medium_02 grass_medium_02 grass_medium_02 2k up 9 1024
fir_tree_01 fir_tree_01 fir_tree_01_twig 2k up 6 1024
jacaranda_tree jacaranda_tree jacaranda_tree_leaves 2k ccw 1 1024
"
PH_BARK="metasequoia_bark palm_bark bark_brown_02 japanese_cedar_bark"

get() { [ -s "$2" ] || curl -sfL -A "$UA" -o "$2" "$1"; }
echo "$ACG" | while read -r id orient n px; do
    [ -z "$id" ] && continue
    mkdir -p "$DL/$id"
    if [ ! -f "$DL/$id/.done2k" ]; then
        get "https://ambientcg.com/get?file=${id}_2K-JPG.zip" "$DL/$id/a.zip"
        (cd "$DL/$id" && unzip -o -q a.zip "*_Color.jpg" "*_Opacity.jpg" "*_NormalGL.jpg" && { unzip -o -q a.zip "*_Scattering.jpg" 2>/dev/null || true; } && rm a.zip && touch .done2k)
    fi
done
echo "$PH" | while read -r set model pre res orient n px; do
    [ -z "$set" ] && continue
    mkdir -p "$DL/$set"
    base="https://dl.polyhaven.org/file/ph-assets/Models"
    get "$base/jpg/$res/$model/${pre}_diff_$res.jpg" "$DL/$set/diff.jpg"
    get "$base/png/$res/$model/${pre}_alpha_$res.png" "$DL/$set/alpha.png"
    get "$base/jpg/$res/$model/${pre}_nor_gl_$res.jpg" "$DL/$set/nor_gl.jpg"
done
for b in $PH_BARK; do
    mkdir -p "$DL/$b"
    get "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/$b/${b}_diff_1k.jpg" "$DL/$b/${b}_diff_1k.jpg"
    get "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/$b/${b}_nor_gl_1k.jpg" "$DL/$b/${b}_nor_gl_1k.jpg"
done

"$PY" -I - "$DL" "$OUT" "$ACG" "$PH" "$PH_BARK" <<'EOF'
import sys, os, glob
from collections import deque
from PIL import Image, ImageFilter, ImageChops
dl, out, acg, ph, barks = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5].split()
sdir = os.path.join(out, "sprites")
for f in glob.glob(os.path.join(sdir, "*.png")): os.remove(f)

def components(alpha, lab_size=512, thresh=100, dilate=2):
    """Connected components of the opacity mask on a downscaled grid.
    Returns (full-resolution bounding box, component mask image), largest first."""
    W, H = alpha.size; s = lab_size / max(W, H)
    w, h = max(1, int(W * s)), max(1, int(H * s))
    small = alpha.resize((w, h), Image.BILINEAR).load()
    m = [[small[x, y] > thresh for x in range(w)] for y in range(h)]
    for _ in range(dilate):   # join stems to their blades
        m2 = [row[:] for row in m]
        for y in range(h):
            for x in range(w):
                if m[y][x]:
                    for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
                        xx, yy = x + dx, y + dy
                        if 0 <= xx < w and 0 <= yy < h: m2[yy][xx] = True
        m = m2
    seen = [[False] * w for _ in range(h)]; boxes = []
    for y in range(h):
        for x in range(w):
            if not m[y][x] or seen[y][x]: continue
            q = deque([(x, y)]); seen[y][x] = True; x0 = x1 = x; y0 = y1 = y; n = 0
            pix = []
            while q:
                cx, cy = q.popleft(); n += 1; pix.append((cx, cy))
                x0, x1, y0, y1 = min(x0, cx), max(x1, cx), min(y0, cy), max(y1, cy)
                for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
                    xx, yy = cx + dx, cy + dy
                    if 0 <= xx < w and 0 <= yy < h and m[yy][xx] and not seen[yy][xx]:
                        seen[yy][xx] = True; q.append((xx, yy))
            if n > 150:
                mk = Image.new("L", (w, h), 0); mp = mk.load()
                for (px, py) in pix: mp[px, py] = 255
                mk = mk.resize((W, H), Image.BILINEAR).point(lambda v: 255 if v > 40 else 0)
                boxes.append((n, (int(x0 / s) - 3, int(y0 / s) - 3, int((x1 + 1) / s) + 3, int((y1 + 1) / s) + 3), mk))
    boxes.sort(key=lambda b: -b[0])
    return [(b, mk) for _, b, mk in boxes]

def luma(im):
    return im.convert("RGB").convert("L")

def translucency(color, scatter, alpha):
    """Light let through, 0..255. From a Scattering scan (the leaf photographed
    against the light) where there is one: its brightness over the bright end of
    the leaf's own range. Otherwise from the colour photo: veins, midribs and
    stems stand out lighter than the blade around them, so the positive part of
    the brightness minus its blur blocks light, and brown (dry) parts let less
    through than green."""
    a = alpha.convert("L")
    if scatter is not None:
        L = luma(scatter)
        hist = L.histogram(mask=a.point(lambda v: 255 if v > 128 else 0))
        tot = sum(hist); acc = 0; p95 = 255
        for v in range(256):
            acc += hist[v]
            if acc >= 0.95 * tot: p95 = max(v, 1); break
        return L.point(lambda v: min(255, int(v * 255 / p95)))
    L = luma(color); r = max(2, max(color.size) // 200)
    # The blur of the leaf alone (the backdrop left out: blur of brightness times
    # coverage over blur of coverage), so a small leaflet is not "lighter than
    # around" just for sitting on a black backdrop.
    m = a.point(lambda v: 255 if v > 128 else 0)
    num = ImageChops.multiply(L, m).filter(ImageFilter.GaussianBlur(r)); den = m.filter(ImageFilter.GaussianBlur(r))
    around = Image.merge("L", [Image.frombytes("L", L.size, bytes(min(255, n * 255 // d) if d else 0 for n, d in zip(num.tobytes(), den.tobytes())))])
    hp = ImageChops.subtract(L, around, scale=1, offset=0)   # lighter than the leaf around it: veins
    vein = hp.point(lambda v: min(255, v * 12))
    rgb = color.convert("RGB"); R, G, B = rgb.split()
    brown = ImageChops.subtract(R, G).point(lambda v: min(255, v * 6))   # red over green: dry
    t = ImageChops.subtract(Image.new("L", L.size, 235), vein.point(lambda v: v * 6 // 10))
    return ImageChops.subtract(t, brown.point(lambda v: v * 4 // 10))

def rotate_normals(nrm, orient):
    """A normal map turned with its image: the vectors turn too."""
    if orient == "up": return nrm
    R, G, B = nrm.split()
    inv = lambda c: c.point(lambda v: 255 - v)
    if orient == "cw":   # image turned clockwise: up becomes right, right becomes down
        R, G = G, inv(R)
        return Image.merge("RGB", (R, G, B)).rotate(-90, expand=True)
    R, G = inv(G), R      # counter-clockwise: up becomes left, right becomes up
    return Image.merge("RGB", (R, G, B)).rotate(90, expand=True)

def save_sprites(name, color, alpha, nrm, scatter, orient, nmax, px):
    alpha = alpha.convert("L")
    if alpha.size != color.size: alpha = alpha.resize(color.size, Image.BILINEAR)
    rgba = color.convert("RGB"); rgba.putalpha(alpha)
    if nrm is not None and nrm.size != color.size: nrm = nrm.resize(color.size, Image.BILINEAR)
    if scatter is not None and scatter.size != color.size: scatter = scatter.resize(color.size, Image.BILINEAR)
    trans = translucency(color, scatter, alpha)
    W, H = rgba.size; k = 0
    for (x0, y0, x1, y1), mk in components(alpha)[:nmax]:
        box = (max(x0, 0), max(y0, 0), min(x1, W), min(y1, H))
        sp = rgba.crop(box)
        a = Image.composite(sp.getchannel("A"), Image.new("L", sp.size, 0), mk.crop(box)); sp.putalpha(a)
        n = nrm.convert("RGB").crop(box) if nrm is not None else Image.new("RGB", sp.size, (128, 128, 255))
        if orient == "cw": sp = sp.rotate(-90, expand=True)
        elif orient == "ccw": sp = sp.rotate(90, expand=True)
        n = rotate_normals(n, orient)
        t = trans.crop(box)
        if orient == "cw": t = t.rotate(-90, expand=True)
        elif orient == "ccw": t = t.rotate(90, expand=True)
        sc = px / max(sp.size)
        if sc < 1:
            size = (max(1, int(sp.width * sc)), max(1, int(sp.height * sc)))
            sp, n, t = sp.resize(size, Image.LANCZOS), n.resize(size, Image.LANCZOS), t.resize(size, Image.LANCZOS)
        sp.save(os.path.join(sdir, "%s_%02d.png" % (name, k)))
        R, G, _ = n.split()
        Image.merge("RGBA", (R, G, t, Image.new("L", sp.size, 255))).save(os.path.join(sdir, "%s_%02d_m.png" % (name, k)))
        k += 1
    print("%-15s %d sprites, %s" % (name, k, "scattering" if scatter is not None else "translucency from the photo"))

def first(pattern):
    f = glob.glob(pattern); return Image.open(f[0]) if f else None

for line in acg.strip().splitlines():
    sid, orient, n, px = line.split()
    d = os.path.join(dl, sid)
    save_sprites(sid, first(d + "/*_Color.jpg"), first(d + "/*_Opacity.jpg"), first(d + "/*_NormalGL.jpg"), first(d + "/*_Scattering.jpg"), orient, int(n), int(px))
for line in ph.strip().splitlines():
    sid, model, pre, res, orient, n, px = line.split()
    d = os.path.join(dl, sid)
    save_sprites(sid, Image.open(d + "/diff.jpg"), Image.open(d + "/alpha.png"), Image.open(d + "/nor_gl.jpg"), None, orient, int(n), int(px))
for b in barks:
    Image.open(os.path.join(dl, b, b + "_diff_1k.jpg")).convert("RGB").resize((512, 512), Image.LANCZOS).save(os.path.join(out, "bark_%s.png" % b))
    Image.open(os.path.join(dl, b, b + "_nor_gl_1k.jpg")).convert("RGB").resize((512, 512), Image.LANCZOS).save(os.path.join(out, "bark_%s_n.png" % b))
    print("bark", b)
EOF
du -sh "$OUT"
