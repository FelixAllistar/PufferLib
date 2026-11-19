#!/usr/bin/env python3
"""Generate motel_court_data.h (authoritative Briar Court court layer) from
tools/motel_court_spec.py.

    python3 tools/gen_motel_court.py

Each closed guest-room bay reuses the motel_v1 room kit with the instance
offsets of the authored Rooms 102-104 (a 4 m bay whose facade is at local
z=0 and rear wall at z=-6): floor, roof, walkway, canopy, facade, rear wall,
a static (locked, non-door) leaf, window insert, AC sleeve and wall lamp,
plus end walls, corner piers and walkway posts. Every row names a template
owner: the authored object whose exterior-kit and architecture-overlay art
is cloned onto the row (relative to the template's 5.0 placement). Rows are
appended after the picnic table, so every existing object index is kept.
"""
import math, os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import motel_court_spec as S

# (asset, local offset, local yaw, material, roof, template owner per variation 102/103/104)
BAY = [
    (4,  (0, 0, -3),         0, 'SWAT_CONCRETE', False, (33, 57, 81)),     # floor
    (5,  (0, 2.8, -3),       0, 'SWAT_WOOD', True,      (34, 58, 82)),     # roof
    (6,  (0, 0, 0),          0, 'SWAT_CONCRETE', False, (35, 59, 83)),     # walkway
    (7,  (0, 2.44, 0),       0, 'SWAT_WOOD', True,      (36, 60, 84)),     # canopy
    (0,  (0, 0, 0),          0, 'SWAT_PLASTER', False,  (37, 61, 37)),     # facade (104's carries the extinguisher)
    (1,  (0, 0, -6),         0, 'SWAT_PLASTER', False,  (38, 62, 86)),     # rear wall
    (9,  (-1.74, 0, 0),      0, 'SWAT_WOOD', False,     (40, 64, 88)),     # static locked leaf
    (10, (0.8, 0.88, 0.018), 0, 'SWAT_WOOD', False,     (41, 65, 89)),     # window insert
    (18, (0.8, 0.2, 0.16),   0, 'SWAT_STEEL', False,    (42, 66, 90)),     # through-wall AC
    (33, (-0.41, 2.34, 0.12), 0, 'SWAT_PLASTER', False, (45, 69, 93)),     # wall lamp
]
END_WALL = (2, (0, 0, -3), math.pi / 2, 'SWAT_PLASTER', False)             # at a bay boundary
# Authored end walls (objects): 104 = office west end at x=-12 (exterior faces
# local -x: quoins, porch lamp), 109 = Room 104's east end at x=8 (faces local
# +x: quoins, wall pack); 107 = a timber party wall with no overlay pieces.
END_TEMPLATE_NEG, END_TEMPLATE_POS, END_TEMPLATE_INT = 104, 109, 107
PIERS = {-1: (110, 111), +1: (112, 113)}         # (front, rear) corner piers at x=-12 / x=8
POST_TEMPLATE = 116                             # post at x=-4 (5.0)
WING_YAW = {'x': 0.0}

def frame(wg, along):
    """Bay frame: origin of the facade centre line and yaw (engine convention:
    world = origin + (c*lx + s*lz, ly, -s*lx + c*lz))."""
    if wg['axis'] == 'x': return (along, wg['line']), 0.0
    yaw = math.pi / 2 if wg['facing'] > 0 else -math.pi / 2
    return (wg['line'], along), yaw

def place(origin, yaw, local):
    lx, ly, lz = local; c, s = math.cos(yaw), math.sin(yaw)
    return (origin[0] + c * lx + s * lz, ly, origin[1] - s * lx + c * lz)

def num(x):
    v = f'{x:.6f}'.rstrip('0').rstrip('.')
    if v in ('-0', ''): v = '0'
    return v + ('f' if '.' in v else '.0f') if '.' in v else v

rows = []   # (asset, (x,y,z), yaw, material, roof, template, comment)
def add(asset, pos, yaw, mat, roof, template, comment):
    rows.append((asset, pos, yaw, mat, roof, template, comment))

for wg in S.WINGS:
    a0 = wg['start']; n = wg['bays']
    for k in range(n):
        along = a0 + (k + .5) * S.BAY
        # The east wing's local +x points toward the street, i.e. its first bay
        # (at the rear) is local -x of the next: keep the door at the rear side
        # of each bay like the rear wing's (west side of each bay).
        origin, yaw = frame(wg, along)
        v = (k + (1 if wg is S.EAST else 0)) % 3
        room = wg['first'] + k
        for asset, local, ly, mat, roof, templates in BAY:
            add(asset, place(origin, yaw, local), yaw + ly, mat, roof, templates[v], f'room {room}')
    # End walls at every bay boundary of the wing; exterior at the wing ends
    # unless the end abuts an existing wall (rear extension starts at room 104's wall).
    for k in range(n + 1):
        along = a0 + k * S.BAY
        if wg is S.REAR_EXT and k == 0: continue           # room 104's east wall (object 109) is the party wall
        origin, yaw = frame(wg, along)
        ext = k in (0, n)
        # Which local side of this boundary is outside: the bays run toward
        # local +x along the wing for yaw 0 and -90, toward local -x for +90.
        runs = -1 if (wg['axis'] == 'z' and wg['facing'] > 0) else +1
        side = (-runs if k == 0 else runs) if ext else 0
        template = END_TEMPLATE_INT if not ext else (END_TEMPLATE_NEG if side < 0 else END_TEMPLATE_POS)
        add(END_WALL[0], place(origin, yaw, END_WALL[1]), yaw + END_WALL[2], END_WALL[3], False,
            template, f"{wg['name']} {'end' if ext else 'party'} wall")
        if ext:
            front, rear = PIERS[side]
            add(3, place(origin, yaw, (0, 0, 0)), yaw, 'SWAT_PLASTER', False, front, 'corner pier')
            add(3, place(origin, yaw, (0, 0, -6)), yaw, 'SWAT_PLASTER', False, rear, 'corner pier')

# Walkway posts: the rear run beyond the six moved authored posts, then the side wings.
for x in S.REAR_POSTS_X[6:]:
    add(8, (x, 0, S.POST_OFFSET), 0.0, 'SWAT_STEEL', False, POST_TEMPLATE, 'rear walkway post')
for wg in (S.EAST, S.WEST):
    x = wg['line'] + wg['facing'] * S.POST_OFFSET
    yaw = math.pi / 2 if wg['facing'] > 0 else -math.pi / 2
    for z in S.side_posts(wg):
        add(8, (x, 0, z), yaw, 'SWAT_STEEL', False, POST_TEMPLATE, f"{wg['name']} post")

out = ['// Generated by tools/gen_motel_court.py from tools/motel_court_spec.py. Do not edit.',
       '// Briar Court court layer: closed guest-room bays, end walls, piers and walkway',
       '// posts appended after the picnic table (see MOTEL_LAYOUT.md). Each row: placement',
       '// (motel_v1 asset, mesh collision like the authored instances) and the authored',
       '// template owner whose exterior/architecture art is cloned onto it.',
       '#ifndef SWAT_MOTEL_COURT_DATA_H', '#define SWAT_MOTEL_COURT_DATA_H',
       'typedef struct SwatMotelCourtRow { SwatMotelInstance placement; int template_owner; } SwatMotelCourtRow;',
       'static const SwatMotelCourtRow court_rows[SWAT_MOTEL_COURT_INSTANCES]={']
for asset, (x, y, z), yaw, mat, roof, template, comment in rows:
    out.append('    {{%d,{%s,%s,%s},{1,1,1},%s,%s,false,%s},%d}, // %s' % (asset, num(x), num(y), num(z), num(yaw), mat,
               'true' if roof else 'false', template, comment))
out[-1] = out[-1].replace('}, //', '} //', 1)
out += ['};', '#endif', '']
root = os.path.dirname(here)
open(os.path.join(root, 'motel_court_data.h'), 'w').write('\n'.join(out))
open(os.path.join(root, 'motel_court_count.h'), 'w').write(
    '// Generated by tools/gen_motel_court.py. Rows in motel_court_data.h.\n#define SWAT_MOTEL_COURT_INSTANCES %d\n' % len(rows))
print('court rows', len(rows))
