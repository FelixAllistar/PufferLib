# Dry-country hill-climb log (round 3)

One line per pass: the fault, the change, kept or reverted, why. Views (960x540 while iterating, Mesa llvmpipe;
`name:x:z:above:yaw:pitch:fov`): `desB` 3500:4300:260:-2.2:-0.3:70 (the near-desert from the air), `desA`
2400:4100:120:-0.785:-0.25:70, `desG` 2760:3700:1.8:0.3:-0.02:70 and `desH` 3050:3650:2:-0.8:-0.03:70 (on the ground
in it), `dAir` 2944:3584:480:0.3:-1.1:70 (straight down on its heart from 480 m), `dPan` 2944:3584:25:1.0:-0.5:70,
`grs` 2444:3384:30:0:-0.15:70, `grB` 3600:2150:180:0.8:-0.45:70 (dry grassland and forest from the air), `grG`
3776:1984:1.8:0.8:-0.02:70 (in the driest open country). References (looked at, not committed): a stony saltbush
plain (Atriplex vesicaria, SW Queensland), Atriplex glauca up close, a cracked reservoir bed (Ouirgane, Morocco).

1. Fault: from the air the near-desert and the dry grassland are one straw-cream; the splat debug view shows the
   desert is mostly stony ground (scree) and bare soil, yet it drew cream, because the dry tint was weighed by the
   splat weights while the height blend drew stones or soil. Change: `terrain.fs` keeps the drawn share of rock,
   scree, soil and sand from the material blend and weighs the dry tints by it; golden straw for the grass (olive
   where the drainage keeps it greener, mottled at 17 m by tussocks), warm red-brown for the desert's bare earth
   near and far, stones in dry country reddened like varnished desert pavement, pans and washes a pale grey-buff
   instead of beach gold. Kept: `desB` and `dAir` show a red-brown desert with dark stony patches apart from the
   golden grass; `grB` a little more golden; the ground shots (inside 20 m) barely change.
2. Fault: no cracked earth, and in fact no pans: the bake's pans and washes covered 0.2% of the near-desert
   (`CRET_TERRA_DBG=1` now prints their share and the three sandiest spots), because three quarters of the desert
   is steeper than 27 degrees and the pan rule asked for flat hollows and still lost to the soil. Change:
   `fetch_textures.py` fetches Poly Haven's `mud_cracked_dry_03` (CC0) as a ninth ground layer, `pan`, which
   `view.c` loads after the eight splat materials (`GROUND_LAYERS`) and `terrain.fs` draws in place of sand
   wherever the ground is dry (the beach never is); `terra.c` lets the pans take most of the desert's flat ground
   and win over the soil there (2.3% of the desert, all of its valley floors), no trees on them; bake version 28.
   Kept: `pG` (on a pan, 1.8 m) is sun-baked polygons of cracked mud; `pA` and `dAir` show pale pans down the
   valley floors. Still: the pan is near white from 480 m and sand at the desert's very edge keeps the beach's
   gold.
3. Fault: the desert scrub is 1-2 m green bushes, a few dozen to the hectare; no low dwarf shrub. Change:
   `veg_gen.h` `gen_dwarf`, a new shrub-layer species (`flora.h` `FL_DWARF`, listed last so no other species
   moves; it takes the second seeds of gunnera and evergreen scrub): a dome 0.2-0.55 m high and up to a metre
   across of 10-15 wiry stems of weathered grey wood, forking into twigs, leaf tufts of the fine scale-leaf
   spray down to the ground, in two lots of dryness (khaki and straw). Its habitat: dry (moisture under 0.3),
   open to half-shaded, not in hollows, so the evergreen scrub keeps the washes and hollows. `terra.c`: the
   desert's shrub layer stands near full on the flats, stony ones too (the scatter squares the layer, which was
   0.2-0.4 there, so a few percent of the sites held a plant). Probe (a throwaway world reader): over 108 desert
   points the shrub layer is 0.41-0.59 (was 0.2-0.4), the dwarf shrub 55% of it, evergreen scrub 32%. Kept:
   `dPan` (25 m up) shows hundreds of small bushes in loose stands where there were a few dozen. Still: they
   read light olive-green, not grey; on the ground (`desG`) the view is still the old tufts.
4. Fault: the dwarf shrubs read light olive-green from 25 m. Change: their leaves drier (frond dryness 105-190
   in two lots, was 75-160). Reverted: `dPan` and `dS` barely change; the plant shader's own tint of leaves in
   dry country (the colour track's) sets most of their colour.
5. Fault: the dry grassland at a hatchling's eye (`grG`) reads as bare tan earth, not a sward. Change:
   `terrain.fs` a tussock pattern on dry grass within 90 m (straw clumps, shaded thatch, a grain of blades).
   Reverted: `grG` does not change (its ground there is bare soil, which the pattern leaves alone), and in `grs`
   the blade grain draws as a regular stipple across a clearing.
6. Fault: (from pass 2) sand at the desert's edge keeps the beach's gold, and the pans draw near white from the
   air. Change: `terrain.fs` the pale grey-buff of pans and washes reaches wherever the ground is at all dry (the
   beach never is) and greys by a fifth from 60 to 400 m. Kept: in `pA` the gold rim along the forest edge is
   gone; `dAir`'s pans a little less stark (small).
