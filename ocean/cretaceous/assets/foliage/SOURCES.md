# Foliage sources

Every file here is derived from CC0 (public domain) assets. Attribution is not required, but we list each source anyway.
Run `bash ocean/cretaceous/tools/fetch_foliage.sh` from the repository root to re-download and re-extract them.

The plant meshes are procedural (`render/veg.c`). These photos only supply leaf, frond and bark texture.
At startup the renderer cuts the sprites into its atlas layers. If the files are missing, it falls back to the painted layers.

## Sprites (`sprites/`)

The script takes the 1K PNG Color and Opacity maps and splits each atlas into single leaves by connected components.
Each sprite is cropped, masked and turned so its base is at the bottom.

| Files | Source | Used for | License |
|---|---|---|---|
| `LeafSet019_*` | [ambientCG LeafSet019](https://ambientcg.com/view?id=LeafSet019) | conifer sprays: Metasequoia, Araucaria branchlet ropes | CC0 |
| `LeafSet013_*`, `LeafSet020_*` | [LeafSet013](https://ambientcg.com/view?id=LeafSet013), [LeafSet020](https://ambientcg.com/view?id=LeafSet020) | podocarp strap leaves | CC0 |
| `LeafSet018_*` | [LeafSet018](https://ambientcg.com/view?id=LeafSet018) | magnolia-like broadleaf twigs | CC0 |
| `LeafSet022_*` | [LeafSet022](https://ambientcg.com/view?id=LeafSet022) | laurel-like broadleaf twigs | CC0 |
| `LeafSet004_*`, `LeafSet024_*` | [LeafSet004](https://ambientcg.com/view?id=LeafSet004), [LeafSet024](https://ambientcg.com/view?id=LeafSet024) | Cercidiphyllum-like (heart/round leaf) twigs | CC0 |
| `LeafSet001_*`, `LeafSet002_*` | [LeafSet001](https://ambientcg.com/view?id=LeafSet001), [LeafSet002](https://ambientcg.com/view?id=LeafSet002) | shrub sprigs | CC0 |
| `Foliage001_*`, `Foliage006_*` | [Foliage001](https://ambientcg.com/view?id=Foliage001), [Foliage006](https://ambientcg.com/view?id=Foliage006) | narrow blades: cycad pinnae, palm fan | CC0 |
| `fern_02_*` | [Poly Haven fern_02](https://polyhaven.com/a/fern_02), diffuse and alpha only (the mesh is not used) | ground fern fronds, tree-fern pinnae | CC0 |

Only `fern_02_00` to `_04` are loaded. `_05` and later are fragments.

## Bark (`bark_*.png`)

These are the 1K diffuse maps, resized to 512².

| File | Source | Used for | License |
|---|---|---|---|
| `bark_bark_brown_02.png` | [Poly Haven bark_brown_02](https://polyhaven.com/a/bark_brown_02) | broadleaf trunks | CC0 |
| `bark_metasequoia_bark.png` | [Poly Haven metasequoia_bark](https://polyhaven.com/a/metasequoia_bark) | conifer trunks | CC0 |
| `bark_palm_bark.png` | [Poly Haven palm_bark](https://polyhaven.com/a/palm_bark) | palm and cycad trunks | CC0 |
| `bark_japanese_cedar_bark.png` | [Poly Haven japanese_cedar_bark](https://polyhaven.com/a/japanese_cedar_bark) | tree-fern trunks (darkened) | CC0 |

## Downloads

Raw downloads go to `build/cret_foliage/dl/<id>/`, one directory per asset, and are never committed.
The script runs no code from them. It reads only PNG and JPG images.
