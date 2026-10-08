# ME01 restrained masonry cut strip

One render-only reusable GLB, no rubble. Runtime path: `motel_breach_v1/masonry_edge.glb`.
Original procedural geometry and cement-masonry maps; existing Room101 warm plaster maps retained.
172 triangles, one mesh / three material primitives. No lower LOD is useful at this size; hide/fade at engine discretion.

## Exact supplied contract

Native glTF coordinates, metres, identity node/root. Bounds X ±0.092 m; Y −0.0015..0.040 m; Z ±0.500 m.
X crosses 180 mm core. Y points into surviving wall, except the expressly permitted cap overlay.
The exposed cap projects 1.5 mm into the opening, below the 2 mm allowance. It is surface detail, not a passable bite.
Scale `(depth / .18, 1, length)`, then local-X roll, world-Y yaw, then origin. Matrix `T @ Ry @ Rx @ S`.
`evidence/rear-wall.json` is the unchanged supplied 24-section packet. Removed IDs:503–505,507–509; eight edge instances.
Visibility is exactly `owner active AND removed_neighbor inactive`. Remove on owner destruction or neighbor return. Engine owns lifecycle and placement.
No collision, health, nav, ballistics, furniture, facade changes or floor strip. Context boxes are preview-only and never exported.

## Material and surface design assumptions

A dense cement-unit core with light mortar course indications, restrained mineral grains and a deliberately shaped plaster-break profile. No debris, loose chunks or random variants.
180 mm underlying wall remains physically intact. Side core overlay at ±90.5 mm and plaster surface at ±92 mm cover the unchanged old skin without coplanarity. The 1.5 mm lip is a visual laminate representing a break, not a measurement of actual wall plaster thickness. This avoids falsely implying a deeper physical excavation.
The maximum 40 mm surviving-wall band is required by the packet. Core and plaster solids overlap internally, so this is render-only geometry, not a unioned solid usable for collision/CSG. Each source component is closed. Two coincident shared back edges create edge-incidence 4 after positional welding; not holes or nonfinite geometry.
Five nominal course indications per metre of authored edge scale with engine instance length. This reusable asset does not claim exact course continuity with hidden building construction.

UV0 carries every channel. Basecolor sRGB; OpenGL tangent normal linear; roughness G / metal B multiplied by metallicFactor=0 in runtime packed maps; AO is separate neutral white R on UV0, explicitly no baked directional shadows. Plaster break uses scalar matte dielectric. Opaque, single-sided. No UV1 dependency.

## Rebuild

Python3 + numpy/Pillow; Blender4.3.2:
1. `python make_textures.py`
2. `blender -b -t 4 --python build_strip.py`
3. `python seal_channels.py`
4. `python audit_glb.py`
5. `blender -b -t 4 --python render_strip.py`
6. `blender -b -t 4 --python render_packet.py`

The three existing plaster PNGs are bundled rebuild inputs; no network calls or downloads. `masonry_edge_strip.blend` is editable source. Render scripts reimport runtime GLB; preview helpers never enter runtime export.

## Verification boundary

GLB/native axes/topology/UV/material and exact packet instance checks are local asset checks. Engine placement/lifecycle/collision regressions still require the engine owner's integration tests; preview boxes are not a claim of a native engine capture.
