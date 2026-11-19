# crowns: the last tree faults (round 3)

One line per pass: the fault, the change, kept or reverted, why. Views: the gallery's LOD-match capture
(`CRET_GALLERY_MATCH`, single trees at 4-30 m, `CRET_GALLERY_MATCH_AIM` to look up into a crown), the
tree gallery pages, and world shots at the tree line (`tl1`, `tl3`, `tl6`, `mtS`, cameras from the
alpine track's log) and in the forest (`redwood`, `canopy`). World shots here were taken with 24 warm-up
frames instead of 64 (a local, uncommitted change to `cretaceous.c`: 64 frames took 10-15 minutes a shot
on this machine's software GL); before and after always on the same settings.

Triangles at the start (full / reduced): araucaria v0 22502 / 3712, v4 22140 / 3494; dead conifer (snag v0)
1248 / 324; the island's tree meshes 739,506 over 112 variants.

1. Fault: the last trees at the tree line are full-height straight conifers (`tl1`, `tl3`, `tl6`: 25-32 m
   araucarias standing on the bare rock at 450-520 m). Change: krummholz, two forms in two species, built in
   `render/veg_gen.h` (`gen_krummholz`) as VEG_SNAG's spare slots 4-7 (that kind had only 4 of its 8 in use):
   a flagged tree (leaning and bowed downwind, limbs only on the lee side, the windward ones grey dead
   stubs, a dense skirt at the foot reaching downwind) and a wind-trimmed mat (prostrate stems under a
   cushion that rises from a low windward edge), each as a cheirolep and as a podocarp (snow totara-like).
   `render/veg_place.h` (`veg_krummholz`) turns a conifer into one by its height (a probe of every conifer's
   elevation: full trees end near 420 m) and turns it to face the prevailing wind (terra.c's 0.6 rad, the
   way the alpine track's scrub skirts reach). First try (from 350 m, 2.6-4.6 m trees, three in four mats at
   the top) turned the whole upper forest and every tree island into specks, and the mats floated over
   steep slopes as flat dark discs; second (375-455 m) still left the islands as dots. Kept as: from 375 m,
   none full from about 440 m, flagged trees 4.5-7.5 m, mats at most 45%, mats extended under the ground so
   their downhill side meets the slope. Kept: the forest edge now ends in stunted, flagged trees and
   cushions; but from 100 m and more the high tree islands read as sparse dark dots where tall trees stood
   (see "still bad").
2. Fault: Norfolk araucaria branches are plain five-sided tubes (a straight cone with the bark squeezed
   along it, no join to the trunk). Change: drawn through `bark()` like other limbs: a collar where they
   leave the trunk, a curve through the skeleton's points, 20% heavier, bark at the limbs' scale. Kept,
   modest: from under the tree (4 m) the limbs swell into the trunk instead of being pinned to it.
   Triangles v0 33218 / 4472, v4 32700 / 4258 (budgets 45004 / 4640 and 44280 / 4367).
3. Fault: araucaria and podocarp trunks read paler than the forest at 20-60 m. Change: their bark's baked
   occlusion times 0.8 on the full mesh, 0.72 on the reduced one (which takes over from 32 m). Kept, modest:
   at 40 m on the plain the trunk is 6-8% darker in sRGB on the full mesh, 9-10% on the reduced; in the
   `redwood` grove the comparison is muddied by the haze changing between runs.
4. Fault: twigs are blunt three-sided tubes. Change: a twig tapers to 15% of its base radius at its tip, bark
   tiled once round it with square texels. Reverted: from under the plane and the redwood at 5 m no visible
   difference (the twigs are hidden among the leaves).
5. Fault: standing dead conifers are thin black poles of 11-18 m, half the height of the living trees round
   them, with random stubs. Change: the dead conifer is a dead araucaria (the uplands' commonest conifer):
   18-27 m, its plated bark, whorls of four or five level stubs every 1.5-2.2 m up its upper two thirds,
   longest at mid height, a third of them fallen. The reduced mesh keeps only the stubs over 0.95 m (all of
   them put it at 496 triangles, over the 405 budget). Kept: at 25 m it reads as the skeleton of a tiered
   araucaria. 1424 / 272 triangles.
6. Fault: krummholz mats read near black from afar. Change: their crown occlusion constant 0.5 to 0.32.
   Reverted: barely lighter at 14 m.
7. Fault: from 100 m the krummholz of the high tree islands read as lone dark dots (`tl3`, `mtS`): one
   small tree stands where one tall tree stood. Change: a flagged krummholz is a clonal tree island: the
   mother stem and two or three younger stems (0.3-0.6 of her height) layered downwind of her, 1.2-2.2 m
   apart, their skirts merging (3300-3500 cards). Kept: at 16 and 60 m each reads as a low wind-shaped
   thicket with a leader, larger than one tree. Triangles 10044 / 1244 and 16218 / 1394 (still under the
   22-40k / 2-4k of the full-height conifers they replace).
8. Fault: from under and beside it the cheirolep's crown is made of palm-like fans: its scale-leaf cluster
   cards (ropes radiating from one base) are big enough to read one by one. Change: 6800 cards of 0.4 m
   (were 4300 of 0.52 m); the reduced mesh keeps the same number of merged cards (every 19th). Kept,
   modest: finer foliage at 2 and 12 m, the fans at the crown's lower edge smaller but still there.
   Triangles 15822 to 20868 (reduced 1182, unchanged).
9. Fault: mats still the most common form high up, and they are what vanish from afar. Change: at most a
   quarter of the krummholz are mats (were up to 45%), from about 420 m.
10. Fault: the broken standing conifer does not look like the lowland redwoods round it. Change: a
    redwood's buttressed, fluted foot (flare 0.65, five flutes). Reverted: at 6 and 25 m barely different
    from the plain flare it had.
11. Checks, no change: from under the magnolia, laurel, katsura, ginkgo, plane, chloranth tree, podocarp
    (both ages), redwood, araucaria, both tree ferns, both palms and the tall cycad (2 m out, looking up):
    no blocky cards; the cheirolep's fans (pass 8) were the one fault. The flagged krummholz side-on to the
    wind (12 m and 3 m): lean and foliage both downwind, the windward side bare with grey stubs. In the
    world (`k2`, 2.5 m up at (3850, 3720), a slope at 410-440 m): a line of small wind-shaped tree islands
    climbs above the forest where full araucarias stood.
12. Fault (lodmatch): the krummholz mats' reduced mesh is 11-16% darker than the full one at 30 m (150 m
    and the impostors within 4%). Changes tried: smaller, more merged cards (0.8-0.85 m, every 5th); then a
    lighter crown occlusion on the reduced mesh only. Reverted both: 0.89 / 0.80 and 0.89 / 0.84, no gain.
13. Fault (lodmatch): pass 8's finer cheirolep cards shade the full crown darker, so its reduced mesh reads
    10% lighter at 30 and 150 m (1.04 and 1.08 before). Tried 5800 cards (worse: the reduced crown, which
    bakes at the full mesh's leaf area, got lighter too: 1.12); kept 6800 with a heavier crown occlusion on
    the reduced mesh (0.6 against 0.5): 1.09 and 1.10. A small gain; the rest is for the tone tables
    (`LOD_TONE`, render/veg.c, the colour track's).
