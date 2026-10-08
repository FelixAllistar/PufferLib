# Conditional masonry edge contract

`rear-wall.json` comes from the actual core via:

    build/swat/physics-comparison/swat_layout_tool motel-walls

It matches the rear-wall proof at parent 14 (Room101). Every listed section is
present before the charge; `survives` describes the proof afterward. World units
are metres, Y up. `source_xy_bounds` is in the original GLB wall plane. Section
local X is normal thickness, Y up and Z along wall. `yaw` rotates about world Y.

The renderer now optionally loads `motel_breach/masonry_edge.glb` and repeats
it over the exported live edges. Asset contract:

- Identity root, metres, opaque static GLB; no light/collider/scripts.
- X spans the nominal 180 mm core, centered at zero. Allow at most 2 mm surface
  overlay beyond each face: X in [-.092,.092].
- Y points INTO the surviving wall from the cut. Confine chipped finish to
  Y [-.002,.04]; at most 2 mm may cover the original flat cap to avoid z-fighting.
- Z is the edge length, centered at zero, with ends exactly -.5 and +.5 metres.
  Keep ends compatible with tiling. The engine scales Z to actual exposed length
  and X to actual core thickness. It does not alter collision.
- UV0 colour/normal/roughness-metal; normal strength from GLB. Use UV0 AO for
  this first strip. Keep primitive/material count low (maximum seven materials).
- Apply scale(depth/.18,1,length), then local X rotation `roll`, then world Y
  rotation `yaw`, then translate to `origin`. All angles are radians.
- `owner` must be active and `removed_neighbor` inactive. Adjacency and overlap
  are recomputed from the current wall, never inferred from a single screenshot.
  Removing the owner removes its strip; restoring its neighbor hides it.
- Do not model loose rubble or alter the clear passage. Floors, sink and
  unrelated art remain outside this asset's ownership.

The [v2 delivered strip](https://drive.google.com/file/d/1rydKzWjKHV0Ecx7XNswU8gODHlYkCNYA/view)
replaces the continuous pale lips with seven separated, tapered plaster patches:
164 triangles, four opaque material primitives and UV0 PBR. The masonry core,
seven texture images and placement envelope are unchanged. The complete archive's
SHA256SUMS were checked before integration; the texture sources match v1 byte for
byte. Native paired views are motel-masonry-v1.png / motel-masonry-1.png and
motel-masonry-edge-close-v1.png / motel-masonry-edge-close.png.

Runtime uses the unversioned motel_breach/masonry_edge.glb path. V1 is preserved
in Git commit 7991c1f7b and in the artist's Drive package, avoiding another copy in
the working tree. Editable current source, texture inputs and original delivery
QA/provenance live in the sibling source folder. Its manifest describes the full
Drive package; duplicate previews and provided helper scripts are not vendored.

The intact capture has no strips. The engine rejects out-of-contract bounds;
absent/invalid art retains the existing physical core. The topology remains
section-based and rectangular. These are exposed-material and shallow finish
details; arbitrary fracture silhouettes and loose rubble remain unfinished.

Native and headless results: [validation](../../motel-reading-lamp/engine/validation.txt).
