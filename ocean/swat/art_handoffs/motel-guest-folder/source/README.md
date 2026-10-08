# Motel guest-information folder v1

One original closed desk folder: restrained dark-brown vinyl, cream paper insert edges, and readable original cover lettering (GUEST INFORMATION / ROOM GUIDE). Static decorative prop, not a document UI or a source of gameplay instructions.

232.5 × 306 × 8.2 mm footprint/depth/thickness; 432 triangles, four watertight parts, two materials, one LOD. A second LOD is unnecessary at this budget. No major furniture, duplicate variant, purchased/downloaded art, or logo.

## Rebuild
Blender 4.3.2 with compatible glTF exporter, Python, NumPy and Pillow.

1. Optional: python make_textures.py (system DejaVu Serif regular for lettering).
2. blender -b -t 4 --python build_prop.py
3. python validate_glb.py
4. python audit_uv1.py
5. blender -b -t 4 --python source_reopen_qa.py
6. blender -b -t 4 --python render_export.py

Independent rebuild: blender -b -t 4 --python build_prop.py -- --out /tmp/motel_folder_rebuild
Then: python check_rebuild.py . /tmp/motel_folder_rebuild

Frozen texture inputs are included; no external downloads needed. Rebuild audit compares all GLB and PNG bytes, excluding .blend timestamps.

## Import and placement
Metres. Root and ANCHOR_surface_contact at bottom support plane (Blender Z=0 / glTF Y=0). Root is nominal footprint center (left spine projects 0.5 mm farther). Blender +Z up, cover heading toward +Y. glTF +Y up, heading toward -Z. Preserve exported transforms and unit scale. Root-local glTF bounds: [-0.1165, 0, -0.153] to [0.116, 0.0082, 0.153]. Place on a desk or nightstand with the root at the actual support height. Keep it away from traversable edges.

Engine owns placement, support attachment, collision, gameplay semantics, interaction and disappearance with support destruction. No collision, rigid body, animation, damage fragments or executable engine changes are supplied. All four render parts belong to the same static folder. Preview support and lighting are not exported.

## Materials
Original 512 × 512 basecolor sRGB; +Y OpenGL normal linear; roughmetal linear, R=255, G=roughness, B=0 metallic. Separate baked AO 512 × 512, R, strength 1, uniquely packed UV1. Material maps use UV0. Top-cover UV0 maps readable title to the physical cover; other vinyl faces sample blank atlas margin. Paper uses restrained grain, with no fake tiny text or black line stacks. Do not merge AO into basecolor or sample it using UV0.

## Qualification
Runtime binary, PBR channels, manifold topology, tangent/normal, UV and source reopen audits accompany the package. UV1 overlap is a 2048-square raster triangle-interior test, not a mathematical proof. Preview images are actual GLB reimports in neutral studio lighting, not engine captures. Final user art approval and in-engine placement remain pending.
