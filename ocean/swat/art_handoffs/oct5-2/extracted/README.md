# Static Crouch Ready / Planted Shared N Low-Ready

This private fixture preserves one planted, source-scale crouch low-ready pose. It contains **one STEP key at time zero**, with a stored animation span of zero and **no authored duration**. Sample the named clip at t=0 and hold it according to consumer state. Do not divide by its duration, invent a playback period, or treat this pose as an idle cycle, stance transition or crouch locomotion.

- GLB: `crouch_ready_planted_shared_n_static.glb`, 3,620,792 bytes
- SHA256: `54957d0769b6ecaaa164969f0c0315316f6834714044a2b376364f2a32fd6d66`
- Exact animation name: `Crouch Ready / Planted Shared N Low-Ready`
- Clip ID: `crouch_ready_planted_shared_n_static`
- Source: `crouch_ready_n.editable.blend`, SHA256 `b316df0d4c4af329dffe5f5330c502da14d17c0d3af77eecd257262a435445d9`
- Source checkpoint: 18,715,894 bytes, SHA256 `6fb887ef7551d9cf23f45ee1a394609a121fa649bfb5695b7ff1f6f382e2cf10`

## Static animation and source preservation

The saved action has 700 scalar curves, each with one LINEAR key at Blender frame 0 and CONSTANT extrapolation. There are no source intervals or temporal tangents. The export has 210 unique TRS channels across the native 70 joints; each stores one STEP sample at t=0. This interpolation representation changes no authored temporal behavior. Unit quaternion normalization and fixed rest-frame composition introduce only the separately measured floating-point error. `SOURCE_CURVES.json`, `animation_channels.json` and `conversion_report.json` disclose the exact source and representation.

The default glTF node transforms and original inverse binds still describe the existing bind skeleton. **Select and sample the static animation to obtain the crouch pose.** A loader that ignores the animation will show the original bind/rest pose. This fixture deliberately does not change the bind skeleton or bake the crouch into mesh geometry.

The native mesh geometry, raw complete weights, oriented topology, loop normals, UV arrays, material slots/face assignments and 70-bone rest hierarchy match the pinned shared source. All static glTF geometry, normal/UV/index arrays, materials, nodes and skin inverse binds match the existing forward fixture. Only the animation accessor data and asset provenance describe this new pose. Native scale remains one. The body retains all seven influences, including JOINTS_1/WEIGHTS_1; 444 seam-split exported body vertices exceed four influences. Normalize across the combined sets without truncation. Eight-influence preflight passes; four-influence preflight must reject.

## Measured pose, ground and view references

Source Blender world Z becomes glTF model Y. The measured source values are:

| Measurement | Metres |
|---|---:|
| Highest body vertex | 1.359917283 |
| Full-body vertical extent | 1.355917257 |
| Hips origin height | 0.670974314 |
| Goggle-surface midpoint height | 1.188156724 |
| Lowest left/right sole | 0.004000026 / 0.004000143 |
| Physical stock-center height | 0.989771485 |
| Physical muzzle height | 0.723586202 |

Both feet retain broad heel/toe support patches and both knees are raised. This is a planted two-foot crouch with retained native anatomical knee construction, not a kneeling pose or uniformly scaled standing body. The 4 mm sole spacing is authored clearance, not exact contact at ground zero. Static geometry checks do not establish physical center of mass, balance, foot locking during movement, or interpolation from standing.

`static_pose_measurements.json` distinguishes native-evaluated body measurements from actual exported node/proxy/weapon measurements. `measured_preview_fit.json` supplies root, basis and sole references plus a fixed unbaked preview transform. Sole indices explicitly refer to the original 24,850-vertex Blender body and include their resolved glTF primitive/vertex matches. The preview support selection uses more than 0.5 cumulative same-side Foot/Toe weight and a 1 mm patch above the minimum. Source QA uses more than 0.65 cumulative same-side Foot/Toe weight and an 8 mm patch. These are different weight-based selections; their namespaces and counts are explicit. Root remains identity. These are preview measurements, not gameplay collider or camera dimensions.

The physical rifle points **19.956483° downward**. The goggle-surface proxy lies approximately **147.868 mm perpendicular to the rear/front sightline**, with its outward surface normal approximately **14.5843° from that line**. Its point-to-rear-sight distance is about 199.812 mm. These are geometric relationships of a low-ready pose, not eye relief, anatomical gaze or ADS calibration. The goggle midpoint is the virtual mean of actual lens-surface vertices 23263/23516, each 100% Head weighted. It is not an anatomical eye, optical nodal point or camera origin.

The central stock reference retains about 2.192 mm axial backing on curved shoulder gear. All 29 samples of the central 3 mm-radius patch hit support;24 are within 5 mm. The full rear face has no source-reviewed crossing but its sampled gap spans approximately 0.755–24.807 mm. This is localized support rather than flush pad seating or a force simulation. The raw hand/grip surface candidates are reference geometry; they are not newly approved IK targets.

## Correct attachment spaces

Units are metres. Blender(x,y,z) converts to glTF(x,z,−y), with +Y up. The rigid rifle asset uses stock-origin axes +X forward/+Y up/+Z right. Every skin slot is resolved from `skin.joints`; a scene node index is not a skin index. Prop_Rifle currently maps node 67→slot 67; Head maps node 1→slot 5.

For the rigid rifle or optical frame:

`model_from_attachment = W_node * IBM[resolved_slot] * B_bind_mesh = W_node * J_joint_local`

Here `J_joint_local = IBM * B_bind_mesh`; W_node is the actual global scene-node matrix **before** inverse bind. If the consumer already has the deformation matrix W_node×IBM, multiply B directly, without applying IBM twice. Apply external actor/preview placement once. Both attachment representations and exact inverse binds are supplied in `measured_preview_fit.json` and `optical_proxy_bindings.json`. `bindings.json` includes all joints, parents, skin slots and semantic nodes. Legacy RifleSocket/Muzzle helper origins are retained source helpers and are not the calibrated physical stock or muzzle.

## Props and persistent state

The installed magazine remains on the rifle. **The stored spare magazine B is visibly noncollapsed at the hip**, alongside the existing spare sleeve. This differs from the collapsed B in the walk fixtures. All five skinned mesh objects are enabled. The actual stored scale and node/skin mapping are recorded in `events_and_state.json` and the binary.

There are no authored fire, footstep, reload, ownership-transfer or reset events. Visual props do not assign game inventory, persistent magazine IDs or pouch IDs. Holding, entering or resetting this static pose must not change authoritative magazine_seated, ammo/chamber/inventory, replay or save/restore state.

## Validation and remaining limits

The producer's full-vertex source/GLB and **unmodified stock Blender 4.3.2 reimport** checks pass for the one authored pose. Maximum native-to-GLB error is 0.658027 micrometres and native-to-reimport error 0.678831 micrometres. All rendered bind/weight vertices and oriented triangles are preserved; the same six unreferenced rifle vertices are omitted as in the static template. Unlike a cubic animation, this single STEP key has no cubic tangent behavior to lose in stock import. `validation.json` records the exact scope.

The independent reviewer separately captures the pinned source and stock reimport and evaluates the actual binary with its own STEP parser. Its native vertex error is approximately 0.812 micrometres; independent source-to-stock-reimport error approximately 0.935 micrometres. These are distinct captures and methods, not interchangeable maxima. The included static reference NPZs and portable actual-byte replay allow verification after extraction; direct Blender regeneration uses the separately pinned source checkpoint. No temporal velocity, loop or moving-pose claim is made from one sample.

`native_pose000.png` and `stock_reimport_pose000.png` use matched neutral framing and were visually inspected. The source's measured front/side, same-scale standing comparison and contact-detail panels are also included and clearly prefixed `source_`. Source panels are not engine screenshots. Neutral material/noisy comparison renders do not establish final lit or textured appearance.

Native own-sleeve/brace/link folds, waist/upper-thigh layers and the exposed underarm panel remain. The source review finds zero opposing leg and arm/leg crossings at this pose; it does not claim a globally intersection-free character. The spare-sleeve back mounting block retains 28 belt crossings identical to standing N; no sleeve-cavity collision is introduced. This inherited accessory mounting overlap is explicitly preserved. Source report flags such as `GLB_export_qualified:false` describe the earlier source-only review; this package's export checks are separate.

The original neutral materials and rest normals are unchanged. Body arrays remain compatible with the separately delivered nine-map 2K texture package; assignments are in `material_compatibility.json`. No image, tangent or substitute material is fabricated here. Original texture-authoring normal convention remains unproven; preserved source graph uses a tangent Normal Map with unchanged green. Shader appearance, runtime normal/tangent handling, lighting, stance calibration, achieved gameplay pose, transitions, GPU integration, ADS and camera approval remain consumer work. No full official Khronos validator run is claimed.

Read `REPRODUCE.md` for exact replay commands. `manifest.json` pins every delivered file. Prior character/weapon/source packages remain unchanged. Licensed source assets are for private project review; public redistribution is not authorized.
