# Wall-bracket fire extinguisher

Original compact near-realism / Xbox 360-era environment source prop. A practical corridor/service-space safety fixture, with formed steel cylinder, valve and gauge, independent carrying handle and squeeze lever, retained rubber hose, nozzle, safety pin/ring and steel wall bracket. Restrained abrasion; no brand.

## Contents
- runtime/motel_fire_extinguisher.glb: 2,412 triangles, one mesh/primitive, one opaque material; embedded 512×512 base-color and metallic-roughness textures. No alpha blending.
- source/fire_extinguisher.blend: packed textures, 32 editable original components plus final joined runtime mesh. Editable collection hidden for clean runtime viewing.
- source/rebuild.sh: deterministic rebuild and audits, then actual-export reimport previews. Requires Blender 4.3.2, Python with Pillow, and DejaVuSans-Bold.ttf at the documented Linux system font path.
- preview/full.png, front.png, close.png, rear.png: actual final exported GLB freshly imported into Blender.
- qa: independent raw accessor audit, packed-source audit, measured contacts, byte-identical clean rebuild and visual review.

## Coordinates / contacts
GLB uses metres, Y up, front +Z, identity node transform. Origin is bottom center of bottle at Y=0. Dimensions approximately 0.275×0.546×0.224 m. Wall contact plane Z=-0.134 m. Lower and upper mount anchors (0,0.093,-0.134) and (0,0.345,-0.134) are verified against exported triangles. Four base-contact samples are measured exported vertices at Y=0. See qa/contact_anchors.json for precise values.

This is a source-art handoff. No world placement, engine edits, collision installation, motion/grip/ground changes, accepted asset modifications, or outer-ridge work. No upload or post performed. Native integration and user art acceptance remain pending.

The label is a fictional decorative graphic, not operating instructions or a safety certification. Original deliverable is CC0; dependency licenses remain separate.
