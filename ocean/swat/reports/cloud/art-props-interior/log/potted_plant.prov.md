# potted_plant — provenance
- Reference: "A healthy Snake Plant (Sansevieria) grows in a clay pot ..." by Bigul Malayi, WordPress Photo Directory, CC0 1.0 — https://wordpress.org/photos/photo/358687d150/ (found via Openverse). Committed downscaled copy: refs/potted_plant.jpg (960x1280, 269 KB).
- Textures: none downloaded. Leaf (256 px) and terracotta (256 px) maps baked from seeded numpy noise; saucer and soil are PBR factors. Leaves are solid double-sided strips (no alpha).
- Asset: assets/environment/motel_props_int_v2/potted_plant.glb — 3,578 triangles, 4 materials, 228,604 bytes; bounds x -0.267..0.278, y 0..1.124, z 0.06..0.456 (origin floor, back-edge centre; pot centre z=0.25, pot dia 0.37 m; leaf tips span 0.55 m wide above 0.6 m height).
- Build: blender -b --factory-startup --python-exit-code 1 -P ocean/swat/tools/blender/props_int_v2_potted_plant.py -- ocean/swat/assets/environment/motel_props_int_v2 [--render PNG]
- Iterations: 7 (Blender).
