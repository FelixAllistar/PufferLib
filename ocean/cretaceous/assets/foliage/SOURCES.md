# Foliage sources

Every file here is derived from CC0 (public domain) assets. Attribution is not required, but we list each source anyway.
Run `bash ocean/cretaceous/tools/fetch_foliage.sh` from the repository root to re-download and re-extract them.

The plant meshes are procedural (`render/veg.c`). These scans only supply leaf, frond and bark texture and their materials.
At startup the renderer cuts the sprites into its atlas layers and its leaf material atlas. If the files are missing, it falls back to the painted layers.
The survey behind these choices, with what was rejected and why, is `docs/plants/sources.md`.

## Sprites (`sprites/`)

The script takes the 2K Color, Opacity and NormalGL maps (ambientCG) or the diffuse, alpha and nor_gl maps (Poly Haven), and the Scattering map where ambientCG has one.
It splits each atlas into single leaves by connected components. Each sprite is cropped, masked and turned so its base is at the bottom (its normals turned with it).
Each `<set>_<nn>.png` has a `<set>_<nn>_m.png` beside it: R, G its normal, B the light it lets through (from the Scattering scan, else from the photo: veins and dry parts darker).

| Files | Source | Used for | License |
|---|---|---|---|
| `LeafSet019_*` | [ambientCG LeafSet019](https://ambientcg.com/view?id=LeafSet019) | scale-leaf conifer sprays (fallback for the fir twigs) | CC0 |
| `fir_tree_01_*` | [Poly Haven fir_tree_01](https://polyhaven.com/a/fir_tree_01), twig maps only | Metasequoia sprays and twig clusters | CC0 |
| `grass_medium_02_*` | [Poly Haven grass_medium_02](https://polyhaven.com/a/grass_medium_02), diffuse, alpha, normal | tall grass and sedge blades | CC0 |
| `jacaranda_tree_*` | [Poly Haven jacaranda_tree](https://polyhaven.com/a/jacaranda_tree), leaf maps only | tree-fern pinnae (with the fern fronds) | CC0 |
| `LeafSet013_*`, `LeafSet020_*` | [LeafSet013](https://ambientcg.com/view?id=LeafSet013), [LeafSet020](https://ambientcg.com/view?id=LeafSet020) | podocarp strap leaves | CC0 |
| `LeafSet018_*` | [LeafSet018](https://ambientcg.com/view?id=LeafSet018) | magnolia-like broadleaf twigs | CC0 |
| `LeafSet022_*` | [LeafSet022](https://ambientcg.com/view?id=LeafSet022) | laurel-like broadleaf twigs (with Scattering) | CC0 |
| `LeafSet004_*`, `LeafSet024_*` | [LeafSet004](https://ambientcg.com/view?id=LeafSet004), [LeafSet024](https://ambientcg.com/view?id=LeafSet024) | Cercidiphyllum-like (heart/round leaf) twigs | CC0 |
| `LeafSet001_*`, `LeafSet002_*` | [LeafSet001](https://ambientcg.com/view?id=LeafSet001), [LeafSet002](https://ambientcg.com/view?id=LeafSet002) | shrub sprigs | CC0 |
| `Foliage001_*`, `Foliage006_*` | [Foliage001](https://ambientcg.com/view?id=Foliage001), [Foliage006](https://ambientcg.com/view?id=Foliage006) | narrow blades: cycad pinnae, palm fan, reeds (grass when grass_medium_02 is missing) | CC0 |
| `LeafSet010_*` | [LeafSet010](https://ambientcg.com/view?id=LeafSet010) | river plane (Platanites-like) palmate leaves | CC0 |
| `LeafSet025_*` | [LeafSet025](https://ambientcg.com/view?id=LeafSet025) | gunnera's lobed leaf, flowering herb leaves (with Scattering) | CC0 |
| `LeafSet014_*` | [LeafSet014](https://ambientcg.com/view?id=LeafSet014) | berry shrub serrate leaves | CC0 |
| `fern_02_*` | [Poly Haven fern_02](https://polyhaven.com/a/fern_02), 4k diffuse, alpha and normal (the mesh is not used) | ground fern fronds, tree-fern pinnae | CC0 |

Only `fern_02_00` to `_04` are loaded. `_05` and later are fragments.

Painted, not photographed (no CC0 photo on these sites): ginkgo fan leaves,
water-lily pads, petals and bracts, fruit and cone skins, fungus surfaces,
clubmoss and moss (`render/veg.c`, `paint_fan`, `paint_lily_pad`,
`paint_petal`, `paint_skin`, `paint_fungus`, `paint_club`).

## Bark (`bark_*.png`)

These are the 1K diffuse maps, resized to 512², and beside each its 1K normal map (`bark_<name>_n.png`, OpenGL convention).

| File | Source | Used for | License |
|---|---|---|---|
| `bark_bark_brown_02.png` | [Poly Haven bark_brown_02](https://polyhaven.com/a/bark_brown_02) | broadleaf trunks | CC0 |
| `bark_metasequoia_bark.png` | [Poly Haven metasequoia_bark](https://polyhaven.com/a/metasequoia_bark) | conifer trunks | CC0 |
| `bark_palm_bark.png` | [Poly Haven palm_bark](https://polyhaven.com/a/palm_bark) | palm and cycad trunks | CC0 |
| `bark_japanese_cedar_bark.png` | [Poly Haven japanese_cedar_bark](https://polyhaven.com/a/japanese_cedar_bark) | tree-fern trunks (darkened) | CC0 |

## Downloads

Raw downloads go to `build/cret_foliage/dl/<id>/`, one directory per asset, and are never committed.
The script runs no code from them. It reads only PNG and JPG images.
