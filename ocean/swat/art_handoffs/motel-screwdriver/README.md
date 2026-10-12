# Wooden-handle flathead screwdriver — MAIN-only candidate

Source-preserving environmental prop for art review. This is not an engine-accepted asset or a physical-tool specification.

## Import

Use `runtime/wood_handle_flathead_screwdriver_250mm_MAIN.glb`. It contains exactly one active mesh object, one primitive, one material, 2,560 triangles and 1,507 exported vertices (attribute splits). It contains no LOD, camera, lights or duplicate comparison object. glTF axes are X width, Y height and Z length; dimensions are approximately 26.250 × 26.400 × 250.000 mm. Its bottom-centered AABB pivot is for placement convenience, not a verified support/contact surface.

**250 mm is an editorial game scale, not a measured or source-certified real-world size.** Original imported bounds were approximately 0.422948 × 4.028110 × 0.425362 Blender units. No physical construction, metallurgy, handle species, strength, collision, interaction, destruction or native-engine performance is established.

## Source and license

SirDraco65, “Set of 18 PBR Tools,” OpenGameArt, 9 October 2019.
https://opengameart.org/content/set-of-18-pbr-tools

The author listing explicitly offers CC0. It describes FBX/OBJ/3DS tools and LODs; the author says texture bases mostly came from cc0textures.com with manual Blender baking. `source/source_listing.html` captures the listing. `source/Tools_Pack_original.zip` is the unchanged downloaded archive, including its other tools and supplied LODs. These are preserved source material, not additional runtime recommendations or newly counted assets.

Original source archive SHA-256:
0780e8fcfc11ffb4165727f00ad96aff9255e024c2aeaa73bb522d0e428b938c

CC0 reference: https://creativecommons.org/publicdomain/zero/1.0/

## What changed and what did not

The named `Screwdriver` MAIN mesh was isolated. Its local mesh positions, polygon indices and loop UVs are unchanged and exactly hash-matched before/after normalization. No welding, repairs, decimation, smoothing changes, shader retuning or fabricated normal maps were applied. Only an object-level uniform transform and placement translation set the editorial length and pivot. The source FBX had no connected material; matching supplied maps were reconnected using Blender 4.3 Principled defaults.

`editable/screwdriver_original_scale.blend` retains the isolated MAIN object's original transform and source mesh. `editable/screwdriver_250mm_MAIN.blend` is the normalized MAIN-only scene. Both include packed source texture images. The complete untouched original scene/source and its LODs remain in the original ZIP. The supplied LOD is deliberately absent from runtime and editable scenes; it is not recommended by this packet.

## Materials and visible caveats

- Original diffuse: 1024 × 1024 PNG, sRGB, Base Color.
- Original roughness: 512 × 512 PNG, Non-Color, Roughness.
- Original metallic: 512 × 512 PNG, Non-Color, Metallic.
- Linear interpolation, repeat sampling, original `Bake` UV layer.
- No supplied normal, AO or height map; none fabricated or upscaled.
- GLB embeds the original diffuse PNG byte-for-byte and one derived 512 × 512 packed texture: G = roughness, B = metallic. Both packed channels exactly match original source pixels. R is exporter-generated 255 and is not used as occlusion. This packing does not replace the original maps.
- The faceted collar maps 768 base triangles to one UV point (1,0). Its uniform sample is RGB 59/255 in each supplied map (Base Color is sRGB, scalar controls are linear). It appears as a uniformly shaded collar, preserved as supplied. This is not proof of the author's mapping intent or a verified physical-material boundary.
- The original indexed mesh has split boundary vertices. A separate diagnostic positional weld could close the MAIN mesh, but **no weld is in this deliverable**, and that diagnostic does not certify physical construction or manufacture.
- GLB is double-sided, reflecting the connected Blender material default. Imported source normals remain; no tangents or normal texture are included.

## Evidence and previews

`qa/conversion_report.json`: original/local hashes, exact counts, transforms, dimensions and channel/color-space record.
`qa/roundtrip_report.json`: one-to-one geometry/UV triangle comparison against an independent fresh Blender GLB reimport. Maximum measured component errors: positions 1.58325e-8 m; UVs 2.98023e-8. Float32 round-trip is not byte-identical source accessors.
`qa/glb_report.json`: runtime byte hash, embedded image hashes, accessor and scene counts.
`qa/runtime_channel_verification.json`: exact packed-channel pixel comparisons.
`previews/CONTACT_SHEET.png`: clearly labelled single-asset front, opposite-surface back, tip and collar views rendered from the runtime GLB. These are inspection renders, not in-engine performance/appearance approval.

The catalog snapshot and UPDATE07 contained no screwdriver entry. The actual older garage-workshop archive was also checked: its toolbox has dividers but no screwdriver; its pegboard has empty hooks. This is a distinct standalone source candidate, not extracted existing rack dressing.

## Portable rebuild

Requires Blender 4.3 or compatible glTF/FBX importers. Run from any location:

    blender -b --python /path/to/package/tools/convert.py -- /path/to/package
    blender -b --python /path/to/package/tools/verify_roundtrip.py -- /path/to/package
    blender -b --python /path/to/package/tools/render_previews.py -- /path/to/package

Optional report/image labelling tools require Python 3 and Pillow:

    python /path/to/package/tools/inspect_glb.py /path/to/package
    python /path/to/package/tools/label_previews.py /path/to/package

The converter uses a temporary extraction directory and the immutable nested ZIP. Source archive contents are never executed. It rebuilds derived outputs only. Packed image paths in editable scenes may retain the creator workspace path, but the packed images are self-contained. Rebuilding may change Blender/container metadata bytes; verify geometry, UVs and image content rather than expecting identical .blend bytes across versions.
