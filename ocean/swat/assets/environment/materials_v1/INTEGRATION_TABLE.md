# Engine handoff: material candidates v1

All paths below are relative to `ocean/swat/assets/environment/materials_v1/`.
No loader, render, lighting, material enum or existing asset is changed.
Basecolor is RGB8 sRGB; normal is RGB8 linear OpenGL +Y; roughness is R8 linear.
Normal application strength is 1.0 for every export (floor source strength 0.65 is already baked).
V follows wood grain; UV axes must be remapped per floor/frame/door role.

| Priority / material | Basecolor filename | Normal filename | Roughness filename | Tile U × V (m) | Normal strength | Encoded roughness min–max |
|---|---|---|---|---:|---:|---:|
| `plaster_painted` | `plaster_painted_basecolor.png` | `plaster_painted_normal.png` | `plaster_painted_roughness.png` | 1 × 1 | 1.0 | 0.8118–0.8745 |
| `door_paint` | `door_paint_basecolor.png` | `door_paint_normal.png` | `door_paint_roughness.png` | 0.6 × 2 | 1.0 | 0.6431–0.6784 |
| `floor_pine` | `floor_pine_basecolor.png` | `floor_pine_normal.png` | `floor_pine_roughness.png` | 2 × 2 | 1.0 | 0.6353–0.6745 |
| `framing_pine` | `framing_pine_basecolor.png` | `framing_pine_normal.png` | `framing_pine_roughness.png` | 0.4 × 2 | 1.0 | 0.8510–0.9412 |
| `door_wood` | `door_wood_basecolor.png` | `door_wood_normal.png` | `door_wood_roughness.png` | 0.6 × 2 | 1.0 | 0.7137–0.8039 |
| `plaster_worn` | `plaster_worn_basecolor.png` | `plaster_worn_normal.png` | `plaster_worn_roughness.png` | 2 × 2 | 1.0 | 0.8510–0.9412 |

Use calm plaster and opaque sage door paint first. Keep floor, framing and door wood assignments separate.
Use worn plaster sparsely on selected surfaces only; it is cosmetic abrasion, not damage geometry.
The current loader only supports one wood map and square metric scaling; engine integration is a separate change.

## Verification and review

- `qa/01_basecolor_pixels.png`: raw pixels, no postprocess or lighting
- `qa/02_neutral_closeups.jpg`: six equal 1 m crops, neutral PBR
- `qa/03_same_light_room_comparison.jpg`: four identical scenes including new basecolor-only
- `qa/05_raw_tiling_test.jpg`: 3×3 raw repeats with boundary markings
- `qa/validation_report.json`: normal, metric scale, edge transitions, source hashes and exact rebuild
- `material_manifest.json`: complete source/provenance/budgets and role bindings

## Exact map SHA256

| File | SHA256 |
|---|---|
| `plaster_painted_basecolor.png` | `540a8ff7152ab914caffd3bb3d20b29ae82b77010ff76055e0ed350c70b5f881` |
| `plaster_painted_normal.png` | `16f4452075b0ac3986bb1929f600c19b5b55047af1fcca7dce5530c12220658b` |
| `plaster_painted_roughness.png` | `046db2d87873e2cd4b0d7880c1a1e4672075d2928d2f1a53abaecc54d568f1df` |
| `door_paint_basecolor.png` | `7698ac36929894fff9bdb3294901472bf53eae854fbcc657b05b3be60fa23fc9` |
| `door_paint_normal.png` | `b46dc025def410b5befc22d7d2c29df93424843be09a5dccf72acc75c6becd28` |
| `door_paint_roughness.png` | `69dc722ad7b88b1a832f4f7019c7d556b44722b34c37999f61dfe933247c542a` |
| `floor_pine_basecolor.png` | `f6a9b7645db4e0fa572ea12b1294a0d96e46b541f3bd2174fefee19cebd63e23` |
| `floor_pine_normal.png` | `8e64c9f878c12a10581ecc559a408993bcd7b2e8a49b515d9dd69ae22474dbe1` |
| `floor_pine_roughness.png` | `b7eaf3acffad62c9b465f1db8309c43c25a9ac409772f01f047853529d6fb61a` |
| `framing_pine_basecolor.png` | `d61852a7641a67068fbf49b24fc9fbaeed973b05bac9463a9d7e22b2188655cb` |
| `framing_pine_normal.png` | `ad519b2b8c9271c6fbd06ef2f876b370c8fac4f3402827b310ceb9833b819ea3` |
| `framing_pine_roughness.png` | `76cfa04b4b6b6e6be32e3e4486931a1b768368830529cffa20d88d4365002f21` |
| `door_wood_basecolor.png` | `19eff6c77c51302cfad2d260e3c19d2edeae86a91b76e63ad9fef39930012440` |
| `door_wood_normal.png` | `4eee14f7f7b28f0fb5eb00977116459a83cc0566a874227c5a8e295c606be7c5` |
| `door_wood_roughness.png` | `bdb8f6c56a0bdad3004862c3444b406e6fcac69ce09cbd716f6b074a67d6de48` |
| `plaster_worn_basecolor.png` | `80d8e456ff463f257b2c6c43f5ab1fd7eaf4f02d64eda8cea80705d1f062e028` |
| `plaster_worn_normal.png` | `c84b2ff837e36f95b1d27e0f16955dfa2fd90c5a383de7339d7420fa5ec09e63` |
| `plaster_worn_roughness.png` | `2a4b061b0ae6329f3ee0da18686512b7be965dc9d0de6ddeef0bd9a53af907ad` |

## Scope and source caveat

The preserved generated plaster remains untouched under its separate provenance.
Procedural surfaces are shadow-free by construction. Floor derives from verified CC0 Poly Haven Diffuse,
not certified calibrated albedo; tiny source knots/nail/joint features may retain cavity shading.
Source photographs and exact transformation scripts are bundled, without overwriting originals.
No game/graphics integration, damage behavior or live PBR support is claimed by these offline captures.
