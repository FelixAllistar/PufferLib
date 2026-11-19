# hollow_v1: The Hollow (`--mission hollow`)

These are the presentation assets for The Hollow. See `ocean/swat/HOLLOW.md`
and `reports/cloud/level-hollow/LOG.md`. Nothing here is read by the
simulation. Collision is the authoritative box map in `hollow_impl.h`.

## materials/ (CC0, Poly Haven, https://polyhaven.com/license)

Each file is an unmodified Poly Haven JPEG map (diffuse, OpenGL normal,
roughness), renamed. The renderer (`hollow_art.h`) puts them on the
authoritative boxes with metric UVs.

| File prefix | Poly Haven asset | Author(s) | Resolution | Used on |
|---|---|---|---|---|
| `planks_grey` | Wood Planks Grey (`wood_planks_grey`) | Rob Tuytel | 2K | House and outbuilding boards, gables, sills and plates |
| `planks_old` | Old Planks 02 (`old_planks_02`) | Rob Tuytel | 1K | Door leaves |
| `floor_boards` | Old Wood Floor (`old_wood_floor`) | Guillaume Monsergent | 1K | Floors, porch deck, stair treads |
| `roof_tin` | Rusty Corrugated Iron (`rusty_corrugated_iron`) | Charlotte Baglioni | 1K | Tin roofs |
| `field_stone` | Old Stone Wall (`old_stone_wall`) | Charlotte Baglioni | 1K | Chimney, cellar headwall and liners, step |
| `yard_ground` | Brown Mud Leaves 01 (`brown_mud_leaves_01`) | Rob Tuytel | 2K | Yard, lane, bank |
| `dirt_floor` | Dirt Floor (`dirt_floor`) | eye-candy.xyz | 1K | Cellar floor |

## GLB models (made here)

Blender scripts under `tools/blender/hollow_*.py` generate the models
deterministically. Their textures are the CC0 maps above, plus Bark Brown 02,
Gravel Floor 02 and Dry Decay Leaves (Poly Haven, CC0), downscaled. See the
model table below once it is filled in.
