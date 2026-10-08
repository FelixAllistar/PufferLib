# Desk oak W2: warm finish with restrained surface wear

A separate unintegrated candidate for engine render owner 24 / source assembly 023 / REUSE__desk__023. No pinned replacement, collision change, outside upload or engine acceptance is claimed.

## Finish
The supplied Poly Haven Oak Veneer 01 diffuse is multiplied by linear-light [0.40,0.32,0.25,1.0], an explicit glTF baseColorFactor approximating a warm absorptive stain. Factors are not sRGB swatches. No shadows, noise or damage are painted into source pixels. Blender uses an equivalent Multiply node. All supplied 1K image bytes and the W1 physical 1.8 m UV projection are retained, with normal scale 0.3 and metalness 0.

## Ridge removal and shallow wear
Seven artificial dark raised stroke components, 84 triangles/168 isolated vertices, are removed. Four new tiny irregular tapered finish-abrasion regions are integrated into the flat tabletop via constrained triangulation. They are 27–62mm long and at most 0.4–0.7mm wide, tapered to zero at the ends. They use the same base color, image sampling, physical UVs and normal as surrounding oak, and only roughness changes to 0.90. They simulate shallow finish scuffing without a displaced gouge, dark rods, dirt or new texture. Their visibility is deliberately subtle and depends on native lighting.

There are no stacked or nearly coplanar overlay faces: all top regions share the original z 0.75 m plane. The original two flat top triangles become 50, including 16 wear triangles. Exported triangle coverage is checked against the original top area, with zero pairwise overlap. Original side/bevel/base surfaces stay unchanged.

Final desk: 544 triangles =580−84−2+50. Oak slab 62 triangles plus wear 16. Cost versus W1: one extra material and render primitive (six rather than five), no extra image. All other 316 source-scene objects, placement, parent and properties remain unchanged. W1 and all 68 baseline GLBs remain immutable.

## Binding and bounds
Use only assets/desk_023_oak_veneer01_w2_comparison.glb as an optional complete render mesh. Retain existing instance transform once, catalog desk / env:01_house/desk, collision, AI, damage and parent authority. Never draw original and candidate simultaneously. Native import/lighting/performance/behavior remains untested for W2.

Old local render Z-up bounds: [-0.71,-0.415,0] to [0.71,0.415,0.7523]m. New: [-0.71,-0.415,0] to [0.71,0.415,0.7500]m. Only the removed decorative ridges caused the 2.3 mm max-height decrease. Do not regenerate collision or authority envelope from the candidate renderer. The existing compiled collider was neither read nor modified here. Exact floats are in qa/RIDGE_REMOVAL.json.

## Standalone runtime rebuild
The source package includes the editable packed desk_023_oak_w2_asset_source.blend and unchanged original source maps. Requires Blender with glTF exporter (tested 4.3.2):

    blender -b -t 2 --python rebuild_runtime.py -- /absolute/output/w2_rebuilt.glb

Rebuild temporarily bypasses Blender's Multiply node during export, then inserts the native glTF baseColorFactor, avoiding baked recoloring. It never edits the packed source. The independent rebuild is checked against the delivered GLB.

## Full-room reconstruction
The full room is not duplicated. SOURCE_DEPENDENCY.json pins the existing W1 scene and W1 GLB. Work in a fresh writable package copy:

    blender -b -t 6 --python build_finish.py -- --source /path/to/W1/room101_desk_oak_w1_comparison.blend --export
    blender -b -t 6 --python add_shallow_wear.py -- /path/to/this/package/room101_desk_oak_w2_comparison.blend

First step builds warm finish plus ridge removal. Second step integrates wear, saves final source/runtime and renders matched desk detail. It requires its supplied clean source to contain 496 polygons and refuses other geometry. Standalone runtime rebuild does not need W1.

## Review scope
qa/desk_detail_w1.png is the unchanged prior W1 final 128-sample render. qa/desk_detail_w2.png matches camera, resolution, source lights, AgX exposure, samples and seed. These are Blender review captures, not native engine captures. Tint-only and clean controls stay local, excluded as separate catalog variants. An abandoned overlay test was replaced before delivery by coplanar topology; no overlay geometry is packaged.
