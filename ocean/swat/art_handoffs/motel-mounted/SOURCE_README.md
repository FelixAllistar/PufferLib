# Motel surface junction box • independent source-art candidate

Compact galvanized service-wall detail: closed square junction box, two real slotted cover screws, top and bottom hollow conduit stubs, cast connectors, hexagonal locknuts, and two bent mounting saddles with slotted screws.

This is a new independent design, not a revision of the service meter or disconnect box. Source art only: no installed placement, engine edits, collision setup, gameplay wiring, ground, grip, motion, or accepted-asset changes.

## Import
- `runtime/motel_surface_junction.glb`: one opaque PBR material, one mesh/primitive, 1,464 triangles; final measured values in `qa/runtime_audit.json`.
- Metres, glTF Y-up, front +Z. All node transforms are identity. Origin is the centre of the junction box's wall-contact plane.
- Overall width 0.108 m, height 0.580 m, wall projection 0.058 m. Box height 0.108 m. The assembly extends from Y -0.225 to +0.355 m around its origin.
- Place the origin on the intended wall surface; wall plane is Z=0. Wall-contact and conduit-rim anchors are numerically checked in `qa/geometry_and_anchors.json`.
- Conduit continuation centers: [0, 0.355, 0.027] and [0, -0.225, 0.027] metres. Outer radius 0.009 m; inner radius 0.0078 m. End rings are open, with no hidden caps. Stubs are meant for continuation into other service routing or off-frame termination, not represented as electrically safe exposed live wiring.
- Original basecolor, packed ORM, and tangent-space normal maps are 1024×1024. R is neutral AO, G roughness, B metallic. One four-region atlas per map; no transparency. No 8K source exists or is claimed.
- No collision proxy is included. Treat as decorative wall detail unless a host project separately chooses collision. The render mesh comprises overlapping closed manufactured parts, not a single Boolean-unioned physical solid. No destructibility or electrical behavior is provided.

## Source and reproduction
`source/junction.blend` includes editable geometry and packed original maps. Geometry is combined for export; disconnected manufactured components can be selected in edit mode. `source/build_asset.py` is the structured geometry rebuild source. `source/make_textures.py` regenerates the seeded original maps. `source/rebuild.sh` rebuilds and audits. Requires Blender 4.3+, Python 3 with numpy, Pillow and scipy. These standard tool dependencies are not included in the archive.

Previews show the actual exported GLB after fresh import, not a separate high-poly object. Lighting/camera staging is preview-only. Low-poly facets remain visible in macro views by design. See `qa/visual_review.md` for the limited art review and remaining considerations. No in-engine test or user art approval is claimed.

All included art and scripts are original CC0-dedicated work. No fonts, logos, external textures, scan files, or third-party meshes are included.
