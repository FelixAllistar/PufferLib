# Original guest-desk stroke cleanup v1

Separate, unapplied render candidate built from the original motel desk, not W2.

## Result
- Only seven isolated HP_wood_dark raised tabletop boxes removed: 84 triangles and 168 exported vertices.
- Original 580 triangles / 1,224 exported vertices becomes 496 / 1,056. Four materials remain.
- All retained geometry, topology, normals and UVs are byte-identical; original embedded texture bytes and material definitions are exact.
- Tabletop slab, edge, base, handles and original finish are unchanged. No new wear, stain, texture, clutter or engine change.
- Original visual top 0.752300024 m becomes actual unchanged slab top 0.750000 m. Preserve original compiled collision/authority envelope and all support/prop transforms.

## Files
- runtime/desk_original_clean_strokes_v1.glb: runtime candidate, local origin, metres, glTF Y up.
- source/desk_original.glb: exact SHA-pinned original baseline.
- source/desk_original_clean_strokes_v1.blend: compact editable candidate, fresh GLB import, Blender Z up.
- source/rebuild.py: deterministic lossless rebuild from baseline (Python 3 + numpy).
- source/reimport_and_render.py: independent Blender import and matched studio renders.
- qa/: measured components, byte preservation, original-scene instance inspection and fresh-import evidence.
- previews/: matched actual-GLB before/after overview and tabletop views. These are studio validation views, not live engine screenshots.
- ENGINE_APPLY_DECISION.json: verified source Room102/103/104 mapping, excluded users, and explicit unresolved current runtime-owner decisions.

## Apply boundary
Nothing has been applied. Do not overwrite the shared motel_v1/desk.glb or rebind all six source users. Room101 W2, the service-area desk, original source master, wallet, glasses, and all pinned exports are excluded. Current dynamic runtime render-owner IDs are deliberately unset. No scene/engine/collider files were edited. Native engine validation remains pending engine-side application.

## Rebuild
Run `python source/rebuild.py` from any directory. It refuses a baseline with any other SHA-256. To recreate the compact Blender source and matched previews, run `blender -b -t 4 --python source/reimport_and_render.py`.

## Provenance
Original batch15 archive was recovered from the existing private project backup and SHA-256 matched to the saved backup record. The exact desk hash matches the original package provenance and catalog r008 motel_v1 runtime record. See SOURCE_PROVENANCE.json. This does not claim a current live repository hash check.
