# Briar Court interior props v2: wall-mounted tourist brochure rack (reception).
# Four stepped wooden tiers of rack cards, after a classic visitor-centre
# brochure rack. Pamphlet covers are made-up generic graphics baked from
# seeded numpy noise (no real brands, logos or text). Wood: Poly Haven CC0.
# Origin: on the wall plane at the bottom centre of the rack; front faces +Z.
# Mount it with its origin 0.75 m above the floor.
#   blender -b --factory-startup --python-exit-code 1 \
#       -P ocean/swat/tools/blender/props_int_v2_brochure_rack.py -- OUT_DIR [--render PNG] [--iter N]
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

reset()
rng = np.random.default_rng(1971)

W, H, DEP = .50, .80, .10
ROWS, COLS = 4, 4
T = .012                                    # board thickness

def tex_covers(seed, n=512, grid=4):
    """Atlas of grid x grid made-up brochure covers (rack cards, 4x9 in)."""
    r = np.random.default_rng(seed)
    img = np.ones((n, n, 3))
    c = n // grid
    palettes = [(214, 84, 52), (46, 120, 170), (236, 196, 64), (70, 150, 92), (180, 60, 120),
                (240, 140, 40), (60, 70, 150), (200, 40, 40), (90, 170, 190), (130, 90, 50)]
    for gy in range(grid):
        for gx in range(grid):
            y0, x0 = gy * c, gx * c
            cell = np.ones((c, c, 3)) * srgb((238, 234, 222))
            band = srgb(palettes[r.integers(len(palettes))])
            style = r.integers(3)
            yy, xx = np.mgrid[0:c, 0:c] / c
            if style == 0:            # full-bleed colour with a picture window
                cell[:] = band
                win = (yy > .32) & (yy < .78) & (xx > .1) & (xx < .9)
                sky = srgb(palettes[r.integers(len(palettes))]) * .5 + .5
                ground = srgb((90, 120, 60)) * (.7 + .6 * r.random())
                horizon = .55 + .1 * np.sin(xx * 9 + r.random() * 6) * .5
                pic = np.where((yy < horizon)[..., None], sky, ground)
                cell[win] = pic[win]
            elif style == 1:          # white card, colour header and footer
                cell[(yy > .3) & (yy < .5)] = band
                cell[yy > .86] = band * .7
                sun = ((xx - .5) ** 2 + (yy - .5) ** 2 < .012) & (r.random() < .5)
                cell[sun] = srgb((240, 190, 60))
            else:                     # light stock, photo panel in the lower half
                cell[:] = band * .25 + .75 * srgb((246, 242, 230))
                win = (yy > .5) & (yy < .9) & (xx > .08) & (xx < .92)
                tree = periodic_noise(r, c, 6, octaves=3)
                pic = srgb((60, 96, 58)) * (.6 + .8 * tree[..., None]) * (yy[..., None] > .62) \
                    + srgb((150, 180, 205)) * (yy[..., None] <= .62)
                cell[win] = pic[win]
            # Blocky "title" bars (not text): dark on light stock.
            dark = srgb((30, 30, 36)) if style != 0 else srgb((250, 248, 240))
            for k in range(r.integers(2, 4)):
                ty = .05 + k * .065
                w = .4 + .45 * r.random()
                m = (yy > ty) & (yy < ty + .035) & (xx > .1) & (xx < .1 + w)
                cell[m] = dark
            # Fine grey "body copy" lines on the light area (not text).
            for k in range(6):
                ly = .62 + k * .035
                if r.random() < .8:
                    m = (yy > ly) & (yy < ly + .012) & (xx > .12) & (xx < .12 + .5 + .3 * r.random())
                    cell[m] = cell[m] * .55 + .45 * srgb((70, 70, 70))
            # Pastel wash: most rack cards are printed on coloured stock.
            if r.random() < .8:
                tint = srgb(palettes[r.integers(len(palettes))]) * .5 + .5
                cell *= tint
            # Paper grain and a faint fold shadow.
            cell *= (.93 + .07 * r.random((c, c)))[..., None]
            img[y0:y0 + c, x0:x0 + c] = cell
    return img

covers = to_image("brochure_covers", tex_covers(42))
wood_img = polyhaven("oak_veneer_01", "diff", "1k", size=512)
m_wood = mat("rack_wood", (1.0, .62, .36), image=wood_img, rough=.55)
m_card = mat("rack_cards", image=covers, rough=.6)
m_edge = mat("rack_card_edge", (.88, .86, .80), rough=.8)

p = Part()
# Back board and side panels.
p.add(bm_box((0, H / 2, T / 2), (W / 2, H / 2, T / 2)), m_wood, uv_scale=1.2)
for sx in (-1, 1):
    p.add(bm_box((sx * (W / 2 - T / 2), H / 2, DEP / 2), (T / 2, H / 2, DEP / 2)), m_wood, uv_scale=1.2)
TIER = H / ROWS
LIP = .04
cw = (W - 2 * T) / COLS
for r in range(ROWS):
    y = r * TIER
    # Tier floor (sloping back slightly) and front lip.
    p.add(bm_box((0, y + T / 2, DEP / 2), (W / 2 - T, T / 2, DEP / 2)), m_wood, uv_scale=1.2)
    p.add(bm_box((0, y + LIP / 2, DEP - T / 2), (W / 2 - T, LIP / 2, T / 2)), m_wood, uv_scale=1.2)
    for c in range(COLS):
        if (r * COLS + c) in (9,):          # a couple of empty slots
            continue
        cx = -W / 2 + T + cw * (c + .5)
        lean = .16 + .05 * rng.random()
        ch = .19 + .015 * rng.random()
        k = r * COLS + c
        u0, v0 = (k % 4) / 4, 1 - (k // 4 + 1) / 4
        # Card stack: thin box leaning back against the board behind the lip.
        thick = .010 + .010 * rng.random()
        bm = bm_box((cx, T + ch / 2, DEP - T - .012 - thick / 2), (cw / 2 - .004, ch / 2, thick / 2))
        bmesh.ops.translate(bm, vec=E(0, y, 0), verts=bm.verts)
        tilt_bm(bm, -lean, 'x', (cx, y + T, DEP - T - .012))
        # Front face gets the cover cell, the rest plain paper edge.
        p.add(bm, m_edge)
        # Cover: a quad slightly in front of the stack's face.
        def fn(u, v, cx=cx, ch=ch, thick=thick):
            return (cx - (cw / 2 - .005) + u * (cw - .010), T + v * ch, DEP - T - .012 + .0006)
        q = bm_grid(1, 1, fn, smooth=False)
        bmesh.ops.translate(q, vec=E(0, y, 0), verts=q.verts)
        tilt_bm(q, -lean, 'x', (cx, y + T, DEP - T - .012))
        idx = p.mat_index(m_card)
        vmap = {v: p.bm.verts.new(v.co) for v in q.verts}
        for f in q.faces:
            nf = p.bm.faces.new([vmap[v] for v in f.verts]); nf.material_index = idx
            for l, src in zip(nf.loops, f.loops):
                co = src.vert.co
                uu = (co.x - (cx - (cw / 2 - .005))) / (cw - .010)
                vv = (co.z - (y + T)) / (ch * math.cos(lean))
                l[p.uv].uv = (u0 + .02 / 4 + uu * .96 / 4, v0 + .02 / 4 + min(max(vv, 0), 1) * .96 / 4)
        q.free()
# Top cap.
p.add(bm_box((0, H + T / 2, DEP / 2), (W / 2, T / 2, DEP / 2 + .004)), m_wood, uv_scale=1.2)

export("brochure_rack", p)
# Reference: "SM Brochure Rack" (vastateparksstaff, Flickr, CC BY 2.0): near
# frontal view from slightly left, eye level just above the rack's middle.
REF_VIEW = {"cam": (-.15, .55, 2.05), "target": (0, .42, .05), "fov": 32, "res": (900, 680),
            "room": {"box": (-1.2, 1.2, 0, 2.5, 2.6), "walls": ["back", "left", "right"], "wall": (.80, .76, .66), "floor": (.4, .33, .25)},
            "lights": [((.2, 1.8, 1.4), 120, (1, .95, .85), .8), ((-1.0, 1.0, 1.5), 40, (1, 1, 1), 1.5)]}
preview(REF_VIEW)
