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

## Step 3: art (materials, mood, props, scenery), hill-climbed in-engine

The loop runs in-engine:
- `exterior_capture --mission hollow --eye --target --fov` at the
  reference's viewpoint.
- A sheet of reference | render | render in greyscale (the reference is
  black and white), in `hillclimb/<item>_<iter>.jpg`.
- The 3 biggest differences are written down, the biggest is fixed, and the
  view is rendered again.

### Main house vs `house_wolcott_1938_a.jpg`
- The reference is flopped: our chimney gable is the east one.
- The camera is at the chimney gable, 3/4 view: eye (30.5, 1.6, -7.8),
  target (19.0, 3.2, -3.0), fov 48.

| Iter | Biggest differences seen (1 silhouette/proportion, 2 material/value, 3 detail) | Change | Closer? |
|---|---|---|---|
| 00 | 1. Chimney is a thin uniform column standing far above the ridge; the reference has a massive stepped field-stone stack with a broad firebox. 2. Boards read near-black; the reference boards are light silver-grey. 3. Board seams and wear do not show. | (baseline, first CC0-material render at dusk) | n/a |
| 01 | (same) | Stepped chimney: a 2.0 m firebox base, a shaft from 2.3 m, and a stack to ridge + 0.45. Sloped shoulders and a slumped-open firebox cavity. Camera moved to the reference's gable-side angle. Dog pen chain-link was an opaque sheet; it is now a wire lattice. | Yes (silhouette) |
| 02 | 1. OK. 2. Walls still almost black, and the chimney is burnt out by the sodium yard light. 3. Seams. | Board albedo graded +50% brightness and -30% saturation. Sodium light 95 -> 55. | Slightly |
| 03 | 2. Walls still black | Dusk sky ambient x2. Sodium 55 -> 32. | No visible change on the boards (kept, it helps the yard) |
| 04 | 2. The real cause: single-board walls sit inside the room-light margin, so their outside face got the interior lamp-only ambient | Hollow-only shader rule: an outward-facing surface outside the strict room box is exterior | **Yes, large**: silver-grey board-and-batten now reads |
| 05 | 1. Roof reads red/orange; the reference tin is dark grey | House and porch roofs use a darker, greyer grade of the rusty iron (`roof_tin_dark`); the barn keeps red rust | Yes |
| 06 | 3. Windows are clean teal glass; the reference has boarded and broken sash | Scrap boards nailed across 3 windows (upstairs front, east gable, kitchen) | Yes |
| 07 | 3. Battens are too even and regular | Varied batten width and tone, 12% missing, rotted short at the sill. A ladder leaned on the house, as in the reference. Found and fixed a silent failure: GLBs with more than 7 materials are refused by the loader; the scripts now assert. | Slightly |
| 08 | 1. The stepped gable fill shows as a staircase above the eave | Collision gable steps hidden; true triangular board gables with battens drawn in the trim (house, barn, smokehouse). Ladder moved to the west gable: on the porch it went through the porch roof. | Yes |
