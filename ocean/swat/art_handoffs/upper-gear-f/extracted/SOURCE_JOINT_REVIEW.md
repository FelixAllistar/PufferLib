# Independent stage F joint and carrier audit

The actual 181 evaluated poses support the bounded three-second carry articulation. Native limb identity, calibrated hinge/dorsal conventions, fixed grips and fingers, and anatomical elbow carrier frames remain preserved within evaluated floating-point precision. All 16 exact action/identity contract checks pass, and all captured numeric arrays are finite. This finding is limited to joints, material frame conventions, carriers, and sampled motion; it gives no surface, attachment-distance, collision, naturalness, gameplay, anatomical-eye, or camera approval.

Read-only Blender 4.3.2 evaluation sampled 0–3 seconds at 60 Hz and added only 1 ms one-sided endpoint diagnostics. Actual evaluated wearer material landmarks were used: all 149 forearm hardware and 49 dorsal-hand markers per side, resolved through unique native_vertex_id values. No scene was saved. All source, reference, and analysis dependency hashes remained unchanged and were checked again when this review was finalized.

## Sources and action

- Movement SHA-256: 7165d1454e0526f01ca85c81f12e0245dfecb22071191918f984477a42c091b1
- Static F SHA-256: 4271f2279bc3cbbcb46594157752b2ac0ca052628c24b4c8629a82c6771a3fc0
- Native N SHA-256: de60108a89bfe7f2f5bb92f2b35f2d692d3a659ca7f1dbc310ca7023572a33ef
- Active action: Upper Gear F / Three-second Carry Articulation. Scene 30 fps, frames 0–90; 720 complete unique location/quaternion/scale curves, including 20 carrier curves. Each has 181 linear keys at half-frame spacing, totaling 130,320 keys.
- All 70 native rest matrices, heads, tails, lengths, parents, connection/deform flags, and scale-inheritance settings exactly equal N. Gear_Elbow_L/R are the only additions, each parented to its matching upper arm. The complete 72-bone rest snapshot matches static F. The rig object matrix exactly equals N.
- The allowed varying local bases comprise the 12 arm/shoulder/rifle/magazine/carrier bones. All other 60 local bases are exactly constant. Human/rifle surface identity and geometry checks belong to the separate surface review and are not duplicated here.

## Joint, grip, and carrier measurements

- Wrist bends: left 36.082277–36.661023 degrees; right 31.354605–31.800196 degrees. Native N already measures 36.661057 / 31.354584 degrees. Neutral carry names the native reference pose; it does not mean straight wrists.
- Elbow bends: left 34.038740–36.120253 degrees; right 108.320742–109.379189 degrees. Signed native upper-arm hinge offsets remain approximately -9.1418 / +12.1688 degrees; maximum absolute residual is 0.000074444 degrees.
- Actual-material dorsal residual absolute maxima: 0.000702811 degrees left / 0.002969687 degrees right. Native N itself measures +0.000569 / -0.002721 degrees. These preserve the calibrated material convention; they are not a medical range-of-motion test.
- Head-to-head upper-arm/forearm lengths differ from rest by at most 0.000309694 mm. All evaluated bone head-to-tail lengths differ by at most 0.000250337 mm. Upper-arm tail-to-elbow gaps near 9.0826 mm left / 14.9401 mm right are inherited from N/rest, rather than new motion disconnections. Forearm tail-to-wrist gap stays below 0.000505762 mm.
- Fixed hand-in-rifle frames: maximum drift from first sample is 0.000492643 mm / 0.000027592 degrees; from native N it is 0.000382924 mm / 0.000034862 degrees.
- All 40 finger local matrix_basis values are exactly constant. Maximum hand-relative frame drift is 0.000400919 mm / 0.000011592 degrees from first sample, and 0.000370627 mm / 0.000027214 degrees from N.
- Both carrier frames were independently reconstructed at the elbow: Y along elbow-to-wrist; Z along normalized upper-arm × forearm; X = Y × Z, with orthonormal reclosure. Worst deviation is 0.000255610 mm / 0.000045406 degrees. These anatomical frames are distinct from forearm pronation.
- Carrier bend-plane cross-norm minima are 0.559753 left / 0.943343 right, avoiding collinear ambiguity. Maximum consecutive 60 Hz carrier rotation is 0.091345 / 0.050945 degrees. Carrier pose determinants range 0.999999369–1.000001336, and maximum carrier channel-scale deviation from one is 0.000000715.
- Across 181 × 72 poses, no pose or deformation determinant is zero or negative. Pose determinant range: 0.999994179–1.000001932; deformation determinant range: 0.999994187–1.000002057; singular-value range: 0.999996899–1.000001522. Maximum keyed scale deviation is 0.000002384. These data show no reflected, collapsed, or deliberately stretched limbs.

## Measured arc and loop qualification

The actual rifle rotation follows a monotonic 0→4.000004654→0 degree outbound/return arc, peaking at 1.5 seconds. Left/right shoulder bones similarly reach 1.500005685 / 1.500003639 degrees about opposite world-Z directions. Maximum sampled half-cosine waveform error is 0.000017243 degrees. The measured rifle axis is approximately (-0.999979, -0.006459, 0). Axes are established from actual midpoint rotations; this verifies amplitude and timing, rather than independently certifying sight-line elevation.

All 72 bone endpoints close within 0.000333200 mm / 0.000328158 degrees; maximum matrix difference is 0.000005513430. No material endpoint snap is indicated at this precision.

Exact C1 velocity continuity is not demonstrated. All keys are linear; their first and last segment slopes are small and opposite. The 1 ms evaluated seam diagnostic measures worst linear velocity difference 2.476688 mm/s at Muzzle, and worst angular difference 0.214623 degrees/s at mixamorig:LeftForeArm. The rifle seam angular difference is 0.144851 degrees/s. Short finite differences amplify float32 evaluation noise. Endpoint closure is supported to numerical precision; exact velocity-smooth or gameplay-ready looping is not established. The machine-readable postprocessor also records every bone's 60 Hz secant-speed bounds, explicitly as interval averages rather than instantaneous velocity limits.

For a repeating three-second export at 30 fps, omit the duplicate frame-90 endpoint: frames 0–89 supply 90 samples. Frames 0–90 inclusive supply 91 images.

No global 120 Hz/midpoint pass was triggered: the specified 181-pose audit found no material joint/carrier anomaly, while the known seam limitation follows from the linear bake. Arbitrary subframe behavior is not exhaustively covered.

## Reproducible artifacts

- audit_movement_joints.py / .log: read-only Blender pose and material-landmark evaluation, adapted from the established stage D audit
- MOVEMENT_JOINTS_INDEPENDENT.json: all 181 sample rows, full curve inventory, native/static rest comparisons, per-bone rigidity and loop metrics, and all input pins before/after
- MOVEMENT_JOINTS_INDEPENDENT_matrices.npz: actual evaluated pose matrices, local bases, scales, lengths, rests, names, and times
- postprocess_motion_diagnostics.py / .log: reproducible captured-matrix analysis adapted from stage D analyze_sampled_motion.py
- SAMPLED_MOTION_CHECK.json: exact contract checks, numeric finiteness, measured path, carrier stability, and sampled secant speeds
- ARTIFACT_SHA256.json: hashes for the review artifacts; the JSON report separately pins the three scenes and calibration/algorithm dependencies
