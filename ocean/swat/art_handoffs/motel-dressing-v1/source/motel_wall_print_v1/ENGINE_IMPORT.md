# Motel wall print candidate v1

One sparse wall-dressing option, not engine-integrated. Room101 v4 is unchanged.

- Runtime: `exports/mw01_framed_woodland_print_lod0.glb`, glTF Y-up metres
- Pivot: wall contact center. Back plane Z=0, front +Z; center at chosen hanging height
- Overall: 0.53 m wide × 0.43 m high × 0.01 m deep; art opening 0.50 × 0.40 m
- Named owner root: `mw01_framed_woodland_print`; all seven parts carry `part_owner`
- 468 triangles, four PBR materials, no skeleton, alpha blending, transmission, or interaction
- Separate UV1 AO, UV0 basecolor/roughness-metallic/normal. Do not bake AO into basecolor
- Surface approximates printed paper behind a budget polystyrene front in a single opaque coated material; acceptable room-distance economy rather than physically modeled glazing
- No collider/destruction or placement manifest. A thin static rectangle collider is optional only if engine authority wants it
- LOD0 only; a second LOD is unnecessary for this small triangle count. Cull by projected size

## Rebuild
Requires Blender 4.3.2, Python 3, Pillow, NumPy, and ImageMagick `convert`.

1. `python make_textures.py` downsamples supplied original art and creates channel sidecars
2. `blender -b -t 4 --python build_prop.py -- --out /tmp/wall_print_rebuild`
3. `python validate_glb.py /tmp/wall_print_rebuild`
4. `blender -b -t 4 --python render_export.py` generates neutral import previews in the package directory

The source blend has relative texture paths. Rebuild GLB and textures are verified byte-exact from the supplied inputs. The blend container itself is not a byte-exact reproducibility claim. Stochastic image regeneration is not part of rebuild.

Preview renders use a generic neutral wall and studio lighting only. They are not Room101 placement tests or engine captures.
