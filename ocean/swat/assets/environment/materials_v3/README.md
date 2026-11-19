# Briar Court materials v3 (scanned CC0 PBR)

Runtime maps for `materials_v3.h`: per material `<name>_basecolor.jpg` (sRGB),
`<name>_normal.jpg` (tangent space, OpenGL +Y) and `<name>_roughness.jpg`
(linear grey; R and G both carry roughness so it also works as a glTF ORM map).
JPEG quality 92/95/90; roughness at half the listed size. 20 materials, 60 maps, 16 MB on disk.

Built by `tools/blender/materials_v3_fetch.py` (download + MD5/SHA-256 check)
and `tools/blender/materials_v3_bake.py` (Blender 4.2, deterministic: a re-bake
reproduces these files byte for byte) from
`tools/blender/materials_v3_recipe.json`; sources are pinned in
`tools/blender/materials_v3_sources.lock.json`. Every parameter was
hill-climbed against a reference photograph: see
`reports/cloud/art-materials-v3/LOG.md` and the contact sheets beside it.
Downloads are not committed; only these derived maps are.

## Sources (all CC0 1.0: https://polyhaven.com/license, https://docs.ambientcg.com/license/)

| Material | Source scan | Author(s) | Licence | Map size | Repeat (m) |
| --- | --- | --- | --- | --- | --- |
| `stucco` | [concrete_wall_003](https://polyhaven.com/a/concrete_wall_003) | Dimitrios Savva, Rico Cilliers | CC0 1.0 | 1024 px | 4.0 |
| `repair` | [white_plaster_02](https://polyhaven.com/a/white_plaster_02) | Rob Tuytel | CC0 1.0 | 512 px | 1.0 |
| `trim` | [plastered_wall_05](https://polyhaven.com/a/plastered_wall_05) | Charlotte Baglioni | CC0 1.0 | 1024 px | 2.0 |
| `fascia` | [painted_concrete](https://polyhaven.com/a/painted_concrete) | Dario Barresi, Dimitrios Savva | CC0 1.0 | 1024 px | 1.0 |
| `gutter` | [painted_concrete](https://polyhaven.com/a/painted_concrete) | Dario Barresi, Dimitrios Savva | CC0 1.0 | 512 px | 1.0 |
| `concrete` | [concrete_floor_worn_001](https://polyhaven.com/a/concrete_floor_worn_001) | Dimitrios Savva, Rico Cilliers | CC0 1.0 | 1024 px | 3.0 |
| `walkway` | [concrete_floor_02](https://polyhaven.com/a/concrete_floor_02) | Rob Tuytel | CC0 1.0 | 1024 px | 1.3333 |
| `plinth` | [dirty_concrete](https://polyhaven.com/a/dirty_concrete) | Rob Tuytel | CC0 1.0 | 512 px | 3.0 |
| `roof` | [tarred_gravel](https://polyhaven.com/a/tarred_gravel) | Dimitrios Savva | CC0 1.0 | 1024 px | 3.0 |
| `lot` | [worn_asphalt](https://polyhaven.com/a/worn_asphalt) | Amal Kumar | CC0 1.0 | 1024 px | 2.0 |
| `road` | [asphalt_04](https://polyhaven.com/a/asphalt_04) | Jenelle van Heerden, Sergej Majboroda | CC0 1.0 | 1024 px | 4.0 |
| `gravel` | [gravelly_sand](https://polyhaven.com/a/gravelly_sand) | Dario Barresi | CC0 1.0 | 1024 px | 3.5 |
| `dirt` | [dry_ground_01](https://polyhaven.com/a/dry_ground_01) | Rob Tuytel | CC0 1.0 | 1024 px | 4.0 |
| `carpet` | [Carpet012](https://ambientcg.com/a/Carpet012) | Lennart Demes (ambientCG) | CC0 1.0 | 1024 px | 1.0 |
| `drywall` | [plastered_wall_04](https://polyhaven.com/a/plastered_wall_04) | Rob Tuytel | CC0 1.0 | 1024 px | 2.0 |
| `ceiling` | [white_stucco_02](https://polyhaven.com/a/white_stucco_02) | Dimitrios Savva | CC0 1.0 | 1024 px | 0.6 |
| `bath_floor` | [Tiles071](https://ambientcg.com/a/Tiles071) | Lennart Demes (ambientCG) | CC0 1.0 | 1024 px | 1.0 |
| `bath_wall` | [Tiles036](https://ambientcg.com/a/Tiles036) | Lennart Demes (ambientCG) | CC0 1.0 | 1024 px | 1.0 |
| `reception_floor` | [Tiles074](https://ambientcg.com/a/Tiles074) | Lennart Demes (ambientCG) | CC0 1.0 | 1024 px | 1.8 |
| `laundry_floor` | [Terrazzo005](https://ambientcg.com/a/Terrazzo005) | Lennart Demes (ambientCG) | CC0 1.0 | 1024 px | 0.75 |

The baked maps are derivatives (resampled, colour graded, flattened, recoloured,
joints added) and are dedicated CC0 1.0 like their sources.

## Reference photographs (used for matching only; not shipped in the game)

The per-item contact sheets in `reports/cloud/art-materials-v3/` include
downscaled copies of these photographs, credited here:

| Item | Photograph | Author | Licence |
| --- | --- | --- | --- |
| Walls | [Abandoned Motel 3, Marathon, Texas](https://www.flickr.com/photos/76792851@N07/15512418618) | TexasExplorer98 | CC BY 2.0 |
| Lot | [Johnson Park](https://www.flickr.com/photos/140641142@N05/53548495024) | dankeck | CC0 1.0 |
| Lot (close) | [Asphalt Texture](https://www.flickr.com/photos/19716902@N00/2329018741) | włodi | CC BY 2.0 (not in a sheet) |
| Road, verge | [Abandoned Motel, Marathon, Texas](https://www.flickr.com/photos/76792851@N07/15077859854) | TexasExplorer98 | CC BY 2.0 |
| Walkway | [Concrete sidewalk](https://www.flickr.com/photos/13288010@N08/3626685752) | roman.nurik | CC BY 2.0 |
| Trim, fascia | [motel office](https://www.flickr.com/photos/7377398@N02/20306831404) | el-toro | CC BY 2.0 |
| Roof | [Boise Project, Arrowrock Division (1950)](https://www.flickr.com/photos/47026734@N08/54731548328) | U.S. Bureau of Reclamation | Public domain |
| Room | [Mt. Si Motel - Room 6](https://www.flickr.com/photos/12508217@N08/49708226402) | Sam Howzit | CC BY 2.0 |
| Ceiling | [Day 125 of 365 - Popcorn Ceiling](https://www.flickr.com/photos/32996142@N05/5722955541) | jesse? | CC BY 2.0 |
| Bathroom | [Motel Bathroom 004](https://www.flickr.com/photos/11726702@N07/2465590593) | cygnus921 | CC BY 2.0 |
| Reception | [Vintage Vinyl Tile Flooring South Beach](https://www.flickr.com/photos/25955895@N03/45568707481) | Phillip Pessar | CC BY 2.0 |
| Laundry | [Terrazzo Floor Former Woolworth](https://www.flickr.com/photos/25955895@N03/8116145669) | Phillip Pessar | CC BY 2.0 |

## Switches

- `SWAT_MATERIALS_V3=0`: previous materials (kit library, generated
  wallpaper/carpet/tile/ceiling, original asphalt/gravel/dirt).
- `SWAT_MATERIALS_V3_MACRO=0`: v3 maps without the world-space macro
  variation and two-scale blend.
- `SWAT_ENVIRONMENT_STYLE=legacy` also disables v3; `SWAT_ENVIRONMENT_PBR=0`
  disables the whole v3 set (it never mixes v3 colour with old relief).
