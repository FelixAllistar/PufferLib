#!/usr/bin/env python3
"""Briar Court motor-court site spec (single source of truth).

Engine metres: +x east, +z toward the street (road z 36..44), ground top
y=-0.08, room floors y=0. The playable core (reception, Rooms 101-104,
laundry; x -12..8, z -10..1.83) is the authored map and does not move. This
spec adds the closed guest-room bays that wrap the lot into a U court, the
walkway posts at real spacing, and the lot paint (2.7 x 5.5 m stalls, 7 m
aisles). It is read by:
  tools/motel_site_plan.py       top-down plan renders (hill-climb)
  tools/gen_motel_court.py       motel_court_data.h (authoritative instances)
  tools/blender/motel_surroundings.py  ground_decals.png bay paint
See MOTEL_LAYOUT.md.
"""
BAY = 4.0            # room bay width (existing guest rooms are 4 m bays)
DEPTH = 6.0          # room depth, facade to rear wall
WALK = 1.83          # covered walkway depth (existing gallery_walkway_4m)
STALL_W, STALL_D, AISLE = 2.7, 5.5, 7.0
POST_PITCH_MAX = 3.0

# Wings: (name, axis, facade line, along-start, bays, facing sign, first room number)
#   axis 'x': rooms run along x, facade at z=line, rooms on the -z side (rear wing)
#   axis 'z': rooms run along z, facade at x=line, rooms behind the facade
#             (facing=+1: wing faces +x, rooms at x<line; -1: faces -x, rooms x>line)
REAR_EXT = dict(name='rear wing (105-106)', axis='x', line=0.0, start=8.0, bays=2, facing=+1, first=105)
EAST = dict(name='east wing (107-112)', axis='z', line=16.0, start=WALK, bays=6, facing=-1, first=107)
WEST = dict(name='west wing (113-119)', axis='z', line=-16.0, start=WALK - BAY, bays=7, facing=+1, first=113)
WINGS = [REAR_EXT, EAST, WEST]

def wing_rooms(wg):
    """Footprint rectangles (x0,x1,z0,z1) and labels of each closed room bay."""
    out = []
    for k in range(wg['bays']):
        a0 = wg['start'] + k * BAY; a1 = a0 + BAY
        if wg['axis'] == 'x':
            out.append(((a0, a1, wg['line'] - DEPTH, wg['line']), wg['first'] + k))
        else:
            x0, x1 = (wg['line'] - DEPTH, wg['line']) if wg['facing'] > 0 else (wg['line'], wg['line'] + DEPTH)
            out.append(((x0, x1, a0, a1), wg['first'] + k))
    return out

def wing_walk(wg):
    a0, a1 = wg['start'], wg['start'] + wg['bays'] * BAY
    if wg['axis'] == 'x': return (a0, a1, wg['line'], wg['line'] + WALK)
    if wg['facing'] > 0: return (wg['line'], wg['line'] + WALK, a0, a1)
    return (wg['line'] - WALK, wg['line'], a0, a1)

def posts(a0, a1):
    n = 1
    while (a1 - a0) / n > POST_PITCH_MAX: n += 1
    return [a0 + (a1 - a0) * k / n for k in range(n + 1)]

# Rear (main) wing walkway: office + 101-104 (existing) + 105-106 (new): x -12..16.
POST_OFFSET = 1.77                           # post centre from the facade (existing)
# The rear run ends at the east wing's post line (its corner post).
REAR_POSTS_X = posts(-12.0, EAST['line'] - POST_OFFSET)          # 10 posts at 2.91 m
def side_posts(wg):
    z = posts(wg['start'], wg['start'] + wg['bays'] * BAY)
    return z[1:] if wg is EAST else z                              # east corner post is the rear run's last

# Court = between the side-wing walkway edges and in front of the rear walkway.
COURT_X = (WEST['line'] + WALK, EAST['line'] - WALK)      # -14.17 .. 14.17
COURT_Z = (WALK, 24.0)                                      # to the lot's street edge
SEDAN_X = -6.0                                              # roadside sedan (authoritative, unchanged)

def rear_stalls():
    """Head-in row in front of the rear wing, centred on the parked sedan."""
    c = [SEDAN_X + STALL_W * k for k in range(-1, 6)]       # -8.7 .. 7.5
    return [(x - STALL_W / 2, x + STALL_W / 2, WALK, WALK + STALL_D) for x in c]

SIDE_Z0 = WALK + STALL_D + 1.2                              # side rows start clear of the rear row
def side_stalls(sign):
    x0 = COURT_X[1] - STALL_D if sign > 0 else COURT_X[0]
    out = []
    for k in range(5):
        z0 = SIDE_Z0 + k * STALL_W
        out.append((x0, x0 + STALL_D, z0, z0 + STALL_W))
    return out

ISLAND = (-1.2, 1.2, WALK + STALL_D + AISLE, 22.6)          # central concrete island (Luna Lodge)
PLANTERS = [(COURT_X[0], -10.05, WALK, WALK + STALL_D), (8.85, COURT_X[1], WALK, WALK + STALL_D)]

def stalls(): return rear_stalls() + side_stalls(-1) + side_stalls(+1)

def stall_lines():
    """Painted lines (each stall: two sides; the head line is the walkway curb)."""
    lines = []
    for (x0, x1, z0, z1) in rear_stalls():
        lines += [((x0, z0), (x0, z1)), ((x1, z0), (x1, z1))]
    for sign in (-1, 1):
        for (x0, x1, z0, z1) in side_stalls(sign):
            lines += [((x0, z0), (x1, z0)), ((x0, z1), (x1, z1))]
    return lines

# Moved authoritative objects (motel_instances indices / layer placements).
SIGN = (6.6, 31.6, -0.13)        # roadside sign at the street, beside the driveway (Luna Lodge: sign at the street)
FENCE_X, FENCE_Z0 = 23.2, 4.0    # chain-link run moved to the east property line
PICNIC = (-12.1, 4.6)            # west corner planter
WHEEL_STOPS_X = [-8.7, -6.0, -3.3, 2.1, 4.8]   # in rear-row stall centres
STAGING = (19.0, -3.0)           # east breezeway, behind the building line
EXTRACTION = (0.0, 10.0)

def plan_paint():
    """(polygon, colour) fills for the plan render, back to front."""
    out = []
    def r(x0, x1, z0, z1, col): out.append((((x0, z0), (x1, z0), (x1, z1), (x0, z1)), col))
    for p in PLANTERS: r(*p, (150, 140, 100))
    r(*ISLAND, (185, 182, 172))
    for wg in WINGS: r(*wing_walk(wg), (176, 172, 160))
    for wg in WINGS:
        for rect, n in wing_rooms(wg): r(*rect, (226, 214, 190))
    for x in REAR_POSTS_X: r(x - .09, x + .09, POST_OFFSET - .09, POST_OFFSET + .09, (30, 110, 110))
    for wg in (EAST, WEST):
        x = wg['line'] + wg['facing'] * POST_OFFSET
        for z in side_posts(wg): r(x - .09, x + .09, z - .09, z + .09, (30, 110, 110))
    x, z, _ = SIGN; r(x - 1.6, x + 1.6, z - .35, z + .35, (20, 90, 70))
    r(FENCE_X - .05, FENCE_X + .05, FENCE_Z0, FENCE_Z0 + 6, (60, 60, 60))
    r(PICNIC[0] - .9, PICNIC[0] + .9, PICNIC[1] - .8, PICNIC[1] + .8, (150, 110, 70))
    for x in WHEEL_STOPS_X: r(x - .9, x + .9, 4.3 - .12, 4.3 + .12, (200, 200, 190))
    return out

def plan_labels():
    out = []
    for wg in WINGS:
        for (x0, x1, z0, z1), n in wing_rooms(wg): out.append(((x0 + x1) / 2 - .8, (z0 + z1) / 2 - .5, str(n)))
    out += [(-11, -4, 'office'), (-7, -3, '101'), (-3, -3, '102'), (1, -3, '103'), (5, -3, '104'), (-11, -9, 'laundry')]
    out += [(ISLAND[0] - .5, (ISLAND[2] + ISLAND[3]) / 2, 'island'), (SIGN[0] - 1, SIGN[1] + 1, 'sign')]
    return out

if __name__ == '__main__':
    rooms = 4 + sum(w['bays'] for w in WINGS)
    print('guest rooms', rooms, 'stalls', len(stalls()), 'rear posts', REAR_POSTS_X, 'side posts', [len(side_posts(w)) for w in (EAST, WEST)])
    print('court x', COURT_X, 'z', COURT_Z)

DIRT = (176, 158, 124)
# Weathered gravel/dirt zones painted over the lot slab so the paved area is the
# court and a rear service drive, not a 48 m square plain (decal paint).
GRAVEL_ZONES = [(-24.0, 24.0, -24.0, -13.5),        # rear yard beyond the service drive
                (-24.0, -22.6, -13.5, 24.0),        # behind the west wing
                (22.6, 24.0, -13.5, 24.0)]          # behind the east wing
def ground_paint():
    """Ground-level surface zones (plan render; decal paint follows the same zones)."""
    out = []
    for (x0, x1, z0, z1) in GRAVEL_ZONES:
        out.append((((x0, z0), (x1, z0), (x1, z1), (x0, z1)), DIRT))
    return out
