# Backward WALK authoring evidence and limits

Status: one native-source candidate authored; parent independent material/pixel review and packaging remain separate. No gameplay, export, controller, other direction, crouch, reload, hand optimization, rig or geometry change is included.

## Verified inputs and intent

- Native FBX SHA-256: `97b1662e913c09ad61f3aa3a2840fa6dc87dfc6ec4b38636f1f3f152b54871eb`
- Sealed N SHA-256: `de60108a89bfe7f2f5bb92f2b35f2d692d3a659ca7f1dbc310ca7023572a33ef`
- Native take: exactly 1.000 s at 60 Hz; one cycle travels +Y 1.921261 m in Blender world space. Target is in place at the same pace and period, 0–30 at 30 fps
- Preserve the native oblique pelvis/chest, gaze and bent-knee backward gait. Native source Hips Z is approximately 0.758–0.841 m; no attempt is made to straighten the source into a neutral slow retreat
- Native FBX has no props. Collapsed `Prop_Magazine_B` is an explicit compatibility choice inherited from the prior upright WALK RIGHT action, documented in `source/prop_magazine_b_compatibility.json`

## Raw-source baseline fidelity

`native_carry/source_fidelity.json` independently reimports the actual FBX and checks saved native-carry animation at 480 Hz.

- Hips/Spine/Spine1/Spine2/Neck/Head/HeadTop_End world rotations differ by at most 0.0000371°; head-position differences after root removal are at most 0.00123 mm
- Source root translation and Z remain intact after subtracting initial XY plus cycle XY. Saved Hips head position error is below 0.000239 mm
- At 240 Hz author samples, foot/toe world rotation departure is below 0.000026° and grounding ankle XY error is below 0.000245 mm
- Between keys at 480 Hz, interpolation produces a maximum foot orientation residual of 0.008255° left / 0.000913° right and maximum ankle XY residual of 0.09944 mm left / 0.00612 mm right
- Maximum ankle-only lift is 16.541 mm. Two-bone IK preserves leg lengths to 0.000148 mm at author samples. The largest knee-head movement is 37.201 mm left / 19.265 mm right; thigh/shin rotations change at most 5.193°/7.046° left and 2.691°/2.545° right. These are the disclosed grounding changes
- Native material knee-plane residual at 480 Hz is at most 0.00678°
- Solver minimum boot surface clearance is 4.000 mm. Saved baseline minimum at 960 Hz is 3.686 mm left / 3.958 mm right. Sampling is not analytic continuous floor proof

The full N carry transports rigidly with raw Spine2. All 180 curves outside the 52-bone carry packet remain exactly equal to the original backward body library. No original hand/grip curves are optimized or replaced with diagnostic hand fits.

## Bounded loop repair

The imported-linear native baseline has a saved-action toe-end linear wrap mismatch of 1.314561 m/s and a toe angular mismatch of 347.9941°/s. The explicit repair is limited to [0,0.125] and [0.875,1] seconds and preserves timing.

- All 700 center curves are exactly equal on [0.125,0.875] s. Dense 960 Hz center world matrices and seven checked full-body center meshes match exactly
- 604 saved curves change within the boundary windows. `seam_a/saved_curve_diff.json` includes every changed saved key, handle and derivative. `curve_diff.json` and `endpoint_target_derivatives.json` retain construction details
- Analytic saved-channel FK wrap residual: maximum linear velocity gap 0.00006604 m/s; maximum angular gap 0.005031°/s. These small residuals are due to stored float precision; no claim of exact symbolic equality is made
- 960 Hz sole minimum: 3.686 mm left / 3.372 mm right; no sampled sole below 3 mm or through the floor
- Maximum boundary surface change: 17.886 mm on the left swinging foot / 2.108 mm on the right support foot. Left ankle/toe orientation change peaks 2.340°/4.378°; right ankle/toe 0.126°/0.122°
- Hips/Spine2/rifle deviations and their exact worst phases are recorded in `seam_a/floor_center.json`

## Surface audit and remaining material limits

Nine matching eighth-cycle phases, including both endpoints and both seam-window joins, were evaluated for baseline and seam candidate against sealed static N. The body/weapon mesh topology, weights and rig rest identities match N exactly. Finite triangle tests do not prove continuous collision absence or penetration depth.

At those phases: arm-dominant/vest crossings 0; rifle/nonhand crossings 0; magazine/body crossings 0; connecting panel component 16480 self-crossings 0. Stock center gap stays approximately 2.192 mm, with 24/29 central 3 mm patch samples within 5 mm, matching N. Arm joint lengths and native hinge/dorsal relations remain stable.

The whole arm-component/vest test has 52 left pairs versus 50 in static N, and 63 right pairs versus 63 in N. There are two new pair identities per side, present in both baseline and seam candidate. Existing own-sleeve/armor layer folds also remain. Some near-contact pair identities vary at floating-point-sensitive intersections. These findings require independent material and pixel classification; this candidate is not described as collision-free. No compensating hand or geometry changes were made.

The raw chest orientation with N carry produces a sampled rifle elevation of about 14.95–17.17°. This follows the explicit rigid-carry choice and is not an independently optimized aim or engine camera alignment.

Initial neutral renders are in `review/native_static/candidate/{front,side,left_arm,feet}/` at times 0,0.25,0.5,0.75 s. They are review evidence, not proof of engine rendering parity. The recovered physical weapon and optical proxy bind conventions remain inherited unchanged.
