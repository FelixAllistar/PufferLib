# Motel bathroom accessories v1

Two original standalone generic designs: wall toilet-roll holder (roll, core, tail included) and double robe hook. This package does not alter Room 101 v4, the catalog, the engine, or previous assets.

## Runtime
- One GLB per asset/LOD, metre units. Blender +Z up/-Y wall outward converts to glTF +Y up/+Z wall outward.
- Root pivot is centre of wall-contact plane; mount wall at local Z=0 in glTF. Named parts and owner metadata remain available. Keep one LOD visible at a time.
- 512px maps: basecolor sRGB; normal linear, tangent-space OpenGL +Y encoded as RGB [0,1]; roughmetal linear, G roughness/B metalness/R white. AO is separate on UV1. Do not multiply AO into basecolor or combine it with roughmetal without explicitly remapping UV sets.
- Metallic/roughness factors 1.0; basecolor factor white; normal scale 1.0. Backface culling enabled. No alpha blending or double-sided dependency.
- LOD1 is approximately 45% collapsed geometry with underside setscrews omitted. Choose screen-space LOD thresholds in engine, not blindly by distance. Tiny hooks may be culled at subpixel size.
- Static assets only. No destruction, rigid-body behaviour, weapon response or damage-state claims. Collision JSON is a proposal, not engine authority.
- Wall layout proof is an illustrative original tiled-wall context, not Room101 placement or a game capture. Tile grid is 101 mm pitch; absolute mount heights in the composition are illustrative, not architectural/accessibility advice.

## Rebuild
Python 3 with NumPy and Pillow, Blender 4.3.2 CPU tested.
1. python make_textures.py
2. blender -b -t 4 --python build_props.py
3. python validate_glb.py
4. blender -b -t 4 --python render_exports.py
Alternative build output: blender -b -t 4 --python build_props.py -- --out /tmp/bathroom-rebuild
The alternate-output path copies source textures then rebakes AO deterministically. Byte-identical GLBs/textures are verified; Blender source-file serialization itself is not promised byte-identical.

## Validation limits
GLB validation checks binary buffers, finite positions/UV/normals/tangents, index bounds, nondegenerate UV0/triangles, unit orthogonal tangent frames, per-part watertight welded topology and positive volume, UV1 bounds, PBR factors/channel semantics, part owners and texture budget. AO atlas layout is packed by Blender. Physical collision, gameplay integration, GPU rendering in the target engine and the user's final art approval remain untested.
