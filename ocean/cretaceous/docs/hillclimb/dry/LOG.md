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
