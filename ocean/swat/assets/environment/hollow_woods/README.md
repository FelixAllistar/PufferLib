# hollow_woods: The Hollow's woods v2 (late-autumn Appalachian woods)

Presentation only. Drawn instanced by `hollow_woods.h` (included by
`hollow_art.h`); nothing here is read by the simulation, collision,
navigation or the protocol. `SWAT_HOLLOW_WOODS=0` turns the whole set off
and restores the previous low-poly `hollow_v1/hollow_woods.glb` (the yard
ground decals then stay, the yard's macro variation goes). See
`ocean/swat/HOLLOW.md` ("Woods v2") and `reports/cloud/hollow-woods/LOG.md`.

## Contents

| Path | What |
|---|---|
| `trees/{oak,poplar,sycamore,hemlock,pine}_N_lod0.glb` | Full tree: recursive branches (3 orders) + twig/needle cards |
| `trees/..._lod1.glb` | First two branch orders + whole-limb cards |
| `trees/..._lod2.glb` | Camera-facing impostor, cut to the tree's silhouette in 10 bands |
| `trees/laurel_N_lod{0,1}.glb` | Rhododendron / mountain-laurel thicket shrubs |
| `trees/wall_{0,1}.glb` | Far woods: two impostor-strip rings at 46 m and 70 m from the clearing |
| `scatter/*.glb` | Logs, stumps, rocks, fallen limbs, brown ferns, goldenrod, dead grass |
| `foliage_atlas.png` | 2048 x 2048 RGBA card atlas (4 x 4 cells), procedural, made here |
| `impostor_atlas.png` | 2048 x 2048 RGBA, lod0 renders (Cycles, 2 views per tree variant) |
| `forest_wall.png` | 2048 x 2048 RGBA, two strips composed from the impostors |
| `textures/bark_*_{color,normal}.jpg` | 1K CC0 scans (below), colour maps graded toward grey |
| `woods_meshes.txt` | Mesh table: path, texture, primitives, radius, height, triangles |
| `woods_placement.txt` | Seeded placement: sets (lod0/lod1/lod2) and instances |

Species and variants: oak/hickory form x3, tulip poplar x2, sycamore x2,
eastern hemlock x3, eastern white pine x2, laurel x3.

## Rebuild (deterministic)

```sh
cd ocean/swat
python3 tools/blender/hollow_foliage_textures.py assets/environment/hollow_woods     # card atlas first
blender -b --factory-startup --python-exit-code 1 -P tools/blender/hollow_trees.py -- assets/environment/hollow_woods
python3 tools/blender/hollow_textures.py TEX_DIR assets/environment/hollow_decals    # existing decal atlas
python3 tools/blender/hollow_ground.py assets/environment/hollow_decals             # + yard ground tiles and placement
blender -b --factory-startup --python-exit-code 1 -P tools/blender/hollow_scene.py -- assets/environment/hollow_v1 --only terrain
```

`hollow_trees.py --render OUT.jpg --only SPECIES` renders the hill-climb stand
preview (Cycles CPU, volumetric fog). Scripts need numpy and Pillow
(`python3` with Pillow; this sandbox used python3.13).

## Sources and licences

All geometry, the card atlas, the impostors and the forest walls are made
here by the scripts above. Scanned maps are CC0 from Poly Haven
(https://polyhaven.com/license), fetched by the scripts and graded:

| File | Poly Haven asset | Author(s) |
|---|---|---|
| `bark_oak_*` (also logs, stumps) | Jolcham Oak Bark 01 (`jolcham_oak_bark_01`) | Charlotte Baglioni |
| `bark_poplar_*` | Bark Brown 02 (`bark_brown_02`) | Rob Tuytel |
| `bark_sycamore_*` | Bark Platanus (`bark_platanus`) | Dimitrios Savva |
| `bark_hemlock_*` | Bark Brown 01 (`bark_brown_01`) | Rob Tuytel |
| `bark_pine_*` | Pine Bark (`pine_bark`) | Dimitrios Savva |
| `bark_laurel_*` | Bark Willow (`bark_willow`) | Dario Barresi, Dimitrios Savva |
| `bark_rock_*` | Mossy Rock (`mossy_rock`) | Rob Tuytel |

Yard ground decal tiles (`hollow_decals/decals_color.png` cells 4..7 x 0..3,
`decals_nr.png`), cut from CC0 Poly Haven scans by `hollow_ground.py`:
Brown Mud Dry (`brown_mud_dry`, Rob Tuytel), Muddy Tracks (`muddy_tracks`,
Amal Kumar), Brown Mud 02 (`brown_mud_02`, Rob Tuytel), Dry Decay Leaves
(`dry_decay_leaves`, Amal Kumar), Gravel Floor 02 (`gravel_floor_02`,
Jenelle van Heerden, Dimitrios Savva), Leafy Grass (`leafy_grass`,
Charlotte Baglioni).

Reference photographs (not shipped as game data) are listed with URLs and
licences in `reports/cloud/hollow-woods/LOG.md`.
