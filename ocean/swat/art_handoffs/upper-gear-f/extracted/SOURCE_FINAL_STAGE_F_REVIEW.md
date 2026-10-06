# Stage F final scoped independent review

**Qualified as a separate rigid upper-gear variant with a tested three-second neutral-carry articulation.** The four new pieces pass the measured static N/ADS and bounded 0–4° carry surface checks, remain rigid, and preserve the original human and rifle contracts. Central cap padding is visibly integrated into each single closed shell. Local sleeve/band proximity is measured; peripheral gaps and physical fastening remain explicit limits.

Static: `upper_gear_stage_f.editable.blend`  
SHA-256: `4271f2279bc3cbbcb46594157752b2ac0ca052628c24b4c8629a82c6771a3fc0`  
Movement: `upper_gear_f_movement.editable.blend`  
SHA-256: `7165d1454e0526f01ca85c81f12e0245dfecb22071191918f984477a42c091b1`

The authoritative decision is [INDEPENDENT_STAGE_F_GATE.json](INDEPENDENT_STAGE_F_GATE.json), which pins the inputs and evidence. Original N and R5 source hashes and both F files remain unchanged. Reviewers saved only new QA files.

## Geometry and preserved source

There are exactly four new one-bone pieces: left/right padded cap shells bound to Gear_Elbow_L/R and left/right headset housings bound to Head. No E soft mount survives. Every new mesh is one closed consistently wound manifold, with positive volume and no detected bind/posed self-crossing. Caps each have 386 vertices / 768 triangles; headsets each have 320 vertices / 636 triangles.

Each cap is one connected shell with paired outer/inner height-field surfaces. All 193 corresponding point pairs have positive thickness, minimum 3.50 mm. The contoured central backing is part of that rigid shell, so the failed mixed-skin sidewall mechanism is absent. Maximum thickness is 12.80 mm left / 20.80 mm right.

The original 70 rest matrices, parents and lengths are exact. Two explicit anatomical carriers make a 72-bone derivative requiring their baked animation channels. All 22,645 retained native bind vertices, skin weights, source polygons, UVs, material assignments and smooth flags remain exact; exactly the declared 27 legacy gear components / 2,205 vertices were removed. Original rifle and other original mesh geometry, weights, UVs and transforms are preserved. Custom corner normals are near-exact after mesh reconstruction, with maximum 0.034677° difference rather than bit-exact identity.

## Actual surface and movement checks

Both stored static actions, `Neutral Carry / Anatomical Gear F` and `ADS Intent / Anatomical Gear F`, have zero confirmed new-gear/preserved-surface, new-gear/rifle, new-part pair or new-part self-crossings. Original rifle versus retained nonhand wearer/gear surfaces is also clear in both poses. Dense wearer probes find no confirmed containment in the closed new pieces.

The movement action `Upper Gear F / Three-second Carry Articulation` was evaluated at 181 states / 60 Hz over three seconds. All states have zero measured new-piece/body/rifle/pair crossings and zero rifle/nonhand-body crossings. Five cardinal cycle states were additionally sampled at 1 mm triangle spacing, giving 167,958 wearer-probe evaluations and zero confirmed inside-new-part witnesses. Rifle vertex/centroid containment screens against the new pieces are also clear.

Movement geometry, weights, UVs, custom normals and object transforms are exact to static F. All pieces retain zero normal reversals. Maximum rigid-position numerical discrepancy is 0.000307 mm; triangle area ratios remain 0.999701–1.000412. These are numerical tolerances on rigid transport, not evidence of material compression.

## Central padding and mounting proximity

Independent 0.5 mm surface grids measure central underside distances to the actual sleeves. The central region covers about 23%–24% of underface area; it must not represent the entire fit.

| Pose / side | Central minimum / median / maximum mm |
|---|---:|
| Neutral left | 1.537 / 2.195 / 2.638 |
| Neutral right | 0.914 / 1.640 / 3.173 |
| ADS left | 1.711 / 4.246 / 6.888 |
| ADS right | 0.527 / 2.122 / 3.841 |
| Carry peak left | 1.029 / 1.664 / 1.897 |
| Carry peak right | 0.679 / 1.409 / 3.018 |

Across all 181 poses, central vertex/centroid median distances stay 1.668–2.211 mm left and 1.413–1.646 mm right. Dense peak sampling is separate and finds closer points than that coarser temporal set; neither is an exact continuous global minimum.

The outer rim remains visibly spaced: whole-underface static maxima reach 14.846–18.621 mm. ADS left is the least snug central fit. Inward headset stems are locally near the retained overhead band, about 0.231 mm minimum / 1.49 mm median, but parts of that face remain up to 7.61 mm away. Contact, fastening, load transfer and a uniformly flush underside are not established.

Actual source-material and isolated side/back pixels support a continuous integrated padded-shell interpretation. The peripheral lip visibly overhangs with daylight. The former E splayed sidewalls are absent. See [padding and stem review](attachments/F_PADDING_PROXIMITY_REVIEW.md), [neutral right pixels](attachments/N_right_PIXEL_REVIEW.png), [ADS left pixels](attachments/ADS_left_PIXEL_REVIEW.png) and [carry-peak pixels](attachments/MOVEMENT_PEAK_PIXEL_REVIEW.png).

## Joint, grip and bounded motion contract

The independent joint audit passes all 16 contract checks across 181 poses. Both hand frames remain fixed in the original rifle frame within 0.000493 mm / 0.0000276°, and all 40 finger local bases remain exact. Anatomical carriers match reconstructed joint frames within 0.000256 mm / 0.0000454°. No reflected, collapsed or materially stretched bones occur.

Actual rifle raise is 4.000005° and clavicle arcs are at most 1.500006°, peaking at 1.5 seconds before returning. Inherited wrist bends remain approximately 36.08–36.66° left / 31.35–31.80° right; this does not demonstrate neutral wrists. Native upper-arm bone-tail offsets are preserved, not newly introduced disconnections.

Endpoints close within 0.000333 mm / 0.000329°. Linear keys have small measured seam-velocity differences, so exact C1 smoothness and broad animation naturalness are not claimed. This gate covers the stated upward 0–4° path only. It does not extend the rejected C downward or D 6° motion ranges.

## Scope of the qualification

The result is a reviewable geometry variant and modest neutral articulation proof. It is not whole-character self-collision clearance, a universal open-shell solid-volume proof, arbitrary native-gear motion approval, engine export/runtime acceptance or gameplay approval. The discrete surface and proximity samples are not continuous collision detection.

No R5 support bolster is integrated or approved. The stored ADS action is diagnostic and does not establish shared shoulder support. The source has no anatomical eyes; preserved lens landmarks remain proxies, with no eye-relief or camera ADS calibration.

Main evidence: [static geometry](STAGE_F_INDEPENDENT.json), [static rifle/wearer](STATIC_RIFLE_WEARER.json), [complete movement surfaces](movement_surfaces/MOVEMENT_SURFACES.json), [joint review](movement_joints/JOINT_REVIEW.md), [attachment measurements](attachments/F_PADDING_PROXIMITY_REVIEW.md).
