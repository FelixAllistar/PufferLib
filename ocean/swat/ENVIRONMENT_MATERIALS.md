# Runtime environment material adapter

The player consumes the verified `materials_v1/` asset-only handoff. The source
package is pinned by ZIP SHA256
`c8151f71f7fbb09451579ce90d3691eeadb8f954e319481f1bec6f6d79ed1c12`.
Its files, source scripts, licenses, hashes and offline comparison evidence are
preserved unchanged; the package README describes the original handoff, before
this engine integration. Original environment maps and generated plaster remain.

| Authoritative render role | Presentation material | Tile U × V (m) |
|---|---|---|
| Active plaster/drywall skin | Painted plaster | 1 × 1 |
| Thin wood finish floor at ground level | Pine floor | 2 × 2 |
| Wood frame/support | Framing pine | 0.4 × 2 |
| Imported door paint/recess | Sage door paint | 0.6 × 2 |
| Other unpainted wood | Door wood | 0.6 × 2 |
| Existing exposed-plaster door detail | Worn plaster | 2 × 2 |

These are render assignments, not new physics materials. Rendering never writes
to the world, damage cells, ammunition, packets, replay or save state. Surviving
skins remain opaque; real destroyed cells expose their existing framing. No
random cosmetic holes or whole-level decorative collision is introduced.
The role heuristic assumes the current ground-level finish-floor boxes. Raised
floors and future level kits need explicit support/finish bindings.

Wood V follows the longest member axis; finish floors use local Z. Adjacent wall
cells share metric coordinates in the wall basis. Unit-space door vertices are
projected with the current collider dimensions; material IDs are from the pinned
door GLB, with Raylib's additional default material slot accounted for. Hardware
keeps its original material. End caps use a planar projection rather than a
separate end-grain map. Source geometry and door hinge motion are unchanged.

Basecolor receives the renderer's existing gamma decode before lighting. Normal
RGB and perceptual roughness R remain linear. All normal maps use strength 1:
the floor's source attenuation is already baked. Source PNG top row is V=1;
their GPU upload rows are reversed once, allowing positive metric V and OpenGL
green to share a basis. Original PNG files are never re-encoded. Signed screen
derivatives construct the normal basis on immediate boxes and the original
door meshes, including mirrored UVs. Degenerate UVs keep the geometric normal.
Metalness is zero; roughness drives the existing GGX direct specular response.
The current renderer adds HDR sky IBL; local scene probes and baked GI remain absent.

Each surface owns three textures with repeating trilinear mipmaps. Auxiliary
normal/roughness use units 12/13, separate from Raylib model maps and depth maps
14/15. The HDR atlas uses unit 9; contact occlusion/depth use 7/8. These slots are
reserved globally and must not be populated as per-model cubemap/IBL maps.
Material transitions flush queued vertices before changing uniforms;
plain geometry, meshes and the next camera reset their state. Shutdown releases
each owned texture once. Missing channels fall back to diffuse shading, missing
new basecolors to the old maps, and missing art to grayboxes. Explicit asset
directories never borrow missing files from a default directory.

- `SWAT_ENVIRONMENT_PBR=0`: new basecolors only; normal/roughness textures are not loaded.
- `SWAT_ENVIRONMENT_STYLE=legacy`: original map family and door appearance.
- `SWAT_PLASTER_STYLE=weathered`: preserved original worn plaster only.
- `SWAT_ENVIRONMENT_ART=0`: graybox rendering.
- `SWAT_LIGHTING=0`: unlit boxes and original door material/UVs.

Graphics checks cover signed +V/mirrored normal response, plain-surface state
restoration, shadows, scaled/rotated mesh equivalence, layered breaches,
unmodified authority, missing/corrupt assets, both comparison modes and texture
lifecycle. Offline asset hashes and channel/source contracts remain in the
handoff. The motel is now a playable authored mission with individual geometry
and physics bindings; see [ENVIRONMENT_ART.md](ENVIRONMENT_ART.md). Warehouse
and carnival kits remain separate candidates.

## Measured native comparison

GTX 1060 3 GB, 1440 × 810, generated seed 42, 1,154 objects / 10 actors,
600 measured frames after 30 warm-up frames, audio and compact live camera:

| Material mode | Mean frame (ms) | Median (ms) | 95th percentile (ms) |
|---|---:|---:|---:|
| Previous renderer/materials | 8.283 | 6.775 | 13.691 |
| New full material shading | 8.922 | 7.231 | 14.739 |
| New basecolor only | 8.362 | 6.681 | 14.259 |

This is one reproducible scene, not a worst-case or portable FPS guarantee.
The full pass costs about 0.64 ms mean here. Before/after 631-step simulation
journals are byte-identical (42,961 bytes, SHA256
`d60bd47515d9271061cb6cbb9535bc30b98858b7915c238d1cab9056a7d2fa81`).
Both Linux D3D12 and native GPU graphics checks passed; the signed normal test
has +V brightness response 285 and mirrored-basis color error zero. All 20 core
CTest checks passed. Play and planning captures were visually reviewed locally;
these runtime captures are separate from the handoff's Blender QA.

## Daylight and material follow-up

The visible daylight sky and filtered reflection environment now share the
CC0 Poly Haven HDR source in [lighting_v1](assets/environment/lighting_v1/README.md).
Its diffuse convolution, six GGX roughness levels and BRDF lookup are baked
offline in C and checked in with the original HDR and provenance. Specular
reflection varies with roughness and view angle for
painted surfaces as well as metal; interior surfaces use restrained room
reflection and contact shading. This remains an approximation without scene
reflection probes or GI. Imported scalar metallic/roughness factors are restored
where Raylib 5.5 omits them for materials lacking an ORM texture; the shader now
supports those materials independently of texture presence. Original rifle
G-roughness/B-metalness maps and their unit factors remain unchanged, following
the [glTF material contract](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metallic-roughness-material).

Room 101 imports also restore glTF occlusion strength. AO uses the texture's red
channel and affects indirect light only. Optional half-resolution camera depth contributes
short-range contact occlusion from current nearby geometry, with no physics
queries or temporal history; doors and removed objects change it immediately.
First-person weapons and small secondary feeds omit this pass. It complements
the existing bounded indoor ambient approximation rather than replacing it with
full light transport.
Use `SWAT_CONTACT_SHADOWS=1` to inspect it; profiling keeps it disabled by default.

First-person weapon rendering uses a 62-degree vertical hip FOV (bounded by the
world FOV) and interpolates to the same sight FOV at full ADS. It preserves the
world depth buffer. World dimensions, physical muzzle, achieved weapon fitting,
obstruction checks, projectiles and hit authority retain their original camera
and pose. This changes presentation framing without scaling the source art.

## Materials v3: scanned CC0 materials for Briar Court

`materials_v3.h` (included once by `environment_art.c`) replaces the motel's
generated/placeholder finishes with 20 scanned CC0 PBR materials from Poly
Haven and ambientCG, baked into `assets/environment/materials_v3/` and
hill-climbed against reference photographs of 1960s-70s roadside motels
(`reports/cloud/art-materials-v3/LOG.md`, contact sheets `01_*.jpg`..`11_*.jpg`,
overview `00_overview.jpg`). Sources, authors and licences:
`assets/environment/materials_v3/README.md`.

| Surface | v3 material | Where it is bound |
|---|---|---|
| Painted stucco walls, repair patches | `stucco`, `repair` | exterior kit library (`stucco`, `repair`) → every shell module |
| Cream trim / soffit, teal fascia and posts, ochre gutter | `trim`, `fascia`, `gutter` | kit library `cream`, `teal`, `ochre` |
| Concrete details, wall plinth, roof deck + canopy sheet | `concrete`, `plinth`, `roof` | kit library `concrete`, `concrete_dark`, `roof` |
| Walkway deck (transverse joints every 1.33 m), ramp | `walkway` | `gallery_walkway_4m` / `gallery_access_ramp` `MC15_concrete` |
| Walkway curb face, wheel stops | `concrete` | `gallery_walkway_4m` `MC15_cream`, `parking_wheel_stop` |
| Parking lot | `lot` | object 0 (metric box) |
| Road, driveway, highway extension | `road` | ground GLB asphalt, `road_extension.glb` |
| Gravel ground, shoulder; zoning dirt blend | `gravel`, `dirt` | ground GLB gravel, `shoulder_render.glb`; ground blend |
| Guest-room/reception walls (papered faces, framed skins) | `drywall` | interior pass wall copies and skin boxes |
| Guest-room carpet, bathroom floor, ceilings | `carpet`, `bath_floor`, `ceiling` | interior pass floor/roof copies |
| Bathroom wainscot | `bath_wall` | `bathroom_partition_4m` `MC15_tile` |
| Reception floor (VCT), laundry floor (terrazzo) | `reception_floor`, `laundry_floor` | own floor copies for owners 2 and 127 |

Room 101 keeps its accepted realism set. Geometry, UVs, collision, physics
materials, navigation, protocol and determinism are unchanged: v3 only swaps
texture maps and sets per-draw uniforms.

**Repeat and repetition.** Each material has a metric repeat (`tile_m`). Mesh
UVs are rescaled in the vertex shader by `uvScale = 1/(uv_per_m*tile_m)`
(motel_v1 modules map 1.1 UV/m). Two cheap, switchable fragment options break
up tiling: `macroVariation` (world-space 3-octave value noise modulating
albedo by ±x at y cycles/m and roughness by ±z) and `macroDetail` (a second,
rotated sample of the same base map at a different scale, blended in where a
low-frequency noise is high). Both reuse texture unit 0; no new samplers. The
gravel's `macroDetail.z` sets the zoning dirt repeat. Every ordinary material
or surface binding resets the three uniforms, so nothing leaks to other draws.

**Ownership.** v3 textures are entries in the shared art texture pool; every
model whose material borrows one becomes a counted owner, and the set holds
its own reference, so close order between the set, the kit, the shells and the
ground is irrelevant. Displaced unpooled maps are released when no other map of
the model references them. Floor copies own only their `MaterialMap` arrays.

**Switches.** `SWAT_MATERIALS_V3=0` restores the previous look (also off with
`SWAT_ENVIRONMENT_STYLE=legacy`; `SWAT_ENVIRONMENT_PBR=0` disables the set
rather than mixing v3 colour with old relief). `SWAT_MATERIALS_V3_MACRO=0`
keeps the maps without macro variation / two-scale blending. The exterior
shells need the exterior kit (`SWAT_MOTEL_EXTERIOR=0` also removes v3 from them).

**Budget.** 60 JPEG maps, 16 MB on disk (basecolor and normal at 1024 px, 512 px
for three small materials; roughness at half resolution). API-format RGB8 with
mips ≈ 167 MiB of texture memory while the motel is loaded; the replaced kit
library and interior maps stay resident in their owners (~40 MiB, unchanged).
Per pixel on v3 surfaces: three value-noise evaluations and, where enabled,
one extra base-colour fetch.

**Rebuild and review.** `python3 tools/blender/materials_v3_fetch.py CACHE`,
then `blender -b --factory-startup -P tools/blender/materials_v3_bake.py --
CACHE assets/environment/materials_v3` (byte-reproducible).
`tools/blender/materials_v3_preview.py` renders the Blender stand-ins
(`materials_v3_views.json`), `materials_v3_compare.py` composes sheets, and
`make -C ocean/swat materials-capture` builds `tools/materials_capture.c`:
`xvfb-run -a build/swat/materials_capture OUT NAME:ex,ey,ez:tx,ty,tz:fov ...`.
