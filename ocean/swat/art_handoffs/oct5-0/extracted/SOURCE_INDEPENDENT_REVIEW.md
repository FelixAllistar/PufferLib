# Walk left: independent saved-action review

**Qualified with the source material limitations below.** Scope is WALK LEFT only. The final saved action is `Walk Left / Shared Ready N C1 Loop A` in `../seam_a/walk_left_ready_n_c1.editable.blend`, SHA256 `6737a18666f08d89ea097ed304e17ed47095ffb28f87f5324d32b34e416bd173`. No source or pose files were edited during review. Right and other directions were not reviewed or changed.

## Source identity and timing

The native FBX is `walk left.fbx.bin`, SHA256 `c66c409acea597462d088a26a2a388127aa8c03238b49c02117264f5504616ba`: 60 Hz, frames 1–61, one second, +X root displacement 1.921261907 m. The saved action is frames 0–30 at 30 fps, with the native body key spacing retained in the carry baseline and the carry sampled at 240 Hz. No retime was introduced.

The original FBX has meaningful pelvis/chest counter-rotation. At its start, horizontal chest-material forward is (-0.269744, -0.962932, 0), and pelvis-material forward is (-0.744358, -0.667781, 0). Their angles to the +X stride vary over the source cycle: chest 100.721–118.013°, pelvis 127.972–159.734°. The asymmetric shoulder line is a biased heading diagnostic. The source's native step arrangement and torso motion were retained, rather than inferred from the clip label.

The carry baseline retains all 180 original body, leg, gaze and unused-prop curves exactly. The final repair alters the two bounded seam windows only. All original interior center key values and interpolation modes are exact. All six saved mesh objects have identical geometry, UVs and complete weights to sealed Shared Ready N. The full body retains 24,850 vertices, maximum seven nonzero skin influences, and both vertices that use seven influences. The 70-bone bind hierarchy is identical; Prop_Magazine_B retains zero scale.

## Saved wrap and repair windows

Repair windows are 0–0.125 s and 0.875–1 s. The center preserves all original keys exactly; splitting boundary segments produces at most 1.192093e-7 scalar evaluation roundoff and 4.768372e-7 FK matrix component roundoff. Full body vertices are identical at seven checked center phases. This is preservation within numerical evaluation precision, not a claim of bit-identical evaluations at every subframe.

Baseline worst analytic wrap velocity gaps were 1.291812 m/s and 760.242744°/s. Saved A reduces these to 0.000068567 m/s and 0.00523241°/s. Scalar endpoint handle slope gap is at most 4.29160e-5 per second. At the repair-window joins, actual stored scalar slope gap is at most 0.000114441 per second. Normalized quaternion FK reconstructs the evaluated endpoint matrices within 4.729e-7. Float32 handle storage leaves small measured residuals; literal exact mathematical C1 is not claimed.

Evaluated C0 is exactly zero for all bone positions/rotations and for the complete actual foot and rifle surfaces. The collapsed unused prop has undefined angular velocity, which is expected and is reported separately.

## Actual arms, gloves, rifle and stock

Across 111 full-cycle and dense boundary phases, actual arm-dominant triangles versus vest have zero crossings on both sides, rifle versus nonhand body has zero, and connector self-crossings have zero. Whole connected sleeve/shoulder-root components retain 50 left and 63 right crossing pairs; their union is identical to the carry baseline. These include native garment folds and must not be described as a completely intersection-free character.

Stock-center gap is 2.191580–2.192072 mm. The same 24 central patch samples remain within 5 mm. At 960 Hz, complete evaluated glove components, rifle and installed magazine retain the sealed N contact geometry in rifle coordinates within 0.002556 mm (left glove), 0.001682 mm (right glove), and 0.000263 mm (rifle). Existing connecting-sleeve panel stretching, root folds and the thin/flattened elbow link remain visible in closeups.

## Floor and lower-body material

At 961 samples / 960 Hz, actual left sole minimum is 3.960442 mm; right minimum is 2.875747 mm at frame 28.1875 (0.939583 s). No sampled sole is below z=0. Right sole is below 3 mm at 23 sampled phases, so the original 4 mm target is not maintained throughout the repair window. Maximum actual foot-surface departures from baseline are 13.912446 mm left at frame 1.3125 and 3.315586 mm right at frame 1.5625. These are bounded pose/path changes, not a speed or timing change.

Matched actual surface checks cover both repair windows at 240 Hz plus three center phases (65 phases per clip). Left-leg versus right-leg finite triangle crossings remain zero. Projected front-view overlap is therefore not itself evidence of interlimb penetration.

Eight newly observed triangle pair identities are confined to existing left hip/pants/strap material families:

- One within component 5118, the continuous waist/crotch/pants skin: new intersection chord 1.429517 mm; family maximum 27.520212 → 28.168948 mm
- Six between pants 5118 and upper-thigh strap/garment component 11165: new maximum chord 1.047192 mm; family maximum 28.405435 → 28.204496 mm
- One between pants 5118 and small strap/buckle junction 7464: new chord 0.081884 mm; family maximum 10.054354 → 10.018853 mm

Each newly observed pair has zero intersection chord at its matched baseline phase. Neutral and component-colored fixed-camera views locate these changes within inherited belt/pants/brace folding, without a newly identified thigh/knee/boot or limb/torso failure. Chord is intersection length, never penetration depth. The retained lower folds are explicitly qualified, not hidden by a broad overlap count.

## Evidence and limits

Use `ARCHIVE_FILES.json` as the compact checkpoint allowlist. It contains source identity, scalar and analytic FK, floor, grip and compact phase-contact reports, reproducible audit scripts, four diagnostic PNGs and two matched sheets. Repetitive raw pair dumps, logs and render trees remain local outside that allowlist. `selected_pixel_evidence.json` describes and hashes the selected images.

These are saved Blender-action checks. Finite sampling and triangle crossings are not continuous analytic collision or watertight-cloth penetration proofs. This review makes no ADS, engine/runtime, resampled-export, or other-direction claim.
