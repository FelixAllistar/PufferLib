#!/usr/bin/env python3
"""Route check for the presentation-only interior props layer v2.

  props_interior_clearance.py TRACKS.txt [--radius 0.25]

The radius is the visible half-width of a character (shoulders ~0.25 m); the
movement hull is 0.41 m (body.c), so feet centres never come closer than
0.41 m to a real wall and a wall piece up to ~0.16 m deep can only be touched,
never entered, by a character body.

TRACKS.txt is the output of build/swat/props_interior_clearance (every
present actor's feet position each tick of recorded motel scenarios). Each
floor-standing or low wall piece in motel_props_interior_v2_data.h is turned
into its world footprint (bounds from the asset's .json, placement yaw) and
inflated by the actor radius; the script reports every actor sample inside a
footprint. Tabletop pieces (on a desk or counter) are skipped: their support
is a real collider. Exit status 1 if any route crosses a prop."""
import json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ASSETS = os.path.join(ROOT, "assets/environment/motel_props_int_v2")
TABLETOP_OWNERS = {"24", "48", "72", "96", "121", "135"}

def pieces():
    src = open(os.path.join(ROOT, "motel_props_interior_v2_data.h")).read()
    files = re.search(r"props_v2_files\[[^\]]*\]=\{(.*?)\};", src, re.S).group(1)
    files = re.findall(r'"([^"]+)"', files)
    enum = re.search(r"enum \{(.*?)SWAT_PROPS_V2_ASSETS", src, re.S).group(1)
    names = [n.strip() for n in enum.replace("\n", "").split(",") if n.strip()]
    table = src[src.index("props_v2_pieces[]"):]
    table = table[:table.index("};")]
    out = []
    for m in re.finditer(r"\{(PIV2_\w+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),(true|false)\}", table):
        kind, owner, x, y, z, yaw, wall = m.groups()
        def num(e):
            e = e.replace("PIV2_ROOM", "room").replace("SWAT_PI", str(math.pi)).replace("f", "")
            return float(eval(e, {"room": lambda r: -6 + 4 * r}))
        out.append(dict(name=files[names.index(kind)], owner=owner.strip(), x=num(x), y=num(y), z=num(z), yaw=num(yaw), wall=wall == "true"))
    return out

# Pieces whose bounding box spans open space: their real floor footprints
# (local x0, x1, z0, z1). Drapes: the two stacks; the ceiling track between
# them is overhead.
PARTS = {"drapes.glb": [(.86, 1.10, 0, .13), (-1.08, -.86, 0, .13)]}
PARTS["drapes_avocado.glb"] = PARTS["drapes.glb"]

def footprints(p):
    meta = json.load(open(os.path.join(ASSETS, p["name"].replace(".glb", ".json"))))
    lo, hi = meta["min"], meta["max"]
    if p["y"] + lo[1] > 1.9:            # Above head height.
        return []
    parts = PARTS.get(p["name"], [(lo[0], hi[0], lo[2], hi[2])])
    return [footprint(p, (x0, lo[1], z0), (x1, hi[1], z1)) for x0, x1, z0, z1 in parts]

def footprint(p, lo, hi):
    c, s = math.cos(p["yaw"]), math.sin(p["yaw"])
    pts = []
    for lx in (lo[0], hi[0]):
        for lz in (lo[2], hi[2]):
            pts.append((p["x"] + c * lx + s * lz, p["z"] - s * lx + c * lz))
    xs = [q[0] for q in pts]; zs = [q[1] for q in pts]
    return (min(xs), max(xs), min(zs), max(zs))

def main(argv):
    radius = float(argv[argv.index("--radius") + 1]) if "--radius" in argv else .25
    boxes = []
    for p in pieces():
        if p["owner"] in TABLETOP_OWNERS:
            continue
        for fp in footprints(p):
            boxes.append((p, fp))
    hits = {}
    samples = 0
    with open(argv[0]) as f:
        for line in f:
            sc, tick, actor, x, y, z = line.split(); x = float(x); z = float(z); samples += 1
            if float(y) > 1.0:
                continue
            for p, (x0, x1, z0, z1) in boxes:
                dx = max(x0 - x, x - x1, 0.0); dz = max(z0 - z, z - z1, 0.0)
                if dx * dx + dz * dz < radius * radius - 1e-6:
                    k = (p["name"], round(x0, 2), round(z0, 2))
                    hits.setdefault(k, {}).setdefault((sc, actor), []).append((int(tick), round(x, 2), round(z, 2)))
    print(f"{len(boxes)} footprints, {samples} actor samples, radius {radius} m")
    for p, fp in boxes:
        k = (p["name"], round(fp[0], 2), round(fp[2], 2))
        print(f"  {p['name']:22s} x {fp[0]:7.2f}..{fp[1]:7.2f} z {fp[2]:6.2f}..{fp[3]:6.2f}  "
              + (f"CROSSED by {len(hits[k])} actor tracks" if k in hits else "clear"))
        for (sc, actor), pts in sorted(hits.get(k, {}).items()):
            print(f"      scenario {sc} actor {actor}: {len(pts)} ticks, e.g. {pts[0]} .. {pts[-1]}")
    return 1 if hits else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
