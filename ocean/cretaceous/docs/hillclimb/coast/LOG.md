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
3. Fault: the big beaches meet the forest or open country with nothing between (the bake's strand belt lies under the
   sand `world.c` lays at load), and the open-country redraw had cut the coast plains' palms (scatter count 798 on
   `cret/hc`, 734 after pass 1). Change: `world.c` `beaches_build` grades the beach inland: salt-hardy scrub and low
   cycads in clumps over the back dunes (0.55-1.35 W), then an open belt (0.85-1.85 W) of palms in 70 m groves, scrub
   and a few cycads on sandy ground, conifers, broadleaves, tree ferns and ferns thinned; `grassland_build` keeps the
   palms of open country near the sea (thinned by its coast nearness). Kept: `airW` and `topW` show a speckled belt of
   palms and scrub between the sand and the forest, `strD` (on the back dunes) stands among palms, cycads and scrub
   where it was bare sand. Palms 734 to 1,384, shrubs 90,243 to 93,415 (the scatter, whole island). Grass cannot grow
   on sand (`veg.c`, not this track's file), so the dunes carry scrub and cycads, not dune grass.
4. Fault (from pass 1): a steep sand wall up to 10 m high at the back of the beach (`strD12`, `strW30`): near a stream
   that runs high the beach floor was held above its water out to 60 m and then dropped to the beach profile at once.
   Change: the held floor blends into the beach profile from 15 to 70 m from the stream. Kept: the wall is gone, the
   back of the beach is a smooth rise into the strand.
5. Fault: where a stream crosses a big beach its bed is the bake's bank mud, which the terrain shader darkens as wet
   ground: from the air every stream drew a black gash across the sand (`topW`, `mouthA`, `strW30`). Change: `world.c`
   lays sand (85 %) on the stream cells on the beach as well. Kept: the streams read as damp lines in the sand.
6. Fault: at the largest river mouth (`mouth`, `mouth_high`) the river now runs through bare sand all the way up its
   lower valley, with no slack-water fringe. Change: a fringe of bank mud and horsetails 3-14 m wide along streams
   behind the front of the beach. Reverted: from the air (`topW`) it drew a pair of dark lines along every stream, new
   stripes, and the big mouth hardly changed.
7. Fault (from pass 2): the palms of the upper beach went too (`bchS` lost its three): the wet-sand rule refused all sand
   below 0.9 m over the sea, which on a 1.2 % beach is its front 45 m. Change: 0.6 m. Kept, small: a palm stands on the
   upper beach in `bchS` again; `coast` and `mouthA` stay clear at the water.
8. Fault: in `bchS` the steep hillside behind the beach is sand to 15 m up. Change: the load's beach sand fades on
   ground that stays steep after the cut. Reverted: no visible change in `bchS`, `airN`, `airS` or `topW`; that face's
   sand does not come from `beaches_build`.
9. Fault: as pass 6, no reeds or horsetails where streams slow behind the beach. Change: horsetail clumps only (no
   mud) within 10 m of a stream from 0.35 W inland, emergent plants exempt from the wet-sand rule. Reverted: 300 more
   horsetails on the island and none to be seen in `mouth_high`, `mouthA`, `strW30` or `topW`.
10. Fault: from 30 m (`strW30`) the back dunes read as a field of identical small cycads, like toy mushrooms in rows.
    Change: dune cycads 0.5 to 0.3 of the layer in fewer clumps, belt cycads 0.3 to 0.15. Kept: `strW30` thins to a
    few cycads among the scrub; up close (`strD12`) the scrub leads and the strand is a little sparser.
11. Fault: from 230 m (`airW`) the strand's palm groves read as a thin scatter. Change: wider groves (palm layer
    0.12-1 over a wider band of the grove noise). Reverted: 122 more palms on the island, no visible difference in
    `airW`, `airN` or `strD12`.
12. Fault: suspected low ridges along the streams at the front of the beach (the ground held above a stream rose 4 %
    from it). Change: 2 %. Reverted: no visible difference in `topW`, `mouthA`, `mouth_high` or `strW30`.
