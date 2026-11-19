# The Hollow: work and hill-climb log

The plan is in `ocean/swat/HOLLOW.md`. Reference photos are in `references/`;
all of them are public domain, with sources listed in HOLLOW.md section 1.

## Step 1: references and plan
- Searched the Library of Congress FSA/OWI, HABS and Highsmith collections.
  Wikimedia Commons search was not reachable from this environment (non-JSON
  responses).
- Building type confirmed as an Appalachian boxed house. The anchor is
  Wolcott's 1938 farmhouse between Morgantown and Elkins: two storeys,
  vertical boards, stone gable chimney, shed porch hood, small sash.

## Step 2: blockout (greybox), playable end to end
- Added `hollow_impl.h`, included from mission.c. It builds the authoritative
  box map: 874 objects, 8 rooms, 10 doors (3 locked), 511 breakable wall
  boards.
- The boxed-house wall is new: `hollow_wall`, one layer of 0.30 m x 25 mm
  vertical boards (SKIN, health 45) on sills and plates (SUPPORT), with
  framed-wall door leaves and glazing.
- Navigation fixes found by the test:
  - The stair was moved onto a nav row and boxed. The nav capsule clipped the
    rear-wall plate and the floor slab over the stair foot.
  - Low doors (1.8 m) were raised to 1.95 m. The nav standing capsule is
    1.83 m and refused them.
  - The cellar passage steps were shortened and the ceiling raised to 1.0 m.
- `tests/test_hollow.c` (`make hollow-test`, part of `make test`, about 3 s)
  checks:
  - Map structure.
  - Staging screened from every house window.
  - Map encode/decode and a replica rebuild.
  - Routes from staging to all 8 rooms.
  - A physical walk to the barn loft, both storeys, the smokehouse, the
    kitchen and the cellar.
  - A hostage escort out of the cellar to the lane, ending in mission success.
- The capture tool takes `--mission hollow` and has 12 review views.
  `blockout_greybox.jpg` shows the greybox with the existing default
  materials.
