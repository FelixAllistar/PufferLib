# The Hollow: props (assets/environment/hollow_v1/props/), one GLB each.
# Placement and colliders: hollow_data.h (local origin = floor centre of the
# collider box, front toward +Z, metres, engine +Y up).
#
#   blender -b --factory-startup --python-exit-code 1 -P hollow_props.py -- OUT_DIR TEX_DIR [--only a,b] [--render PNG]
#
# TEX_DIR holds hollow_textures.py output (procedural, made here). Other maps
# are CC0 Poly Haven scans fetched by props_int_v2_common.polyhaven().
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import props_int_v2_common as C
from props_int_v2_common import Part, bm_box, bm_cyl, bm_sphere, bm_tube, bm_grid, rotate_bm, tilt_bm, soften, mat
import numpy as np
import bpy

TEX = C.ARGS[1] if len(C.ARGS) > 1 and not C.ARGS[1].startswith("--") else "hollow_textures"
ONLY = set(C.ARGS[C.ARGS.index("--only") + 1].split(",")) if "--only" in C.ARGS else None

def image(name):
    img = bpy.data.images.load(os.path.join(os.path.abspath(TEX), name)); img.pack(); return img

def M():
    """Shared material palette (created lazily per Blender session)."""
    ph = C.polyhaven
    return {
        "wood": mat("wood_old", (1, 1, 1), .85, image=ph("old_planks_02", "diff", "1k", 512), normal=ph("old_planks_02", "nor_gl", "1k", 512)),
        "grey": mat("wood_grey", (1, 1, 1), .9, image=ph("wood_planks_grey", "diff", "1k", 512), normal=ph("wood_planks_grey", "nor_gl", "1k", 512)),
        "rust": mat("rust", (1, 1, 1), .8, .3, image=ph("rusty_metal_02", "diff", "1k", 512), normal=ph("rusty_metal_02", "nor_gl", "1k", 512)),
        "tin": mat("tin", (1, 1, 1), .75, .4, image=ph("rusty_corrugated_iron", "diff", "1k", 512)),
        "bus": mat("bus_paint", (.62, .42, .08), .7, .1, image=ph("rusty_painted_metal", "diff", "1k", 512)),
        "car": mat("car_paint", (.28, .30, .26), .65, .2, image=ph("rusty_painted_metal", "diff", "1k", 512)),
        "white": mat("appliance", (.78, .76, .70), .5, .05),
        "black": mat("black_iron", (.05, .05, .05), .6, .6),
        "tyre": mat("tyre", (.04, .04, .04), .9),
        "glass": mat("dirty_glass", (.10, .11, .10), .25, .2),
        "chrome": mat("dull_chrome", (.45, .45, .44), .35, .9),
        "fabric": mat("old_fabric", (.30, .24, .17), .95),
        "plaid": mat("recliner", (.30, .18, .12), .95),
        "mattress": mat("mattress", (1, 1, 1), .95, image=image("mattress.png")),
        "tarp": mat("tarp", (1, 1, 1), .55, image=image("tarp.png")),
        "flyers": mat("flyers", (1, 1, 1), .9, image=image("flyer_wall.png")),
        "skin": mat("pale_skin", (.50, .44, .40), .6),
        "hay": mat("hay", (.52, .43, .22), .95),
        "canvas": mat("canvas", (.42, .40, .33), .9),
        "lime": mat("lime_bag", (.50, .49, .45), .9),
        "soil": mat("turned_soil", (.13, .09, .06), 1.0),
        "chain": mat("chain", (.18, .15, .13), .45, .8),
        "leaf": mat("laurel", (.028, .042, .022), .8),
        "jar": mat("jar_glass", (.32, .30, .18), .2, .1),
        "leather": mat("shoe_leather", (.12, .09, .07), .7),
        "sneaker": mat("shoe_canvas", (.40, .40, .42), .9),
        "red": mat("faded_red", (.35, .08, .06), .8),
        "lino": mat("linoleum", (1, 1, 1), .6, image=image("linoleum.png")),
    }

PROPS = {}
def prop(fn):
    PROPS[fn.__name__] = fn; return fn

def legs(p, m, xs, zs, top, r=.025):
    for x in xs:
        for z in zs:
            p.add(bm_box((x, top / 2, z), (r, top / 2, r)), m)

@prop
def stall_partition(m):
    # Stall side: board panel on posts, with a slatted gate at the aisle end
    # hanging slightly open (HABS WV-266-C stalls).
    p = Part(); rng = np.random.default_rng(14)
    for i in range(5):  # horizontal boards on two posts, 1.3 m
        L = 1.45 - (.42 if i % 2 else .38)
        p.add(bm_box((-1.45 + L, .12 + i * .27, 0), (L, .11, .025)), m["grey"])
    for x in (-1.45, .55, 1.45):
        p.add(bm_box((x, .7, 0), (.06, .7, .06)), m["grey"])
    gate = Part()
    for k in range(6):  # vertical slats with a rounded top
        z = .62 + k * .14
        p.add(rotate_bm(bm_box((z, .65 + .05 * math.sin(k / 5 * math.pi), .07), (.035, .6 + .05 * math.sin(k / 5 * math.pi), .012)), .25, (.55, 0, 0)), m["wood"])
    for y in (.25, 1.05):
        p.add(rotate_bm(bm_box((1.0, y, .07), (.42, .04, .015)), .25, (.55, 0, 0)), m["wood"])
    return p

@prop
def workbench(m):
    p = Part(); rng = np.random.default_rng(3)
    p.add(bm_box((0, .86, 0), (.35, .04, 1.1)), m["wood"])
    p.add(bm_box((0, .2, 0), (.3, .02, 1.0)), m["wood"])
    legs(p, m["wood"], (-.3, .3), (-1.0, 1.0), .82, .04)
    p.add(bm_box((.33, 1.2, 0), (.02, .3, 1.1)), m["grey"])  # pegboard against the wall
    for k in range(6):  # hanging tools: saw, hammer handles, a cleaver shape
        z = -.9 + k * .35
        p.add(bm_box((.29, 1.25, z), (.01, .14, .03)), m["black"] if k % 2 else m["wood"])
    p.add(bm_box((-.05, .93, -.5), (.12, .03, .18)), m["rust"])  # vise / box
    p.add(bm_box((0, .92, .4), (.2, .02, .25)), m["canvas"])      # a rag
    return p

@prop
def chest_freezer(m):
    p = Part()
    p.add(bm_box((0, .43, 0), (.4, .43, .75), bevel=.03, seg=2), m["white"])
    p.add(bm_box((0, .885, 0), (.41, .025, .76), bevel=.01), m["white"])
    p.add(bm_box((.415, .8, 0), (.01, .03, .2)), m["chrome"])
    # Hasp and padlock on the front: locked shut.
    p.add(bm_box((.42, .78, 0), (.012, .05, .025)), m["chrome"])
    p.add(bm_box((.46, .70, 0), (.02, .045, .035)), m["rust"])
    p.add(bm_tube([(.44, .76, -.02), (.47, .79, 0), (.44, .76, .02)], .006, 6, cap=False), m["chrome"])
    p.add(bm_box((0, .02, 0), (.41, .02, .76)), m["rust"])  # rusted base
    return p

@prop
def hay_bale(m):
    p = Part(); rng = np.random.default_rng(9)
    p.add(soften(bm_box((0, .22, 0), (.42, .22, .8), bevel=.05, seg=2), rng, .012), m["hay"])
    for z in (-.4, .4):
        p.add(bm_box((0, .22, z), (.425, .225, .01)), m["canvas"])
    return p

@prop
def couch(m):
    p = Part()
    p.add(bm_box((0, .22, 0), (.45, .2, .95), bevel=.04, seg=2), m["fabric"])
    p.add(bm_box((-.32, .55, 0), (.13, .32, .95), bevel=.05, seg=2), m["fabric"])
    for z in (-.85, .85):
        p.add(bm_box((0, .45, z), (.45, .16, .1), bevel=.04, seg=2), m["fabric"])
    for z in (-.45, .45):  # sagging cushions
        p.add(bm_box((.05, .46, z), (.38, .06, .44), bevel=.04, seg=2), m["fabric"])
    return p

@prop
def recliner(m):
    p = Part()
    p.add(bm_box((0, .25, 0), (.42, .22, .42), bevel=.05, seg=2), m["plaid"])
    p.add(tilt_bm(bm_box((-.32, .68, 0), (.12, .32, .42), bevel=.05, seg=2), -.18, 'z', (-.32, .4, 0)), m["plaid"])
    for z in (-.38, .38):
        p.add(bm_box((0, .55, z), (.4, .08, .07), bevel=.03, seg=2), m["plaid"])
    return p

@prop
def kitchen_table(m):
    p = Part()
    p.add(bm_box((0, .74, 0), (.5, .025, .8)), m["wood"])
    legs(p, m["wood"], (-.43, .43), (-.72, .72), .72, .03)
    p.add(bm_box((-.75, .45, .3), (.2, .02, .2)), m["wood"])  # a chair pushed back
    legs(p, m["wood"], (-.92, -.58), (.14, .46), .44, .018)
    p.add(bm_box((-.92, .7, .3), (.02, .25, .2)), m["wood"])
    p.add(bm_cyl((.1, .8, -.3), .05, .1, 10), m["white"])  # a mug, plates
    p.add(bm_cyl((-.1, .77, .1), .12, .02, 14), m["white"])
    return p

@prop
def wood_stove(m):
    p = Part()
    p.add(bm_box((0, .45, 0), (.28, .3, .33), bevel=.01), m["black"])
    legs(p, m["black"], (-.24, .24), (-.28, .28), .16, .02)
    p.add(bm_cyl((0, 1.6, .15), .075, 2.0, 12), m["black"])  # stovepipe up to the ceiling
    p.add(bm_box((.285, .45, 0), (.01, .14, .18)), m["rust"])
    return p

@prop
def counter(m):
    p = Part()
    p.add(bm_box((0, .43, 0), (.3, .43, 1.0)), m["wood"])
    p.add(bm_box((0, .885, 0), (.31, .02, 1.01)), m["grey"])
    p.add(bm_box((.05, .87, .3), (.2, .01, .25)), m["chrome"])  # enamel sink
    for z in (-.6, -.1, .5):
        p.add(bm_box((.305, .5, z), (.005, .3, .22)), m["grey"])
    return p

@prop
def mattress(m):
    p = Part(); rng = np.random.default_rng(4)
    p.add(soften(bm_box((0, .11, 0), (.95, .11, .7), bevel=.05, seg=2), rng, .004), m["mattress"], uv_mode="fit")
    return p

@prop
def flyer_table(m):
    p = Part()
    p.add(bm_box((0, .74, 0), (.3, .02, .7)), m["wood"])
    legs(p, m["wood"], (-.25, .25), (-.62, .62), .72, .025)
    for k in range(5):  # loose papers, a box of clippings
        p.add(rotate_bm(bm_box((-.05 + .04 * k, .765, -.4 + .18 * k), (.1, .002, .14)), .3 * k), m["flyers"], uv_mode="fit")
    p.add(bm_box((.05, .82, .45), (.15, .07, .12)), m["canvas"])
    return p

@prop
def flyer_wall(m):
    # Papered board panel on the east wall of the flyer room (faces -X).
    p = Part()
    p.add(rotate_bm(bm_box((0, 1.45, 0), (.006, .85, .9)), 0), m["flyers"], uv_mode="fitside")
    return p

@prop
def wardrobe(m):
    p = Part()
    p.add(bm_box((0, .9, 0), (.4, .9, .25), bevel=.01), m["wood"])
    p.add(bm_box((0, .9, .255), (.38, .85, .005)), m["grey"])
    p.add(bm_box((.12, .95, .27), (.01, .05, .01)), m["chrome"])
    return p

@prop
def cot(m):
    p = Part()
    p.add(bm_box((0, .38, 0), (.95, .03, .4)), m["mattress"], uv_mode="fit")  # stained ticking pad
    for x in (-.9, .9):
        p.add(bm_box((x, .2, 0), (.02, .2, .4)), m["rust"])
    for z in (-.38, .38):
        p.add(bm_box((0, .36, z), (.95, .02, .02)), m["rust"])
    # Restraints: leather straps at both ends, buckled to the frame.
    for x in (-.7, .7):
        p.add(bm_box((x, .41, 0), (.04, .01, .41)), m["leather"])
        p.add(bm_box((x, .3, .42), (.04, .1, .006)), m["leather"])
        p.add(bm_box((x, .3, -.42), (.04, .1, .006)), m["leather"])
    p.add(bm_box((-.4, .41, .1), (.25, .02, .2)), m["mattress"], uv_mode="fit")  # a stained pillow ticking
    return p

@prop
def jar_shelf(m):
    p = Part()
    for y in (.02, .5, 1.0, 1.45):
        p.add(bm_box((0, y, 0), (.2, .02, 1.6)), m["grey"])
    for z in (-1.55, 0, 1.55):
        p.add(bm_box((0, .75, z), (.2, .75, .025)), m["grey"])
    rng = np.random.default_rng(12)
    for y in (.04, .52, 1.02):
        for k in range(10):
            z = -1.4 + k * .3 + rng.normal(0, .03)
            if rng.random() < .25: continue
            p.add(bm_cyl((0, y + .11, z), .055, .2, 10), m["jar"])
    return p

@prop
def salt_box(m):
    # Salting bench: a shallow trough on four legs with a shelf below
    # (HABS MO-1220 smokehouse bench), crusted with old salt.
    p = Part(); salt = mat("old_salt", (.55, .52, .46), .95)
    legs(p, m["grey"], (-.55, .55), (-.2, .2), .74, .035)
    p.add(bm_box((0, .76, 0), (.6, .03, .25)), m["grey"])                 # trough floor
    for z in (-.24, .24):
        p.add(bm_box((0, .86, z), (.6, .1, .015)), m["grey"])               # trough sides
    for x in (-.59, .59):
        p.add(bm_box((x, .86, 0), (.015, .1, .25)), m["grey"])
    p.add(bm_box((0, .795, 0), (.56, .008, .22)), salt)                    # crust of salt
    p.add(bm_box((0, .25, 0), (.56, .02, .22)), m["grey"])                 # lower shelf
    p.add(bm_cyl((-.3, .34, 0), .09, .16, 10), m["rust"])                    # a crock / can
    return p

@prop
def school_bus(m):
    # An old school bus body set down on cinder blocks, wheels gone, lived in
    # and then abandoned (FSA 1941 photo): low on its blocks, rounded roof,
    # faded paint, a rear door and windows soaped over.
    p = Part(); rng = np.random.default_rng(21)
    # Procedural faded chrome-yellow paint, chalked and streaked, with rust blooms.
    nrng = np.random.default_rng(7)
    n1 = C.periodic_noise(nrng, 256, 3.0); n2 = C.periodic_noise(nrng, 256, 10.0)
    rust = np.clip((n1 * .85 + n2 * .15 - .66) * 4, 0, 1)
    yellow = np.array([.62, .50, .20]) * (.85 + .25 * n2[..., None])
    streak = (np.sin(np.linspace(0, 40, 256))[None, :] * .5 + .5) * .08
    rgb = yellow * (1 - streak[..., None]) * (1 - rust[..., None]) + np.array([.24, .11, .05]) * rust[..., None]
    paint = mat("bus_faded", (1, 1, 1), .8, .05, image=C.to_image("bus_paint", rgb))
    y0 = .38
    p.add(bm_box((0, y0 + 1.0, .2), (1.2, 1.0, 4.5), bevel=.08, seg=2), paint)
    # Rounded roof: a squashed half-cylinder along the body.
    roof = bm_cyl((0, y0 + 2.0, .2), 1.2, 9.0, 16, axis='z', smooth=True)
    for v in roof.verts:
        if v.co.z < (y0 + 2.0): v.co.z = y0 + 2.0
        else: v.co.z = y0 + 2.0 + (v.co.z - (y0 + 2.0)) * .3
    p.add(roof, paint)
    p.add(bm_box((0, y0 + .5, -4.55), (1.15, .45, .35), bevel=.08), paint)  # hood stub
    for k in range(9):
        for x in (-1.205, 1.205):
            p.add(bm_box((x, y0 + 1.45, -3.4 + k * .85), (.01, .3, .36)), m["glass"])
    p.add(bm_box((0, y0 + 1.45, -4.25), (1.1, .35, .01)), m["glass"])
    # Rear: emergency door and two windows.
    p.add(bm_box((.5, y0 + .95, 4.705), (.4, .9, .01)), m["wood"])
    for x in (-.62, .5):
        p.add(bm_box((x, y0 + 1.62, 4.705), (.36, .26, .01)), m["glass"])
    for x in (-1.21, 1.21):
        p.add(bm_box((x, y0 + .95, .2), (.012, .04, 4.4)), m["black"])  # rub rails
    p.add(bm_box((.5, y0 + 1.62, 4.716), (.4, .3, .012)), m["wood"])            # boarded rear window
    for k in (2, 6):
        p.add(bm_box((1.218, y0 + 1.45, -3.4 + k * .85), (.012, .33, .4)), m["wood"])  # plywood over side windows
    letters = mat("bus_letters", (1, 1, 1), .8, image=image("bus_letters.png"))
    p.add(bm_box((-.58, y0 + .55, 4.708), (.5, .19, .004)), letters, uv_mode="fit")  # rear lettering panel
    for z in (-3.2, 3.0):
        for x in (-.9, .9):
            p.add(bm_box((x, y0 / 2, z), (.2, y0 / 2, .4)), m["grey"])  # cinder blocks
    return p

@prop
def junk_car(m):
    p = Part()
    p.add(bm_box((0, .55, 0), (.88, .32, 2.2), bevel=.1, seg=2), m["car"])
    p.add(bm_box((0, 1.05, .2), (.78, .25, 1.1), bevel=.12, seg=2), m["car"])
    for z in (-.95, 1.35):
        p.add(tilt_bm(bm_box((0, 1.05, z), (.74, .2, .01)), .5 if z < 0 else -.5, 'x', (0, 1.05, z)), m["glass"])
    for x in (-.79, .79):
        p.add(bm_box((x, 1.05, .2), (.01, .18, .9)), m["glass"])
    for x in (-.8, .8):
        for z in (-1.4, 1.4):
            p.add(bm_cyl((x, .3, z), .3, .2, 14, axis='x'), m["tyre"])
    p.add(bm_box((0, .45, -2.21), (.85, .1, .01)), m["chrome"])
    return p

@prop
def camper(m):
    p = Part()
    p.add(bm_box((0, 1.45, 0), (1.15, 1.05, 3.0), bevel=.25, seg=3), m["white"])
    p.add(bm_box((0, 1.0, 0), (1.16, .05, 3.0)), m["red"])
    p.add(bm_box((1.16, 1.5, .8), (.01, .9, .35)), m["wood"])  # door
    p.add(bm_box((1.16, 1.7, -1.2), (.01, .3, .5)), m["glass"])
    p.add(bm_box((0, .35, 3.5), (.06, .06, .6)), m["rust"])  # tongue
    for x in (-1.0, 1.0):
        p.add(bm_cyl((x, .3, 0), .3, .2, 14, axis='x'), m["tyre"])
    return p

@prop
def burn_barrel(m):
    p = Part()
    p.add(bm_cyl((0, .45, 0), .29, .9, 18, cap=False), m["rust"])
    p.add(bm_cyl((0, .7, 0), .27, .02, 18), m["black"])  # ash
    p.add(bm_box((.05, .74, .05), (.12, .03, .08)), m["fabric"])  # scorched cloth, half burnt
    return p

@prop
def woodpile(m):
    p = Part(); rng = np.random.default_rng(31)
    for x in (-.22, .22):
        log_stack(p, m, 5, 8, -1.1, .3, .12, .23, 'x', .4, rng, (x, 0))
    return p

@prop
def laurel_berm(m):
    p = Part(); rng = np.random.default_rng(41)
    p.add(soften(bm_box((0, .5, 0), (3.6, .5, 1.6), bevel=.4, seg=3), rng, .08), m["soil"])
    leaf2 = mat("laurel_dark", (.05, .04, .02), .85)
    for k in range(34):  # mountain laurel / rhododendron thicket on top, dark and dense
        x = rng.uniform(-3.3, 3.3); z = rng.uniform(-1.3, 1.3)
        s = soften(bm_sphere((x, 1.35 + rng.uniform(-.25, .45), z), .55 + .35 * rng.random(), (1.3, .75, 1.0), 7, 4), rng, .07)
        p.add(s, m["leaf"] if k % 3 else leaf2)
    for k in range(14):  # bare stems
        x = rng.uniform(-3.2, 3.2); z = rng.uniform(-1.2, 1.2)
        p.add(bm_tube([(x, .9, z), (x + rng.normal(0, .3), 2.3, z + rng.normal(0, .3))], .02, 4, cap=False), m["chain"])
    return p

@prop
def pen_post(m):
    p = Part()
    p.add(bm_cyl((0, .9, 0), .03, 1.8, 8), m["rust"])
    return p

@prop
def dog_pen_wire(m):
    # Chain-link panels between the four posts (3 x 3 m), gate ajar, chain on a stake, empty dish.
    p = Part()
    wire = mat("chain_link", (.30, .30, .29), .5, .7)
    def fence(x0, z0, x1, z1, yaw=0.0, about=(0, 0, 0)):
        # An open wire lattice (see-through), not a sheet: verticals and rails.
        L = math.hypot(x1 - x0, z1 - z0); n = int(L / .16)
        for k in range(n + 1):
            t = k / n; x = x0 + (x1 - x0) * t; z = z0 + (z1 - z0) * t
            p.add(rotate_bm(bm_box((x, .9, z), (.003, .88, .003)), yaw, about), wire)
        for y in (.05, .45, .9, 1.35, 1.76):
            p.add(rotate_bm(bm_box(((x0 + x1) / 2, y, (z0 + z1) / 2), (max(abs(x1 - x0) / 2, .004), .006, max(abs(z1 - z0) / 2, .004))), yaw, about), wire)
    fence(-1.5, -1.5, 1.5, -1.5); fence(1.5, -1.5, 1.5, 1.5); fence(-1.5, 1.5, -1.5, -1.5)
    fence(-1.5, 1.5, 0.0, 1.5, .6, (-1.5, 0, 1.5))  # gate hanging open
    p.add(bm_cyl((0, .15, 0), .02, .3, 6), m["rust"])  # stake
    pts = [(0, .1, 0)] + [(.12 * k, .02, .2 * k) for k in range(1, 7)]
    p.add(bm_tube(pts, .012, 5), m["chain"])
    p.add(bm_cyl((.8, .03, .9), .14, .06, 14), m["chrome"])  # dish
    p.add(bm_box((.3, .01, -.6), (.25, .01, .35)), m["soil"])  # dug-out hollow
    return p

@prop
def mailbox_flyer(m):
    p = Part()
    p.add(bm_box((0, .6, 0), (.05, .6, .05)), m["grey"])
    p.add(bm_cyl((0, 1.28, 0), .12, .45, 14, axis='z'), m["rust"])
    p.add(bm_box((0, 1.2, .0), (.13, .08, .2)), m["rust"])
    p.add(bm_box((.06, .85, 0), (.002, .13, .1)), m["flyers"], uv_mode="fitside")  # a flyer stapled to the post
    return p

@prop
def lime_and_shovel(m):
    p = Part(); rng = np.random.default_rng(51)
    for k in range(4):
        p.add(soften(bm_box((-.6 + .38 * (k % 2), .12 + .22 * (k // 2), -.3 + .45 * (k // 2) + .1 * k), (.18, .1, .3), bevel=.05, seg=2), rng, .01), m["lime"])
    # Shovel stuck upright in the turned earth.
    p.add(tilt_bm(bm_cyl((.8, .75, .4), .018, 1.2, 8), .2, 'z', (.8, .2, .4)), m["wood"])
    p.add(tilt_bm(bm_box((.75, .15, .4), (.12, .15, .01)), .2, 'z', (.8, .2, .4)), m["rust"])
    p.add(bm_box((-.2, .005, .7), (.6, .005, .3)), m["lime"])  # spilt lime on the ground
    return p

@prop
def turned_ground(m):
    p = Part(); rng = np.random.default_rng(61)
    # A long low mound of fresh dark earth, about the length of a person.
    mound = bm_grid(24, 10, lambda u, v: ((u - .5) * 2.4, .16 * math.sin(math.pi * u) ** .6 * math.sin(math.pi * v) ** .8, (v - .5) * 1.0))
    p.add(soften(mound, rng, .015), m["soil"])
    return p

@prop
def hooks_chains(m):
    # Smokehouse: a beam across the joists with meat hooks and hanging chains.
    p = Part()
    p.add(bm_box((0, 2.1, 0), (1.4, .05, .06)), m["grey"])
    p.add(bm_box((0, 2.1, -.9), (1.4, .05, .06)), m["grey"])
    for k in range(9):
        x = -1.2 + k * .3; z = 0 if k % 2 else -.9
        L = .45 + .3 * (k % 3) + .1 * (k % 2)
        pts = [(x, 2.05 - t * L / 6, z + .02 * math.sin(t)) for t in range(7)]
        p.add(bm_tube(pts, .01, 5), m["chain"])
        y = 2.05 - L
        p.add(bm_tube([(x, y, z), (x, y - .12, z), (x + .06, y - .17, z), (x + .1, y - .1, z)], .008, 5), m["chrome"])
    return p

@prop
def tarp_shape(m):
    # A blue tarp over a long shape on the barn floor; one hand slightly shows.
    p = Part(); rng = np.random.default_rng(71)
    def h(u, v):
        x = (u - .5) * 2.2; z = (v - .5) * 1.4
        body = .24 * max(0, 1 - (z / .45) ** 2) ** .7 * (1 if abs(x) < .85 else max(0, 1 - (abs(x) - .85) / .25))
        return (x, .015 + body + .02 * math.sin(u * 17) * math.sin(v * 13), z)
    p.add(soften(bm_grid(28, 16, h, double=True), rng, .006), m["tarp"], uv_mode="fit")
    # The hand: palm and four curled fingers, palm down, just past the edge.
    p.add(bm_box((.35, .035, .78), (.05, .02, .045), bevel=.015, seg=2), m["skin"])
    for k in range(4):
        x = .31 + .028 * k
        p.add(bm_tube([(x, .035, .82), (x, .03, .87), (x + .005, .015, .9)], .009, 6), m["skin"])
    return p

@prop
def shoe_pile(m):
    # Too many shoes: a heap of mismatched adult shoes in the last stall.
    p = Part(); rng = np.random.default_rng(81)
    for k in range(34):
        r = math.sqrt(rng.random()) * .55; a = rng.uniform(0, 2 * math.pi)
        x, z = r * math.cos(a), r * math.sin(a); y = .05 + .32 * (1 - r / .55) * rng.random()
        mm = [m["leather"], m["sneaker"], m["red"], m["canvas"]][k % 4]
        s = bm_box((x, y, z), (.045, .04, .13), bevel=.02, seg=1)
        s = rotate_bm(s, rng.uniform(0, math.pi), (x, y, z))
        p.add(tilt_bm(s, rng.uniform(-.5, .5), 'x', (x, y, z)), mm)
    return p

@prop
def clothesline(m):
    p = Part()
    for z in (-3.0, 3.0):
        p.add(bm_box((0, 1.0, z), (.05, 1.0, .05)), m["grey"])
        p.add(bm_box((0, 1.95, z), (.5, .04, .04)), m["grey"])
    for x in (-.4, .4):
        sag = [(x, 1.92 - .25 * math.sin(math.pi * t / 8), -3 + .75 * t) for t in range(9)]
        p.add(bm_tube(sag, .004, 4, cap=False), m["chain"])
    # A single stained work shirt and a sheet left out.
    p.add(bm_box((-.4, 1.45, -1.0), (.005, .3, .35)), m["canvas"])
    p.add(bm_box((.4, 1.2, 1.0), (.005, .55, .7)), m["mattress"], uv_mode="fitside")
    return p

@prop
def wood_chair(m):
    p = Part()
    p.add(bm_box((0, .45, 0), (.21, .02, .21)), m["wood"])
    legs(p, m["wood"], (-.18, .18), (-.18, .18), .44, .02)
    p.add(bm_box((-.19, .75, 0), (.02, .3, .2)), m["wood"])
    return p

@prop
def bucket(m):
    p = Part()
    p.add(bm_cyl((0, .17, 0), .15, .34, 14, r2=.12, cap=True), m["rust"])
    p.add(bm_tube([(-.15, .34, 0), (0, .5, 0), (.15, .34, 0)], .005, 4, cap=False), m["chrome"])
    return p

@prop
def yard_light_pole(m):
    # Wooden utility pole with a dusk-to-dawn sodium head (lighting.c hollow_night_lights).
    p = Part()
    p.add(bm_cyl((0, 3.6, 0), .13, 7.2, 10, r2=.11), m["grey"])
    p.add(bm_box((-.35, 6.6, 0), (.4, .03, .03)), m["rust"])
    p.add(bm_box((-.8, 6.52, 0), (.18, .07, .13), bevel=.03), m["black"])
    p.add(bm_box((-.8, 6.44, 0), (.15, .02, .1)), mat("sodium_lens", (1.0, .55, .2), .3, emission=(1.0, .5, .15)))
    return p

@prop
def ladder(m):
    # A wooden ladder left leaning on the front wall, as in the reference photo.
    p = Part(); L = 4.6; lean = .32
    for x in (-.22, .22):
        p.add(tilt_bm(bm_box((x, L / 2, 0), (.025, L / 2, .035)), lean, 'x', (x, 0, 0)), m["grey"])
    for k in range(13):
        y = .3 + k * .33
        p.add(tilt_bm(bm_box((0, y, 0), (.22, .018, .018)), lean, 'x', (0, 0, 0)), m["grey"])
    return p

@prop
def tree_stand(m):
    # Hunter's ladder stand against a lone oak by the lane: the "Lane bend"
    # overwatch post stands on its platform (its collider is the stone plinth).
    p = Part(); top = .97
    p.add(bm_cyl((0, 5.0, .75), .32, 10.0, 8, r2=.18, smooth=False), m["grey"])
    for x in (-.38, .38):
        p.add(bm_box((x, top / 2, 0), (.03, top / 2, .03)), m["rust"])
    for k in range(4):
        p.add(bm_box((0, .2 + k * .25, -.03), (.36, .015, .015)), m["rust"])
    p.add(bm_box((0, top, 0), (.85, .03, .85)), m["wood"])
    p.add(bm_box((0, top + .5, -.83), (.85, .02, .02)), m["rust"])
    for k in range(3):  # bare limbs
        a = 1.2 + k * 1.7
        p.add(bm_tube([(0, 4 + k * 1.5, .75), (2.2 * math.cos(a), 6 + k * 1.5, .75 + 2.2 * math.sin(a))], .07, 5, cap=False), m["grey"])
    return p

def log_stack(p, m, rows, cols, x0, dx, y0, dy, axis, length, rng, center=(0, 0)):
    """Split logs: bark on the sides, pale end grain on the cut faces."""
    end = mat("end_grain", (.42, .34, .24), .9)
    for row in range(rows):
        for k in range(cols):
            r = .09 + .015 * rng.random(); a = x0 + k * dx + rng.normal(0, .015); y = y0 + row * dy
            c = (a, y, center[1] + rng.normal(0, .02)) if axis == 'z' else (center[0] + rng.normal(0, .02), y, a)
            p.add(bm_cyl(c, r, length, 7, axis=axis, cap=False), m["grey"])
            for s in (-1, 1):
                cap = (c[0], c[1], c[2] + s * length / 2) if axis == 'z' else (c[0] + s * length / 2, c[1], c[2])
                p.add(bm_cyl(cap, r * .96, .01, 7, axis=axis), end)

@prop
def porch_woodpile(m):
    p = Part(); rng = np.random.default_rng(32)
    log_stack(p, m, 4, 7, -.66, .22, .1, .2, 'z', .46, rng)
    return p

@prop
def washtub(m):
    p = Part()
    p.add(bm_cyl((0, .16, 0), .33, .32, 16, r2=.36, cap=False), m["rust"])
    p.add(bm_cyl((0, .01, 0), .33, .02, 16), m["rust"])
    for x in (-.37, .37):
        p.add(bm_box((x, .28, 0), (.03, .015, .05)), m["rust"])
    return p

@prop
def lino_rug(m):
    # A printed linoleum rug (2.4 x 2.0 m) laid over the boards, edges curling.
    p = Part()
    p.add(bm_grid(12, 10, lambda u, v: ((u - .5) * 2.0, .003 + .012 * (max(0, abs(u - .5) - .42) + max(0, abs(v - .5) - .42)) * 8, (v - .5) * 2.4)), m["lino"], uv_mode="top", uv_scale=1 / 2.42, uv_offset=(.5, .5))
    return p

if __name__ == "__main__":
    os.makedirs(C.OUT, exist_ok=True)
    for name, fn in PROPS.items():
        if ONLY and name not in ONLY: continue
        C.reset(); C.MATS.clear()
        part = fn(M())
        assert len(part.mats) <= 7, (name, len(part.mats))  # hollow_art.h loader limit
        ob = C.export(name, part)
        if C.RENDER and ONLY and len(ONLY) == 1:
            C.preview({"cam": (2.2, 1.6, 2.6), "target": (0, .5, 0), "fov": 45, "sun": ((-.4, -.8, -.3), 2.5),
                       "lights": [((2, 2.5, 2), 150, (1, .8, .6), .3)], "sky": (.18, .18, .2), "ambient": .5}, ob)
