#!/usr/bin/env python3
"""Top-down site plan of Briar Court from a motel_site_dump CSV.

    motel_site_dump > site.csv
    python3 tools/motel_site_plan.py site.csv out.png [--court] [--title T]

Draws every authoritative object footprint at a fixed 8 px/m (north = -z up,
street at the bottom), the lot slab, painted stalls from the court spec
(tools/motel_court_spec.py, when --court) and a 10 m scale bar. Used for the
plan hill-climb in reports/cloud/motel-layout/LOG.md.
"""
import csv, math, sys, os
from PIL import Image, ImageDraw, ImageFont
PX = 8.0
X0, X1, Z0, Z1 = -34.0, 34.0, -30.0, 46.0
def to_px(x, z): return ((x - X0) * PX, (z - Z0) * PX)
def rect(d, cx, cz, hx, hz, yaw, fill, outline=None):
    c, s = math.cos(yaw), math.sin(yaw)
    pts = []
    for lx, lz in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz)):
        # world from local: x = c*lx + s*lz, z = -s*lx + c*lz (engine yaw convention)
        pts.append(to_px(cx + c * lx + s * lz, cz - s * lx + c * lz))
    d.polygon(pts, fill=fill, outline=outline)
# Objects the court spec moves (drawn from the spec in --court previews of a
# map that still has them at their old places): canopy posts, sign, wheel
# stops, fence owners and pieces, picnic table.
HIDDEN = set(range(114, 120)) | set(range(140, 146)) | set(range(155, 159)) | set(range(957, 1044)) | set(range(1422, 1472))
GROUND = (1119, 1173)   # SWAT_MOTEL_GROUND_FIRST .. +SWAT_GROUND_PARTS
spec = None
def main():
    src, out = sys.argv[1], sys.argv[2]
    court = '--court' in sys.argv
    global spec
    if court:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import motel_court_spec as spec
    title = sys.argv[sys.argv.index('--title') + 1] if '--title' in sys.argv else os.path.basename(src)
    rows = list(csv.DictReader(open(src)))
    im = Image.new('RGB', (int((X1 - X0) * PX), int((Z1 - Z0) * PX)), (214, 196, 160))
    d = ImageDraw.Draw(im)
    # road and driveway (authoritative ground supports, motel_ground placement manifest)
    d.rectangle([*to_px(-64, 36), *to_px(64, 44)], fill=(70, 70, 74))
    for x in range(-34, 34, 6): d.line([to_px(x, 40), to_px(x + 3, 40)], fill=(220, 180, 60), width=2)
    objs = [r for r in rows if r['kind'] == 'obj']
    slab_mat = objs[0]['material']
    # ground supports (motel_ground: soil / asphalt) and the lot slab first
    for r in objs[GROUND[0]:GROUND[1]] + objs[:1]:
        col = (96, 96, 100) if r['material'] == slab_mat else (214, 196, 160)
        rect(d, float(r['cx']), float(r['cz']), float(r['hx']), float(r['hz']), 0, col)
    if court:
        for poly, col in spec.ground_paint():
            d.polygon([to_px(*p) for p in poly], fill=col)
    if court:
        for poly, col in spec.plan_paint():
            d.polygon([to_px(*p) for p in poly], fill=col)
        for a, b in spec.stall_lines():
            d.line([to_px(*a), to_px(*b)], fill=(235, 235, 225), width=2)
    for r in objs:
        i = int(r['index']); active = r['active'] == '1'
        cx, cy, cz = float(r['cx']), float(r['cy']), float(r['cz'])
        hx, hy, hz, yaw = float(r['hx']), float(r['hy']), float(r['hz']), float(r['yaw'])
        if GROUND[0] <= i < GROUND[1]: continue
        if i == 0 or not active or (court and i in HIDDEN): continue
        top = cy + hy
        if cy - hy > 2.3:   # roofs and canopies: translucent
            continue
        part = int(r['part'])
        if top < 0.25 and hy < 0.2: fill = (176, 172, 160)       # slabs, walkways, ground supports
        elif r['door'] == '1': fill = (150, 60, 50)
        else: fill = (238, 232, 214)
        rect(d, cx, cz, hx, hz, yaw, fill, (60, 55, 50))
    # roofs as outlines
    for r in objs:
        cy, hy = float(r['cy']), float(r['hy'])
        if r['active'] == '1' and cy - hy > 2.3 and float(r['hx']) < 20:
            rect(d, float(r['cx']), float(r['cz']), float(r['hx']), float(r['hz']), float(r['yaw']), None, (40, 90, 90))
    for r in rows:
        if r['kind'] in ('staging', 'extraction'):
            x, y = to_px(float(r['cx']), float(r['cz'])); col = (40, 160, 60) if r['kind'] == 'staging' else (60, 90, 220)
            d.ellipse([x - 7, y - 7, x + 7, y + 7], outline=col, width=3); d.text((x + 9, y - 6), r['kind'], fill=col)
        if r['kind'] == 'actor':
            x, y = to_px(float(r['cx']), float(r['cz']))
            d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(200, 30, 30) if r['part'] != '0' else (30, 120, 220))
    if court:
        for (x, z, label) in spec.plan_labels():
            d.text(to_px(x, z), label, fill=(20, 20, 20))
    # scale bar and title
    sx, sy = 20, im.height - 24
    for k in range(5):
        d.rectangle([sx + k * 2 * PX, sy, sx + (k + 1) * 2 * PX, sy + 6], fill=(0, 0, 0) if k % 2 == 0 else (255, 255, 255), outline=(0, 0, 0))
    d.text((sx, sy - 14), '10 m', fill=(0, 0, 0)); d.text((10, 8), title + '  (street at bottom, 8 px/m)', fill=(0, 0, 0))
    im.save(out)
main()
