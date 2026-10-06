# Briar Court motor-court motel

Batch 15 · Original CC0-1.0 low-to-mid-poly environment art · Metres

A modest single-storey motel around an open parking court. Four 4 × 6 m guest rooms repeat along an exterior gallery; a small reception connects to a 4 × 4 m laundry/linen room with a side service opening. Sun-faded teal, cream stucco, ochre fascia bands, worn concrete and modest dark glazing keep the location grounded. The eastern room has a newer paint finish. The fictional name, lettering, geometry and plan are original.

## Included

- 24 individually exported GLB modules and fixtures, 13,452 triangles in the new-design library
- Packed editable `motor_court_motel.blend`, with new/reused source libraries, assembled location, roof/cutaway toggle and presentation collections
- Complete `motel_example.glb` and roofless `motel_example_cutaway.glb` (including hidden ceiling-mounted light fixtures)
- 16 existing-library dressing designs, copied byte-for-byte and identified in `manifest.json`; repeated/scaled instances are not new designs
- 24 per-piece previews, four rendered location views, labelled contact sheet and view sheet
- Deterministic original textures, complete build/render/QA scripts and manifests

The kit includes: facade, rear wall, side wall, corner cap, carpet/tile floor, flat roof, gallery slab/canopy/post/railing, separate hinged door and window insert, reception facade/counter/key cubbies, roadside sign, ice and vending shells, room plaque, through-wall air conditioner, wheel stop, shallow access ramp, service doorway and bathroom partition.

## Coordinate and pivot contract

Blender: right-handed Z-up, front −Y, metres. GLB: right-handed Y-up, front +Z. Export maps `(x, y, z)` to `(x, z, −y)` exactly once. Arbitrary custom properties are not automatically transformed, so each connection supplies both coordinate systems.

Authored pivots are assembly datums, not necessarily AABB centres or lowest points. Floors and gallery slabs have their walk-surface plane at Z=0. The ramp runs from court Z=−0.079 to gallery Z=0. The door leaf has a left hinge at the origin and extends along +X; it is opened 100° in guest-room examples. Window insert origin is its lower frame datum. Plaque origin is its backplate centre. Consult exact bounds and named sockets in the manifest rather than automatically bottom-centering everything.

Four-metre facade bays, 6 m room-depth side walls and 2.8 m enclosure height make the main shell. New door and window insert are separate assets. The complete example has all roof and wall geometry; only the saved presentation view/cutaway hides roofs and their ceiling fixtures. The single-storey layout intentionally needs no stair asset.

## Runtime boundary

This is a static art deliverable. Physics and automatic collision flags are disabled. There are no colliders, navmesh, functional vending systems, working doors, animation, LODs or engine performance claims. Opaque dusty glazing is intentional and will not provide transparent views. All light pools are Blender presentation lights; GLB exports omit lights/cameras. Reused GLBs stay byte-identical, including any legacy descriptive collision suggestions; ignore those suggestions and use the assembled instances' render-only defaults.

QA checks include independent binary/index/UV/material/embedded-image validation, fresh Blender imports, actual socket positions, nine sampled 0.6 m diameter × 1.8 m tall capsule routes, furniture support rays, reuse hashes and a portable rebuild. These are geometry checks on named routes, not building-code, accessibility, safety or engine certification. Doors are shown open; a closed leaf naturally blocks its entrance.

## Rebuild

Requirements: Blender 4.3.2+ with its bundled Python/NumPy; Python 3.10+ with NumPy and Pillow for independent audit/sheet composition/packaging. No external model, font or texture downloads are required. All reused inputs are under `reused_assets/`. Build and render sequentially with one process and two threads.

1. `blender -b -t 2 --python-exit-code 1 --python build_motel.py`
2. `blender -b -t 2 --python-exit-code 1 --python qa/reimport_and_routes.py`
3. `blender -b -t 2 --python-exit-code 1 --python qa/assembly_connections.py`
4. `python qa/independent_glb_audit.py` and `python qa/audit_example_summary.py`
5. `blender -b -t 2 --python-exit-code 1 --python render_motel.py`
6. `python compose_sheets.py`
7. `python qa/verify_portable_rebuild.py`
8. `python package_batch.py`

Rendering uses CPU Cycles without denoising. It may take several minutes; some enclosed-view grain is normal. Use `-- --catalog-only` or `-- --locations-only` after the render script for partial rerenders. A build rewrites the native scene; rerun render to restore final camera/light staging.

## Provenance

Architecture and scenes are original, based on general motel construction vocabulary. The project brief used the Library of Congress hospitality image guide as a setting reference: https://guides.loc.gov/hospitality-restaurants-hotels/history/images . No source photographs, commercial game assets, commercial maps, logos or real-incident floor plans are redistributed or traced. All included art and generator code are dedicated under CC0-1.0. Reused source file locations, asset IDs and SHA-256 hashes are recorded in the manifest. `textures/provenance.json` records deterministic texture generation.
