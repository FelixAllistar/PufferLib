# Look hill-climb: trees, mountains, biomes

A brief for the cloud workers of the long look pass (started 2026-11-19; this is round 2).
Each worker takes one track below, on its own branch, and climbs: render,
look, fix the worst thing, render again, keep it only if it is better.

## What Felix said (2026-11-19)

- Ground plants look better. Cliffs in the mountains look great in places.
- **Most of the trees are horrendous.** Many are short, scrappy little
  things, and many have trunks that are smooth cylinders with no texture.
- Green grass reaches slightly too high on the mountains.
- He wants realistic mountains, realistic near-desert biomes, many biomes,
  and the trees fixed in the same pass, because the mountains need trees.

## Where it stands (round 2)

Round 1 (four tracks, 65 passes, all merged on branch `cret/hc`) gave: real
tree sizes and fuller crowns, dense conifers with young and old forms, bark
by species with root flares, a lowered grass line, scree fans, forest
climbing the gullies, and a first near-desert. Their logs and sheets are in
`docs/hillclimb/{trunks,conifers,broadleaf,mountains}/`: read the "still
bad" list of the one nearest your track.

What is still wrong, seen on the merged build:
- **Far trees.** Past about 30 m the game shows the reduced mesh, and that
  is what most of any view is made of. Reduced crowns are thinner and more
  see-through than the full ones; redwood and araucaria come out 6 to 18%
  brighter reduced and as impostors; distant forest is one flat saturated
  green; pale trunks stand out of far slopes as grey poles.
- **Near trees.** Leaf cards look blocky from under the crown; twigs and
  araucaria branches are plain tubes; cycad and tree-fern trunks go near
  black under their crowns; palm and tree-fern trunks are pale and smooth at
  10 to 30 m; the plane's mottling is a cold teal-white; no trunk leans or
  bows; cycads and small woody plants (shrubs, understorey, saplings) still
  look scrappy.
- **Biomes.** The near-desert is an even beige with abrupt edges and no
  cracked earth; wet conifer forest, fern prairie, riverside, wetland and
  coast were not worked; stands do not mix ages or leave clearings.
- **High ground up close.** Steep ground is smeared down the fall line;
  pale talus patches remain on the highest slopes.

## The loop

1. **Set up.** Build, fetch the assets, render the baseline views of your
   track and keep them as `before/` (outside the repository, for example
   `/tmp/hc/before`).
2. **Get references.** For each species or landscape you work on, download 3
   to 6 photos of the real thing from Wikimedia Commons, for looking only.
   Keep them under `/tmp`. Never commit them.
3. **Climb.** At least 12 passes:
   - look at your latest renders next to the references (open the PNGs
     yourself with Read; crop to full resolution where it matters);
   - write down the worst fault you can see;
   - change the code, rebuild, render the same views again;
   - put before and after side by side; keep the change only if it is
     clearly better and nothing else got worse, otherwise revert it;
   - commit each kept step with a plain message, and add one line to
     `ocean/cretaceous/docs/hillclimb/<track>/LOG.md`: the fault, the change,
     kept or reverted, why.
4. **Judge at three distances:** 3 m (a hatchling beside the trunk), 30 m,
   and 300 m (the reduced mesh and the impostor), and in real world shots,
   not only in the gallery.
5. **Stop** after at least 12 passes when three passes in a row bring no
   visible gain, or after about four hours of work. Do not stop after the
   first improvement.
6. **Push as you go** (every few kept steps), so nothing is lost.

## Commands (from the repository root)

```bash
cd ocean/cretaceous && make player && cd ../..       # build/cretaceous/cretaceous
bash ocean/cretaceous/tools/fetch_foliage.sh          # leaf and bark scans (CC0)
python3 -I ocean/cretaceous/tools/fetch_textures.py   # ground materials (CC0)
bash ocean/cretaceous/tools/fetch_props.sh            # rocks and logs; go on if it fails

X="xvfb-run -a -s '-screen 0 1280x720x24'"
# Every tree, one page per few, at the automatic LOD and (LODS=1) forced to full, reduced, impostor:
CRET_GALLERY_ROWS=trees CRET_GALLERY_LODS=1 CRET_GALLERY_FRAMES=8 \
  $X build/cretaceous/cretaceous capture /tmp/hc/gal --gallery --size 1280 720
# World shots (the island; the first run bakes it, which is slow on a CPU, then it is cached):
CRET_SHOT_CAM="redwood:4610:1765:3.0:0.72:-0.03:65;skyh:2896:2432:4:0.6:0.12:70;ga0:2620:1344:2.5:0:0.08:70;gup:2620:1344:45:0.5:-0.7:70" \
  $X build/cretaceous/cretaceous capture /tmp/hc/world \
  --shot ridge,canopy,aerial,coast,river,ground,morning,redwood,skyh,ga0,gup --size 1280 720
cd ocean/cretaceous && make test                      # the fast suite
```

- A camera of your own: add `name:x:z:metres_above_ground:yaw:pitch:fov` to
  `CRET_SHOT_CAM` and name it in `--shot`.
- There is no GPU: Mesa draws in software. Time your first render and size
  the rest to fit (960x540 is fine while iterating; final sheets 1280x720).
- There are no dinosaur models in the cloud. That is expected.
- Known in the cloud: the system Pillow is broken, so run the fetch scripts
  with `PYTHON=python3.12` (or pip-install Pillow); Wikimedia rate-limits
  downloads, so fetch few and slowly; a capture takes at most 16 cameras of
  your own; the island bakes in about 30 s and seven world shots take about
  18 minutes.
- These fail in the cloud on an untouched tree too, for lack of converted
  props and models: `test_sim` (2 checks), `test_net_codec` (3), `animcheck`.
  `make test` stops at the first, so run the later lines one by one.
- Read `ocean/cretaceous/README.md` (plants, terrain and capture sections),
  `docs/plants/README.md`, `docs/plants/sources.md` and
  `docs/quality/README.md` before you start.
- `tools/lodmatch.py` and `tools/lodpop.py` measure how well the reduced
  mesh and the impostor match the full tree.

## Rules

- Touch only `ocean/cretaceous/**`. Never `src/`, `build.sh`, the trainer or
  another environment. Never the dinosaur models or their clips.
- New pictures (bark, leaves, ground): CC0 only, from Poly Haven or
  ambientCG, fetched by the fetch scripts and never committed. List each in
  `docs/plants/sources.md`. Nothing from a `_PLACEHOLDER_RIPS_do_not_ship`
  folder.
- Cost: the gallery labels print each tree's triangle counts (full,
  reduced). The full mesh may grow to twice today's count, the reduced mesh
  to 1.25 times. Put the counts before and after in your report.
- The Windows build is clang under mingw: no identifier named `near` or
  `far`, and no implicit function declarations.
- A switch a player should try goes in Settings. Environment variables are
  for development only.
- `make test` must pass. One exception: budgets that depend on the baked
  world (ecoprobe, camprobe) may move when the terrain changes. Say so in
  the report and leave the budget files alone; they are rewritten on Felix's
  machine.
- Do not weaken or delete a test. Do not report anything you did not look at.
- Keep to the files your track owns, so the four branches merge cleanly.
- Update the docs your change touches, in plain words.

## Handing in

- Branch: `cret/hc2-<track>` off `cret/hc` (the merged round 1; not `5.0`).
- Before and after sheets as JPEGs under
  `ocean/cretaceous/docs/hillclimb/<track>/` (side-by-side pairs, at most 12
  files, at most 400 KB each).
- The last commit's subject starts with `cret cloud done: <track>`. Its body
  is the report: what changed, passes kept and reverted, triangle counts,
  the test result, and what is still bad, worst first.

## Tracks

### far: the trees you see past 30 m

**Owns:** the reduced-mesh paths in `render/veg_gen.h` (the `lod` branches),
the impostor and level-of-detail code in `render/veg.c`, the distance terms
of the plant shaders, and the switch distances. Not the full meshes.

**Goal:** a forest at 50 m, 300 m and 2 km looks like the same forest you
stand in.
- The reduced mesh and the impostor of every tree match its full mesh in
  outline, crown density and tone (`tools/lodmatch.py` and `lodpop.py`
  measure it; aim within 5% luma and no visible pop at the switch).
- Reduced crowns are not see-through where the full crown is solid.
- Far forest has variety: tone and hue differ by species, age and slope,
  with shadow between crowns, not one flat saturated green.
- Trunks at distance sit in the tone of the forest, not pale poles.
- Judge mostly in world shots: slopes across a valley, the canopy from
  above, a ridge from the shore. Use the gallery's forced LOD pages too.

### near: the trees you stand under

**Owns:** the full-mesh paths in `render/veg_gen.h` (leaves, twigs, limbs,
trunks), the near shading of bark and leaves in the plant shaders, and the
small woody plants. Not the `lod` branches or the impostors.

**Goal:** from a hatchling's eye, within 15 m, every tree and shrub holds up.
- Leaves: no blocky cards from under the crown; sprays and leaf clusters
  with depth, light through them.
- Twigs and araucaria branches carry bark and taper; no plain tubes.
- Trunks under their own crown keep visible form (cycads, tree ferns),
  palm and tree-fern trunks are not pale smooth columns, the plane's
  mottling is warm, and trunks lean or bow a little (do it without
  reshuffling the crowns' random draws).
- Cycads and the small woody plants (shrubs, understorey, saplings): each
  should look like a real young or small plant, not a scrap. Felix hates the
  "short lil" ones most.

### biomes: places that differ

**Owns:** the lowland rules of `terra/terra.c` (materials and plant layers
below the tree line) and the plant placement code. Not the tree meshes, the
terrain shader, or the rules above the tree line.

**Goal:** several biomes that read as different places from the air and on
the ground, with real-looking edges.
- Near-desert: broken up, not an even beige: bare cracked pans, stony
  flats, dry washes, clumps of scrub and cycads, soft edges into the forest.
  Its size should not depend on the world's size (a quarter of the small
  test world is desert today).
- Wet conifer forest, fern prairie, riverside and wetland, coast: each with
  its own mix of plants and ground.
- Stands and clearings: trees clump, thin out and mix ages (the conifers
  now have young, mature and old variants; use them), with gaps and edges.
- Time the bake and say how the world-dependent test budgets moved.

### alpine: high ground up close

**Owns:** `shaders/terrain.fs` and the rules above the tree line in
`terra/terra.c`. Not the lowland rules or the placement code.

**Goal:** the mountains hold up from the ground, not only from the air.
- Steep ground is not smeared down the fall line (project the textures so
  cliffs and steep slopes keep their grain).
- Rock, scree and talus look like stone at 2 m: broken, layered, varied in
  size; no pale flat patches on the highest slopes.
- The tree line's last shrubs and stunted trees read as wind-shaped.
- Keep the cliff bands and the lowered grass line as they are now.
