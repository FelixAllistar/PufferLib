# Backward WALK source diagnosis

The actual source is a 1.000-second translating backward gait at 60 Hz, with 1.921261 m backward travel per cycle. Preserve its native oblique torso and gaze, support timing, and pace. It is not a neutral forward-facing slow retreat.

## Verified inputs and scope

- FBX: `attachments/36ceef8e-18e8-4a70-8cf1-584b6203aec2/walk backward.fbx.bin`
- FBX SHA-256: `97b1662e913c09ad61f3aa3a2840fa6dc87dfc6ec4b38636f1f3f152b54871eb`
- Frozen N: `animation_rebuild/shared_pose_fit/shared_ready_n.editable.blend`
- N SHA-256: `de60108a89bfe7f2f5bb92f2b35f2d692d3a659ca7f1dbc310ca7023572a33ef`
- Both hashes rechecked unchanged after the probe. Original files were never saved. All diagnostic writes are in this new probe folder.

## Native format, units, and correspondence

FBX 7700, take `mixamo.com`, ticks 0–46,186,158,000 (exactly 1 second), TimeMode 3/60 Hz. There are 315 raw scalar curves: 222 with 61 evenly spaced samples, 90 with one sample, and three LeftForeArm rotation curves with 65 samples. Those three add times 0.0083333, 0.0100000, 0.0116667, and 0.0133333 seconds. The Blender import retains those extra keys. Its action is frames 1–61 at 60 fps; the default scene frame_end 250 is unrelated and must not determine duration.

The FBX is centimeters, Y-up, with character forward +Z. Blender applies object scale 0.01 and a +90° X rotation: meters, Z-up, forward −Y, backward +Y. Hips is the top-level animated bone; there is no separate root bone. The native file contains skeleton animation only, no source mesh or foot contact constraints.

The source has 65 Mixamo bones; N has the same 65 names and hierarchy plus `Muzzle`, `Prop_Magazine_A`, `Prop_Magazine_B`, `Prop_Rifle`, and `RifleSocket`. Bone lengths agree to within 0.000619 mm, but rest transforms are not identical. N Hips rest is 14.3205 mm lower and 5.8771 mm toward −Y than source Hips. N thigh/shin rest axes differ about 1.090° from source; corresponding ankle rest positions differ about 21.30 mm. N upper arm/forearm rest rotations differ about 4.45°/5.18°; finger rest changes reach 27.58°. Thus correspondence is structural, not permission to copy all source local transforms blindly.

## Actual direction and posture

Hips displacement in world meters is [−0.000000180, +1.921261410, +0.000004232]. The horizontal pace is 1.921261 m/s (6.9165 km/h) at original timing. Hips height varies 0.757699–0.841070 m, compared with source rest height 0.953600 m.

Anatomical-facing directions are calculated by applying each bone's source-rest-to-pose rotation to neutral forward [0,−1,0]. They are orientation proxies, not optical lens or camera axes:

- Hips yaw: −48.53° to −27.01°
- Chest/Spine2 yaw: −34.46° to −23.80°
- Head yaw: −13.27° to −12.09°
- Head forward-axis elevation: −16.87° to −14.44°
- Hip-to-neck axis tilt from vertical: 10.86° to 13.02°, mainly toward −X; its sagittal forward component is 2.09°–5.76°

These are native source choices. Skeleton phases are illustrated in `source_skeleton_phases.png`; horizontal recentering is only for that diagram.

## Loop and foot timing

After removing only horizontal cycle travel, the largest joint-head endpoint mismatch is 0.08894 mm at HeadTop_End and the largest world rotation mismatch is 0.01625° at LeftArm. The maximum raw endpoint scalar Euler difference is 0.02277° at LeftArm Z. The nearly matching endpoints support a one-second cycle, with one left/right pair of steps (about 120 steps/min). A 60 Hz cyclic playback should use a one-second period, treating the matching endpoint as the boundary rather than adding a 61st-frame hold.

Strong support proxies are world horizontal ankle/toe-head speed below 0.05 m/s:

- Left ankle and toe: frames 16–40.75, times 0.2500–0.6625 s
- Right ankle and toe: frames 1–10 and 48.25–61, times 0–0.1500 and 0.7875–1.0000 s

The seam is in right support while the left foot is swinging. These are skeleton kinematics, not assertions about actual sole clearance. ToeBase heights reach minima 7.41 mm left and 8.30 mm right; no source sole exists to convert those into surface clearance.

## Velocity and import conventions

All native curves have flags 8456 / 0x2108: cubic, auto, time-independent tangents. Blender 4.3.2's FBX importer explicitly assumes linear interpolation (`import_fbx.py`, lines 715–736 and 851–890). The import is therefore not evidence of the native cubic derivatives.

Raw Hips first/last one-frame translation secants, converted to Blender-world meters/second, are approximately [0.031754, 2.257660, −0.348729] and [0.014588, 1.830048, −0.275569]. These source sampled slopes differ materially, independent of frame offset and unit convention. Exact native cubic endpoint derivatives were not resolved.

For the imported linear-channel/normalized-quaternion representation, double-precision analytic FK reconstructs Blender endpoint matrices with maximum absolute error 3.51e−7 and measures these endpoint derivative mismatches:

| Bone | Linear mismatch m/s | Angular mismatch °/s |
| --- | ---: | ---: |
| Hips | 0.43417 | 13.9145 |
| Spine2 | 0.40347 | 30.0931 |
| Head | 0.34380 | 48.4154 |
| LeftFoot | 0.69634 | 184.7261 |
| LeftToeBase | 1.10318 | 347.8843 |
| RightFoot | 0.19606 | 6.2716 |
| RightToeBase | 0.19177 | 5.9635 |

Therefore exact C1 continuity is not established. Any seam repair should be bounded and assessed against the measured source samples and right-support/left-swing boundary, without changing the source's overall posture, timing, travel, or trajectory. This probe does not prescribe or author a repair.

## Reproducible outputs

- `SOURCE_DIAGNOSIS.json`: complete per-bone rest, endpoint, analytic imported derivative, native secant, support, and immutable-input evidence
- `native_fbx.json`, `native_curve_targets.json`: actual raw FBX properties, key values/times, flags, and targets
- `import_initial.json`, `import_curve_samples.json`: import representation and exact stored keys
- `source_motion_samples.json`: evaluated matrices at quarter frames plus short endpoint checks
- `base_rest_snapshot.json`: N rest correspondence
- `source_import.blend`: diagnostic imported source only
- `probe_initial.py`, `probe_motion.py`, `analyze_motion.py`, `finalize_report.py`: replay scripts

No source/N pose edits, new grip work, engine transfer, crouch work, retiming, or target authoring were performed.
