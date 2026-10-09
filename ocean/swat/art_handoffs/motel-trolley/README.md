# Two-shelf service trolley
Source-only original near-realism prop for motel reception/service supply movement. Empty static pose. Not placed, integrated or accepted as game content.

- 2,416 triangles, one mesh/primitive, one opaque PBR material
- Two 512 × 512 embedded PNG maps: original base color and metallic/roughness
- Metres, Y-up identity GLB; front +Z; root at floor center
- Four measured wheel contact lines at Y=0; wheel centers X ±0.410, Z ±0.230 m
- Approximate bounds: 0.899 W × 0.924 H × 0.588 D m
- Editable named original components retained in hidden collection in packed Blender source; visible joined export mesh. No external art files needed to open the blend.
- Formed sheet-steel trays, turned lips, support channels, tubular handle frames, rubber bumpers and caster forks/hubs. Restrained wear and finish variation.

## Files
runtime/motel_service_trolley.glb is the runtime candidate. source/service_trolley.blend is editable packed source. preview images are freshly reimported GLB renders, never the source scene. qa holds numerical, provenance-gap and rebuild evidence.

## Rebuild
Requires Blender 4.3.2, Python 3 and Pillow. Run bash source/rebuild.sh from anywhere. Build and render scripts resolve their package root, not a fixed machine path. No engine, collision, world placement, upload or third-party asset dependencies. Contact lines belong to static low-poly tires, not physics colliders. Both support shelves are one static design, not interactive animation.
