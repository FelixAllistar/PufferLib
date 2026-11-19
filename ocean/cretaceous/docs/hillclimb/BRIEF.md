# Look hill-climb: trees, mountains, biomes

A brief for the cloud workers of the long look pass (started 2026-11-19).
Each worker takes one track below, on its own branch, and climbs: render,
look, fix the worst thing, render again, keep it only if it is better.

## What Felix said (2026-11-19)

- Ground plants look better. Cliffs in the mountains look great in places.
- **Most of the trees are horrendous.** Many are short, scrappy little
  things, and many have trunks that are smooth cylinders with no texture.
- Green grass reaches slightly too high on the mountains: "just the tip of
  some curve barely touches that point".
- He wants realistic mountains, realistic near-desert biomes, many biomes,
  and the trees fixed in the same pass, because the mountains need trees.

Round 1's baseline, as the gallery shows it: broadleaf trees are 5 to 8 m
bushes on a thin dark stick; conifers are thin and see-through; trunks have
no visible bark at 30 m; cycads and tree ferns are small and sparse.

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

- Branch: `cret/hc1-<track>` off `5.0`.
- Before and after sheets as JPEGs under
  `ocean/cretaceous/docs/hillclimb/<track>/` (side-by-side pairs, at most 12
  files, at most 400 KB each).
- The last commit's subject starts with `cret cloud done: <track>`. Its body
  is the report: what changed, passes kept and reverted, triangle counts,
  the test result, and what is still bad, worst first.

## Tracks

### trunks: no more smooth cylinders

**Owns:** the trunk, branch and bark code in `render/veg_gen.h` (`bark`,
`tube`, the branch skeleton), the bark atlas in `render/veg.c`, the bark part
of the plant shaders, the bark section of `tools/fetch_foliage.sh`. Not the
crowns or leaves, and not the species' heights.

**Goal:** no trunk or limb of any tree reads as a smooth cylinder, at any
distance or level of detail.
- Bark you can see: enough texels, a normal map that catches the sun, shade
  in the furrows. Today there are four 512 px bark pictures for every
  species; fetch more and larger ones.
- Bark by species: fibrous red-brown for dawn redwood, ringed plates for
  araucaria, furrowed grey for ginkgo, mottled flakes for the plane, the
  diamond leaf-base armour of a cycad, the fibrous root mantle of a tree fern.
- Trunk shape: taper, a root flare where it meets the ground (no pinched or
  floating bases), a slight lean or curve, girth that fits the height (a
  mature tree's trunk is about 1/25 to 1/40 of its height across), limbs
  that thin step by step, a few stubs and scars.
- The reduced mesh and the impostor keep the bark's colour and shading.

### conifers: real, dense, tall conifers

**Owns:** the conifer crown and foliage code in `render/veg_gen.h` and the
conifers' entries in the species tables (heights, crown shape). Kinds: dawn
redwood, araucaria, podocarp, cheirolepid cypress. Not the bark or trunk code.

**Goal:** each is recognisable against photos of its living relative
(Metasequoia, Araucaria araucana and heterophylla, Podocarpus, Cupressus and
Taxodium), and a stand of them looks like a forest.
- Real size: dawn redwood 30 to 45 m, araucaria 25 to 40 m with tiered whorls
  and an umbrella top on old trees, podocarp 15 to 25 m and dense, cypress 15
  to 30 m.
- Dense, layered foliage. No stick trees you can see through.
- Ages: the variants should give a young tree, a mature one and an old one,
  with bare lower trunk and a few dead branches on the old.
- The reduced mesh and the impostor keep the crown's density and outline.

### broadleaf: real trees, not bushes on sticks

**Owns:** the broadleaf, cycad and tree fern crown and foliage code in
`render/veg_gen.h` and their entries in the species tables. Not the bark or
trunk code.

**Goal:** today these are 5 to 8 m bushes on a stick. Make them trees.
- Real size and habit: ginkgo 15 to 25 m, upright then spreading; katsura 12
  to 20 m, several stems; laurel 8 to 15 m, dense and dark; plane 20 to 30 m
  with a wide crown on heavy limbs; magnolia 10 to 18 m; chloranth a 3 to 6 m
  shrub.
- Full crowns: visible limb structure carrying layered masses of leaves, not
  a sparse scatter of cards.
- Cycads: a stout armoured trunk 1 to 4 m tall under a crown of stiff fronds.
  Tree ferns: 4 to 10 m, a fibrous trunk, arching fronds, a skirt of dead ones.
- The reduced mesh and the impostor keep the crown's density and outline.

### mountains: grass line, mountains and biomes

**Owns:** `terra/terra.c` (ground materials, plant layers), the terrain
shaders, and plant densities in the placement code. Not the tree meshes.

**Goal:**
- **The grass line.** Green grass reaches slightly too high. The highest
  ground should be rock, scree and thin brown-green alpine turf, with no
  green cap touching a summit. This is a small correction: lower the top of
  the green a little and thin it where the ground is exposed. Do not strip
  the mountains.
- **Mountains that look real.** Keep the cliff bands (Felix likes them). Add
  what real ranges show in photos: scree fans under cliffs, wetter and
  greener gullies, dense forest on the lower slopes, and a tree line that
  thins into scattered shrubs, not a hard edge.
- **Biomes.** Several that read as different places from the air and on the
  ground: wet conifer forest, fern prairie, riverside and wetland, coast,
  upland, and a dry near-desert scrub (bare cracked ground, sparse cycads and
  shrubs). Real-looking transitions between them.
- **Stands and clearings.** Trees in each biome clump, thin out and mix ages
  as they do in nature.
- A terrain change needs the world baked again. Time the bake, iterate on a
  small world where you can, and check the full island at the end. Put the
  bake time in the report.
