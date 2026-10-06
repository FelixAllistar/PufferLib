# Upper-gear remake F · separate 72-bone asset

This private package contains a new geometry revision with rebuilt rigid padded elbow guards and helmet-mounted headset pieces. It preserves the existing natural hand pose and rifle geometry. It does not claim a new grip fix, fitted ADS pose, shared neutral-to-aim shoulder support, or compatibility as a drop-in replacement for the old geometry bank.

GLB: `swat_upper_gear_remake_f_v1.glb`, 4,046,700 bytes

SHA256: `5dd68cf3010b4ba24ec719fb949e1d8f62053c5761142c37da41079848f00835`

Source checkpoint: `swat-upper-gear-remake-f-selfcontained-checkpoint.tar.gz`, 18,171,720 bytes, SHA256 `1e586e869db06e5e6a0026b5c56860e5e0ed7f2d9bc1ae9bbee0eb8ca1fd3e35`

## Select the intended clip

| Exact animation name | Clip ID | Contract |
|---|---|---|
| Neutral Carry / Anatomical Gear F | upper_gear_f_natural_hold_static | One STEP key at t=0; no authored duration. Sample and hold; do not divide by its zero stored span |
| Upper Gear F / Three-second Carry Articulation | upper_gear_f_bounded_carry_proof_3s | Original 3-second bounded raise/return proof, 181 LINEAR samples at 60 Hz; not an approved gameplay loop |

Both clips contain all 216 translation/rotation/scale vector channels for the 72 joints. The source has 720 scalar curves; the movement has 130,320 keys. Original authoring times are retained. The motion raises the rifle approximately 4° and returns, with approximately 1.5° clavicle protraction. The inclusive 3-second endpoint remains in the asset.

The original small endpoint difference and LINEAR endpoint-velocity seam are retained. No cubic seam repair, retiming or motion extension is applied. Source joint review measured a 1 ms finite wrap diagnostic up to 2.477 mm/s and 0.214623°/s; those values are source finite differences, not continuous velocity bounds or a loop approval. The stored `ADS Intent / Anatomical Gear F` source action is deliberately absent from this GLB; it was a raised gear-clearance diagnostic without the shared shoulder bolster.

## Geometry, rest rig and carrier contract

The original 70 bone definitions, parents and native rest matrices remain exact. Two deforming bones are added:

| Carrier | Parent | Exported node | Skin slot |
|---|---|---:|---:|
| Gear_Elbow_L | mixamorig:LeftArm | 25 | 31 |
| Gear_Elbow_R | mixamorig:RightArm | 52 | 58 |

Each carrier requires all three glTF TRS vector channels. Together these represent the source's 20 scalar carrier curves. The anatomical carrier frames are separate from forearm pronation. Blender's `inherit_scale NONE` behavior is baked from evaluated joint worlds into ordinary glTF local TRS. No live IK, driver or constraint is required. Maximum local matrix-to-TRS residual is 5.813e-7; full skin parity measures its actual effect.

Resolve nodes through `skin.joints`; never use a node index as a skin slot. `bindings.json` contains the complete map, parents, original inverse binds and carrier semantics. The default skeleton remains in bind/rest space; explicitly select a clip to display its pose.

The old body mesh loses 27 identified gear components: 2,205 vertices, 2,144 polygons and 3,962 triangles. The compacted `SWAT_Wearer` retains 22,645 original vertices, 22,882 polygons and 42,339 triangles. Retained human positions, complete weights, proportions, polygon groups and UV corners are preserved through explicit original IDs. The four new closed rigid pieces are two 386-vertex elbow guards and two 320-vertex headset pieces. Guards have one carrier influence; headset pieces have one Head influence. Rifle, installed/stored magazines and spare sleeve retain their original geometry and weights. All nine character meshes and both magazines remain visible. Review floor, cameras and lights are excluded.

`geometry_material_changes.json`, the source compatibility files and independent source audit give exact changes. `native_vertex_and_polygon_map.json` and the two correspondence NPZs preserve original N, compacted F and exported seam-index namespaces. The source body has 416 vertices above four influences, including two with seven. Export seam splitting yields 444 vertices above four. Preserve both JOINTS/WEIGHTS sets and normalize all influences together. Source raw weight sums are not assumed to equal one. Eight-influence preflight passes; four-influence preflight deliberately rejects.

## Retained body textures and separate new materials

Every retained wearer triangle maps back to its original N polygon and material group. UV corners agree after the standard glTF V flip within 2.981e-8. `body_texture_group_correspondence.json` and `body_export_correspondence.npz` provide the complete checked mapping:

| Wearer primitive | glTF material | Original N/F slot | Existing texture group |
|---|---|---:|---|
| 0 | 3 · Ch15_body.001 | 0 | Ch15_1001 |
| 1 | 4 · Ch15_body1.001 | 1 | Ch15_1002 |

The original nine-map 2K character texture package remains separately pinned at SHA256 `be6b60251053f034761c400eec63b66ea870ac4336f67400eba3a882bdeb1b7f`. This export does not reinterpret original diffuse/specular/gloss/normal semantics or override the engine's current body shader policy. Its body material factors are the source's neutral preview values. The selected source scenes have no linked image textures, so the GLB has no image payload.

New rigid parts use their own source-authored `SWAT_Gear_Matte_Polymer` and `SWAT_Gear_Soft_Padding` materials and planar untextured UVs. Both are metallic 0, roughness approximately 0.78, taken from actual Principled node inputs. Polymer RGBA is approximately (0.09, 0.10, 0.11, 1); padding (0.038, 0.044, 0.048, 1). These are explicit material factors, not newly invented body texture maps. All six exported PBR definitions and assignments match the source.

Normals are near-exact, not bit-identical. The original N-to-compacted-F custom-normal change is at most 0.034677°. The separate F-to-GLB normal encoding/normalization difference is approximately 0.0048° maximum. Tangents are not fabricated; runtime normal/tangent generation and final appearance remain consumer validation.

## Attachments, stance and mounting limits

Units are metres; Blender (x,y,z) becomes glTF (x,z,−y), with +Y up. Engine reference axes are +X forward/+Y up/+Z right. `measured_static_fit.json` supplies identity root, wearer bounds, source-preserving soles and an unbaked scale-one preview alignment. Sole maps distinguish original N IDs, compacted F IDs and seam-split exported vertices. Centroids average unique source support vertices, not duplicated seam copies.

Physical attachments use `Wnode × IBM[resolved slot] × B_bind_mesh`, equivalently `Wnode × J`, with `J = IBM × B`. Wnode is actual global node world before inverse bind. Apply external model placement once. Prop_Rifle is node69/slot69; Head is node1/slot5. `physical_and_optical_bindings.json` contains both representations, all physical rifle landmarks and mapped lens vertices. Old optical IDs23263/23516 now map to compacted IDs21466/21719; exported primitive indices are separately supplied. This remains a goggle-surface midpoint, not an anatomical eye, camera origin, IPD or optical nodal point. Legacy Muzzle/RifleSocket helpers are not calibrated physical landmarks.

Mounting is approximate. Neutral central cap-underface nearest sleeve distances are about 0.914–3.173 mm. In the delivered movement's sampled central region, left maximum stays about 2.638 mm and right about 3.173 mm; denser peak samples find minima about 1.029/0.679 mm. The often-mentioned 6.888 mm left gap belongs to the separate stored ADS diagnostic, not this movement clip. Cupped whole-underface/rim gaps are much larger, up to about 18.6 mm across the reviewed poses. Headset stems approach the retained overhead band locally, with sampled minimum about 0.231 mm, while much of the stem face remains separated. These unsigned sampled distances do not prove flush fastening, contact force or load-bearing support. The source mounting review is included in full.

Existing source grips and localized stock support are preserved through the bounded proof. They are not newly repaired or approved for a shared aiming shoulder solution. Source surface checks pass only their stated sampled scope; broader garment/accessory clearance, full motion coverage, gameplay mapping and ADS/camera approval remain unresolved. Clip selection does not assign inventory/pouch IDs or alter magazine_seated, ammo, chamber, replay or save/restore state.

## Verification and reproduction

Independent actual-byte evaluation checks all 47,599 exported vertices, 14 primitives, 72 joints, every influence, oriented triangles/material groups, UVs and normal tolerances. It compares the neutral pose and all 361 motion samples at 120 Hz, including every authored key and half-key. Dense direct source-to-binary vertex error is below 0.8 micrometres; binary-to-stock Blender reimport error is below 1 micrometre. All source hashes remain unchanged. This is sampled export/reimport fidelity, not engine runtime acceptance.

The compact portable references contain every native/reimport joint pose, full bind geometry/skin and 13 direct quarter-second evaluated controls. Portable replay explicitly distinguishes all-361 algebraic skin comparisons from those 13 direct controls. The separate dense direct reports document the original full evaluated-vertex pass; the much larger dense reference arrays are not silently claimed to be included. See the independent review and `REPRODUCE.md` for exact commands and scope.

Matched native/stock-reimport images show movement start and peak; the `natural000` filename denotes the proof's natural starting pose, not an additional animation. The source neutral comparison and source movement video are separately prefixed. Static pictures do not prove temporal continuity or mounting force.

This is a private licensed-source derivative. The original character/rifle licensing constraints remain; public raw redistribution is not authorized. Original checkpoints are untouched. No engine code, controller, geometry-bank activation or public repository was changed.
