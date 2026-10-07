# Motel guest-room utility set v1

Two new, reusable CC0 props. Art candidate only; no renderer, Room 101 v4, physics category, engine catalog or public repository was changed.

## Assets

- mu01_luggage_rack: 0.5588 W × 0.5080 H × 0.3530 D metres; 2,160 triangles, LOD1 860. Walnut-finish generic wooden X-frame with four muted-green webbing straps, metal pivots and rubber glides. Static open pose. No fold animation or rig.
- mu01_wastebasket_small: 0.2984 W × 0.3112 H × 0.2096 D metres; 1,156 triangles, LOD1 324. Unbranded warm-cream molded shell with rounded corners, closed base and open rolled rim.

Each GLB is independently usable. Unit scale is 1 metre. glTF axes are +Y up, +X width, Z depth; placement root is floor center at [0,0,0]. Blender source is +Z up. Do not apply an additional 100× centimetre conversion. Per-export bounds and SHA-256 are in GLB_QA.json. LOD1 slight silhouette shrink is <1 mm on the basket and <0.3 mm on the rack. LODs are separate files, not an automatic glTF extension; pick by projected size after engine review (suggest a first visual test around 100 px object height). They are not extra unique catalog designs.

## Ownership and materials

Named mesh parts carry part_owner and part_id extras under one asset root. Keep part ownership when instancing. All exported surfaces are single-sided closed solids; normal and tangent attributes are supplied. Straps are solid ribbons rather than alpha cards. Source meshes are manifold and all faces have nonzero area.

512 × 512 PNG maps, maximum runtime dimension 512 (below the 1K cap):
- baseColorTexture: sRGB; factor [1,1,1,1]
- normalTexture: linear / non-color, tangent-space OpenGL (+Y), scale 1
- metallicRoughnessTexture: linear; R=255 unused, G=roughness, B=metallic; factors 1 and 1
- occlusionTexture: physically separate per-prop AO image, linear R sampled on TEXCOORD_1, strength 1; clamp sampler
- Basecolor, normal and roughmetal use TEXCOORD_0 and repeat samplers. Real repeat sizes in REFERENCES.json

The original Blender exporter tries to repack AO into RM. seal_glb.py restores separate AO and RM PNG data while preserving geometry and explicit factors. Run this step through build_props.py; do not bypass it if re-exporting. In Blender the glTF Material Output node stores the separate AO contract. Studio renders rely on path-traced geometric occlusion; the final_glb_reimport preview checks the sealed exports, not a game renderer.

## Collision and gameplay

COLLISION_PROPOSAL.json is a discussion proposal only. It is not collision authoring, a physics-body assignment, or an engine semantic decision. Rack: suggested OBB proxies for major wooden members; leave openings and straps visually open. Basket: suggested compound four wall boxes and base, retaining an open top; engine should refine the tapered fit. Engine owns categories, dynamic/static decisions, projectile handling, interaction and final collision shapes. No hidden runtime collider meshes are included. No destructibility, damage states, break pieces, animation, navigation changes or gameplay tests are supplied.

## Rebuild and verification

Requires Blender 4.3.2 or compatible 4.x, Python 3, numpy and Pillow for map generation/audit.

1. python make_textures.py
2. blender -b -t 4 --python build_props.py -- --out /absolute/output/path --no-render
3. python validate_glb.py /absolute/output/path

Omit --no-render for source closeups and context previews. A clean build was checked and all four resulting GLBs were byte-identical (REBUILD_QA.json). SOURCE_QA.json checks source manifoldness/areas; GLB_QA.json independently parses exported binary accessors, verifies bounds, indices, welded manifold topology and outward winding via positive signed volume, finite/unit normals/tangents, tangent orthogonality, nondegenerate geometry/UV0, unique AO-UV range and PBR maps/factors. AO is shared by LODs; LOD1 retains interpolated UVs, so small bake mismatch is possible at near-view distances and is another reason to reserve it for small projected size.

The .blend is editable and keeps every component separately named. The scripts reconstruct it from original geometry and original procedural maps. Rebuilding writes only the output directory. Vendor reference links and any uncertain dimensional provenance are disclosed in REFERENCES.json; no vendor imagery is redistributed.

## Review scope

Visually inspected vendor photos, source studio closeups, small neutral set/context previews and sealed-GLB reimport. Restraint is intentional: modest grain and weave variation, softened edges and slight roughness variation; no heavy dirt decals or fictional damage. Engine import, game lighting, LOD switching and art acceptance remain pending.
