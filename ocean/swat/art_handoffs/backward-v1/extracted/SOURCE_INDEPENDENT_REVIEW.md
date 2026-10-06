# Upright backward WALK: qualified independent review

The saved candidate preserves the measured source intent and sealed N carry, retains the center exactly, and substantially repairs the loop boundary. It is **not a material-clean or zero-regression result**. Source motion mapped onto the N mesh already produces crowded pants/strap contacts; ankle grounding changes their identities and extent, and the bounded seam changes them further within the same component families. The evidence does not justify changing the gait or creating another pose variant solely from this review.

## Exact reviewed assets

- Native-carry baseline: `../native_carry/ready_walk.editable.blend`, action `Walk Backward / Shared Ready N Native Timing`, SHA-256 `e90cb6001ecd60f2b395eb94e3a4bb1c67fd9270c7111c2c8aacaa6c40d10f66`
- Seam A: `../seam_a/walk_backward_ready_n_c1.editable.blend`, action `Walk Backward / Shared Ready N C1 Loop A`, SHA-256 `91cc7919e4eaba63a8e6ddd658448bb7be0d2ac406738d22c9a0641d22e135e5`
- Both actions are frames 0–30 at 30 fps, exactly one second. The duplicate endpoint is a cycle boundary, not a 61st-frame hold.
- Every mesh's coordinates, topology, UVs and complete weights, plus the 70-bone rest hierarchy, match sealed N. The body has 24,850 vertices and retains all seven influences where present. Prop_Magazine_B remains explicitly collapsed; the separate stored sleeve remains visible.

All review work is read-only with respect to Blender inputs. The raw-leg controls change evaluation state in memory without saving a pose or action. Original backward/right FBX and sealed N hashes match their prior pins. All 83 restored RIGHT payload hashes match the archive manifest. The historical `all16_actions.blend` path is absent from this restored workspace; this review does not claim to have checked that unavailable file. Candidate and staged-source hashes are unchanged after checks.

## Source preservation and grounding

A fresh import of the actual pinned backward FBX verifies the one-second cycle and 1.921261410 m +Y travel, corresponding to 1.921261410 m/s before in-place horizontal travel removal. The native oblique torso and gaze are preserved, rather than neutralized. At 241 source-matched phases, baseline torso/gaze rotations differ by at most 0.000028 degrees; foot/toe rotations differ by less than 0.000038 degrees. The source importer evaluates LINEAR channels despite FBX cubic-auto flags. These checks do not recover the native cubic derivatives.

N and source rest frames differ, so literal source heads and an independently reconstructed ungrounded N hierarchy are reported separately. Relative to that ungrounded control, baseline ankle XY changes are at most 0.000542 mm, native projected knee-pole errors are below 0.000039 degrees, and leg-length error is below 0.000203 mm. Grounding raises ankles by up to 16.541 mm; associated thigh/shin rotation changes reach 5.192/7.045 degrees and knee-head motion reaches 37.201 mm. These are measured grounding changes, not exact lower-body pose preservation.

At 961 phases / 960 Hz, actual complete boot components and weighted sole selections agree on the minimum height:

| Clip | Left minimum | Right minimum |
| --- | ---: | ---: |
| Grounded native carry | 3.685705 mm | 3.957968 mm |
| Seam A | 3.685705 mm | 3.371753 mm |

No sampled boot is below 3 mm or the floor. The nominal 4 mm target is not maintained at every sampled phase. This is a dense sampled clearance check, not an analytic continuous guarantee.

## Bounded seam and continuity

Exactly 604 of 700 saved scalar curves change. `curve_contract.json` enumerates every changed key/handle and its curve, both ±0.125-second windows, scalar endpoint/join slopes, and all-bone analytic pose/linear/angular derivatives. All original interior key coordinates/interpolation, 721 center scalar evaluations, dense center world matrices, and seven complete center body-surface checks are exact. Some unused LINEAR-key handle records change; this does not alter those center curves.

Actual body and rifle endpoint surface gaps are zero for A. Normalized-quaternion analytic FK reconstructs the saved Blender matrices within 5.692e-7, with no unsupported hierarchy flags. Raw quaternion signs are continuous at adjacent keys; normalization is included in derivatives.

| Saved-action wrap metric | Baseline | Seam A |
| --- | ---: | ---: |
| Worst analytic linear-velocity gap | 1.314561020 m/s | 0.000066036 m/s |
| Worst analytic angular-velocity gap | 347.994073 deg/s | 0.005031 deg/s |

The boundary-window joins also have small stored-float residuals: maximum candidate linear gaps are 0.000040658 / 0.000049228 m/s and angular gaps 0.002453 / 0.005442 deg/s. Existing imported LINEAR center knots are not globally C1. Literal mathematical exactness is not claimed; the collapsed magazine's angular velocity is undefined and excluded explicitly.

Seam edits depart from ankle-only-Z grounding. Source-relative ankle XY changes reach 8.001 mm left and 1.496 mm right inside the windows. Actual foot-surface departures from grounded baseline reach 17.886 mm left (swing) and 2.108 mm right (support); center departures are zero. Source-relative hip, chest and head rotation departures inside the windows reach 0.172, 0.375 and 0.600 degrees, respectively. These bounded changes are distinct from the baseline grounding policy.

## N carry, anatomy and upper material

The physical rifle coordinate frame is fitted from all 5,346 actual evaluated rifle vertices; maximum rigid-fit residual is 0.000576 mm. The old Prop_Rifle shortcut is independently equivalent for static N within 0.000176 mm, but the review's contact metrics use the actual-vertex fit. There is no unverified socket-frame assumption.

At 960 Hz, complete connected gloves retain the sealed N surface in that physical rifle frame within 0.001951 mm left and 0.001500 mm right in A. Actual rifle/installed-magazine surfaces remain within 0.000663 / 0.000270 mm. The carry remains braced relative to the chest; no added weight or inertial lag is claimed. Rifle elevation is 14.947–17.206 degrees: the native raised Ready, not an ADS/camera approval.

At 109 full-cycle and densely sampled boundary phases, arm-dominant/vest, rifle/nonhand, installed/collapsed-magazine/body, and tested elbow-connector self-crossing counts are zero. Native hinge offsets and dorsal skin landmarks remain within small sampled tolerances; source wrist/elbow geometry and nonzero joint skin singular values are retained. These are the actual sealed N relationships, not C/C1 grip-diagnostic adoption.

Physical stock-center gap is 2.191711–2.192493 mm. The localized 3 mm-radius patch has 24 of 29 points within 5 mm at every checked phase; it is not a claim that the entire stock pad is flush.

Whole sleeve-root/vest crossings remain 52 left and 63 right. Baseline and A pair unions are identical. Four pair identities are new relative to static N: `(938,8976)`, `(938,8977)`, `(1523,7624)`, `(1787,7687)`. All are Spine2-dominant vest component 256 versus sleeve-root components 4248/3554, not arm-dominant vest penetrations. Their largest chords are 2.986, 5.637, 0.885 and 2.144 mm. Native root folds, the stretched connecting sleeve panel, and the thin/flattened elbow link remain visible in neutral views.

## Lower material: source mapping, grounding, then seam

The native FBX contains no source mesh. Three same-N-mesh raw-leg controls therefore separate the effects without claiming to reconstruct original human clothing:

- At t=0.079167 s, raw-source-mapped N has 192 cross-leg pairs; grounded N has 191. Grounding creates 72 different pair identities at that phase, with a largest new strap/strap chord of 13.099 mm. The raw right sole is -11.570 mm.
- At t=0.233333 s, raw has 132 cross-leg pairs and a 32.506 mm maximum chord; grounded has 138 and 34.885 mm. Grounding creates 36 different pairs, with a largest new chord of 8.819 mm. The raw left sole is -8.697 mm.
- At t=0.716667 s, cross-leg pairs are zero in both controls. The right-leg/torso maximum chord is already 38.894 mm in the raw control and 38.896 mm after grounding.

Across 109 phases, grounded baseline/A cross-leg pair unions are 1,312/1,229 and per-phase maxima 191/185. A introduces 49 union pair identities in the same pants/upper-thigh-strap families (components 5118/11165/11623), plus three right-leg/torso union pairs. No new lower component family is observed.

The largest newly observed cross-leg pair `(13349,21367)` has a 20.704 mm chord at t=0.066667 s. It partly represents contact moving to a neighboring pants triangle: the corresponding family sum decreases from 318.733 to 300.258 mm at that phase. Separately, the pants/right-strap family sum increases from 111.721 to 134.450 mm at t=0.0375 s. Thus a lower overall pair count does not establish zero regression. Triangle chord and chord-sum are intersection-segment measures, not penetration depth.

Matched neutral and component-colored front/rear closeups show the same crowded inner upper-thigh/strap region, without a new broad visible tear in these views. The raw/grounded controls show changes in knee/strap placement as clearance is recovered. Pixels do not override the measured local material changes or prove watertight clearance.

## Separate sleeve and F-gear preview

The separate stored spare-magazine sleeve has 48 vertices/72 triangles, is wholly Hips-weighted, and has saved render/viewport visibility enabled. Across 109 matched phases it has the same 28 crossing pairs in baseline and A, all against Hips-weighted body component 64, with a 20.691 mm maximum chord. No thigh/leg-tag crossings or new seam pair are observed. It was not silently hidden or relocated for this review. Its nesting is reported separately from the collapsed Prop_Magazine_B.

The parent's separate F-gear compatibility report, `../f_gear_preview/NEW_GEAR_SAMPLE_AUDIT.json`, covers 61 poses per clip: existing caps/headsets have zero tested body/rifle triangle crossings, existing F geometry/rest is unchanged, and native 70-bone baseline pose error is zero. This is a sampled compatibility result; it does not broaden the source material review or establish engine/gameplay behavior.

## Reproducible evidence

Core reports are `baseline_source_carry.json`, `seam_a_source_carry.json`, `baseline_analytic_fk.json`, `seam_a_analytic_fk.json`, `curve_contract.json`, `floor_center.json`, `baseline_material.json`, `seam_a_material.json`, `grounding_control.json`, and `sleeve_comparison.json`. The new scripts in this folder reproduce them without saving source/candidate files.

Targeted pixels are in `lower_pixels/` (baseline/A at 0.0375 and 0.066667 s) and `grounding_pixels/` (raw/grounded at the three control phases), with neutral and component-colored front/rear views. Cyan identifies left upper-thigh strap component 11165, orange right strap 11623, and purple component 18662. The stored sleeve is visible.

No anatomical eye/camera calibration, ADS/gameplay approval, runtime edit, resampled export validation, other direction, or force/contact-pressure claim is made. Finite samples and finite-triangle crossings are not continuous analytic or watertight penetration proofs.
