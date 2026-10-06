# Visual review — Morrow Block / batch 16

Reviewed actual Blender-generated images, first at draft resolution and then the full-size final player-height renders and modular contact sheet. These are art inspections, not screenshots from the game engine.

## Corrections made before release

- Separated the structural floor slab top (−8 mm) from the finish top (0 mm). The early coplanar faces caused black self-shadowing; the final floor surfaces render normally.
- Stopped the masonry-pier body below its cap and made window/door/transom cross-members meet their rails rather than overlap on the same surface. The black corner/top artifacts in the early catalogue are gone.
- Corrected workshop-tool support to the actual 0.8815 m workbench tabletop. Triangle-ray support checks place all six supported props within 2–3.5 mm of their real support surfaces.
- Re-established scene ownership metadata after source GLB imports. Explicitly switched reused instances from imported quaternion mode to XYZ before applying scene yaw.
- Export-only triangulation supplies valid tangent bases for every normal-mapped material while keeping native meshes editable. Optional tangents on materials without normal maps are omitted, avoiding meaningless zero tangent vectors on tiny presentation bevels.

## Final visual observations

- Street view: two distinct tenants read clearly through the awning/shutter rhythm, differently colored original signs and open entries. Frames, sill, cornice, roller housing and canopy brackets have actual depth. Brick scale and material variation remain quiet.
- Laundromat: existing washer/dryer rows have believable appliance scale, with a clear central walking line and visible shared-service connection. A payment fixture, laundry hamper, ceiling lights and a folding nook establish use without random clutter. Tile grout is visible and floor artifacts are resolved.
- Pawn/repair: warmer timber floor, a glazed case, service counter, second-hand display goods and steel shelving distinguish the room. The partial closure is conveyed by one shuttered front bay; the room is intentionally sparse.
- Shared service: existing workbench, drill, pliers and shelving form a small coherent work area. The adjoining doors and cross-passage remain clear. Utility sink and laundry stock occupy the opposite end and are visible in the cutaway.
- Cutaway: bounds-based framing retains all building corners, the original room plan and the open rear service leaf; the two public rooms and shared rear work area are visible together.
- Contact sheet: all 24 masters are framed separately with names, dimensions and triangle counts. The reusable door leaves, frames, transom, shutter and track assembly remain distinct editable assets.

## Presentation limits

Final player-height views are 1120 × 784 pixels, Cycles CPU, maximum 80 samples with adaptive sampling, and two render threads. The installed Blender build lacks OpenImageDenoise support, so these are unfiltered actual renders; some sampling grain is visible, especially on interior ceilings/glass. This does not hide geometry errors. The contact sheet uses 360 px source previews at 20 samples. No image generation, image-based retouching or photographic background is used.

The original models are CC0; previous CC0 model/texture provenance remains preserved. The old provisional image-generated plaster is absent. No collisions, game navigation, door operation, damage, engine rendering or building-code compliance is demonstrated by these images.
