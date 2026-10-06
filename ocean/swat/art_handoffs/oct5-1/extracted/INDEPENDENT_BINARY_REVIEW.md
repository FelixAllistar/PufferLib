# Independent WALK RIGHT cubic fixture review

**PASS, qualified for numerical export fidelity and retained loop/carry behavior.** The saved source retains visible opposing upper-thigh strap/pants intersections; five small cross-leg pair identities and one same-side hip/strap pair are added by its bounded repair. This review does not qualify interlimb clearance, zero material regression, runtime, gameplay, ADS, or artistic acceptance.

## Identity and independence

- GLB SHA256: `edcb227870a0483d06c99e89df491e24f6eb22c879e35833e597f3d6a196b899`; 5,730,956 bytes
- Native source SHA256: `f85525ba445013f73884677bd8202d7ffff226b04fedee930250c90cbe402770`
- Action: `Walk Right / Shared Ready N C1 Loop A`; frames 0–30 at 30 fps; one second
- Source checkpoint: 17,702,044 bytes, SHA256 `93a8ec6e810c9d161c16cc90e5842952a07311a1dd5f9ab4e8a8ef669ef32180`
- All 83 archive manifest entries verified directly; 84 files including the manifest

The unchanged independent NumPy `replay_unit.py` reads actual GLB JSON/BIN bytes, evaluates Hermite polynomials with per-second tangents, normalizes quaternion polynomials with their exact normalization differential, propagates scale-aware FK derivatives, extracts polar angular derivatives, and uses every stored skin influence. It imports neither the fixture converter nor its `gltf_math` module. Native Bezier time is independently inverted. The structural inspector changes only its default direction basename and report schema from the previously reviewed LEFT version.

Saved-curve identity checks resolve all 700 scalar curves and 159,854 keys (40,321 BEZIER, 119,533 LINEAR), all candidate key counts, and 40,982 saved boundary-key values/handles/interpolation fields exactly. The verified RIGHT changed-curve count is 661. The initial 657 count was a LEFT carryover and has been corrected in delivered metadata. Source repair windows are 0–0.125 s and 0.875–1 s; export perturbations are measured separately.

## Serialized structure, skin and static data

All 459 accessors pass finite-value, byte-range, alignment, type and stride checks. The hierarchy has unique parents and no cycles. All 210 unique translation/rotation/scale channels cover 70 joints with CUBICSPLINE, correct 3-values-per-key output counts, increasing times, and exact 0/1 s endpoints. Maximum unit key quaternion norm residual is 4.040163737e-08.

Nodes, skins, meshes, materials, scenes, and all 39 geometry/inverse-bind accessor arrays exactly match sealed LEFT static data (SHA256 `4fe97ec1997bb4799440216ddc3a74d858154c0d8e51bf863e2d86082b1a1996`). This compares shared static data only; the RIGHT animation comes from its distinct source curves. Two exported body vertices retain seven influences, and 444 body vertices exceed four influences. Complete weights are normalized together across JOINTS_0/1 and WEIGHTS_0/1. Largest raw exported weight-sum residual is 1.32713466883e-7.

All 48,755 exported vertices independently match captured native bind positions exactly and complete normalized source weights within 2.21251010846e-8, with no ambiguous matches. Reconstructed native-index oriented triangle multisets exactly match every source rendered mesh. The rifle source has six unreferenced vertices omitted by the existing static template; all rendered topology is retained.

`Prop_Magazine_B` scale keys and every tangent are exactly zero. Its world 3×3 linear transform and derivative are exactly zero at 961 phases. Its angular velocity is undefined due to collapse; only angular quantities exclude it. Positions, skinning, and all-vertex comparisons include the collapsed prop.

## Endpoint pose and analytic velocity

Every exported vertex endpoint position and every joint endpoint translation is exactly equal in the independent evaluator. The maximum joint-world matrix element gap is 2.328139539e-10, at RightToe_End, with maximum orientation gap 1.333980489e-08 degrees. Thus the world matrices are not claimed literally identical.

| Measurement | Saved source | Serialized GLB |
|---|---:|---:|
| Maximum joint linear seam, m/s | 6.852982618e-05 | 6.855263591e-05 |
| Maximum joint angular seam, degrees/s | 0.004866553585 | 0.004865103195 |

The serialized worst linear joint is mixamorig:LeftToe_End; worst angular joint is mixamorig:LeftLeg. Maximum endpoint source-to-GLB matrix error is 4.181793756e-07; one-sided linear derivative error is 1.21734482e-06 m/s and angular error 3.976808673e-05 degrees/s. Source and exported seams are separate measured residuals, not exact mathematical C1.

| Primitive | Vertices | Analytic vertex velocity seam,m/s |
|---|---:|---:|
| Rebuilt spare magazine sleeve, primitive 0 | 144 | 1.063145643e-05 |
| Rebuilt SWAT full body, primitive 0 | 7638 | 5.562168831e-05 |
| Rebuilt SWAT full body, primitive 1 | 24049 | 6.923204328e-05 |
| Rifle 7, primitive 0 | 14936 | 5.768438797e-06 |
| Removed magazine, primitive 0 | 994 | 4.497514807e-06 |
| Fresh magazine, primitive 0 | 994 | 0 |

Weighted foot subsets contain 899 LEFT/916 RIGHT exported vertices across two body primitives. Maximum foot velocity seams are 6.92320432775e-5 m/s LEFT and 4.55250489458e-5 m/s RIGHT. Endpoint native-versus-export vertex velocity error reaches 1.28684430801e-6 m/s.

## Finite chords and interior fidelity

Finite chords compare the two one-sided position differences over a nonzero interval. They include acceleration and are distinct from analytic endpoint tangent residuals.

| Interval,s | Joint linear gap,m/s | Joint angular gap,degrees/s | Body gap,m/s | Rifle gap,m/s |
|---|---:|---:|---:|---:|
| 0.01666666667 | 0.4029032498 | 51.34583398 | 0.4167058666 | 0.05968008945 |
| 0.004166666667 | 0.09909595914 | 6.854815506 | 0.1019823897 | 0.004206886275 |
| 0.002083333333 | 0.04947138943 | 2.999709455 | 0.05096436755 | 0.001098308339 |
| 0.001041666667 | 0.02472322973 | 1.414198498 | 0.02548589804 | 0.0003128287699 |
| 1e-06 | 7.334440196e-05 | 0.00496907217 | 7.3853127e-05 | 5.867592668e-06 |
| 1e-07 | 6.867634745e-05 | 0.004869079557 | 6.931938225e-05 | 5.777445146e-06 |
| 1e-08 | 6.855677447e-05 | 0.004864520797 | 6.923198167e-05 | 5.829075304e-06 |

Shrinking-epsilon results converge to the analytic residual, with subtraction noise at the smallest intervals. They evaluate stored float32 data in float64; runtime float32 differentiation is not certified.

Native-curve comparison covers 961 phases: source union knots, quarter/mid/three-quarter probes and both repair boundaries; 721 samples lie in the middle 75%. Maximum all-duration joint-position error is 3.979648999e-07 m, matrix element error 5.257772149e-07, and orientation error 2.452253764e-05 degrees. Body error on common GLB geometry is 3.77209135111e-7 m overall and 3.73069937971e-7 m in the middle 75%; rifle error is 5.96612779357e-8 m. At 540 middle interior points away from source LINEAR corners, joint derivative errors reach 0.0001044438584 m/s and 0.01142457646 degrees/s. Exact algebraic source-polynomial preservation is not claimed.

The sampled minimum quaternion polynomial length is 0.9999966341. A separate numeric root analysis of all 15,920 quaternion intervals evaluates endpoints and every numerically real interior critical point of squared norm: minimum 0.9999966266 at t=0.1188181086 s, maximum 1.000000681. This is numerical polynomial-root evidence, not a symbolic extremum certificate. Every replay normalizes the polynomial before applying its rotation.

## Direct native geometry, attachments and preview metadata

In addition to common-GLB-geometry animation parity, an independent native control check covers every exported vertex at 16 separately Blender-evaluated control poses. Maximum error is 1.017093209e-06 m. This uses the producer’s pinned native capture as reference but performs its own bind-point/weight mapping and binary evaluation. Capture creation itself is not independently rerun by this reviewer.

All 70 joint-map rows, all 210 channel rows, and 17 root/semantic/prop rows resolve to actual serialized data. Rifle node 67 maps to skin slot 67; Head node 1 maps to skin slot 5. Phase-zero advertised world matrices match within 5.551115123e-16. The attachment bridge is W_node×IBM[resolved_slot]×B_bind_mesh, equivalently W_node×J with J=IBM×B. Across 1,243 reference phases and 5,346 native rigid vertices, rifle position error is 3.979094397e-07 m and matrix error 3.9286233e-07. The two correct forms agree within 2.22044604925e-16. Omitting IBM causes matrix error 1.071531966.

The two mapped lens vertices are rigidly Head-weighted. At 16 native evaluated controls, LEFT / RIGHT lens errors are 3.014619083e-07 m/3.287551187e-07 m; midpoint-to-frame-origin error is 2.355138688e-16 m. The goggle midpoint is an optical-surface proxy, not an anatomical eye or camera calibration. The independent control replay covers 16 poses; the producer’s denser optical capture is separately scoped.

All five LEFT/four RIGHT native phase-zero sole-support IDs resolve to correct exported primitive/vertex IDs, exact bind positions and normalized weights. Maximum difference from actual native evaluated vertices is 1.93725078725e-7 m; maximum support-centroid metadata difference is 1.75014472307e-7 m. Native weighted foot subsets 654/668 and GLB seam-split subsets 899/916 are explicitly separate from the source reviewer’s component-defined subsets.

The unbaked preview transform maps its measured rifle heading to engine +X and stated feet origin to zero. Native travel is approximately −1.921261549 m along Blender X at 1.92126154902 m/s; after the preview alignment it becomes (+0.522702288, −0.000000477, +1.848791026) m. Travel and rifle facing are different. The loop is in-place and preserves native RIGHT chest/pelvis counter-rotation. Events/timing agree with actual animation; no gameplay ownership/event changes or runtime acceptance are claimed.

## Source limitations and visual/capture review

The pinned source’s inherited opposing upper-thigh contact remains visible in the component-colored and matched neutral source closeups. Up to 98 cross-leg pairs occur at a sampled phase; five added cross-leg identities reach 0.655282 mm chord and one same-side hip/strap addition reaches 0.050905 mm. Chords and summed intersection length are not penetration depths. Sleeve-root folds, stretched connectors and flattened elbow links remain. Source floor minima are 3.954407 mm LEFT / 2.931984 mm RIGHT, with 17 of 961 RIGHT samples below 3 mm; there is no planted-foot lock or new ground solve. Sampled checks do not establish continuous collision freedom.

Native and explicit cubic-reimport stills at phases 0 and 0.95 were inspected; gross pose and framing agree. The neutral, noisy comparison renders do not establish material appearance, and static images do not prove velocity continuity. The included authoring video is source playback.

The separate producer native-capture report passes 1,243 poses: maximum native-to-GLB vertex difference 4.635512871e-06 m; explicit cubic-aware Blender-reimport difference 4.775054619e-06 m. Stock Blender 4.3.2 drops cubic tangents and is not qualified: its maximum vertex difference is 0.002990688127 m. These capture results and the independent 16-control result are distinct datasets/methods.

## Reproduction and dependencies

Run these portable Python 3 + NumPy checks from the extracted package root. They use only the extracted package and its delivered scripts.

```sh
python tools/replay_cubic_validation.py --glb walk_right_shared_ready_n_c1_loop_a.glb --source-curves SOURCE_CURVES.json --source-analytic-fk SOURCE_ANALYTIC_FK.json --output-dir replay_results
python review/inspect_binary.py --package . --output structure_replayed.json
python review/inspect_curve_contract.py --package . --output curve_contract_replayed.json
```

`inspect_binary.py --static-reference PATH` additionally compares exact shared static arrays with the pinned LEFT GLB. `inspect_curve_contract.py --source-blend PATH` optionally verifies actual separately obtained source bytes; its normal portable run verifies consistency of the packaged source hashes and boundary-key data.

`inspect_native_controls.py` is an audit-workspace script requiring the separately recreated parity_work/source.npz, sample_times.npy and capture_report.json next to the package. Obtain the pinned source checkpoint and run the native capture described in REPRODUCE.md first; the private source NPZ is not included in this ZIP. No native-controls replay is claimed from the ZIP alone.

Full numerical evidence is in binary_unit_validation.json, endpoint_report.json, structure_validation.json, curve_contract_validation.json, native_control_validation.json, sole_mapping_validation.json and preview_metadata_validation.json. This review does not run a full official Khronos validator or certify runtime/GPU integration, animated normals, texture appearance, gameplay, ADS, or other directions.
