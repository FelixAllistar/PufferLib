# Masonry edge strip v2: patchy plaster refinement

One art-only candidate for the supplied native feedback. The continuous pale lips are replaced by seven disconnected, uneven-width finish islands (three on one face, four on the other). Broad unequal gaps and tapered ends break the frame-like pale band. Two subdued plaster material tints and the darker fresh-break tone reduce uniform contrast; all texture images are byte-identical to v1. No new noise, painted shadows, external assets, loose rubble, or arbitrary fracture.

## Runtime and ownership
- Runtime candidate: `motel_breach_v2/masonry_edge.glb`. Separate from v1; not installed into an engine.
- 164 triangles, four opaque material primitives; one identity-root static mesh, metres/Y-up.
- Native bounds X [-.092,.092], Y [-.0015,.04], Z [-.5,.5]. End widths match across each face's tile seam.
- The original core primitive, including geometry, UV, material and maps, is unchanged. It still supplies the straight section-based masonry cap; this is a plaster treatment, not fracture topology.
- Apply exactly T(origin) @ Ry(yaw) @ Rx(roll) @ S(depth/.18,1,length), using the supplied `evidence/rear-wall.json` eight edges.
- Visibility remains owner active AND removed neighbor inactive. Owner removal deletes its strip; neighbor restoration hides it. No collider, light, scripts, extra physics or clear-passage changes.
- Pinned W2 desk, accepted lamp/anchor fix, intact Room101 v4 material assets and edge v1 remain untouched.

## Review evidence
`evidence/native_art_review/` includes the supplied native edge captures and desk/lamp/edge README and validation records. The four unchanged desk/lamp PNGs were reviewed from the original input but are omitted from this bounded edge-only archive to avoid duplicate unrelated payload. Its validation.txt reports native/headless passes for v1 integration. Those engine tests were NOT rerun here, and they do NOT establish a native v2 pass.

`previews/all8_v1.png` and `all8_v2.png` are matched Blender GLB-reimport comparisons showing the exact packet and all eight instances. `detail_v1.png` and `detail_v2.png` use a second matched close camera. Both pairs use identical lights, camera, context geometry and the v1 intact-context material. These are art-review renders, not native captures or replicas of native lighting. Native v2 integration/capture remains required before acceptance.

## Rebuild and checks
Requires existing Blender 4.3.2, Python, NumPy and Pillow. No network access required.

1. blender -b -t 4 --python build_strip.py
2. python seal_channels.py
3. python check_channels.py
4. python check_instances.py
5. blender -b -t 4 --python check_source.py
6. python check_rebuild.py
7. blender -b -t 6 --python render_comparison.py

The packed editable blend is the geometry/material-map source. `seal_channels.py` is a required deterministic export stage: it sets explicit opaque/single-sided state, UV0 neutral AO and the two final GLB plaster reflectance factors. Its factors are not painted illumination. Preview and QA consume the sealed GLB. Do not skip sealing when exporting the blend.

GLB_QA covers finite attributes, identity, bounds and nondegenerate geometry/UVs. SOURCE_QA covers packed images, manifold closed geometry, positive volume, and absence of helpers/rigid bodies. CHANNEL_QA checks UV0 colour/normal/roughness/AO, zero metallic and neutral white AO. PACKET_CONTRACT_QA executes all eight supplied transforms and four lifecycle states locally. REBUILD_QA confirms a clean isolated rebuild is byte-identical. IMMUTABLE_QA verifies preserved local references and maps. These checks do not execute the game engine.

`comparison/masonry_edge_v1.glb` is an unchanged comparison reference only. Source/map provenance and CC0 terms remain in PROVENANCE.json and LICENSE.txt.
