# Rigid weapon handoff

Load **rifle7_rigid_textured.glb** first. It contains the existing Rifle 7 and
one seated magazine as two static mesh nodes, with embedded original 4K PBR
maps. It has no skeleton, skin attributes, animations, cameras or lights and
does not depend on the character skinning adapter.

The optional **sidearm50_rigid_source_scale.glb** is an existing suppressed
pistol with an under-barrel attachment. It is untextured and its intended
physical scale is unresolved. It is reference geometry awaiting engine
calibration, not a fitted replacement for the current sidearm.

## Rifle placement and measured bindings

Units are meters. Forward is +X, up +Y and right +Z. The origin is the measured
center of the actual stock rear pad. Root/node transforms are identity. The
extraction applies only a rigid rotation and translation to the existing
game-scale source mesh; no new scale or deformation is applied.

Use `rifle_bindings.json` for exact values, source vertex/triangle references,
measurement methods, the provisional hand frames and their limitations.
Positions below are relative to the exported origin, in meters:

| Landmark | X forward | Y up | Z right |
| --- | ---: | ---: | ---: |
| Stock pad reference | 0 | 0 | 0 |
| Muzzle tip-ring center | 0.900000 | 0.043626 | ~0 |
| Rear sight aperture center | 0.299822 | 0.124749 | ~0 |
| Front sight post top | 0.654974 | 0.124726 | ~0 |
| Pistol-grip component center | 0.261773 | -0.070997 | ~0 |
| Right-hand surface candidate | 0.261773 | -0.070997 | 0.014665 |
| Support-hand underside candidate | 0.558073 | 0.012222 | ~0 |

The stock-to-muzzle vector is about 0.90 m forward and 0.04363 m up;
its length is **0.901057 m**. The sight line is about **0.0811 m above the bore**.
These measured source dimensions differ from the engine's current nominal
carbine shoulder-to-muzzle length of 0.56 m and sight height of 0.055 m.
The engine owner should decide the mesh/pose alignment; this package does not
shrink the asset to fit those constants.

`Rifle7_RigidBody` and `Rifle7_SeatedMagazine` are independent static nodes in the
same coordinate frame. The magazine is seated in the native bind position.
It can be hidden or transferred by an engine-owned reload adapter later.
This package implements no magazine inventory, reload timing or attachment state.

The stock, sights and muzzle are geometric measurements. The grip surface
points are measured candidate contacts, not an anatomical grip solve. Older
authoring hand matrices are included and explicitly marked provisional. Current
Ready/shoulder/palm fitting is being revised separately. Use achieved
`swat_pose` state and keep authoritative muzzle, clearance and firing rules in
the simulation; an art landmark does not change them.

## Materials and dependencies

The rifle GLB embeds three 4096×4096 PNG images and one core metallic/roughness
material. There are no external image or buffer URIs:

- Base color: original RGB PNG, byte-identical, interpreted as sRGB
- Normal: original RGB PNG, byte-identical, standard glTF tangent-space use
- Metallic/roughness: original grayscale inputs packed into blue/green channels;
  every channel value matches the original source pixel exactly

The supplied height map is recorded in provenance but unused. No displacement,
new texture painting, downsampling or geometry deformation was applied. The
normal-map convention was not separately documented by its source; the package
uses standard glTF interpretation. The engine owner controls shader support,
lighting, exposure and pose calibration. A basic color-only material will not
show the full metallic/roughness/normal result.

The source render triangles were made explicit to produce tangents. Blender's
custom-normal storage introduced a conservative normal-direction difference
under 0.058 degrees. Generated tangent frames were reorthonormalized; 12 rifle
and 3 sidearm degenerate directions needed deterministic orthogonal fallbacks.
`tangent_validation_fix.json` records the affected counts and exact file hashes.
Positions, triangles, UVs, normal maps and tangent handedness were untouched by
that postprocess. These are export diagnostics, not shader-performance claims.

## Sidearm scale and binding status

The source FBX declares centimetres and an authored object scale around 49.0708.
Blender correctly applies that as roughly 0.490708 per local axis. This produces
an imported world bounding length of about 2.716 m; it is not a missed centimetre
conversion. Removing the source's small placement rotation yields a canonical
axis-aligned length of **2.714939 m** with the same geometry and dimensions.

The export preserves this imported scale. It does not resize to the engine's
0.36 m sidearm profile. Its origin is the rear-slide axial station at measured
bore-center height, not a stock or hand target. `sidearm_recipe.json` records
the original transform and reversible conversion; `sidearm_bindings.json`
contains measured muzzle/sight candidates and a geometric grip region.
No hand-wrap/support-hand contact or physical-size calibration is claimed.
The pistol has no stock, and no matching source textures were found.

## Verification and views

`validation.json` verifies both final GLBs directly:

- All rendered source triangles retain orientation and UVs; maximum position
  difference is below 0.00012 mm and maximum UV difference below 0.000000030
- Static normals and tangents are finite; tangent frames are orthogonal within
  the recorded numeric bounds
- Embedded base-color/normal bytes and metallic/roughness pixels match sources
- No skin, animation or external payload dependency is present

The six loose Rifle 7 source vertices unused by any triangle are omitted by
the exporter. All 10,273 rifle-body and 780 magazine surface triangles remain.
The sidearm retains all 26,806 surface triangles. GLB vertex counts include
splits for UVs, normals and tangent seams.

`views/` contains neutral Blender reimport side, top, three-quarter and sight-line
views of the rifle, plus a source-scale sidearm view. They were inspected after
export. They are not in-game screenshots. No engine load, game lighting, collision,
ADS fit or shader performance test has been performed here.

The F character fixture and all current character/Ready work remain unchanged.
No raw asset was added to public GitHub. Source art redistribution rights are
not established; keep this package in the authorized private handoff.

## Reversible extraction

`recipe.json` and `sidearm_recipe.json` contain exact source hashes, node names,
original/imported transforms, forward and inverse matrices and export settings.
The `*_source.npz` files retain the selected source vertices, rendered triangle
indices, original per-corner UVs and normals. They contain no character body or
animation bank. The inverse matrix restores source mesh-local coordinates from
an exported point; no scale-to-profile operation must be added.

Blender 4.3.2 was used. Rebuilding from the original private inputs:

```sh
blender -b --python tools/export_rifle.py -- /path/to/upright16_editable_geometry_base.blend /path/to/original_texture_folder /path/to/output
blender -b --python tools/export_sidearm.py -- /path/to/50.fbx.bin sidearm_geometric_landmarks.json /path/to/output
python tools/repair_tangents.py /path/to/output
python tools/validate_rigid.py /path/to/output /path/to/original_texture_folder
blender -b --python tools/render_neutral.py -- /path/to/output
```

Export uses installed Blender only. Validation uses NumPy, SciPy and Pillow.
These validation tools are not runtime dependencies. The complete source inputs
are identified in `SOURCE_PROVENANCE.json`; their hashes must match before
reproduction. Check all delivered files against `manifest.json` before import.
