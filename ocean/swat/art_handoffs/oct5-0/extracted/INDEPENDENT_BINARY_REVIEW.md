# Independent WALK LEFT cubic fixture review

**Numerically consistent with the saved Walk Left action, with the measured residuals and source limitations below.** This review independently reads the actual GLB bytes. It does not change the source, writer, GLB, or engine, and it does not establish runtime, gameplay, ADS, or another-direction approval.

## Identity and method

- Saved action: `Walk Left / Shared Ready N C1 Loop A`, frames 0–30 at 30 fps, one second
- Actual editable source SHA256: `6737a18666f08d89ea097ed304e17ed47095ffb28f87f5324d32b34e416bd173`, checked directly from its bytes
- GLB SHA256: `4fe97ec1997bb4799440216ddc3a74d858154c0d8e51bf863e2d86082b1a1996`
- Native FBX: `walk left.fbx.bin`, SHA256 `c66c409acea597462d088a26a2a388127aa8c03238b49c02117264f5504616ba`, 60 Hz, frames 1–61

The unchanged standalone `replay_unit.py` from the earlier independent forward-cubic audit decodes JSON/BIN chunks and typed, strided accessors. It implements float64 Hermite interpolation with per-second tangents; normalized-quaternion derivatives; scale-aware TRS and hierarchy product rules; and polar-rotation angular derivatives. It imports no conversion code or writer-side arrays. Saved Bezier time is independently inverted instead of assumed perfectly affine. `inspect_binary.py` extends this independent decoder to validate structure, metadata, attachments, and optical controls. These measurements are numerical evaluation, not exact symbolic certification.

All 459 accessors and their buffer views have valid finite data, bounds, component alignment, types, and strides. The hierarchy is acyclic with unique parent assignments. All 210 unique CUBICSPLINE node/path channels cover translation, quaternion rotation, and scale for all 70 joints, begin at exactly 0 seconds, and end at exactly 1 second. All sampler outputs have the required three values per key. Quaternion key-norm residual is at most 4.4842623e-8.

Nodes, skins, meshes, materials, scenes, and all 39 geometry/inverse-bind accessor arrays exactly match the independently reviewed sealed forward-cubic fixture, SHA256 `82dd05c813a017b23ee205c2cb43a98edc56b062f4dd567741099d3e6a040d69`. This is a static-data comparison, not substitution of the forward animation.

## Actual endpoint derivatives

All 70 joint endpoint world matrices are exactly equal in this evaluator. Every one of the 48,755 exported vertex endpoint positions is exactly equal. The GLB retains the saved source's small nonzero analytic seam; it does not produce a literally zero derivative gap.

| Measurement | Saved native reference | Actual GLB |
|---|---:|---:|
| Maximum joint linear velocity gap, LeftToeBase (m/s) | 6.85669556e-5 | 6.86182060e-5 |
| Maximum joint angular velocity gap, LeftLeg (degrees/s) | 0.00523240969 | 0.00523483598 |
| Maximum endpoint world-matrix pose gap | Equal source endpoints | 0 exactly |

The maximum endpoint source-to-file matrix-component difference is 4.86501418e-7. Against both independently reconstructed source curves and the pre-existing analytic source report, maximum one-sided linear-velocity error is 1.38610930e-6 m/s and angular-velocity error is 7.69372292e-5 degrees/s. Source and GLB seam residuals are separate values and must not be presented as identical or as hard thresholds that the GLB numerically undercuts.

`Prop_Magazine_B` remains in matrix, position, and skin tests. Every stored scale key and tangent is exactly zero. Its world linear transform and derivative are exactly zero at all 961 tested phases. Its angular velocity is undefined because the prop is collapsed, so it is excluded only from angular quantities.

## Complete skin and finite-chord coverage

All stored JOINTS_n/WEIGHTS_n sets are used, normalized across the complete influence list. The maximum original weight-sum residual is 1.32713467e-7. Both exported body vertices using seven nonzero influences remain present. The six primitives span five exported mesh nodes; UV/material seam splitting means exported body vertex counts differ from the native body count.

| Primitive | Exported vertices | Maximum analytic vertex velocity seam (m/s) |
|---|---:|---:|
| Spare magazine sleeve | 144 | 1.03904505e-5 |
| Full body, primitive 0 | 7,638 | 5.66208192e-5 |
| Full body, primitive 1 | 24,049 | 7.02193417e-5 |
| Rifle | 14,936 | 8.32531819e-6 |
| Installed/removed magazine A | 994 | 6.17487647e-6 |
| Fresh/collapsed magazine B | 994 | 0 |

Foot subsets use cumulative weight greater than 0.5 on that side's Foot, ToeBase, and Toe_End joints. They include 899 left and 916 right exported vertices across both body primitives. Their maximum analytic seams are 7.02193417e-5 m/s left and 1.83473463e-5 m/s right. The body seam produced by independent native transforms on the same GLB geometry and inverse binds is 7.01681081e-5 m/s; maximum one-sided exported-versus-source vertex velocity error is 1.47317580e-6 m/s. This comparison isolates animation error. Direct Blender geometry parity is a separate native-capture check.

Finite chords compare `(P(e)-P(0))/e` with `(P(1)-P(1-e))/e`. They include interval curvature and are not analytic C1 seam measurements.

| Interval | Joint linear chord gap (m/s) | Joint angular chord gap (degrees/s) | Body chord gap (m/s) | Rifle chord gap (m/s) |
|---|---:|---:|---:|---:|
| 1/60 s | 0.429874011 | 82.1476783 | 0.459384381 | 0.115741586 |
| 1/240 s | 0.108628953 | 10.5737990 | 0.110646169 | 0.00795774001 |
| 1/480 s | 0.0544960073 | 4.59025647 | 0.0558010478 | 0.00189380485 |
| 1/960 s | 0.0272984283 | 2.16642169 | 0.0280435194 | 0.000448798819 |
| 1e-6 s | 7.40990868e-5 | 0.00521544431 | 7.58016936e-5 | 8.20549555e-6 |
| 1e-7 s | 6.87356627e-5 | 0.00522397851 | 7.03493529e-5 | 8.31278101e-6 |
| 1e-8 s | 6.86305298e-5 | 0.00523772618 | 7.02356576e-5 | 8.33144528e-6 |

The shrinking intervals converge toward the analytic residual, with subtraction noise at the smallest intervals. At 1e-8 s the maximum individual joint linear derivative error is 1.92984018e-7 m/s. Practical-rate chord gaps are larger because they include acceleration; they must not be relabeled as tangent mismatch.

## Interior fidelity

The source comparison covers 961 phases: the source union knots, quarter/mid/three-quarter points between them, and both repair boundaries. The middle 75% contains 721 checked phases. These are sampled maxima, not certified continuous maxima.

- Full-duration maximum joint-position error: 4.12468547e-7 m
- Full-duration maximum world-matrix component error: 5.46886310e-7
- Full-duration maximum world-orientation error: 3.16512334e-5 degrees
- Full-duration maximum body vertex error: 4.19048402e-7 m
- Middle-75% maximum body vertex error: 3.87101690e-7 m
- Maximum rifle vertex error: 4.93193174e-8 m
- Minimum sampled pre-normalization quaternion length: 0.99998656224

At 540 interior probes away from native LINEAR corners, middle-75% derivative errors reach 1.08107577e-4 m/s and 0.0191842001 degrees/s. Unit-key normalization and float32 curve/time storage introduce measurable perturbations. Physical pose differences remain submicrometer and well below a millidegree at the probed phases, but exact unchanged central curves are not claimed for the export.

## Binding, optical, and direction checks

Every row in the 70-joint map and every one of the 210 animation-channel rows resolves to the actual node, parent, joint slot, inverse bind, default transform, and sampler. The 17 root/semantic/prop rows also resolve correctly. Phase-zero advertised node-world matrices agree within 7.77156117e-16.

The rifle bridge uses `W_node * IBM[resolved_joint_slot] * B_bind_mesh`, equivalently `W_node * J_joint_local`. Across 1,243 independent native-reference phases and all 5,346 native rigid reference vertices, maximum matrix-component error is 5.01097751e-7 and maximum vertex-position error is 5.63349694e-7 m. The two correct forms agree within 2.22044605e-16. Omitting the inverse bind produces a matrix-component error of 1.07048051, so the correction is material.

The two actual exported goggle lens vertices resolve to their recorded primitives and are rigidly head-weighted. At the 16 included independent native control poses, maximum lens errors are 4.98006111e-7 m left and 4.18383216e-7 m right. Their midpoint agrees with the optical-frame origin within 2.54384052e-16 m. The proxy is a goggle-surface measurement, not an anatomical eye or camera calibration. This independent optical replay covers the 16 controls; any denser native-capture claim belongs to its separately recorded capture.

The measured preview transform is a fixed, unbaked candidate that maps measured phase-zero rifle facing to engine +X and its stated feet origin to zero. Original native travel remains approximately +1.921261907 m along Blender X, at 1.92126190665 m/s. The same travel transformed by the preview heading becomes approximately (-0.216701162, 0.000000298, -1.909001813) m. Travel and rifle facing are therefore demonstrably different. Native chest/pelvis counter-rotation and its asymmetric stance must not be replaced with a direction inferred from the `walk left` label. The exported loop is in place; native travel is provenance, not a new animated translation or selected gameplay speed.

Sole support metadata explicitly names its native Blender vertex namespace and separately resolves exported primitive/vertex pairs. All 18 left and four right phase-zero support mappings were independently checked against the captured native body's bind positions, normalized complete weights, and Blender-evaluated vertices. Bind positions and normalized weights match exactly. Maximum actual phase-zero source difference is 1.94247445e-7 m; support-centroid metadata agrees within 1.56592448e-7 m. Native foot subsets contain 654/668 vertices, while GLB seam-split subsets contain 899/916; these index namespaces must remain distinct.

## Source and review limits

The pinned native review retains sleeve/shoulder-root folds and intersections, connecting-sleeve stretch, and the flattened elbow link. At 960 Hz the saved source's minimum sole clearances are approximately 3.96044 mm left and 2.87575 mm right; the original 4 mm target is not maintained through the repair. Eight newly encountered triangle-pair identities remain in inherited hip/pants/strap families. Crossing chord is not penetration depth. This export audit neither removes these limitations nor supplies a new continuous collision proof.

The matched native and cubic-aware reimport stills at phases 0 and 0.95 were visually inspected; framing and gross pose agree. Their neutral, noisy render is suitable for comparison, not material or photorealistic approval. The independent numerical replay does not rely on these pixels or on stock Blender importing the tangents correctly. Runtime full-body consumption, material appearance, speed/phase mapping, interactions, eye alignment, ADS, and WALK RIGHT are outside this review.

The separately supplied `validation.json` was reviewed for consistent scope: its direct Blender native-to-GLB maximum vertex difference is 4.67482500e-6 m and its explicit cubic-aware reimport maximum is 4.57590701e-6 m. Those native geometry/capture comparisons differ from this replay's animation-only comparison on common GLB geometry. The unmodified Blender 4.3.2 import is separately recorded as not preserving cubic tangents, with maximum vertex difference 0.00303127035 m. This failure is disclosed and is not included under the qualified cubic-aware result.

## Reproduction

Run with Python 3 and NumPy:

```sh
python review/replay_unit.py --glb walk_left_shared_ready_n_c1_loop_a.glb --source-curves SOURCE_CURVES.json --source-analytic-fk SOURCE_ANALYTIC_FK.json --output-dir replay_results
python review/inspect_binary.py --package . --output structure_validation.json
```

The optional `--static-reference` argument to `inspect_binary.py` additionally verifies exact static data against the pinned forward-cubic GLB. `binary_unit_validation.json`, `endpoint_report.json`, and `structure_validation.json` contain the full per-bone, per-primitive, byte-layout, derivative, and attachment evidence.
