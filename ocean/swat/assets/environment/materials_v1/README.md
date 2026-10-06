# Environment material quality candidates v1

Six restrained, coherent materials for the existing SWAT damageable environment.
**This is an asset-only selection draft, not a live renderer change.** Everything
here is new and versioned under `materials_v1/`. Original textures, provisional
imagegen plaster, existing door GLB, catalogs and all engine files are unchanged.

## Review actual pixels

- [`qa/01_basecolor_pixels.png`](qa/01_basecolor_pixels.png): raw encoded basecolor pixels for the six candidates and the three preserved references.
- [`qa/02_neutral_closeups.jpg`](qa/02_neutral_closeups.jpg): six equal 1 m material crops under identical neutral illumination, using supplied normal and roughness channels.
- [`qa/03_same_light_room_comparison.jpg`](qa/03_same_light_room_comparison.jpg): preserved weathered wall/floor, provisional painted wall/floor, and the new candidates on identical geometry, light, camera and exposure.
- [`qa/04_new_materials_room.jpg`](qa/04_new_materials_room.jpg): 4 m wall, 3 m height, 4 m floor with metric rulers, framing and painted/wood door examples.
- [`qa/05_raw_tiling_test.jpg`](qa/05_raw_tiling_test.jpg): direct 3×3 repeats, including the optional worn variant so its repetition is visible rather than hidden.
- [`qa/06_basecolor_only_room.jpg`](qa/06_basecolor_only_room.jpg): new wall/floor with flat normals and constant roughness, isolating basecolor quality.

These are Blender reference tests, not current SWAT captures. Old/new wall maps
retain their documented tile sizes (source 1.8 m, provisional 1 m, new calm 1 m).
The floor remains 2 m per tile in every comparison; its grain is intentionally
rotated with its normal basis. New fixture materials stay the same in all room
comparisons. The new basecolor-only room comparison isolates the channel set
that the current engine supports from the staged PBR maps. `qa/render_report.json`
records the renderer, light settings and input hashes. Comparison sheets that
include provisional imagegen pixels are not a CC0 claim for those pixels.

## Material/scale contract

Each material has three 512×512 PNGs named `<id>_basecolor.png`,
`<id>_normal.png`, and `<id>_roughness.png`. IDs are presentation IDs in the
sidecar, **not new `SwatMaterial` physics enum values**.

| Material ID | Full texture tile U × V | Intended role | Surface source |
|---|---:|---|---|
| `plaster_painted` | 1.0 × 1.0 m | Default intact painted plaster/gypsum skins | Original periodic procedural |
| `plaster_worn` | 2.0 × 2.0 m | Sparse selected abraded skins | Original; <2% shallow chalk-fleck coverage |
| `floor_pine` | 2.0 × 2.0 m | Wooden floor only | Verified CC0 Poly Haven pine scan derivative |
| `framing_pine` | 0.4 × 2.0 m | Dry exposed studs/supports | Original periodic procedural |
| `door_paint` | 0.6 × 2.0 m | Sage painted leaf surfaces | Original periodic procedural |
| `door_wood` | 0.6 × 2.0 m | Unpainted door wood/existing chips | Original periodic procedural |

UV = `(u_metres / tile_U_metres, v_metres / tile_V_metres)`. Wood grain is
along **+V**, aligned to the member's long axis or intended floor direction.
Square pixel dimensions do not imply square physical dimensions. Repeating
UVs must remain continuous across adjoining wall damage cells: never restart
the entire texture at every cell. Physical size is an authored, explicit unit
contract; it is not a claim of instrument-measured optical material properties.

`material_manifest.json` contains per-map hashes, all source hashes, exact
dimensions, channel semantics, roughness/relief ranges, advisory bindings,
provenance and memory budgets.

### Encoding and PBR interpretation

- Basecolor: RGB8 **sRGB**, decode exactly once before lighting. The procedural
  surfaces contain no shadow/AO/specular/light-direction term. Their slight
  color variation represents pigment/aggregate, not baked geometry shading.
- Floor input is Poly Haven's official **Diffuse** map. It is pigment-graded
  and desaturated, with no added lighting/AO. Inspection found no broad
  directional illumination gradient, but tiny dark knots, nails and joints may
  retain photographic cavity shading. It is **not certified calibrated or
  perfectly shadow-free albedo**. These source features are disclosed rather
  than painted away or mislabeled as known material reflectance.
- Normal: RGB8 **linear / Non-Color**, tangent-space **OpenGL +Y**, positive
  green is +V, red is +U, blue is outward. PNG top row is V=1. Decode `2*RGB-1`
  and renormalize after filtering. A DirectX normal consumer must invert green
  exactly once. Procedural derivatives use metres independently on U and V.
- Floor normal: pixels are rotated 90° CCW; tangent XY is rotated
  `(Nx, Ny) -> (-Ny, Nx)`, XY slopes multiplied by 0.65, and XYZ renormalized.
  This avoids the common rotated-color/unrotated-normal defect.
- Roughness: R8 **linear / Non-Color**, perceptual roughness, not gloss.
  A GGX implementation may square it for microfacet alpha. Metallic is 0
  throughout. No AO, metallic, displacement or baked-light maps are supplied.
- Use repeat U/V, trilinear mipmapping and suitable anisotropic filtering.
  RGB8 quantization is expected; normalize sampled normals. Separate normal
  mipchains and BC compression are not supplied in this draft.

Use **normal application strength 1.0** for all six exported normal maps.
The floor's 0.65 source attenuation is already baked into its export; do not
multiply by 0.65 a second time. [`INTEGRATION_TABLE.md`](INTEGRATION_TABLE.md)
lists exact filenames, sizes, encoded roughness ranges and primary hashes.

## Ownership and integration boundary

Base `a2d84371c9cbadf46733afb87cc39b73f72dc405` loads one plaster and one wood
texture via `environment_art.c`, and its lighting consumes basecolor only.
Nothing in this directory is automatically selected. Extra loaded runtime
memory is therefore currently **zero**.

The engine owner must select a separate role-based wood map, rectangular metric
UVs and a valid tangent basis before using all channels. Existing door GLB UVs
are not a metre-space contract, so these maps are not drop-in embedded material
replacements. Do not modify `render.c`, lighting, shared loader or physics
material semantics as part of this art branch.

Keep every visual bound to its current `SwatObject`: each skin cell owns only
its own surface; exposed studs remain separate owners; all door primitives
follow the same door owner and disappear with it. Do not add permanent overlays,
new colliders, cross-piece masks, static door skins or replacement geometry.
The worn map represents only shallow cosmetic abrasion, never missing wall
area or a simulated damage state. Use it on sparse selected surfaces: if used
everywhere its small marks necessarily repeat every 2 m. No end-grain material
is included yet.

## Budget

All six 512 px candidates together:

- 18 PNGs: **1,840,045 bytes** (1.75 MiB on disk)
- Basecolors alone: **850,080 bytes** (0.81 MiB)
- Native RGB8/R8 decoded storage: **11,010,048 bytes** (10.5 MiB)
- Conservative all-RGBA8 allocation: **18,874,368 bytes** (18 MiB)
- Conservative RGBA8 full mip chains: **25,165,800 bytes** (24 MiB)

These are maximum storage figures if all maps are loaded, not GPU timing or
compressed VRAM measurements. Source JPEGs, QA and scripts are offline files,
not runtime payload. Keep only selected role/channel maps loaded in a future
integration; don't recursively load every file in this folder.

## Provenance and reversible export

Five surfaces are original seeded procedural fields. Their complete editable
source is `source/build_materials.py` plus `source/parameters.json` (pigments,
seeds, dimensions and roughness). `LICENSE_CC0.txt` dedicates those original
works to CC0 to the extent rights exist. No generated-image art is relabeled.

`floor_pine` uses **Wood Floor Worn by Dimitrios Savva**, CC0:

- [Official asset and 2 m scale](https://polyhaven.com/a/wood_floor_worn)
- [Official asset license](https://polyhaven.com/license)
- [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)

The source came from the existing house-kit texture pack; all three unmodified
1K JPEG inputs are included under `source/cc0/`. Pinned SHA256 values match the
original source record. Official asset/license pages were freshly verified on
2026-10-04. Full download URLs and hashes are in the manifest. No third-party
website preview renders, brand assets, or non-CC0 textures are used.

Exports are non-destructive and offline. Re-running the script regenerates
everything from editable parameters and unchanged originals; adjusting the
grade does not destroy or overwrite source pixels. It also does not overwrite
the preserved environment files outside `materials_v1/`.

```sh
# CPython 3.12.14, NumPy 2.3.5, Pillow 12.3.0; no network or Blender for export.
python3 source/build_materials.py --out /tmp/swat-materials-rebuild
python3 source/validate_materials.py --rebuild --report qa/validation_report.json
python3 source/test_materials.py

# Blender 4.3.2 CPU reference render; see script --help for output/scene options.
python3 source/render_qa.py --help
```

The validator checks exact source/output hashes, file set, RGB/R8 dimensions,
channel semantics, normal normalization, periodic wrap transitions, wear
coverage, budgets, and signed metric derivatives on a non-square tile. Fresh
exports are compared byte for byte (all 18 PNGs and manifest). Encoder versions
are pinned for exact byte reproducibility; different library versions can
change PNG encoding or numerical rounding. `qa/validation_report.json` is the
machine-readable result. Original assets pass their existing validators.
The eight contract tests include seven temporary malformed fixtures: altered
source, wrong transfer function, wrong metre scale, path escape, unexpected
image, invalid normal with a matching hash, and an inverted scan normal basis.

Engine graphics, live PBR, Windows, trainer and gameplay tests are not claimed
by this draft. Those become relevant when the separate engine integration
selects the maps and changes the loader or rendering path.
