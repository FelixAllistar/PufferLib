# Morrow Block — neighborhood storefronts, batch 16

An original two-tenancy block: **Morrow Wash**, a maintained neighborhood laundromat, beside **Alder Pawn / Buy Sell Repair**, a partly shuttered shop. These are fictional names and a newly authored layout, not a commercial game-map reconstruction or a real incident site.

## Open and inspect

- `neighborhood_storefronts.blend`: editable, metre-scale native scene; all textures packed. Collections separate the new asset masters, reused masters, structure, furnishings, site and toggleable roof.
- `assets/`: **24 original modular GLBs**, authored separately with editable component vertex groups and JSON socket/pivot metadata.
- `storefronts_example.glb`: complete furnished example including roof.
- `storefronts_example_cutaway.glb`: same static scene with roof and roof-mounted light fixtures omitted for inspection.
- `storefronts_contact_sheet.jpg`, `storefronts_location_views.jpg`, and the five numbered full-size PNGs: actual CPU-rendered geometry, not image-generated concepts.
- `manifest.json`: bounds, triangle counts, source IDs, SHA-256, physical texture-tile sizes, materials, pivot descriptions and sockets.
- `assembly_manifest.json`: exact instance TRS and world matrices, declared material overrides, socket connections and support relationships.

## Design and dimensions

The original building footprint is **12 × 10 m**, split into two **6 × 6 m** public rooms and a shared **12 × 4 m** rear service/storage area. Wall height is 3.2 m, with a raised front fascia. Structural bays and floor/roof slabs use a 2 m grid. The public doors are separate 1.2 m glass leaves with left-hinge origins, fitted into 1.22 m-clear frames. Stockroom/service openings are 1.24 m clear. Rear connections and the front openings are genuine holes in wall geometry, not facade cards.

The laundry front has a full transom rhythm and striped awnings; the pawn front has exposed roller boxes and a single closed shutter. A compact display case, counter, original payment fixture, wall notice, vented backdoor and service hatch give the shops distinct functions. Public-room tile/wood separation and a plainer shared work line establish legible zones without excessive wear.

Fifteen earlier laundry, household, furniture, fixture and workshop designs are reused under their original IDs. Their full source GLBs remain byte-identical in `reused_assets/`; **none is counted as a new design**. Several designs have multiple scene instances. Catalog/provenance records include source hashes, bounds, license evidence and copied original manifests.

## Materials and UV metres

The six quiet material families from `inputs/materials_v1` are included unchanged with editable sources, source scans, license and map hashes. Used roles include intact plaster, sage door paint, timber framing, door wood and a desaturated pine-floor scan derivative. The optional worn-plaster source is included but is not used broadly. The earlier provisional image-generated plaster is excluded, and this package does not relabel it CC0.

Original additional surfaces distinguish buff brick, mortar, enamel, cloth, metallic trim, tile and glass. These have small quiet authored pigment variation; wear is restrained. Each material has an explicit physical tile in the manifest. UV = projected surface metres / material tile metres, with no randomized per-instance UV phase. The family normal maps are tangent-space OpenGL +Y, Non-Color, strength 1.0. Roughness is linear perceptual roughness; color is sRGB. glTF may combine roughness into an ORM texture during export without changing the source input files. A temporary export-only triangulation modifier supplies explicit tangent attributes, including handedness, for the normal-map basis; unused tangent attributes on materials without normal maps are omitted. The editable native polygon topology and vertex groups are retained.

Glass uses core glTF alpha blend at 0.20. It does not require a transmission extension; appearance and transparency sorting remain consumer-dependent. Reused props retain their own original materials and UV contracts. Their embedded old textures are not silently replaced.

## Build and validate

Use Blender 4.3.2 (or compatible glTF exporter) and two CPU threads:

```sh
OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 blender -b -t 2 --python-exit-code 1 --python build_storefronts.py
OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 blender -b -t 2 --python-exit-code 1 --python render_storefronts.py
python compose_sheets.py
python qa/verify_portable_rebuild.py
```

`build_storefronts.py`, `geometry_core.py`, `lettering.py`, `inputs/` and `reused_assets/` are the full reconstruction inputs. No earlier batch or repository checkout is required. Blender supplies its Python/Numpy; standalone QA and sheet composition use NumPy and Pillow; contact-sheet labels use the standard DejaVu Sans system font. The model build itself has no font-file dependency. The input material family also preserves its own generator sources and dependency notes.

The packaged QA scripts/reports check binary glTF accessors and embedded textures, fresh imports, exact transforms, true socket alignment, furnishing support against triangles, specified capsule routes, and full isolated regeneration. The portable check requires exact SHA-256 reproduction of all 24 module GLBs and both assembled GLBs. Archive verification reads every payload back and compares hashes.

## Integration boundary

Everything is **render-only art**. No collider, rigid body, navmesh, damage behavior, animation or runtime interaction is supplied or tested. Source GLB legacy suggestions must be overridden by consumers where appropriate. Separate doors are suitable visual parts, not working game doors. Static capsule-route QA is an offline geometry check for the listed paths, not building-code compliance or game-engine navigation verification.

No engine loader, physics, renderer, networking, gameplay code, old kit, source asset or texture was modified. Import into an engine and assess collision, visibility, lighting, alpha sorting, performance and interaction separately.

## Licensing

Original geometry, fictional sign artwork, scripts and original procedural texture additions: CC0-1.0, see `LICENSE.txt`. Reused assets and material-family input sources retain their original CC0 provenance. The pine scan source is Poly Haven **Wood Floor Worn** by Dimitrios Savva. Source licenses and hashes are preserved. CC0 conveys no trademark/publicity rights or warranty.
