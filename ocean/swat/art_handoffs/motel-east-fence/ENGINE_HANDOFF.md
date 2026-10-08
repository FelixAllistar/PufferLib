# Briar Court east boundary: open-ended reuse candidate

Placement is bounded to the approved straight run at X=12.5, Z=0..6 m (approval engine1791474528.653899). Both flanks remain open. There are no returns, gate, extra boundary assets, ground asset or colliders in the supplied render GLB.

## Import

Preferred: load assets/chainlink_panel_2m.glb once and instance it three times using placement.json, then load the matching terminal once. These are byte-identical existing exports. Source archive and editable original source member are identified in PROVENANCE.json. The full original archive is preserved locally and was not re-uploaded.

Alternative: east_open_run_render.glb is an offline placement preview at world coordinates. Do not place it a second time. east_open_run_source.blend is a fresh-import editable assembly of the same assets. Do not deploy both alternatives.

The root is the left/start post ground centre, NOT panel centre. Engine/glTF is Y-up in metres. Rotation is +90 degrees about Y, quaternion xyzw=(0,0.70710678,0,0.70710678); roots are (12.5,-0.08,6), (12.5,-0.08,4), (12.5,-0.08,2). Local +X runs toward world -Z. Add the terminal root at (12.5,-0.08,0) with the same orientation. Scale stays (1,1,1). Nominal panel centres are Z=5,3,1, but those are not root translations.

The source panel owns only its start post. Exactly one terminal supplies the otherwise missing Z=0 end post. The upper Z=6 end already owns its post. This makes a straight fence ending freely at both ends; the terminal does not close either flank.

Ground owner remains engine Y=0. Root Y=-0.08 seats the bottoms slightly below ground. Source height is 1.625 m including cap, giving actual top Y=1.545 m, inside the proposed Y upper limit 1.60 m. Use the actual bounds from placement.json, not catalogue dimensions as centred extents.

## Render and low-cost suggestions

The diamond holes are genuine missing geometry between narrow rods. Original materials use core OPAQUE PBR; there is no alpha-cutout/blend dependency and no solid face behind the wire. Keep original opaque materials. Do not add transparency or an invisible opaque backing.

Three panels are 12,564 triangles; necessary terminal is another 232, total 12,796. Prefer sharing the single panel mesh/material/texture resources across instances; deduplicate original embedded textures between modules if the loader supports it. Material primitive counts are not measured engine draw calls. No reduced LOD or decimation is included; an engine budget decision and measured performance remain pending. Avoid runtime per-wire rigid bodies. Preserve wire silhouettes/holes if a later reduction is separately approved.

## Collision GUIDANCE ONLY

No collision geometry, engine code or physics behavior is supplied or verified. The engine owns movement and ballistics implementation and playtesting.

- Keep render, movement collision and projectile collision/filtering separate. Never infer a collider from the aggregate fence AABB or convex-hull the full panel.
- Low-cost candidate: four short post capsules/cylinders at world X12.5, Z6/4/2/0, matching the visible post shaft (source radius 0.028 m, height 1.6 m; cap radius 0.035 m). Visible brackets protrude beyond the shaft; authoritative group bounds are in placement.json.
- Rails can use narrow cylinder/capsule approximations along each 2 m segment, at world Y1.42 (top source1.5) and Y0.05 (bottom source0.13), radii 0.018 / 0.012 m. Preserve the measured start offsets and end positions; do not extend past the run ends. See component bounds for exact mesh envelopes.
- Wire may need a separate bounded static-mesh or thin-rod query strategy if the intended player movement should be blocked. This needs explicit engine design and validation. A post/rail-only proxy alone does NOT enforce complete pedestrian containment through the mesh.
- Projectile queries must preserve actual diamond holes or use an explicitly chosen perforated/material-aware policy. A continuous invisible box/slab behind the mesh is prohibited. Do not reuse any broad movement approximation as a solid ballistic blocker.
- No collider, nav obstacle, corner or endpoint return may bridge an open flank. Test walking around both ends and shots through holes, through ends and into posts; check camera clipping and neighbouring spawns/wall.

## QA boundary

QA.json and placement.json measure freshly imported source GLBs, roots, triangle count, component geometry bounds, and assembled envelope. source_component_bounds.json derives semantic groups from the original editable source; world component bounds use the same verified root transformation. Current reception-approach.png is a tight office/Room101 image and does not include the far-right Room104 perimeter, so it cannot prove parking-camera benefit for this run.

The schematic previews show a neutral ground plane at Y0 and a reference wall at X8 solely to make 4.5 m centreline clearance and open flanks legible. They are not current-engine screenshots or proof of spawn clearance. Ground/reference wall are excluded from delivered GLB and source assembly.

Receiving engine must confirm actual level spawn clearance, wall alignment, lighting/material import, movement, end passages and ballistics before integration acceptance. No new external materials or uploads were used.
