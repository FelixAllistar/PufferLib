# anim: motion matching

A general-purpose motion-matching animation library. An offline Python builder
turns BVH clips into a binary database (MMDB); a C99 runtime (`mm.h`, `mm.c`,
libm only) matches and plays that database so that a humanoid body follows a
game-driven capsule. It does not depend on SWAT or any other game.

Animation is presentation only. The game simulation moves a capsule; the
runtime renders the body at the capsule and never feeds anything back.

## Files

| Path | Purpose |
| --- | --- |
| `mm.h`, `mm.c` | Runtime: load, FK, trajectory springs, search, playback, inertialization, foot locking IK |
| `tools/mm_build.py` | Offline builder: BVH -> retarget -> mirror -> root, contacts, features -> MMDB |
| `tools/mm_convert.py` | Converts an MMDB between versions 1 and 2 (dropping joints, recomputing stance) without the BVH files |
| `tools/gltf_skeleton.py` | Extracts a skeleton description (JSON) from a .glb skin |
| `tools/gltf_body_shape.py` | Fits body collision volumes (JSON) from a .glb's skinned mesh |
| `tools/bvh.py` | BVH reader and writer |
| `tools/mmmath.py` | Vectorized quaternion helpers for the tools |
| `tools/synth_bvh.py` | Procedural SOMA-like biped generator (test and benchmark data) |
| `tools/mm_demo.c` | raylib 5.5 stick-figure demo with a headless CSV mode |
| `tests/test_build.py` | Builder tests |
| `tests/make_fixtures.py` | Builds the synthetic test database and the FK reference |
| `tests/test_mm.c` | Runtime tests |
| `tests/bench_mm.c` | Search and controller benchmark |

Python tools need Python 3 and numpy only.

## Building and testing

```
make            # build/libmm.a
make test       # builder tests, synthetic database, runtime tests
make bench      # ~408k-frame synthetic database (about 10 minutes), load, size, error and search numbers
make demo       # build/mm_demo (raylib 5.5 from ../raylib-5.5_linux_amd64 or /opt)
./build/mm_demo build/test.mmdb                         # interactive
./build/mm_demo build/test.mmdb --headless 1200 out.csv # scripted, no window
./build/mm_demo build/test.mmdb --record 400 shot.png   # scripted, screenshot
```

Building a real database:

```
python3 tools/gltf_skeleton.py ../ocean/swat/assets/characters/ready.glb officer.json
python3 tools/gltf_body_shape.py ../ocean/swat/assets/characters/ready.glb officer_body.json
python3 tools/mm_build.py --clips /path/to/kimodo_bvh --skeleton officer.json \
    --map soma77_to_officer --body-shape officer_body.json --manifest tags.json \
    --fps 30 --auto-stance 0.82 --out officer.mmdb
# Same, without finger animation (the fingers play their rest pose):
python3 tools/mm_build.py ... --drop-joints '.*Hand(Thumb|Index|Middle|Ring|Pinky).*' --out officer_nofingers.mmdb
# An existing version 1 database converts without the BVH files:
python3 tools/mm_convert.py officer_v1.mmdb officer.mmdb --auto-stance 0.82
```

Builder options: `--map` takes a JSON file `{"source joint": "target joint"}` or
one of `soma77_to_mixamo`, `soma77_to_officer`, `identity`; `--features` takes a
JSON file overriding entries of `DEFAULT_FEATURES` (feature blocks and weights,
trajectory frames, root smoothing, contact thresholds, end margin, tag
vocabulary, role joints); `--source-yaw-deg` rotates the source so it faces +Z;
`--source-rest-bvh` takes the source rest pose from frame 0 of a BVH instead of
the zero-rotation pose; `--no-mirror` skips mirrored copies;
`--skeleton-from-bvh` uses a BVH hierarchy as the target; `--auto-stance RATIO`
tags every frame `stand` or `crouch` from the hips height (step 2);
`--format 2` (default) writes the compact format and `--format 1` the float32
format of earlier versions; `--drop-joints REGEX` leaves out every joint whose
name fully matches the regular expression (version 2 only, step 10). The joints
the runtime needs (hips, legs, feet, toes and feature joints) cannot be dropped.
Dropping a joint drops its animation, not its children's: drop whole subtrees
such as the fingers.

`mm_convert.py IN OUT [--format 2] [--drop-joints REGEX] [--auto-stance RATIO]`
rewrites a database. Converting a version 1 file gives byte for byte the
version 2 file the builder writes from the same clips (tested), because both
serialize the same stored arrays.

## Conventions

Y is up, yaw 0 faces +Z, positive yaw turns toward +X, and a character's left
side is +X when it faces +Z (glTF and Mixamo convention). Database lengths are
in the target skeleton's units (meters for glTF rigs), so source centimeters
disappear through the hip-height ratio. Quaternions are x, y, z, w.

## Builder pipeline

1. **Skeleton.** `gltf_skeleton.py` reads the first skin of a .glb: joint names,
   parent indices reordered depth first so parents precede children, rest local
   TRS from the nodes, and the inverse bind matrices. Non-joint ancestors (the
   armature node) are baked into root joints' rest transforms; scaled armatures
   are rejected. For `ready.glb` this gives 70 joints (the 65 `mixamorig:`
   bones plus RifleSocket, Muzzle and three prop joints), whose rest pose equals
   the bind pose (checked by a test).
2. **Tags.** From `--manifest` (`{"clips": {"name": ["tactical", "stand"]}}` or
   `{"name": {"style": "tactical", "stance": "stand"}}`), otherwise from the
   tokens of the clip's directory and file name that are in the tag vocabulary.
   A clip without `stand` or `crouch` gets `stand`. Mirrored copies add
   `mirrored`. At most 32 tags, shared by clip tags and frame tags.

   With `--auto-stance RATIO` the stance comes from the motion instead, per
   frame: the hips height above the rest floor (the rest hips height minus the
   standing hips height of step 4), divided by the standing hips height, is
   Gaussian smoothed (sigma 3 frames); a frame enters `crouch` below RATIO and
   leaves it above RATIO + 0.06 (hysteresis; the first frame is crouched when it
   is below RATIO); then any run of fewer than 6 frames takes the stance of the
   run before it (the first run that of the run after it). Every frame gets
   exactly one of `stand` and `crouch` as a *frame tag*, and the clip tags lose
   both, so a clip that walks, crouches and stands up again is searchable under
   `crouch` only in its crouched part. Style tags and `mirrored` stay clip tags;
   a mirrored copy has the same frame tags as its source. Generated "crouch"
   clips are often upright (Kimodo ignored the crouch in 3 of 6 categories of
   the first locomotion set), and a wrong stance tag makes the runtime play
   standing motion while crouched. Version 1 files have no frame tags: a clip
   gets each frame tag that more than half of its frames have.
3. **Resampling** to `--fps` (linear positions, normalized-lerp rotations) when
   a BVH has a different frame time.
4. **Retargeting by world rotation change from rest.** For each mapped target
   joint, `W_target(t) = (W_src(t) * W_src_rest^-1) * W_target_rest` (rotations
   only), turned into a local rotation under the animated parent. Unmapped
   target joints keep their rest local rotation. The source rest is the
   zero-rotation BVH pose unless `--source-rest-bvh` is given; both rests must
   be the same pose (T-pose here). The hips get the source hips' world
   translation change from rest scaled by `target hip height / source hip
   height`, where a hip height is the rest height of the hips above the lowest
   foot or toe joint. The source rest pose is assumed to stand on the motion's
   floor. The target hips must be a root joint.
4b. **Arm-body penetration (optional, `--body-shape`).** Retargeting keeps
   the source's joint directions, so on a target that is wider, deeper or
   shorter-armed than the source (the SWAT officer's vest: torso 26% longer
   and shoulders 25% wider than Kimodo's body at equal hip height) hands and
   elbows pass through the body. `tools/gltf_body_shape.py` fits stacked
   elliptical slices to the skinned mesh vertices of the hips, spine, neck,
   head and thigh joints, in each joint's bind frame, using the 95th
   percentile so straps and pouches do not inflate them. Per clip the builder
   then tests the elbow, three forearm points, the wrist and the palm (as
   spheres of 3 to 4 cm) against those volumes: the elbow's push moves the
   IK pole, the largest of the others moves the hand target, both are
   Gaussian-smoothed over time (sigma 1.5 frames per iteration, so a
   correction eases in), and two-bone IK re-solves the upper arm and forearm
   while the hand keeps its world rotation. Up to 12 iterations, stopping when
   no push exceeds 0.5 mm. On the Kimodo tactical locomotion set this takes
   frames with an arm more than 5 mm inside the body from 48% to well under
   1%. Poses the arm cannot reach out of (a hand forced past the far side of
   the chest) stay partly inside. The build prints before and after numbers.
5. **Mirroring.** Every clip is also added mirrored: each joint takes its
   Left/Right partner's world rotation change from rest, reflected across the
   character's sagittal plane at frame 0 (`(x, y, z, w) -> (x, -y, -z, w)` in
   the frame of the starting facing), times its own rest world rotation. The
   hips path is reflected in the same plane. Unmapped joints keep their rest
   local rotation. Mirroring twice is the identity (tested).
6. **Simulation root.** The hips projected to the ground, smoothed by a Gaussian
   (sigma 3 frames). Facing is the horizontal forward vector
   `cross(left - right, up)` of the hip line plus the shoulder line, smoothed by
   a Gaussian (sigma 6 frames) and normalized. The filters pad the clip ends by
   odd reflection (`x[-k] = 2 x[0] - x[k]`), which reproduces straight lines
   exactly, so constant-velocity motion keeps its exact root up to the clip
   edges. The hips are then stored relative to this root; every other joint is
   stored as its local rotation.
7. **Contacts.** A foot is in contact when its ankle is less than 5 cm above its
   rest height and moves slower than 0.35 m/s; gaps and then contacts shorter
   than 3 frames are removed.
8. **Features**, all in the current root's space: left and right foot position
   (weight 0.75) and velocity (1.0), hips velocity (1.0), future root positions
   x, z at +10, +20, +30 frames (1.0) and future facing directions x, z at the
   same frames (1.5): 27 values by default. Velocities are central differences.
   Each block is normalized by its per-dimension mean and one standard
   deviation for the whole block (the mean of the per-dimension deviations),
   divided by the block weight. The trajectory of frames within 30 frames of the
   clip end continues with the clip's final root velocity (mean of the last 5
   frames) and holds the final facing; those frames are flagged as
   extrapolated. The last 10 frames of every clip are flagged not searchable.
9. **Stored arrays.** Rotations, hips and root are float32 after this point;
   features are normalized. Both file versions serialize these arrays.
10. **Compact storage (version 2).**
    *Joint tracks*: a joint matching `--drop-joints` is not stored and plays
    its rest local rotation; a joint whose rotation never leaves a 0.01 degree
    cone around its first frame's (props, sockets, end joints, fingers that do
    not move) is stored once as a float32 quaternion; every other joint (and
    always the hips) is a *track* with one rotation per frame.
    *Rotations* of tracks use the smallest-three encoding in 48 bits: the
    largest-magnitude component is dropped after making it positive (q and -q
    are the same rotation), its index i (0..3 for x, y, z, w) goes in bit 15 of
    the first word (i & 1) and of the second word (i >> 1), and the other three
    components, in index order, each in [-1/sqrt 2, 1/sqrt 2], are stored in the
    low 15 bits of the three words as `round((c * sqrt 2 + 1) / 2 * 32767)`;
    bit 15 of the third word is 0. Decoding: `c = (v / 32767 * 2 - 1) / sqrt 2`
    and the dropped component is `sqrt(max(0, 1 - c0^2 - c1^2 - c2^2))`. One
    quantization step is 4.3e-5, so the angle error is at most about 0.005
    degrees (0.0048 measured; the builder prints the maximum).
    *Hips, root position and yaw* stay float32 (the root's y is 0): about 4% of
    a version 2 file, and float32 keeps positions exact at any distance.
    *Features* are int16 with a per-feature step that is the smallest power of
    two for which the feature's largest magnitude fits (`k * step`, |k| <=
    32767), so a stored value `(float)k * step` is an exact float32 number and
    the error is at most half a step (2.4e-4 in normalized units on the test
    database, against a block standard deviation of 1 / weight). The search
    runs on these dequantized values and returns exactly what the brute-force
    scan returns on them. Box minimums and maximums are the int16 extremes of
    each box.

## Runtime

```c
mm_db db;
mm_db_load(&db, "officer.mmdb");
mm_config cfg;
mm_config_default(&cfg);
mm_input in = {0};                       /* capsule state + intent, every tick */
in.filter.required = mm_db_tag_bit(&db, "stand") | mm_db_tag_bit(&db, "tactical");
in.filter.forbidden = 0;                 /* e.g. mm_db_tag_bit(&db, "mirrored") */
mm_controller c;
mm_controller_init(&c, &db, &cfg, 0, &in);
for (;;) {
    in.position = ...; in.velocity = ...; in.yaw = ...;          /* game capsule */
    in.desired_velocity = ...; in.desired_yaw = ...;             /* game intent */
    mm_controller_update(&c, &in, dt);
    /* c.local_rot[j], c.world_pos[j], c.world_rot[j] in target joint order;
     * mm_transform_matrix(c.world_pos[j], c.world_rot[j], m) times the joint's
     * inverse bind matrix (db.inverse_bind + 16 * j) gives the skinning matrix. */
}
mm_controller_free(&c);
mm_db_free(&db);
```

Lower-level functions: `mm_forward_kinematics`, `mm_sample_pose`,
`mm_predict_trajectory`, `mm_trajectory_features`, `mm_search`,
`mm_search_brute_force`, `mm_frame_cost`, `mm_frame_allowed`, `mm_two_bone_ik`,
quaternion helpers and `mm_transform_matrix` (column major, the memory layout
of raylib's `Matrix`).

Added with version 2 (the functions above keep their signatures and accept
both versions): `mm_db_load_memory` loads from a buffer, `mm_db_rotation`
returns one joint's local rotation at a frame, `mm_db_frame_features` a frame's
stored features, `mm_frame_tags` a frame's clip tags OR frame tags, and
`mm_controller_memory` what a controller allocates. For a version 2 database
the struct fields `rotations`, `features` and the float box arrays are NULL
(the quantized arrays `rotations_q`, `features_q` and friends replace them),
so code that read `db.rotations` or `db.features` directly should call
`mm_db_rotation` or `mm_db_frame_features`; every other field (skeleton, clips,
tags, `hips_pos`, `root_pos`, `root_yaw`, `flags`) is as before.

**Loading.** `mm_db_load` reads the header, then reads the whole file with one
`fread` into a single allocation that is followed by the tables derived at
load time (names, skeleton arrays, clips, blocks, frame-to-clip map, per-box
tag masks, joint storage); every pointer of `mm_db` points into it and
`mm_db_free` frees it. The bulk arrays (rotations, hips, root, flags, frame
tags, features, boxes) are used in place, so the database needs the file size
plus about 6 bytes per frame of memory. The loader checks every count against
the file size, so a truncated or corrupt file is rejected rather than read out
of bounds. (Memory mapping would need the derived tables in a second
allocation; the single read is portable and is not the bottleneck, see the
benchmark.)

**Sharing one database.** No function writes to an `mm_db` after
`mm_db_load` returns (every runtime function takes `const mm_db *`), so any
number of characters, on any number of threads, can play one loaded
database. All playback state is in `mm_controller`: the playing frame, timers,
capsule history, inertialization offsets, foot locks, the query and the pose
buffers. A controller allocates `mm_controller_memory(db)` bytes, which is
`88 * joints + 4 * features` (four quaternions and two vectors per joint: the
inertialization rotation and angular velocity offsets, the sampled pose, the
output local rotations, world positions and world rotations, plus the query),
6,268 bytes for the 70-joint officer with 27 features, plus the 488-byte
struct itself. A test runs a second controller on the same database between
every update of the first and checks that the first's output hash does not
change.

**Trajectory prediction.** The capsule's velocity, an acceleration estimated
from the previous update's velocity, its yaw and a yaw rate estimated the same
way feed two critically damped springs: the velocity approaches the desired
velocity with half-life `velocity_halflife` (0.27 s) and the position is the
closed-form integral of that spring; the yaw approaches the desired yaw with
half-life `rotation_halflife` (0.27 s). The spring is evaluated at the
database's trajectory frames (+10, +20, +30 at 30 fps) and turned into features
exactly as the builder does.

**Search.** The query is the playing frame's stored pose features plus the
predicted trajectory features. A frame's tags are its clip's tags OR its frame
tags (stance); the per-box tag unions and intersections are computed over
those combined tags, so tag skipping stays exact. `mm_search` scans the 64-frame boxes, then the
16-frame boxes inside a surviving large box, then its frames, in index order. A
box is skipped when no frame in it is searchable, when its tag union lacks a
required tag or its tag intersection has a forbidden one, or when its squared
distance lower bound exceeds the best cost. Bounds (and a cheap per-frame
pre-check) are computed branch free in four interleaved partial sums; every
term of a bound is at most the matching term of any frame inside the box, in
floating point too, and only the order of the additions differs from the exact
sequential frame cost, so a box or frame is skipped only when its bound exceeds
the best cost by a relative slack of 1e-5 (well above the rounding error of a
27-term sum). Frames that survive get the exact sequential cost with early exit
and a strict comparison. The result therefore equals the brute-force scan
exactly: same frame, same cost (tested on 2000 random queries with clip-tag
and frame-tag filters for both file versions, and on the 408k-frame
benchmark). For version 2 every feature, box limit and bound term is computed
as `(float)k * step` in the same way in `mm_search`, `mm_search_brute_force`
and `mm_frame_cost`.

**Playback.** One database frame per `1/fps` seconds with normalized-lerp
interpolation between frames. A search runs every `search_interval` (0.1 s),
immediately when the desired velocity changes by more than
`force_search_velocity` (0.5 m/s) or the desired yaw by more than
`force_search_yaw` (0.5 rad) in one update, when the tag filter changes and
rejects the playing frame, and at the end of a clip. The best candidate must be
cheaper than continuing by `transition_margin` (0.05), and candidates within 3
frames of the playing frame in the same clip are not transitions.

**Inertialization.** On a transition each joint gets a rotation offset and an
angular velocity offset so that the displayed pose and its velocity do not
change (`offset = displayed * destination^-1`, `offset_velocity = source velocity
+ offset_velocity - destination velocity`), and the hips get position and
velocity offsets likewise. The offsets decay with a critically damped spring of
half-life `inertialization_halflife` (0.1 s), starting on the update after the
transition. There are no crossfades.

**Synchronization with the game.** The rendered root is always the capsule:
position and yaw. The animation's own root motion is only used for matching
(through the trajectory and velocity features). With `root_adjust_max > 0`
the rendered root is moved off the capsule by an offset that integrates the
difference between the animation root velocity and the capsule velocity,
decays toward zero with `root_adjust_halflife` and is clamped to
`root_adjust_max`; this hides some sliding at the cost of the body leaving the
capsule by a few centimeters. It is off by default.

**Foot locking.** When a foot's contact bit starts, its current position (plus
any remaining unlock offset) is locked. While locked, the foot's offset is the
lock position minus the animated foot, and two-bone IK on upper leg, leg and
foot moves the ankle onto the lock while keeping the foot's world rotation.
When contact ends, or the animated foot drifts more than
`foot_unlock_distance` (0.1 m) from the lock, the offset decays to zero with a
critically damped spring (`foot_unlock_halflife`, 0.1 s) and its velocity at
release. `mm_two_bone_ik` is a standalone function: it bends the chain at both
joints about the chain plane normal (or the pole direction when the chain is
straight) to the target distance, then swings the root joint onto the target.

**Determinism.** No random numbers, no clocks, no threads, no allocation after
initialization. Identical inputs give bit-identical outputs (tested by hashing
two runs, for a version 1 and a version 2 database). A version 1 file plays
bit-identically to earlier releases of this runtime. Build with `-ffp-contract=off` (the Makefile does) so that
floating-point contraction does not differ between compilers or call sites.

## Benchmark

`make bench` builds 680 random ten-second clips (1360 with mirrors, 408,000
frames, 27 features, a 520 MB database) and times `mm_search` on one core of
the cloud CPU (Intel Xeon @ 2.30GHz, shared, timings vary by up to 50% between
runs), `-O2`:

| Query | Mean | p99 | Brute force |
| --- | --- | --- | --- |
| Controller queries on a meandering 60 s path, stand filter, from scratch | 0.24-0.27 ms | | |
| Same, with the controller's initial bound (cost of continuing) | 0.15-0.17 ms | | |
| Worst case: pose of one random frame, trajectory of another, stand + not mirrored | 0.23-0.31 ms | 0.43-0.63 ms | 2.0-3.6 ms |
| Worst case, no tag filter | 0.52-0.94 ms | 1.1-1.8 ms | 6-16 ms |

With `-O3 -march=native` the unfiltered worst case is 0.46-0.53 ms. A full
controller update averages 0.04 ms. All searches matched brute force exactly.

## MMDB file format (version 1)

Little endian. All sections are 4-byte aligned. `u32`/`i32` are 4-byte integers,
`f32` IEEE floats. A *string* is `u32 length`, the UTF-8 bytes, and zero padding
to a multiple of 4. Sizes: J joints, N frames, C clips, T tags, F features, B
blocks, K trajectory points, S = ceil(N / 16) small boxes, L = ceil(N / 64) large
boxes.

| Offset | Type | Field |
| --- | --- | --- |
| 0 | char[4] | magic `MMDB` |
| 4 | u32 | version, 1 |
| 8 | f32 | fps |
| 12 | u32 | J |
| 16 | u32 | N |
| 20 | u32 | C |
| 24 | u32 | T |
| 28 | u32 | F |
| 32 | u32 | B |
| 36 | u32 | K |
| 40 | u32 | small box size, 16 |
| 44 | u32 | large box size, 64 (a multiple of the small size) |
| 48 | u32 | reserved, 0 |
| 52 | i32 | hips joint |
| 56 | i32[8] | role joints: left upper leg, left leg, left foot, left toe, right upper leg, right leg, right foot, right toe |
| 88 | | sections below, in order |

1. Skeleton, J records: string name, i32 parent (-1 or lower than the joint's
   index), f32[3] rest local translation, f32[4] rest local rotation, f32[16]
   inverse bind matrix (column major).
2. Tag names, T strings. Tag i is bit `1 << i`.
3. Clips, C records: string name, u32 first frame, u32 frame count, u32 tag bits.
   Clips are contiguous and in frame order.
4. Trajectory frames, K x i32 (frame offsets, default 10, 20, 30).
5. Feature blocks, B records: string name, u32 type (0 joint position, 1 joint
   velocity, 2 trajectory positions, 3 trajectory directions), i32 joint (-1 for
   trajectory blocks), u32 first feature, u32 feature count, f32 weight.
   Trajectory blocks hold x, z pairs per trajectory frame.
6. Feature means, F x f32; feature scales, F x f32. A stored feature is
   `(raw - mean) / scale`, with `scale = block standard deviation / weight`.
7. Local rotations, N x J x f32[4]. The hips rotation is relative to the
   simulation root; other root joints hold their rest rotation.
8. Hips position relative to the simulation root, N x f32[3].
9. Root position in clip space, N x f32[3] (y = 0); root yaw, N x f32.
10. Frame flags, N x u8, zero padded to a multiple of 4: bit 0 left contact,
    bit 1 right contact, bit 2 trajectory extrapolated, bit 3 not searchable.
11. Features, N x F x f32, normalized.
12. Small boxes: S x F f32 minimums, then S x F f32 maximums, over frames
    `[16 s, 16 s + 16)`; large boxes: L x F minimums, then L x F maximums over
    frames `[64 l, 64 l + 64)`. Boxes span clip boundaries and include
    unsearchable frames; the runtime derives per-box tag and searchability masks
    at load time.
13. Magic `MMDB` again, then the end of the file.

## Bone maps

`soma77_to_mixamo` and `soma77_to_officer` map Hips, Spine1 -> Spine,
Spine2 -> Spine1, Chest -> Spine2, Neck1 -> Neck, Head, and per side Shoulder,
Arm, ForeArm, Hand, Thumb1 to Thumb3, finger joints 2 to 4 of Index, Middle,
Ring and Pinky onto Mixamo joints 1 to 3 (SOMA's first finger joint is a
metacarpal about 4 cm from the wrist; its second is the knuckle, where Mixamo's
first is), Leg -> UpLeg, Shin -> Leg, Foot and ToeBase, all with the
`mixamorig:` prefix. The SWAT officer in `ready.glb` is a Mixamo rig: every target name of
the officer map was read from that file and a test checks they exist. SOMA's
Neck2, finger metacarpals, finger tips, toe ends and any other unmapped joints follow their parents;
the officer's props and sockets keep their rest pose relative to their parent.

## Synthetic test data

`tools/synth_bvh.py` builds a 76-joint skeleton with SOMA joint names
(centimeters, T-pose rest from zero rotations, facing +Z) and drives it along
key-framed speed, heading and facing paths. Footholds are planned from the gait
phase and the predicted root, stance feet stay exactly where they land, and the
legs are solved with analytic two-bone IK. The standard set has walks, a 90
degree left and right turn, a strafe, a stop, a start, idle, a run, a
walk-run-walk, a backpedal, crouch walk and idle, a turn in place and a circle.
`--random N` makes 10-second random locomotion clips for the benchmark.

## License note

`mm.c` adapts formulas from Daniel Holden's Motion-Matching sample
(github.com/orangeduck/Motion-Matching, MIT license): the critically damped
spring and its decay form, the character velocity spring prediction, the
inertialization offsets, the two-level bounding-box search and the
law-of-cosines two-bone IK. The MIT notice is kept in the header of `mm.c`.
