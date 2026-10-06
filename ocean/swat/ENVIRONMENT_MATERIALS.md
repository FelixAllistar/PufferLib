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
This is still preview lighting, without scene IBL or baked GI.

Each surface owns three textures with repeating trilinear mipmaps. Auxiliary
normal/roughness use units 12/13, separate from Raylib model maps and depth maps
14/15. Material transitions flush queued vertices before changing uniforms;
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

The visible daylight sky and analytic reflection environment now share their
linear radiance. Specular reflection varies with roughness and view angle for
painted surfaces as well as metal; interior surfaces use restrained room
reflection and contact shading. This remains an approximation without scene
reflection probes or GI. Imported scalar metallic/roughness factors are restored
where Raylib 5.5 omits them for materials lacking an ORM texture; the shader now
supports those materials independently of texture presence. Original rifle
G-roughness/B-metalness maps and their unit factors remain unchanged, following
the [glTF material contract](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metallic-roughness-material).

First-person weapon rendering uses a 62-degree vertical hip FOV (bounded by the
world FOV) and interpolates to the same sight FOV at full ADS. It preserves the
world depth buffer. World dimensions, physical muzzle, achieved weapon fitting,
obstruction checks, projectiles and hit authority retain their original camera
and pose. This changes presentation framing without scaling the source art.
