# Briar Court reception noticeboard v1

Source-only original compact wall fixture, unplaced. No engine or floorplan changes. Engine owns mounting, support, collision, placement and damage.

## Contents
- runtime/motel_reception_noticeboard.glb: one joined mesh, one opaque material, one 1024px atlas set (base color and ORM), 1,416 triangles.
- source/reception_noticeboard.blend: packed images and 13 hidden editable original components. Enable the Editable_original_components collection to edit; hide the joined runtime object to avoid duplicate display.
- source/make_atlas.py and build_asset.py: reproducible procedural sources; source/rebuild.sh rebuilds and checks the package.
- preview: actual exported GLB freshly imported, full/front/readable-paper detail/rear views.
- qa: independent raw GLB checks, packed-source audit, mounting dimensions, rebuild and visual evidence.

## Integration
GLB is metres, Y-up, front +Z, identity node transform. Extents: X ±0.360 m, Y ±0.250 m, Z 0–0.032 m. Origin is center of the rear wall-contact plane Z=0. Frame rear faces contact that plane; backing is recessed 1.5 mm forward to avoid coplanar surfaces. Suggested reference anchor centers are (-0.25,0.234,0) and (+0.25,0.234,0), 0.50 m apart. These are measured placement reference coordinates, not installed fasteners or a structural mounting design. Engine should determine fixing/support appropriate to its wall surface. No wall slab, floor footprint, collision object, physics setup or damage system is included.

Timber frame: nominal 33 mm face width × 32 mm depth, with 1.2 mm two-segment bevel and mitred corners. Backing 6 mm, recessed 1.5 mm from the rear; cork insert 18 mm; papers 0.28 mm, shallow natural bottom curl. Four burgundy low-profile pins; no glass. Subtle old pinholes and differently faded removed-paper footprints age the exposed cork; lower frame corners have restrained dark hand wear and rubbed edges; paper edge aging is irregular without obscuring type. Three notices are fictional set dressing, not actual motel policy. Headline uses ordinary serif typography; other text is conventional sans-serif, all rasterized into the atlas with no text-geometry overhead.

## Atlas budget
One 1024 × 1024 basecolor PNG (sRGB), one matching ORM PNG (non-color: R=255 AO, G=roughness, B=0 metallic). No normal map, transparency, decals, extra materials or runtime fonts. A 512px reduction would leave 20–24px body lettering at only 10–12px before UV sampling/mip filtering; 1024px preserves the readable close-up requested. The whole set is 6 MiB RGB decoded, ~8 MiB with a full mip chain (engine GPU formats may differ). Alpha remains opaque. Distant text readability is not guaranteed after game mipmapping.

## Rebuild
Requires Blender 4.3.2, Python 3, Pillow 12.3.0 and NumPy 2.3.5 (versions tested). Run source/rebuild.sh. Seed 271828 is fixed. Font files are included unchanged with their actual license. The canonicalizer sorts triangle order and cyclically rotates indices without changing triangle winding, positions, UVs or normals: Blender's native unordered triangle iteration otherwise changed binary ordering across exports. Final GLB and atlas are byte-identical across independent rebuilds. The .blend binary serialization itself is not claimed byte-identical.

See SHA256SUMS.txt for exact file hashes and qa/runtime_audit.json for measured numerical results. No engine draw, collision or wall placement test was performed.
