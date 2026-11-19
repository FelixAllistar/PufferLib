# Coast hill-climb log (round 3)

One line per pass: the fault, the change, kept or reverted, why. Views (960x540, Mesa llvmpipe; `name:x:z:above:yaw:pitch:fov`):
`airW` 2750:4500:230:3.1416:-0.55 and `topW` 2420:4450:520:3.1416:-1.45 (the big west beach from the air and from above),
`airN` 2437:1613:220:-2.36:-0.55, `airS` 3677:6291:180:2.36:-0.7 (two more big beaches), `mouthA` 2408:4864:50:2.5:-0.5
(a stream crossing the beach), `bchS` 3340:6388:1.8:0.79:-0.03 (on a beach), `rivG` 2296:1600:1.8:3.37 (a river mouth),
plus the brief's `aerial` and `coast`. `CRET_MAP_DBG=file.ppm` (new, `world.c`) writes the island top-down as the load
leaves it (materials, water, plant layers); `CRET_BEACH_DBG=1` prints spots on the big beaches.

1. Fault: the big beaches are striped from the air: every stream that crosses one kept a band of old fern and forest
   ground about 120 m wide right across the sand (`beaches_build` left everything within 25-60 m of a river or lake
   uncut). Change: `world.c` cuts the beach across streams too, but keeps the ground near a river above its water (the
   nearest river's level, carried with the distance transform, + 0.3 m, rising 4 % away from it), and ground already
   lower than the beach profile is sanded instead of left as it was. Kept: `airW`, `topW` and `aerial` show continuous
   sand with a thin stream channel across it; `mouthA` lost its block of fern ground on the beach.
2. Fault: plants on the wet sand: a row of ferns and shrubs at the water's edge at the river mouth (`coast`), and fern
   tufts scattered over open sand where a beach lies in fern prairie (`mouthA`: the prairie added ferns even where the
   sand had cleared them). Change: `render/veg_place.h` `placeable` refuses sand within 0.6 m of the water beside it or
   below 0.9 m over the sea; `world.c` the prairie does not thicken ferns on sand. Kept: `coast` and `mouthA` are bare
   sand at the water, `bchS` and `airW` unchanged.
