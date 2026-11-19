# Look hill-climb: trees, mountains, biomes

A brief for the cloud workers of the long look pass (started 2026-11-19; this is round 3, the last).
Each worker takes one track below, on its own branch, and climbs: render,
look, fix the worst thing, render again, keep it only if it is better.

## What Felix said (2026-11-19)

- Ground plants look better. Cliffs in the mountains look great in places.
- **Most of the trees are horrendous.** Many are short, scrappy little
  things, and many have trunks that are smooth cylinders with no texture.
- Green grass reaches slightly too high on the mountains.
- He wants realistic mountains, realistic near-desert biomes, many biomes,
  and the trees fixed in the same pass, because the mountains need trees.

## Where it stands (round 3, the last)

Rounds 1 and 2 (eight tracks, 127 passes, all merged on branch `cret/hc`)
gave: real tree sizes, dense conifers with ages, bark by species, trunks
that sit in the forest's tone, matched far trees with shade between the
crowns, fuller cycads and shrubs, a lowered grass line, scree, forest up the
gullies, fractured rock up close, a broken-up near-desert of fixed share,
river banks with horsetails, stands with clearings, grassland and fern
prairie. Their logs and sheets are in `docs/hillclimb/<track>/`: read the
"still bad" list of the tracks nearest yours before you start.

What is still wrong, seen on the merged build:
- **Dry country.** From the air the near-desert and the dry grassland are
  one straw-cream tone (the terrain shader recolours bare earth by its
  luminance); there is no cracked earth; the desert scrub is 1 to 2 m green
  bushes where it wants a low grey dwarf shrub.
- **Forest colour.** Far stands are a saturated teal (hue about 199,
  saturation 0.45 in the canopy shot). Ground ferns and some broadleaf crowns
  are a neon lime. Species differ too little in green.
- **Coast.** The big beaches show dark stripes from the air: old fern ground
  between the strips of sand the beach builder lays (`world.c`). The strand
  belt is narrow. Plants stand on wet sand at the water's edge by river
  mouths.
- **Trees.** Araucaria branches and all twigs are plain tubes. The last
  trees at the tree line are full-height straight conifers: nothing is
  stunted or wind-shaped. Leaf cards still look blocky from under some
  crowns. Podocarp and araucaria trunks are lighter than the forest at 20 to
  60 m.

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
- The player's graphics card is a GTX 1060 with 3 GB. Do not add per-pixel
  work without need: prefer what can be baked into a texture or a vertex, and
  keep new shader work behind the high preset if it is not cheap. Say in
  the report what you added per pixel.
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

- Branch: `cret/hc3-<track>` off `cret/hc` (the merged rounds 1 and 2; not `5.0`).
- Before and after sheets as JPEGs under
  `ocean/cretaceous/docs/hillclimb/<track>/` (side-by-side pairs, at most 12
  files, at most 400 KB each).
- The last commit's subject starts with `cret cloud done: <track>`. Its body
  is the report: what changed, passes kept and reverted, triangle counts,
  the test result, and what is still bad, worst first.

## Tracks

### dry: near-desert and dry grassland

**Owns:** the dry-country parts of `shaders/terrain.fs` (the dry tint and
anything new for dry ground), the near-desert and grassland rules in
`terra/terra.c`, the material set in `tools/fetch_textures.py`, and one new
small shrub in `render/veg_gen.h` (shrub kind only). Not the tree meshes,
the plant shaders, the coast or `world.c`.

**Goal:** dry country that reads as dry country from 300 m up and from a
hatchling's eye.
- From the air: pans, washes, stony flats and bare earth differ in tone and
  colour; dry grassland is not the same cream as desert.
- Cracked earth on the pans (fetch a CC0 cracked-mud material) and sun-baked
  ground that looks hard, with stones.
- A low grey dwarf shrub (0.2 to 0.6 m, Karoo or saltbush-like) speckling the
  flats in the hundreds, with the bigger scrub in clumps along washes.

### colour: the greens of the forest

**Owns:** the plant shaders' colour terms (`shaders/veg_light.glsl`, the
colour parts of `veg.fs` and the impostor shaders) and the tone and tint
tables in `render/veg.c`. Not the meshes, the terrain shader or the bake.

**Goal:** the forest's colour looks photographed, not painted.
- Far stands: natural dark green that greys and blues gently with haze, not
  teal. Compare with photos of conifer and mixed forest across a valley.
- No neon lime: ground ferns, fern prairie and broadleaf crowns in sun.
- Species differ: blue-black araucaria, fresh green dawn redwood, dark
  glossy laurel, yellow-green ginkgo and plane, olive cycads, and so on,
  within a palette that holds together.
- The same tree keeps its colour through every level of detail (the far
  track's tone tables did this for brightness; keep them fitted,
  `tools/lodmatch.py`).

### coast: beaches, strand and river mouths

**Owns:** `world.c` (the beach and open-country builders), the coast and
river-mouth rules of `terra/terra.c`, and plant placement near the shore in
`render/veg_place.h`. Not the dry-country rules, the shaders or the meshes.

**Goal:** a coast that looks like one.
- No stripes on the beaches: from the air a big beach is continuous sand
  that grades into dune grass and scrub, then strand forest.
- A strand belt you can see: palms in groves, scrub, driftwood line, with
  the forest behind.
- River mouths: no plants standing in the wet sand at the waterline; mud,
  sand bars and reeds or horsetails where the water slows.
- Check what the open-country redraw did to the coast plains (palms fell
  from 1,088 to 798).

### crowns: the last tree faults

**Owns:** the tree meshes in `render/veg_gen.h` (full and reduced paths) and
the species tables. Not the shaders, the bark atlas or the bake.

**Goal:**
- Araucaria branches and the twigs of every tree: tapered, bark-covered,
  not plain tubes (keep the triangle budget: taper and shade, do not just add
  sides).
- Leaves from under the crown: no blocky cards on any species; check each.
- A wind-shaped, stunted form for the last trees at the tree line
  (krummholz: low, flagged to one side, dense), chosen by height above the
  tree line; the alpine track left the clumps for it.
- Podocarp and araucaria trunks no lighter than the forest at 20 to 60 m.
- Dead standing trees and fallen trunks that look like the species around
  them.
