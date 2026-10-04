# SWAT environment runtime slice

Three small CC0 architecture files (about 284 KiB) plus six decorative tabletop
props (about 817 KiB). The full runtime pack is 1,127,179 bytes, about 1.08 MiB.
Nothing in either source pack is modified.

| File | Purpose |
| --- | --- |
| `plaster_diffuse.png` | 256 × 256 RGB, faded worn plaster from the kit's art-directed Poly Haven derivative |
| `wood_diffuse.png` | 256 × 256 RGB, worn wood from the kit's original Poly Haven diffuse |
| `door_leaf.glb` | Standalone leaf, panels, paint chips, two knobs/escutcheons and hinges |
| `catalog.json` | Exact source/output hashes, source licensing/provenance and runtime contract |

## Runtime transform and damage contract

The GLB contains one identity-transform node and five triangle primitives. There
is no casing, static frame, animation, pre-open rotation or source scene offset.
**Primitive 0 is the logical slab:** its bounds are exactly `[-0.5, +0.5]` on each
axis. Local **X is thickness, Y is height/up, Z is width**. The origin is the slab
center. Positive source width runs toward +Z. Its original metric slab size in
this axis order was `[0.044, 2.02, 1.12]` metres.

Scale by `2 * o.half` axis-for-axis, then use the existing authoritative SWAT
object yaw/pitch and position. Use the engine's existing OBB rotation convention.
Do **not** normalize by `GetModelBoundingBox`: knobs and hinges deliberately lie
outside the logical slab. Their exact visual bounds are in the catalog.

All five primitives belong to **one** active/health door object. Hide or destroy
the entire model when that object disappears. Never render any part as a second
static scene instance. Source trim is mounted to the door leaf, not to its frame.
The current SWAT logical slab remains the collision object; protruding art does
not create collision. Wall/floor textures likewise decorate existing damageable
object draws rather than introducing permanent overlays.

The GLB uses standard glTF base color factors and one embedded 256px PNG paint
map. The map applies subtle source wood luminance to the source green door paint;
all 1,236 source triangles, including four geometric paint chips, are retained.
It needs no normal/roughness/metallic maps or PBR shader. The renderer must keep
its existing procedural fallback when an asset fails to load.

## Provenance and licenses

The supplied `house_kit/LICENSE_ARCHITECTURE.txt` explicitly dedicates the original
architecture geometry to CC0 1.0. Its exact text is preserved in
`LICENSE_SOURCE_ARCHITECTURE.txt`. The supplied texture license is preserved as
`LICENSE_SOURCE_TEXTURES.txt` (its relative path references refer to the original
pack). Selected source records are embedded verbatim in `catalog.json`.

- Worn Plaster Wall, Dimitrios Savva: https://polyhaven.com/a/worn_plaster_wall
- Wood Floor Worn, Dimitrios Savva: https://polyhaven.com/a/wood_floor_worn
- Source asset license: https://polyhaven.com/license
- CC0 1.0: https://creativecommons.org/publicdomain/zero/1.0/

The plaster is the already-authored `alder_faded_plaster_diff.png` derivative,
not a newly claimed third-party work. The kit records it as 28% source plaster
with 72% greige paint in Blender. These files retain that provenance. The catalog
quotes the source's recorded retrieval date without inferring when this runtime
adaptation was downloaded or created. Source documents and local hashes are the
basis for the licensing record; no claim of fresh external license verification
is made. No solid furniture mesh, optional downloaded reference model or game IP is used.

## Rebuild and validate

The builder verifies every pinned source checksum before writing. It only writes
the selected output directory. A byte-identical reference build uses Python 3.12
and Pillow 12.3.0 (PNG encoding may vary with library versions).

```sh
python3 build_assets.py --source /path/to/house_kit
python3 validate_assets.py --source /path/to/house_kit
```

`--out /tmp/swat-runtime-rebuild` can be used for an independent comparison.
`validate_assets.py` without `--source` validates shipped hashes, PNG sizes,
GLB buffers, finite attributes, normals, indices, exact slab/visual bounds,
geometry counts, material maps and ownership using only the standard library.
Rebuilding needs Pillow but no Blender or network access. Rebuild source images
are not duplicated in this lightweight slice; their original paths and SHA256
values are pinned in both builder and catalog.

## Decorative tabletop subset

Six `prop_*.glb` files are derived from the supplied CC0 Household Clutter Batch
02. `props_catalog.json` records all source/output hashes, AABBs, retained
geometry, texture changes and supplied provenance. `LICENSE_SOURCE_CLUTTER.txt`
is the exact source dedication. See `../../ENVIRONMENT_ART.md` for the live
placement, support ownership, decorative-only and clearance contract.

```sh
python3 build_props.py --source /path/to/02_household_clutter
python3 validate_props.py --source /path/to/02_household_clutter --header ../../environment_props.h
```

Rebuild uses Pillow; standalone validation uses the Python standard library.
The source position/normal/UV/index streams are unchanged. Materials use embedded
128px RGB diffuse maps, with PBR maps removed. Double-sided details are rendered
by the scoped Raylib prop pass. An independent rebuild is byte-identical with
the reference Pillow version recorded in the catalog.
