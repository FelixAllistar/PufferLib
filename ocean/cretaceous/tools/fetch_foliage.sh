#!/bin/bash
# Re-downloads the CC0 photo sources for the vegetation atlas and re-extracts
# them into ocean/cretaceous/assets/foliage/ (see SOURCES.md there):
#   sprites/<set>_<nn>.png  single leaves / fronds / sprays, RGBA, base at the
#                           bottom edge, tip at the top, sorted by size
#   bark_<name>.png         tileable bark albedo, 512x512
# Needs curl, unzip and python3 with Pillow. Run from the repository root:
#   bash ocean/cretaceous/tools/fetch_foliage.sh
# The renderer composites these into its procedural plant atlas at startup
# and falls back to painted layers when the files are missing.
set -e
OUT=ocean/cretaceous/assets/foliage
DL=build/cret_foliage/dl
mkdir -p "$OUT/sprites" "$DL"
UA="cretaceous-foliage-fetch"

# ambientCG atlases (CC0): id  orientation  max_sprites  max_px
ACG="
LeafSet019 cw 3 512
LeafSet022 up 6 256
LeafSet018 up 3 256
LeafSet024 up 9 256
LeafSet013 up 6 256
LeafSet020 up 5 256
LeafSet001 up 6 256
LeafSet002 up 6 256
LeafSet004 up 6 256
Foliage001 up 9 512
Foliage006 up 6 512
LeafSet010 up 6 512
LeafSet025 up 9 512
LeafSet014 up 6 256
"
# Poly Haven (CC0): fern_02 leaf texture (diffuse + alpha) and bark albedo.
PH_FERN="https://dl.polyhaven.org/file/ph-assets/Models/png/2k/fern_02/fern_02_diff_2k.png https://dl.polyhaven.org/file/ph-assets/Models/png/2k/fern_02/fern_02_alpha_2k.png"
PH_BARK="metasequoia_bark palm_bark bark_brown_02 japanese_cedar_bark"

echo "$ACG" | while read -r id orient n px; do
    [ -z "$id" ] && continue
    mkdir -p "$DL/$id"
    if [ ! -f "$DL/$id/${id}_1K-PNG_Color.png" ]; then
        curl -sfL -A "$UA" -o "$DL/$id/a.zip" "https://ambientcg.com/get?file=${id}_1K-PNG.zip"
        (cd "$DL/$id" && unzip -o -q a.zip "*_Color.png" "*_Opacity.png" && rm a.zip)
    fi
done
mkdir -p "$DL/fern_02"
for u in $PH_FERN; do [ -f "$DL/fern_02/$(basename "$u")" ] || curl -sfL -A "$UA" -o "$DL/fern_02/$(basename "$u")" "$u"; done
for b in $PH_BARK; do
    mkdir -p "$DL/$b"
    f="$DL/$b/${b}_diff_1k.jpg"
    [ -f "$f" ] || curl -sfL -A "$UA" -o "$f" "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/$b/${b}_diff_1k.jpg"
done

python3 -I - "$DL" "$OUT" "$ACG" "$PH_BARK" <<'EOF'
import sys, os, glob
from collections import deque
from PIL import Image
dl, out, acg, barks = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4].split()
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
    seen = [[False] * w for _ in range(h)]; boxes = []; lab = [[-1] * w for _ in range(h)]
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

def save_sprites(name, color, alpha, orient, nmax, px):
    rgba = color.convert("RGB"); rgba.putalpha(alpha.convert("L"))
    W, H = rgba.size; k = 0
    for (x0, y0, x1, y1), mk in components(alpha)[:nmax]:
        box = (max(x0, 0), max(y0, 0), min(x1, W), min(y1, H))
        sp = rgba.crop(box)
        a = Image.composite(sp.getchannel("A"), Image.new("L", sp.size, 0), mk.crop(box)); sp.putalpha(a)
        if orient == "cw": sp = sp.rotate(-90, expand=True)
        elif orient == "ccw": sp = sp.rotate(90, expand=True)
        sc = px / max(sp.size)
        if sc < 1: sp = sp.resize((max(1, int(sp.width * sc)), max(1, int(sp.height * sc))), Image.LANCZOS)
        sp.save(os.path.join(sdir, "%s_%02d.png" % (name, k))); k += 1
    print("%-12s %d sprites" % (name, k))

for line in acg.strip().splitlines():
    sid, orient, n, px = line.split()
    d = os.path.join(dl, sid)
    save_sprites(sid, Image.open(glob.glob(d + "/*_Color.png")[0]), Image.open(glob.glob(d + "/*_Opacity.png")[0]), orient, int(n), int(px))
d = os.path.join(dl, "fern_02")
save_sprites("fern_02", Image.open(d + "/fern_02_diff_2k.png"), Image.open(d + "/fern_02_alpha_2k.png"), "up", 8, 512)
for b in barks:
    Image.open(os.path.join(dl, b, b + "_diff_1k.jpg")).convert("RGB").resize((512, 512), Image.LANCZOS).save(os.path.join(out, "bark_%s.png" % b))
    print("bark", b)
EOF
du -sh "$OUT"
