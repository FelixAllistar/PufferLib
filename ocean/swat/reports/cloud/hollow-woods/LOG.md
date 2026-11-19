# The Hollow woods v2: hill-climb log

The task was to replace the low-poly blob trees, the uniform yard ground and the crowns that the
planning cutaway drew over the clearing with real late-autumn Appalachian woods. Each item was
hill-climbed against reference photographs:
1. render;
2. put the render beside the reference;
3. look, and name the 3 biggest differences (silhouette, then colour/value, then detail);
4. fix the biggest one and render again.

An iteration that was not closer was reverted. The in-game look is what counts, so species work
started in Blender stand previews (`hollow_trees.py --render`) and finished in-engine
(`exterior_capture --mission hollow`, software GL, xvfb).

- Pairs from each iteration: `hillclimb/`.
- Contact sheets (reference | first | final in-game): `contact/`.
- Before/after of the 12 original review views plus the 3 new ones: `before_after_*.jpg`.

## References

| File (committed copy, <= 1280 px, <= 300 KB) | Used for | Title / author / date | Source | Licence |
|---|---|---|---|---|
| `hardwood_fog_rhodo_creek_loc_highsmith.jpg` | oak/hickory, mood, laurel | "Fog creeps into a winter scene within the Tennessee portion of Great Smoky Mountains National Park", Carol M. Highsmith, 2021-11-03 | https://www.loc.gov/pictures/item/2021756574/ | Public domain (Highsmith gift) |
| `hardwood_bare_wv_loc_highsmith.jpg` | tulip poplar, bark value | "Trail at the site of the Cheat Summit Fort, Cheat Mountain, Monongahela NF, WV", Carol M. Highsmith, 2015-10-26 | https://www.loc.gov/pictures/item/2015634447/ | Public domain |
| `sycamore_creek_wikimedia_tonelli.jpg` | sycamore form and bark (summer photo) | "Hokendauqua Creek (1)", Nicholas A. Tonelli, 2014-09-03 | https://commons.wikimedia.org/wiki/File:Hokendauqua_Creek_(1)_(14946860658).jpg | CC BY 2.0, credit: Nicholas A. Tonelli |
| `hemlock_wv_wikimedia_chrism.jpg` | eastern hemlock (and white pine, see below) | "Tsuga canadensis Cathedral State Park 1", Chris M Morris, 2011-05-15 | https://commons.wikimedia.org/wiki/File:Tsuga_canadensis_Cathedral_State_Park_1.jpg | CC BY 2.0, credit: Chris M Morris |
| `rhodo_understory_creek_loc_highsmith.jpg` | laurel/rhododendron thicket, floor | "Scene along the stream ... Roaring Fork Motor Nature Trail", Carol M. Highsmith, 2021-11-03 | https://www.loc.gov/pictures/item/2021756601/ | Public domain |
| `forest_floor_logs_wv_loc_highsmith.jpg` | logs, leaf litter | "Remnants of ... old-growth woodland along the Gaudineer Interpretive Trail, WV", Carol M. Highsmith, 2015-10-26 | https://www.loc.gov/pictures/item/2015634453/ | Public domain |
| `forest_edge_clearing_wv_loc_highsmith.jpg` | forest edge from open ground | "Trail at the site of the Cheat Summit Fort, WV", Carol M. Highsmith, 2015-10-26 | https://www.loc.gov/pictures/item/2015634446/ | Public domain |
| `ridges_fog_loc_highsmith_a.jpg` | far woods in fog | "Fog descends upon the ridgeline ... Great Smoky Mountains NP", Carol M. Highsmith, 2021-11-03 | https://www.loc.gov/pictures/item/2021756575/ | Public domain |
| `ridges_fog_loc_highsmith_b.jpg` | far woods, half-bare silhouettes | "Fog in the Tennessee portion of Great Smoky Mountains NP", Carol M. Highsmith, 2021-11-03 | https://www.loc.gov/pictures/item/2021756577/ | Public domain |
| `farmyard_hollow_fsa_rothstein.jpg` | yard ground | "Home of Fannie Corbin ... Corbin Hollow farm", Arthur Rothstein (FSA/OWI), Oct. 1935 | https://www.loc.gov/pictures/item/2017721396/ | Public domain |
| `farmyard_mud_ruts_fsa_wolcott.jpg` | tyre ruts, puddles | "Muddy road after thaw, near Stowe, Vermont", Marion Post Wolcott (FSA/OWI), 1940 | https://www.loc.gov/pictures/item/2017756209/ | Public domain |
| (not committed) | late-autumn sycamores, WV | "Chesapeake and Ohio Canal near Shepherdstown, West Virginia", Acroterion | https://commons.wikimedia.org/wiki/File:Chesapeake_and_Ohio_Canal_near_Shepherdstown,_West_Virginia_MD1.jpg | CC BY-SA 4.0: URL only |
| (level-hollow) `mood_fog_greenbrier_highsmith.jpg` | mood | "Fog over the Greenbrier River", Carol M. Highsmith | https://www.loc.gov/pictures/item/2015634485/ | Public domain |

Gaps:
- Wikimedia's API was rate limited from the sandbox, so the agent searched LOC first.
- No free white-pine photo was found. White pine was judged against the hemlock and stand photos,
  and on its form (whorled tiers, plumes, dead lower stubs).
- The only CC-BY sycamore photo is a summer one, used for form and bark only.
- No reference has dusk light. The fog photos set the value structure, and the existing dusk
  lighting was kept.

## Viewpoints

- **Blender stand preview.** Eye height 1.7 m, 60 deg horizontal FOV. A stand of the species is
  scattered 18-90 m away in volumetric fog under flat grey light, with exposure +0.8. This is the
  flat foggy light of the Highsmith photos.
- **In-game views:**
  - the original 12 Hollow views;
  - `hollow12_edge` (east clearing edge);
  - `hollow13_porch_woods` (woods across the yard from the porch);
  - `hollow14_lane_treeline` (the lane where it enters the woods);
  - a mood view from the yard to the north-west woods, `--eye 8,1.7,-4 --target -12,6,18 --fov 60`;
  - a raised yard view, `--eye 12,7,-20 --target 20,0,-4 --fov 65`;
  - a rut view, `--eye 6,1.8,-15 --target 1,0.3,-6`;
  - an orthographic top-down of the yard.

## A bug that reset the first renders

The first Blender iterations (`hillclimb/*_pre*.jpg`) and the first in-engine captures were made
while every exported UV was Blender's default 0..1 square. `to_object` created the colour
attribute after the UV layer, which invalidated the Python handle, so the UV write went nowhere.
Every card showed the whole atlas: the "brush-stroke" crowns and the "icicle" far woods.

After the fix the species loops restarted at iteration 0. The pre-fix renders are kept but not
counted.

## Oak / hickory (Blender, then in-engine)

| Iter | 3 biggest differences | Change | Result |
|---|---|---|---|
| 0 | Crowns are dark clumps at the branch tips (eucalyptus look); crowns far too dark and dense; limbs hidden | Baseline after the UV fix | - |
| 1 | Each card reads as a round puff; still dark; limbs too straight | Sprays spread along the limbs; no duplicate tip sprays; lighter twig albedo; more crooked limbs | Closer, kept |
| 2 | Crowns read as in-leaf masses; umbrella shape; trunks dark | Directional sympodial twig cells (a leader with short side shoots); more upright limbs (50 to 40 deg) | Closer, kept |
| 3 | Still too dense to see the limbs; too many leaf cards; limbs thin | Fewer, smaller sprays (n3 3, 1.7 m); a leaf cell 1 time in 4; heavier, lighter limbs | Clearly closer (limbs visible through the haze), kept |
| 4 | Uniform heights; little lean; trunks darker than the ref's lichen grey | H 16-26 m, more lean, lighter trunk | About the same, kept (neutral) |
| 5 | Try a fuller upper-crown haze | n3 4, cs 1.55 | Worse (dense and dark again): reverted. Blender stop: two in a row without gain |
| 6 (in-game) | Trunks warm tan under the dusk light, not grey; floor orange; no fog between the trunks | Bark scans graded toward grey; litter graded dull; fog start 35 to 12 m | Much closer, kept |
| 7 (in-game) | Near trunks too pale: the woods do not silhouette against the fog | Bark grade value down (oak 1.1 to 0.78); fog start 16 m | Closer, kept |

## Tulip poplar

| Iter | Differences | Change | Result |
|---|---|---|---|
| 0 | Crowns thin and narrow; trunks lean (the lean was species-wide); trunks too dark | Baseline | - |
| 1 | (as 0) | Lean 0.03 for poplar; R 0.34-0.46; 13 limbs, l1 0.30 | Closer (straight, columnar stand), kept |
| 2 | Value cannot be judged in the flat preview | Trunk vertex value | Neutral |
| 3 (in-game) | Warm tan bark | Grey grade (shared with oak) | Closer, kept |
| 4 (in-game) | Too pale against the fog | Grade value 0.82 | Closer, kept |
| 5 (in-game) | Stand too dense to see through (and expensive) | Second-growth spacing 3.3 to 4.2 m | Closer to the open WV stand, kept |

## Sycamore (creek)

| Iter | Differences | Change | Result |
|---|---|---|---|
| 0 | Reads like an oak; trunk too thin and upright; no pale upper limbs | Baseline | - |
| 1 | (as 0) | R 0.45-0.6; lean 0.16; crown base 0.26; wide (60 deg), long limbs | Closer (massive, leaning, spreading), kept |
| 2 | Pale upper limbs invisible in the flat preview | Lower bole 0.42, pale above 2.5 m | Neutral in the preview |
| 3 (in-game) | Bark warm | Grade 0.4/1.05 (paler than the other species) | Closer, kept |
| 4 (in-game) | Fog washes the pale limbs | Fog start 16 m | Kept (shared) |
| 5 (in-game) | Spacing | 4.2 m spacing (creek stand) | Kept |

## Eastern hemlock

| Iter | Differences | Change | Result |
|---|---|---|---|
| pre0, pre1 | (UV bug: not counted) Far too sparse; then bushy to the ground | 84 limbs; forest-grown crown base for v1 and v2 | - |
| 0 (in-game) | Pale grey-green flat cards; crown OK | Baseline after the UV fix | - |
| 1 (in-game) | Too pale: hemlocks should be the darkest masses | Spray albedo darker ((34,50,36) to (22,34,26)); bark grade 0.7 | Closer, kept |
| 2 (in-game) | Each card reads as one giant fern frond up close | Cells redrawn as a drooping limb with many small 2-ranked fronds (true needle scale) | Clearly closer, kept |
| 3 (in-game) | Spacing / density | Shared spacing change | Kept |

## Eastern white pine

| Iter | Differences | Change | Result |
|---|---|---|---|
| pre0, pre1 | (UV bug) Bare snags; then plumes too thin | Bigger, denser plume cards along the outer 75% of the limbs | - |
| 0 (in-game) | Pale | Baseline | - |
| 1 (in-game) | Needle colour too light and blue | Darker needles and twigs | Closer, kept |
| 2 (in-game) | Bark warm | Grey grade 0.75 | Closer, kept |

## Laurel / rhododendron thicket (in-game)

| Iter | Differences | Change | Result |
|---|---|---|---|
| 0 | The ref has a dense dark evergreen band at the foot of the woods; ours is a sparse scatter | Baseline | - |
| 1 | (as 0) | Placement weighted to 1-6 m outside the edge (p 0.95 peak) | Neutral: the sampler's try budget capped the count |
| 2 | (as 0) | 9000 tries, shrubs 0.9-1.5x | Closer (dark thicket band), kept |

## Forest edge (in-game, `hollow12_edge`, `hollow13_porch_woods`, `hollow14_lane_treeline`)

| Iter | Differences | Change | Result |
|---|---|---|---|
| 0 | Bright orange floor; warm tan trunks; no depth haze | First v2 capture | - |
| 1 | (as 0) | Litter graded; bark grey; fog start 12 m | Much closer, kept |
| 2 | Goldenrod seed heads read as white puffs | Darker, sparser seed heads; 170 clumps | Closer, kept |
| 3 | Too dense to see into; frame cost | 4.2 m spacing, fewer grass/fern clumps | Closer to the open stand, kept |
| 4 | Edge not dark enough to be a wall | Darker bark and conifers (mood 1) | Closer, kept |
| 5 | No thicket band | Laurel band (laurel 2) | Closer, kept |

## Far woods (in-game, lane and front views)

| Iter | Differences | Change | Result |
|---|---|---|---|
| 0 | Pale "icicle" shapes | Cross-quad impostors | - |
| 1 | White fringes | The impostor bleed divided straight alpha twice: fixed | Closer |
| 2 | Shapes smeared | UV fix (above) | Closer |
| 3 | +54 ms of overdraw from about 420 impostors | Camera-facing billboards trimmed in 10 bands; trees only to 40 m; beyond, two forest-wall rings | Same look, much cheaper, kept |
| 4 | Far band too uniform and pale | Fog start 16 m; darker bark | Closer, kept |
| 5 | Try denser fog (0.019 to 0.023) | | Worse (the woods melt into flat fog): reverted |

## Yard ground (in-game)

| Iter | Differences | Change | Result |
|---|---|---|---|
| 0 | Paths and ruts invisible: decal values match the dark base | First ground decals (paths, ruts, puddles, drifts, patches) | - |
| 1 | No value contrast between bare paths and the ground | Paths and grass patches lighter (bare dirt, tan dead grass) | Closer, kept |
| 2 | Patches are an even sprinkle of yellow camo blobs; ruts covered; green leaves | Ruts and puddles on higher layers; dry oak litter; grass clustered at the edges, mud and wear by the buildings | Closer, kept |
| 3 | Trampled areas at the doors too small | Larger worn areas, stronger main paths | About the same |
| 4 | The ref's yard is strewn with stones | Small field stones inside the edge | About the same at review distance |
| 5 | The base tile still reads uniform | World-space macro variation and a rotated second repeat on the yard material | Closer, kept |

## Mood (in-game, mood view and `hollow13_porch_woods`, vs the Highsmith fog photo)

| Iter | Differences | Change | Result |
|---|---|---|---|
| 0 | Near trunks pale grey: the woods do not silhouette against the fog; hemlocks pale; no warmth | - | - |
| 1 | (as 0) | Darker bark grades; darker conifer cards; fog start 16 m | Clearly closer, kept |
| 2 | Hemlock cards read as giant fronds | Hemlock cells rebuilt (see hemlock 2) | Closer, kept |
| 3 | No dark thicket band | Laurel weighting | Neutral |
| 4 | (as 3) | Laurel tries and size | Closer, kept |
| 5 | Try denser fog | 0.023 | Worse: reverted |

## Performance (software GL, llvmpipe/xvfb, 1280x720, staging view, `performance_tool --hollow`, p50 total)

The machine drifts by +-10 ms between runs, so the woods were measured interleaved with the old
woods in the same build (`SWAT_HOLLOW_WOODS=0`).

| Measurement | Woods v2 | Old woods | Motel |
|---|---|---|---|
| Session start, origin/5.0 build, 1 run | - | 178.5 ms (draw_cpu 58, 444 draws, 1.21 M vertices) | 270.9 ms |
| Before the last laurel trim, 5 interleaved rounds (median) | 224 ms | 192 ms (+17 %) | 277 ms |
| Final, 3 paired rounds | 211.7 / 192.2 / 183.8 | 186.3 / 174.1 / 164.7 | - |
| Final, paired ratio | +13.6 % / +10.4 % / +11.6 % (median +11.6 %) | | |

- The final frame stays under the motel's, and within the target of about 15 % over today's Hollow.
- Woods-side counts at staging: about 55 instanced draws (one per mesh part).
- GL draws per frame: 359-523 with v2, 328-468 with the old woods (the count depends on shadow
  refreshes).
- Vertices: 0.86-1.23 M with v2, 1.2-1.57 M with the old woods.
- Triangles per mesh: lod0 trees 1.7-3.7 k, lod1 0.3-0.9 k, impostors 20, laurel 40-216, scatter
  16-176, forest walls about 0.3 k each.

On a software rasterizer, fill matters more than triangles. These changes paid:
- forest walls instead of about 420 far billboards (-25 ms);
- impostors trimmed to the silhouette and turned to the eye;
- 4.2 m second-growth spacing;
- LOD 14/30 m;
- shrubs culled beyond 45 m in the fog.

These made no measurable difference:
- skipping the sun-shadow lookup on cards;
- front-to-back sorting (kept, because it cannot hurt).

## Iteration counts

The counts are post-UV-fix iterations, Blender and in-game together:
- oak 8 (6 Blender + 2 in-game);
- poplar 6;
- sycamore 6;
- hemlock 4 (plus 2 pre-fix);
- white pine 3 (plus 2 pre-fix);
- laurel thicket 3;
- forest edge 6;
- far woods 6;
- yard ground 6;
- mood 6.

Hemlock, pine and laurel got fewer dedicated iterations than the 6 asked for. Their later changes
came through the shared edge and mood passes.
