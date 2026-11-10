#!/usr/bin/env python3
"""Packs the data of the Cretaceous web build (tools/package_web.sh runs it).

The page (web/loader.js) downloads the bundles below before main starts and
writes every file into the in-memory file system at the path the native game
opens, so the C code loads them unchanged. Static hosting gives no
Content-Encoding, so the bundles are gzipped here and inflated in the browser
with DecompressionStream, and each file on the server stays at or under
--part-mb (default 14 MiB): a bigger bundle is split into numbered parts that
the page streams back together.

Bundles:
  core    shaders, config (water look, scenarios), the credits and licences
          (every *.md of the assets directory) and the HUD font with its licence
  assets  textures (ground materials re-encoded at --tex-size), foliage,
          props, dinosaurs and audio: whatever the assets directory holds;
          anything missing is drawn with the game's fallbacks
  world   the baked island (build/cretaceous/worlds/s1_n2048_*.terra),
          filtered so gzip finds the redundancy: each float field is delta
          coded along rows as 32-bit integers and split into byte planes,
          the byte fields are split into planes and delta coded. Lossless.
          Fields the game never reads after the bake (rain, temperature,
          soil; checked by searching the sources) are zeroed unless
          --keep-all-fields.
  cache   extra prebaked files (--cache SRC=DEST), e.g. the cloud noise.

Usage (from the repository root):
  python3 -I ocean/cretaceous/tools/web_pack.py --out build/cretaceous/web \\
      --assets ocean/cretaceous/assets --world build/cretaceous/worlds/s1_n2048_X.terra
Needs Pillow for the texture re-encode (without it the textures ship as they are).
"""
import argparse, glob, gzip, io, json, os, re, struct, sys, time

SRC = "ocean/cretaceous"
GAME_ASSETS = SRC + "/assets"           # where the game opens the assets (relative to the cwd, /)
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"   # first path hud.c tries
FONT_LICENSE = "/usr/share/doc/fonts-dejavu-core/copyright"
TERRA_HEADER = 32
# terra.c's TERRA_FIELDS order: (name, bytes per sample)
TERRA_FIELDS = [("height", 4), ("water", 4), ("flow", 4), ("rain", 4), ("soil", 4), ("temp", 4),
                ("water_kind", 1), ("flow_dir", 2), ("splat", 8), ("veg", 8), ("hardness", 1)]
BAKE_ONLY = ("rain", "soil", "temp")


def log(*a):
    print(*a, flush=True)


def runtime_reads(field):
    """A game source outside terra/, tools/ and tests/ that reads TerraMap.field
    (as map.field, m->field, ...), or None. Assignments (a world built in code,
    like the gallery's plain) are not reads."""
    use = re.compile(r"\b(map|m|M|tm|world)\s*(->|\.)\s*%s\b" % field)
    write = re.compile(r"\s*(\[[^\]]*\])?\s*=(?!=)")
    for path in glob.glob(SRC + "/**/*.[ch]", recursive=True):
        rel = os.path.relpath(path, SRC)
        if rel.startswith(("terra" + os.sep, "tools" + os.sep, "tests" + os.sep)):
            continue
        for line in open(path, errors="replace"):
            code = line.split("//")[0]
            for mt in use.finditer(code):
                if not write.match(code, mt.end()):
                    return path
    return None


def filter_terra(raw, keep_all):
    """Returns (filtered bytes, segment list for the page's inverse filter)."""
    import numpy as np
    magic, version, _hash, n = struct.unpack_from("<4sIQi", raw, 0)
    if magic != b"TERA":
        raise SystemExit("not a .terra file")
    N = n * n
    out = [raw[:TERRA_HEADER]]
    segs, o = [], TERRA_HEADER
    for name, k in TERRA_FIELDS:
        seg = np.frombuffer(raw, np.uint8, N * k, o)
        if name in BAKE_ONLY and not keep_all:
            user = runtime_reads(name)
            if user:
                log("  world: keeping %s (read by %s)" % (name, user))
            else:
                seg = np.zeros(N * k, np.uint8)
        if k == 4:
            a = seg.view(np.uint32).reshape(n, n)
            d = a.copy()
            d[:, 1:] = a[:, 1:] - a[:, :-1]          # wraps: lossless
            d[1:, 0] = a[1:, 0] - a[:-1, 0]
            planes = d.view(np.uint8).reshape(N, 4).T.copy()
            segs.append({"o": o, "k": 4, "n": n, "m": "f32"})
        else:
            planes = seg.reshape(N, k).T.reshape(k, n, n).copy()
            planes[:, :, 1:] = planes[:, :, 1:] - planes[:, :, :-1]   # uint8 wraps
            segs.append({"o": o, "k": k, "n": n, "m": "u8"})
        out.append(planes.tobytes())
        o += N * k
    if o != len(raw):
        raise SystemExit("terra size mismatch: %d fields bytes vs %d file bytes (TERRA_FIELDS changed?)" % (o, len(raw)))
    return b"".join(out), segs


def reencode_texture(path, size):
    try:
        from PIL import Image
    except ImportError:
        return open(path, "rb").read()
    im = Image.open(path)
    if im.width <= size and im.height <= size:
        return open(path, "rb").read()
    im = im.resize((size, size), Image.LANCZOS)
    if im.mode not in ("RGB", "L"):
        im = im.convert("RGB")
    b = io.BytesIO()
    im.save(b, "JPEG", quality=90)
    return b.getvalue()


class Bundle:
    def __init__(self, name):
        self.name, self.files, self.parts, self.size = name, [], [], 0
        self.data = []
        self.filter = None

    def add(self, dest, data):
        self.files.append([dest, self.size, len(data)])
        self.data.append(data)
        self.size += len(data)


def write_bundle(b, out_dir, part_bytes):
    if not b.files:
        return None
    blob = b"".join(b.data)
    t0 = time.time()
    z = gzip.compress(blob, 9, mtime=0)
    parts = []
    for i in range(0, len(z), part_bytes):
        name = "data/%s.%03d.bin" % (b.name, len(parts))
        open(os.path.join(out_dir, name), "wb").write(z[i:i + part_bytes])
        parts.append(name)
    log("  %-6s %4d files %8.1f MB -> %7.1f MB gzip in %d part(s) (%.0f s)" %
        (b.name, len(b.files), len(blob) / 1048576, len(z) / 1048576, len(parts), time.time() - t0))
    entry = {"name": b.name, "size": len(blob), "csize": len(z), "parts": parts, "files": b.files}
    if b.filter:
        entry["filter"] = b.filter
    return entry


def walk(d):
    for root, dirs, files in os.walk(d):
        dirs.sort()
        for f in sorted(files):
            yield os.path.join(root, f)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="the web build directory (holds index.html)")
    ap.add_argument("--assets", default=GAME_ASSETS, help="assets directory (default %(default)s)")
    ap.add_argument("--world", help="baked world (.terra); default: the newest build/cretaceous/worlds/s1_n2048_*.terra")
    ap.add_argument("--cache", action="append", default=[], metavar="SRC=DEST", help="ship a prebaked file at DEST (repeatable)")
    ap.add_argument("--tex-size", type=int, default=1024, help="ground material size on web (default %(default)s; 0 keeps the files)")
    ap.add_argument("--part-mb", type=float, default=14.0, help="largest file on the server, MiB (default %(default)s)")
    ap.add_argument("--keep-all-fields", action="store_true", help="ship the world's bake-only fields too")
    a = ap.parse_args()

    os.makedirs(os.path.join(a.out, "data"), exist_ok=True)
    for old in glob.glob(os.path.join(a.out, "data", "*.bin")):
        os.remove(old)
    part = int(a.part_mb * 1048576)

    core, assets, world, cache = Bundle("core"), Bundle("assets"), Bundle("world"), Bundle("cache")
    for d in ("shaders", "config"):
        for p in walk(os.path.join(SRC, d)):
            core.add(os.path.relpath(p, "."), open(p, "rb").read())
    if os.path.exists(FONT):
        core.add(FONT, open(FONT, "rb").read())
        if os.path.exists(FONT_LICENSE):
            core.add(GAME_ASSETS + "/LICENSE-DejaVu.txt", open(FONT_LICENSE, "rb").read())
    else:
        log("  note: no %s here; the HUD uses raylib's built-in font" % FONT)

    have = {"textures": 0, "foliage": 0, "props": 0, "dinos": 0, "audio": 0}
    if not os.path.isdir(a.assets):
        raise SystemExit("assets directory %s not found" % a.assets)
    for p in walk(a.assets):
        rel = os.path.relpath(p, a.assets).replace(os.sep, "/")
        if os.path.basename(rel).startswith("."):
            continue
        dest = GAME_ASSETS + "/" + rel
        top = rel.split("/")[0]
        if rel.lower().endswith((".md", ".txt")) or "licen" in rel.lower():
            core.add(dest, open(p, "rb").read())     # credits and licences always ship
            continue
        if top in have:
            have[top] += 1
        if top == "textures" and rel.lower().endswith(".jpg") and a.tex_size:
            assets.add(dest, reencode_texture(p, a.tex_size))
        else:
            assets.add(dest, open(p, "rb").read())
    log("  assets: " + ", ".join("%s %d files" % (k, v) for k, v in have.items()))

    wpath = a.world
    if not wpath:
        c = sorted(glob.glob("build/cretaceous/worlds/s1_n2048_*.terra"), key=os.path.getmtime)
        wpath = c[-1] if c else None
    if wpath:
        filtered, segs = filter_terra(open(wpath, "rb").read(), a.keep_all_fields)
        world.add("build/cretaceous/worlds/" + os.path.basename(wpath), filtered)
        world.filter = segs
    else:
        log("  note: no baked world; the browser will bake it (minutes, single-threaded)")

    for spec in a.cache:
        s, d = spec.split("=", 1)
        if os.path.exists(s):
            cache.add(d, open(s, "rb").read())
        else:
            log("  note: no %s; the browser derives it" % s)

    bundles = [e for e in (write_bundle(b, a.out, part) for b in (core, world, cache, assets)) if e]
    # The in-memory file system is gone on reload, so the derived-data cache
    # (render/cache.c) would only hold a few hundred MB of RAM: off on web.
    env = {"CRET_CACHE": "0"}
    if a.tex_size:
        env["CRET_MAT_SIZE"] = str(a.tex_size)
    manifest = {"version": 1, "bundles": bundles, "env": env}
    json.dump(manifest, open(os.path.join(a.out, "data", "manifest.json"), "w"), indent=1)
    total = sum(os.path.getsize(p) for p in walk(a.out) if not p.endswith(".wasm"))
    big = max((p for p in walk(a.out) if not p.endswith(".wasm")), key=os.path.getsize)
    log("  download: %.1f MB in total; largest file %s (%.1f MB)" % (total / 1048576, os.path.relpath(big, a.out), os.path.getsize(big) / 1048576))


if __name__ == "__main__":
    main()
