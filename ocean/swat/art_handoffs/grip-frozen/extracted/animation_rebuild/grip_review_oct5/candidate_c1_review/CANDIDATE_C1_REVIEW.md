# C1 review: frozen diagnostic and recovery handoff only

**C1 is frozen as a diagnostic and recovery/handoff checkpoint only. Do not promote it to the engine or replace the current engine hand pose.** The user stopped further authored grip tuning and moved final thumb/finger fitting to the actual in-game ADS view. The requested thumb-wrap intent remains unresolved.

The bounded release does restore the palmar index-base crease almost to the original posed F/A area and appearance while preserving C's partial central-distal palm benefit, hand/arm/gear frames, and native data. The index relaxes slightly away from the rifle, leaving roughly **2 mm clearance** and much lighter index-pad proximity. These measurements do not establish approval of the whole grip.

This is **not automatic whole-grip acceptance or baseline replacement**. The cup still looks only modestly more supported than F, fingertips remain visibly arched, proximal palm seating remains incomplete, maximum sampled overlap is still **0.825 mm versus F's 0.538 mm**, and the existing native Ring-fold indicators remain. No engine ADS/camera, shoulder, transition, or motion qualification follows.

## Exact inputs and authorized change

| Input | SHA-256 |
|---|---|
| F/A original reference | `4271f2279bc3cbbcb46594157752b2ac0ca052628c24b4c8629a82c6771a3fc0` |
| Sealed C | `f1b39d78b32da500f9fd4370d942d364c49a1d9d788ac1122282b4275e371dcb` |
| C1 | `17c1c13ffbea9d8fac459914eb2bbb1c6c336b785f9382450002f0ad6a53ac99` |

C1 source: `../candidate_c1/support_grip_index_release_c1.editable.blend`; saved action **Support Grip / Index Base Release Candidate C1**, frame 0.

The independent C→C1 action comparison confirms exactly the four quaternion-value channels for **mixamorig:LeftHandIndex1** changed. No other action channel or local bone basis changed. The maximum global-pose difference outside the index chain is **0.0**. In particular, LeftArm, LeftForeArm, LeftHand, Gear_Elbow_L, Prop_Rifle, and RightHand matrices are exactly unchanged.

All 480 left-glove vertices with zero index-chain influence are exactly unchanged. The largest change among index-influenced glove vertices is 6.327 mm; mixed index/palm skin may therefore move even though the hand frame itself does not. Full mesh/rest/topology/weight/UV and rig-rest hashes match F. Evaluated rifle and right glove match F exactly. All three pinned input hashes remain unchanged, and no sealed C report or original C image was written by this review.

## The actual crease correction

Target native triangle **[8513,8515,8516]**, dominated by **LeftHandIndex1**, at the palmar index-base crease:

| Measurement | F/A pose | Sealed C | C1 |
|---|---:|---:|---:|
| Actual face area | 6.171 mm² | 2.560 mm² | **6.124 mm²** |
| Area/rest | 16.09% | 6.67% | **15.96%** |
| Altitude to longest edge | 0.859 mm | 0.358 mm | **0.854 mm** |
| Longest edge | 14.370 mm | 14.320 mm | 14.350 mm |
| Geometric normal vs transported-rest alignment | +0.977 | +0.939 | +0.982 |

Rest area is 38.362 mm². “Restored” here means restored near **F's original posed crease**, not expanded to the unposed rest area. C1 is within about 0.8% of F's posed area. No glove face remains below 10% rest area; no new negative-normal or self-intersection witness appears.

I reused the exact sealed-C close camera, target, scale, shading settings, and 900×900 resolution. In the unmarked C1 oblique view, C's narrowed pointed transition becomes broader and smoother, close to F's appearance. This is a visible localized improvement, rather than an approval based only on the changed angle.

- [C1 crease, exact oblique camera](crease_views/C1_oblique.png)
- [C1 crease with separate measurement outline](crease_views/C1_oblique_marked.png)
- Original comparison pixels remain in `../candidate_c_review/crease_views/C_oblique.png` and `A_oblique.png`

The crease images are explicitly skin-only diagnostics: rifle/gear were hidden in rendering so they could not obscure the patch. No source mesh, material, pose, or saved scene was edited. The orange outline is a separate observational curve along the real triangle edges.

## Palm benefit is retained

The same evaluated material bands and finer palm sampling were applied against frozen F:

| Left palm band | F/A median gap | C median gap | C1 median gap | C1 opposed area within 2 mm |
|---|---:|---:|---:|---:|
| Proximal 0–25% | 43.45 mm | 40.02 mm | 40.02 mm | 0 |
| Central 25–50% | 23.15 mm | 18.30 mm | 18.30 mm | 0 |
| Central 50–75% | 10.56 mm | 5.55 mm | **5.548 mm** | **75.58 mm²** |
| Distal 75–100% | 4.54 mm | 3.16 mm | 3.13 mm | 410.05 mm² |
| Beyond 100% / web transition | 8.07 mm | 10.34 mm | 10.28 mm | 0.87 mm² |

The finer estimated total opposed palm area within 2 mm is **486.50 mm²** in C1, versus C 453.52 and F 245.76 mm². The central 50–75% patch is 75.58 mm² versus C 62.01 and F 0. This preserves the useful partial seating change. It does not establish full proximal-palm seating, and mixed palm skin now has a somewhat larger shallow overlap described below.

## Index contact tradeoff, with a corrected sampling limit

Releasing Index1 makes the index visibly a little more relaxed while the overall cup remains close to C. It eliminates the index/rifle finite triangle crossings. It also gives up C's broad close index-pad patch.

The original coarse surface sampling reported **2.00384 mm** minimum index gap and **zero sampled opposed area within 2 mm**; it found about **36.2 mm² within 3 mm**. Because this lies directly on the threshold, I checked the finite native triangle geometry rather than treating coarse sampling as an exact boundary.

The targeted float64 finite-triangle distance calculation finds a closest gap of **1.90664 mm** between native Index2 triangle **[8107,8090,8108]** and Rifle 7 triangle **8490**, with **zero exact finite index/rifle crossing pairs**. It tests vertex/triangle and all edge/edge distances, culling only pairs whose bounding-box lower bound cannot improve the result.

Therefore the correct description is **roughly 2 mm index clearance with much lighter near-contact**, not a universal absence of skin within 2 mm. The coarse zero-area result is retained as a sampled estimate, not promoted to an exact zero-area proof. No additional fine area integration was undertaken after the parent requested final disposition.

## Combined geometry and material gate

| Result | F/A | C | C1 |
|---|---:|---:|---:|
| Maximum sampled left rifle-in-glove signed depth | 0.538 mm | 0.825 mm | **0.825 mm** |
| Samples deeper than 1 mm | 0 | 0 | 0 |
| Palm-region maximum sampled depth | 0.538 mm | 0.501 mm | **0.769 mm** |
| All finite rifle/glove triangle-crossing pairs | 54 | 103 | 103 |
| Index/rifle finite crossing pairs | — | Present | **0** |
| Nonlocal glove self-intersection pairs | 0 | 0 | 0 |
| All finite glove self pairs | 2 | 2 | 2, same native pairs |
| New negative-normal faces versus F | — | 0 | 0 |
| Glove faces below 10% rest area | 0 | 1 | **0** |
| Left/right elbow-cap/arm crossings | 0 / 0 | 0 / 0 | 0 / 0 |

The total 103 rifle/glove crossing pairs do not mean the same local contacts persisted unchanged: index crossings disappear, while mixed palm skin changes. The palm's signed-depth increase **0.501→0.769 mm** must not be hidden by the unchanged global maximum. The unchanged maximum is at the thumb.

Three pre-existing Ring2 negative-normal transport indicators remain. C1 does not repair those native folds, nor does this review certify custom corner normals. Global hand skin-Jacobian conditioning remains the same as C (minimum normalized singular value approximately 0.785; all determinants positive). The bounded correction removes C's added severe index-base compression without making the whole native mesh pristine.

Glove/cuff seam crossings remain the same 123 C pairs, a subset of F's 125, with no new native pair. Whole arm/body seam-layer crossings remain the same 148 baseline pairs. No new dominant arm-skin/body, rifle/nonhand-arm, headset, magazine, or rigid-gear collision was found.

## Both-arm anatomical and cuff review

Hand/arm/gear world matrices are exactly C's. The skin-landmark check independently confirms near-native hinge/dorsal relations:

| Descriptor | C1 left | Right, unchanged |
|---|---:|---:|
| Wrist axis bend | 35.508° | 31.355° |
| Elbow axis angle | 34.044° | 109.379° |
| Native hinge residual | +0.000065° | +0.000033° |
| Dorsal material residual | −0.009833° | −0.002721° |

The left dorsal value changes slightly from C's −0.007377° because the real dorsal skin landmark includes small index influence; it is not evidence that the hand/forearm frame changed. These are native material-frame descriptors, not clinical limits.

Matched `left_inner`, `left_outer`, `left_below`, and `near_visor_inspection` views were inspected. Their camera parameters exactly match F's, and the targeted crease camera exactly matches sealed C's. The wrist/cuff and partial supporting cup retain C's appearance. Fingers remain arched; the index is subtly more relaxed, with no new gross separation failure or inter-digit crossing. The near-visor view remains explicitly uncalibrated.

## Disposition

**Freeze C1 as diagnostic/recovery evidence only. Preserve the current engine hand pose.** The local index-base release achieved its narrow crease objective, but the user-requested thumb-wrap intent remains unresolved. Final thumb/finger fitting belongs in the actual in-game ADS view; no further authored grip tuning, sampling, or promotion is authorized by this result.

Keep F/A and the sealed checkpoints available for recovery. This checkpoint is not a bank replacement, full-palm/ADS approval, or motion qualification. The next authoring goal communicated by the parent is backward walk, not another grip iteration.

No additional pose, derivative, or parameter iteration was authored by this review. The result is limited to the supplied immutable C1.

## Evidence

- `sealed_C_channel_and_skin_comparison.json`: exact channels, local/global frames, weighted-skin changes, target-face restoration
- `C1_static_skin_grip_audit.json`, `C1_palm_support_distribution.json`: actual native contacts, overlap, skin axes and central support
- `index_surface_gap_exact.json`: finite-triangle closest gap and exact index/rifle crossing check
- `material_clearance_comparison.json`, `anatomy_material_comparison.json`: whole gate against frozen F
- `invariants_comparison.json`, `CAMERA_PARAMETER_VERIFICATION.json`: preservation and matched views
- `crease_views/CREASE_VIEW_IDENTITY.json`: original sealed-C camera identity and diagnostic limits
- `FINAL_DISPOSITION.json`, `GATE_PROGRESS.json`, `full_gate.log`: final qualified status and completed stages
