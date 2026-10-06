# Frozen support grip C1 diagnostic

Final thumb/finger tuning has moved to the actual in-game ADS view at the user's request. Preserve the current engine pose: neither C nor C1 is automatically promoted. The requested thumb wrap remains unresolved here.

C1 retains C's modest improvement in support-palm seating, while releasing only the index-base flexion that sharpened one native glove crease. The fingers still have an arched silhouette, proximal palm support remains incomplete, and native material folds remain. This is a qualified static source option, not an automatic replacement bank or an ADS/motion approval.

Open `../candidate_c1/support_grip_index_release_c1.editable.blend`, action `Support Grip / Index Base Release Candidate C1`, frame 0. `support-grip-c1-matched-comparison.jpg` compares the original F/N hold with C1 through identical side, underside, palm/wrist and uncalibrated near-visor views.

## What changed

Relative to C, exactly four local quaternion channels on `mixamorig:LeftHandIndex1` change. Its added native-local flexion is reduced from +4.45114295° to zero; the +0.42138296° spread remains. All 716 other curves, including index-child local channels, remain exact. The child bones follow their unchanged local poses. C's hand, arm, elbow carrier, other digits, rifle and right grip remain fixed. There are no geometry, weight, rest, UV or material edits.

The existing palmar index-base triangle [8513,8515,8516] returns from C's 2.560 mm² to 6.124 mm², 99.2% of original F's 6.171 mm². Its narrow altitude returns from 0.358 to 0.854 mm (F 0.859 mm), with a positive transported normal. No glove face falls below 10% of rest area in C1. This targeted restoration does not remove the original ring-finger folds or certify all material behavior.

Central 50–75% palm median gap remains improved at 5.548 mm (F 10.56), with 75.58 mm² opposed contact within 2 mm. The proximal-central 25–50% band remains 18.30 mm away. The released index pad sits roughly 2 mm clear: coarse samples give 2.004 mm, while an exact finite-triangle distance check gives 1.907 mm. The coarse zero-area ≤2 mm patch is sampling-dependent, not an exact absence of close surface area; approximately 36.2 mm² is sampled within 3 mm. The actual support is shared by the partial palm, thumb and other digits.

Maximum sampled parity-confirmed glove overlap remains 0.825 mm versus F 0.538 mm, with no samples above 1 mm. The mixed palm overlap locally rises from C's 0.501 to 0.769 mm. No new nonlocal glove or elbow-cap/arm crossings are introduced. These finite surface measurements do not certify zero penetration, a global maximum, or force support.

The final independent report and exact scope checks in `../candidate_c1_review/` take precedence over isolated numeric summaries. Native wrist/hinge/dorsal relationships are unchanged from C. No dynamic transition, R5 transplant, engine camera, eye relief, gameplay or ADS fit was tested here.

## Exact application data

`../candidate_c1/EXACT_LEFT_GRIP_HANDOFF.json` contains all desired 24 left-chain local pose records and 240 exact scalar curves relative to frozen F, plus the separate four-channel C→C1 replacement. It explicitly distinguishes native Rifle 7 mesh coordinates from the calibrated stock-origin frame (+X forward, +Y up, +Z right). Matrices use row-major arrays acting on column vectors; positions are metres; quaternions use w,x,y,z.

The unchanged C1 physical wrist position is (0.494427394, −0.027409224, −0.053811514) m. Apply the complete matrix and finger channels, not this rounded point alone. The physical rifle transform for a resolved exported skin remains `W_node × inverse_bind[skin slot] × B_bind_mesh`. This package supplies source pose data and no new GLB.

The 72-bone F gear contract remains, including the two anatomical elbow carriers. F, N, C, R5 and all banks remain separately recoverable. C1 has no hidden carrier offset or physical rifle scaling.

## Recovery

The checkpoint includes packed C1, packed C dependency, frozen F comparison source, an action-only library, exact channel deltas and handoff, authoring/review scripts, calibrated rifle/optical/anatomy references, final selected views and complete numeric evidence. Full-resolution render caches omitted for size are reproducible from source and included camera definitions; selected review JPEGs and final panels are included.

Run Blender 4.3.2 with `--background --python .../candidate_c1_checkpoint/replay_saved_index_delta.py`. It copies the frozen C action and patches four saved quaternion values, compares all 720 serialized curves, every pose matrix and evaluated mesh against C1, then writes only `replay_output/`. Exact replay returned zero pose-matrix and evaluated-vertex error. Rebuilt Blender file bytes need not be identical.

`replay_surface_review.py` relocates the archived read-only gates into a separate `replay_audit/` output folder. The archived combined visual judgement remains authoritative. All shipped payloads are pinned in `MANIFEST.json`; remote backup verification is recorded separately without changing the sealed archive.
