#!/usr/bin/env python3
"""Download the CC0 terrain material sets and write them as
assets/textures/<slot>_{albedo,normal,rough,height,ao}.jpg at SIZE.

Sources: ambientCG (ambientcg.com) and Poly Haven (polyhaven.com), both CC0.
Downloads land in build/cretaceous/dl/<asset>/ and are only read as images.
Run from the repository root:  python3 -I ocean/cretaceous/tools/fetch_textures.py
Requires Pillow.
"""
import io, json, os, sys, urllib.request, zipfile
from PIL import Image

SIZE = int(os.environ.get("CRET_TEX_SIZE", "2048"))
OUT = "ocean/cretaceous/assets/textures"
DL = "build/cretaceous/dl"
# slot -> (library, asset id)
SLOTS = {
    "rock":   ("acg", "Rock030"),
    "scree":  ("ph", "rocks_ground_01"),
    "dirt":   ("acg", "Ground067"),
    "forest": ("ph", "forest_leaves_04"),
    "fern":   ("ph", "forest_leaves_02"),
    "mud":    ("ph", "brown_mud_03"),
    "sand":   ("acg", "Ground088"),
    "moss":   ("acg", "Moss003"),
}
UA = {"User-Agent": "cretaceous-fetch-textures"}

def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120).read()

def acg(asset):
    """ambientCG 2K JPG zip -> {map: PIL image}."""
    d = os.path.join(DL, asset); os.makedirs(d, exist_ok=True)
    z = os.path.join(d, "2K-JPG.zip")
    if not os.path.exists(z):
        open(z, "wb").write(get("https://ambientcg.com/get?file=%s_2K-JPG.zip" % asset))
    keys = {"albedo": "_Color", "normal": "_NormalGL", "rough": "_Roughness", "height": "_Displacement", "ao": "_AmbientOcclusion"}
    out = {}
    with zipfile.ZipFile(z) as zf:
        for name in zf.namelist():
            if not name.lower().endswith(".jpg") or "/" in name or ".." in name: continue
            for k, tag in keys.items():
                if tag in name: out[k] = Image.open(io.BytesIO(zf.read(name)))
    return out

def ph(asset):
    """Poly Haven 2K JPG maps -> {map: PIL image}."""
    d = os.path.join(DL, asset); os.makedirs(d, exist_ok=True)
    files = json.loads(get("https://api.polyhaven.com/files/" + asset))
    keys = {"albedo": "Diffuse", "normal": "nor_gl", "rough": "Rough", "height": "Displacement", "ao": "AO"}
    out = {}
    for k, tag in keys.items():
        if tag not in files: continue
        res = files[tag].get("2k") or files[tag].get("1k")
        fmt = res.get("jpg") or res.get("png")
        p = os.path.join(d, k + os.path.splitext(fmt["url"])[1])
        if not os.path.exists(p): open(p, "wb").write(get(fmt["url"]))
        out[k] = Image.open(p)
    return out

def main():
    os.makedirs(OUT, exist_ok=True)
    src = ["# Terrain textures", "", "All CC0 (public domain). Regenerate with tools/fetch_textures.py.", ""]
    for slot, (lib, asset) in SLOTS.items():
        maps = acg(asset) if lib == "acg" else ph(asset)
        for k in ["albedo", "normal", "rough", "height", "ao"]:
            im = maps.get(k)
            if im is None:
                if k == "ao": im = Image.new("L", (SIZE, SIZE), 255)
                else: sys.exit("%s (%s) has no %s map" % (slot, asset, k))
            im = im.convert("L" if k in ("rough", "height", "ao") else "RGB").resize((SIZE, SIZE), Image.LANCZOS)
            im.save(os.path.join(OUT, "%s_%s.jpg" % (slot, k)), quality=90)
        url = "https://ambientcg.com/view?id=" + asset if lib == "acg" else "https://polyhaven.com/a/" + asset
        src.append("- %s: %s" % (slot, url))
        print(slot, asset, sorted(maps))
    open(os.path.join(OUT, "SOURCES.md"), "w").write("\n".join(src) + "\n")

if __name__ == "__main__":
    main()
