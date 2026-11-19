# Teaching the dinosaurs to walk

This is physics-based locomotion for Cretaceous: an environment in which a
ragdoll raptor, built from our own rig, learns to walk by imitating its walk
clip, and to take a shove and walk on. It plugs into the PufferLib trainer
without changing it. Trained policies walk (runs 2 to 4 below), and their
motion is baked into ordinary clips the game plays ("New clips from physics").

- Code: `ocean/cretaceous/loco/` (header-only), `tools/loco_play.c`,
  `tools/loco_view.c`, `tools/loco_bake.c`, `tests/test_loco.c`,
  `config/cretaceous_loco.ini`.
- The sim is untouched: its hashes and the determinism suite are the same.
  Loco mode is a separate build of the binding. The game only plays the baked
  clips (render/dino.c), which change what is drawn, never the sim.

## Quick start

```
make -C ocean/cretaceous loco_play loco_view
build/cretaceous/loco_play --all                       # rig, stability checks, PD baseline, throughput
build/cretaceous/loco_view                             # watch the PD baseline (add --policy CKPT.bin later)
ocean/cretaceous/loco/train.sh                         # build the trainer in loco mode and train (GPU box)
```

All three read `ocean/cretaceous/assets/dinos/raptor.dino` by default. That file
is not in this sandbox (it comes from `tools/fetch_dinos.sh` and a licensed
download), so every number below comes from two stand-ins: the
`tools/dino_testrig.py raptor` rig and the committed test fixture
`tests/fixtures/fixture_biped.dino`. Both are synthetic box-limbed bipeds with
procedural clips. Run `loco_play --all` on the real raptor first; it prints
the same tables for it.

`pd_baseline_testrig.mp4` in this folder is a 6 s capture of the PD baseline
on the stand-in. The solid shapes are the physics body; the blue wire figure
is the clip's pose at the same moment. Notice how the body falls a second or
so after each reset, which is what a policy has to learn to stop.

## Measured on the real raptor (2026-11-16, Felix's machine)

The cloud build never saw `raptor.dino`; on the real file two things broke
and are fixed: its bones carry numbered suffixes (`Tail_1_074`), which the
bone table now strips before matching, and its bind pose is in centimetres,
which the builder now scales to metres from the skinned joints (test_loco
checks the raptor comes out between 0.5 and 3 m tall).

- **Body:** 23 bodies, 22 joints, 52 action dimensions, 302 kg (see the open
  questions), 3.0 m long. Tail 46 kg in 5 segments (15%), 140 kg ahead of
  the hips. Walk clip: 1.29 s cycle, 1.80 m/s, pelvis 0.92 m up.
- **Stability:** passes at the default 4 substeps (pinned drift 1.7° mean,
  12.6° worst joint, 1.4 cm separation; free stand 1.2 s; dropped 2.3 cm).
  1 and 2 substeps fail. The worst-joint drift is about ten times the
  stand-in's; 8 and 16 substeps do not shrink it, so it is the springs'
  steady-state sag under the heavier body, not solver error.
- **PD baseline (no learning):** falls after 0.92 s on average, tracking error
  10.4° per joint and 51 cm at the end effectors, reward 0.26 per step. With
  `--passive-tail`: same 0.92 s, but 8.4° and 35 cm, reward 0.35.
- **Throughput:** 3,345 agent steps/s on one core (58 µs per ragdoll step),
  half the 7,000 estimated below. At 4 threads (this machine) that is about
  13,000 steps/s, so 300 M steps is about 6.5 hours; the CPU, not the GPU,
  sets the pace.

## First training runs (2026-11-17, Vast 5060 Ti, 15 cores, ~60K steps/s)

- **Run 1** (lr 3e-4, γ/λ 0.95, the cloud's first settings): 300M steps,
  nothing learned. Up 1.2 s at the end, and its mean actions did no better
  than no actions.
- **Run 2** (`drone.ini`'s optimizer settings, now in the ini): the raptor
  walks. Every episode reaches the 15 s cap from about 175M steps on; at
  300M, return 103, joint error 9°, end effectors 19 cm off, 26.5 m per
  episode (the clip's 1.8 m/s). Its mean actions, run on the CPU: 128 of 128
  episodes up the full 15 s, 29 m each, 6.7° joint error. The tracking is
  loose: almost all of the reward is the pose term; the end-effector and
  centre-of-mass terms sit near 0, so the body walks the clip's gait but
  drifts from where the clip is.
- **Run 3 and 3p** (2026-11-18; the COM velocity term and k_ee 10, then the
  same with `passive_tail = 1`): both walk. Scored alike (mean actions, 64
  animals x 60 s, tail joints left out of the body numbers):

  | run | falls | m per 15 s (clip 27.0) | body joints | feet + head | COM speed error | tail joints vs clip |
  |---|---|---|---|---|---|---|
  | 2 | 0 of 256 | 29.2 | 6.5° | 15.0 cm | 0.82 m/s | 5.9° |
  | 3, active tail | 0 of 256 | 27.5 | 7.0° | 13.7 cm | 0.69 m/s | 4.7° |
  | 3p, passive tail | 0 of 256 | 29.9 | 7.0° | 13.8 cm | 0.72 m/s | 20.8° |

  Run 3 now walks at the clip's speed and tracks the feet, head and COM
  speed better. The passive tail costs the body nothing: it balances as
  well. Its tail swings about 21° off the clip's pose; whether that looks
  more alive or just floppy is a judgement for the videos
  (`D:\puffer_media\cretaceous\loco_run3_20261118`).
- The CPU runner (`loco/loco_policy.h`) first read the checkpoint wrongly:
  the trainer's allocator starts every tensor on a 16-byte boundary, so the
  MinGRU weights sit 3 values after where a packed layout puts them. Fixed;
  `test_loco` checks the runner against 60 steps of actions recorded inside
  the trainer (`tests/fixtures/loco_policy_run2*`).
- `--headless` is needed for `puffer_loco eval`: without it the trainer
  renders, and the loco build draws nothing, so it returns at once.

## New clips from physics (2026-11-18)

Why train a physics raptor at all, when the game already has a walk clip? An
artist's clip covers the one situation it was animated for. The physics
raptor has to cope with whatever happens to it, and what it does then is
motion nobody animated that still obeys gravity and friction. Baking turns
that motion back into ordinary clips: no physics or policy runs in the game.

- **Shoves** (`push_interval`, `push_min`, `push_max`, `push_seconds`; off
  by default): a push on the pelvis, spine or chest from a random side, sized
  as a change of the whole animal's speed. The reward stays the clip's, so the
  policy learns to take the hit and walk on. Run 3, never shoved in training,
  holds 1 m/s (3 falls in 64 episodes) and falls in 21 of 67 at 1.5 m/s.
- **Baking** (`tools/loco_bake.c`): each frame's body poses become the rig's
  joint rotations; joints inside a body (a merged bone pair, fingers, a welded
  tail rod) keep the artist's clip, so they still move. `--selftest` bakes the
  walk clip itself and gets it back within 0.0001°. It writes `walk_phys` (one
  cycle of the policy's walk, looping) and sixteen hit reactions
  `hit_<front|back|left|right>_<0..3>` (the blow lands at four points of the
  stride, 2.6 s each, ending in step with the walk), appended to a copy of the
  `.dino`. `tools/fetch_dinos.sh` runs it on the raptor when the policy file
  `loco/policies/raptor_walk_shoves.bin` is there.
- **In the game** (`render/dino.c`): a blow while walking plays the clip for
  the side it came from, the one whose stride phase is nearest, starting where
  its shove lands, so it takes over mid-stride and hands back in step. The
  pelvis stays on the walk's line in the clip: the sim's knockback moves the
  animal. Settings > Physics animation switches the hit reactions (on) and the
  physics walk (off); the gallery's "hit while walking" shows them.
- **The physics walk skates less.** `animcheck` measures how fast planted
  feet slide against the ground: the artist's walk 49% of its speed, run 3's
  baked walk 15%. The ragdoll cannot skate; its feet stick or it falls.

### The tail was a whip

The passive-tail run (3p) looked broken at the tip, and measurement agrees.
Relative angular speed of each tail joint over 32 animals x 20 s (deg/s, the
clip's own in brackets), and the share of time against the joint's cone limit:

| joint | run 3p passive (20 Hz, ζ 0.4) | at limit | run 3 active (40 Hz, ζ 1) |
|---|---|---|---|
| tail_1 | 55 (0) | 0% | 21 (0) |
| tail_2 | 101 (41) | 0% | 47 (41) |
| tail_3 | 276 (29) | 0% | 43 (29) |
| tail_4 | 778 (50) | 40% | 138 (50) |
| tail_5 | 1268 (40) | 56% | 177 (40) |

The segments weigh 35, 8.5, 1.9, 0.32 and 0.15 kg: each lighter than the one
before, on its own loose spring, free to bend 30-39°. That is a whip: motion
runs down the chain into the light tip and cracks it against its limits. Even
the active tail's tip flutters at 3-4 times the clip's speed.

- More damping helps the passive tail (ζ 1), and so do springs that stiffen
  toward the tip (`tail_stiffen` 8 brings the tip to 74 deg/s and off its
  limits), but stiff springs on 0.15 kg bodies are past what the solver steps
  stably: the active tail at `tail_stiffen` 6 falls every episode.
- The anatomical fix is a rod. A dromaeosaur's tail bends at the base; beyond
  it the vertebrae carried long bony rods that kept it stiff (described for
  Deinonychus by Ostrom, 1969). `tail_flex = 2` keeps the first two tail
  joints and welds the rest into one rigid body (20 bodies, 43 actions). In
  the no-learning baseline its remaining tail joint moves at 40 deg/s against
  the clip's 41 (active) or 62 (passive, ζ 1), never at a limit. The baked
  clips keep the artist's motion of the bones inside the rod.

## Several clips in one policy (2026-11-18)

A first step toward stage 2 without the discriminator: one policy tracks
several clips, and the target speed tells it which.

- **The list.** `clip` takes several names joined by `+`, for example
  `clip = idle+walk+trot+run` (at most 8, each a looping clip of the
  `.dino`). Each episode starts on one of them at random, at a random phase.
  One name is the env as before, with the same random numbers.
- **The command.** The observation keeps its size; its last float, 0.2 x the
  target speed (raptor: idle 0, walk 1.8, trot 3.6, run 6.0 m/s), is now the
  command that says which clip to do. The phase floats are that clip's,
  counted from the shared start of the stride.
- **Switches** (`clip_switch`, seconds, 0 by default: never): each animal
  switches to a neighbour in the list every `clip_switch` seconds on average
  (list the clips slowest first: idle to walk to trot to run, as an animal
  changes gait; a run cannot stop in 0.3 s), at the matching point of the
  stride. The clips start their cycles on different feet (the raptor's run
  starts about 0.45 of a cycle from its walk), so the env finds each moving
  clip's offset against the first one when it loads. The command jumps at once; the
  reference the reward compares against blends from the old clip to the new
  one over `clip_blend` seconds (0.3 by default), so the target pose does not
  jump, and its forward travel carries on from where it was.
- `tools/loco_play.c` and `tools/loco_view.c` take the same list with
  `--clip`, and `--clip-switch` and `--clip-blend`; the viewer shows the
  followed animal's clip and target speed. The training log gains
  `clip_speed`, the mean target speed, which shows the mix.
- **Which way to face.** Runs 4 to 6p took a shove from the side and walked
  on 30 to 60 degrees off their line, even with the heading reward: the
  observation is all in the animal's own heading frame, so it could not see
  that it had turned. Two floats were added at the end of the observation,
  the sine and cosine of the yaw from the animal's heading to the
  reference's. They are also the steering command for later (turn the
  reference, the animal follows). Checkpoints from before (runs 2 to 7) still
  load: the runner feeds them the observation without the two floats.
- **Trained: runs 8, 9, 10** (64x2, 128x2, 256x2; 600M steps each, shoves
  with the curriculum, rod tail). 32 raptors for 30 s per test, falls out of
  episodes, final checkpoints:

  | Test | 64x2 | 128x2 | 256x2 |
  |---|---|---|---|
  | Idle alone | 3 / 64 | 0 / 64 | 0 / 64 |
  | Walk alone | 10 / 65 | 0 / 64 | 0 / 64 |
  | Trot alone | 0 / 64 | 0 / 64 | 0 / 64 |
  | Run alone | 0 / 64 | 0 / 64 | 0 / 64 |
  | Switching every ~4 s | 1 / 64 | 1 / 64 | 0 / 64 |
  | Switching + 1.5 m/s shoves | 30 / 71 | 5 / 64 | 1 / 64 |
  | Switching + 2.0 m/s shoves | 50 / 83 | 11 / 64 | 2 / 65 |
  | Switching + 2.5 m/s shoves | 90 / 106 | 29 / 70 | 8 / 66 |
  | Joint error while switching | 13.8 deg | 8.6 deg | 11.4 deg |
  | Speed at trot (3.6 m/s asked) | 2.48 | 2.26 | 1.87 |
  | Speed at run (6.0 m/s asked) | 1.53 | 2.05 | 1.85 |

  One run per size. Walking under 1.5 m/s shoves the 128 and 256 policies
  average 8.7 and 9.4 degrees off their heading (runs 6 and 6p, without the
  heading floats: 24 degrees); single animals still get spun right around.
  The 256 policy walks off at 1.6 m/s when told to idle.

## Speed first, and the animal sets its step rate (2026-11-18)

**Nobody ran.** Runs 8 to 10 match the trot and run poses within about 10
degrees but travel at about 1.9 m/s whatever gait is asked. Two causes were
traced, both in the setup:

- **The speed reward was flat.** The only term that sees travel speed is the
  COM term, exp(-10 x error^2): at 0.7 m/s of error it is already under 0.01,
  so from 2 m/s nothing points toward 3.6 or 6.
- **The clips step too slowly for their speeds.** The raptor's trot and run
  clips take 1.00 s a stride (walk 1.29 s) and their feet reach 0.73 to
  0.79 m fore and aft (walk 1.02 m). Without long leaps that is about 2 m/s.
  The speed the stance foot moves backward in each clip (the speed the animal
  travels without sliding) is about 1.3 (walk), 1.4 (trot) and 1.7 m/s (run),
  against the 1.8, 3.6 and 6.0 the game slides the model at. The stance
  detection is crude (lowest foot within 4 cm of the ground).

Three keys, all off by default (two more follow run 12 below) (single-clip and runs 8 to 10 behave as before):

- `reward_speed` (with `speed_floor`): a term for the commanded velocity
  alone. The error is the length of the difference between the mass centre's
  ground velocity and the clip's speed along the clips' travel axis (+X),
  over max(speed, speed_floor). The term is 1 - error, not below 0: a
  straight slope, and at idle it pays for standing still.
- `cadence`: the action after the rig's last joint sets how fast the clip
  plays, 2^e with e within +-cadence (1.5: 0.35x to 2.8x), eased a quarter of
  the way per policy step. The observation's last float is the current e
  (LOCO_OBS 367; older checkpoints load as before). The reference's joint
  velocities scale with the rate. The tools take `--cadence X`, which must
  match training. The log's `cadence` is the mean rate.
- `kernel = 1`: every reward term is 1 / (1 + x) in place of exp(-x) (the
  speed term 1 / (1 + (2 error)^2)). The same near a perfect track, but it
  still slopes far from it, so the clip pulls from any distance: a guide the
  policy can leave and still feel.

**Run 11 (256x2) ran sideways: my bug.** Settings of runs 8 to 10 plus
`reward_speed 0.45`, `cadence 1.5`, a looser pose (`reward_pose 0.3,
k_pose 1`, `reward_ee 0.1, k_ee 5`, `reward_vel 0.05`, `reward_com 0`). Its
speed term measured speed along the pelvis bone's X axis, taken for
"forward". On the raptor that axis points sideways (the heading code only
ever used it as a yaw reference, where the offset cancels). The test rig's
pelvis happens to line up, so the tests passed; there is now a check on the
raptor itself. What the run did, final checkpoint, 32 raptors for 20 s:

| Gait | Asked | Travel along the lane | Step rate | Falls |
|---|---|---|---|---|
| Idle | 0 | 1.75 m/s | 2.4x | 0 / 32 |
| Walk | 1.8 | 3.10 m/s | 2.6x | 0 / 32 |
| Trot | 3.6 | 3.90 m/s | 2.5x | 0 / 32 |
| Run | 6.0 | 5.29 m/s | 2.5x | 0 / 32 |

A trace of one animal on the run clip: about 4.9 m/s along the lane and
6.5 m/s across it at once, about 8 m/s over the ground, both feet more than
12 cm up on about 22 of every 30 steps, joints 32 degrees off the clip, pose
term 0.002 (flat: the clip no longer pulled). Under switching with 2.5 m/s
shoves 2 of 32 episodes fell. So the numbers say nothing about a forward run,
but the body can cover ground at 8 m/s and stay up.

**Run 12 (256x2): follows the speed, loses the animal.** Run 11's settings
with the speed term on the travel axis and `kernel 1`. Final checkpoint,
32 raptors for 20 s:

| Gait | Asked | Forward speed | Falls | Joints off the clip | Step rate |
|---|---|---|---|---|---|
| Idle | 0 | -0.06 m/s | 9 / 33 | 34 deg | 2.7x |
| Walk | 1.8 | 1.92 m/s | 0 / 32 | 31 deg | 2.8x |
| Trot | 3.6 | 3.33 m/s | 0 / 32 | 32 deg | 2.8x |
| Run | 6.0 | 5.58 m/s | 0 / 32 | 33 deg | 2.8x |

Switching gaits 0 / 32; with 1.5, 2.0, 2.5 m/s shoves 0 / 32, 5 / 34, 9 / 37.
Sideways drift in a trace of one animal: under 0.5 m/s, about 1.1 m/s on run.
What was traced about the pose:
- **Not spring lag.** Against the reference 17 to 83 ms earlier the error
  only grows (run: 32.7 deg at no lag, 40.8 at 83 ms).
- **Every joint, not only the legs.** Neck, chest, arms and tail are 30 to
  44 degrees off on every gait, idle included; in the video the body is
  hunched into a ball. Run 10 (plain tracking) is 5 to 17 per joint.
- **The step rate sits at the top of its range** (2.83x) on every gait.
- **Joint power** (sum of |torque x relative angular velocity|): 335 to 600
  W/kg moving, against 50 to 66 for run 10. The numbers are far above an
  animal's (stiff springs chatter), so read them as relative.

Two more keys came from that, off by default:
- `pose_each = 1`: the pose term is the mean over bodies of each joint's own
  term, the end-effector term the mean over end effectors (k_pose and k_ee
  then scale one joint's error: about 10 and 25). Summed inside one term, the
  legs leaving the clip take the whole term to 0 and nothing holds the neck.
- `reward_effort` with `effort_scale`: 1 / (1 + e / effort_scale), e the
  joint power per unit of weight and commanded speed (a cost of transport;
  the log's `effort`). Run 10 sits at 1.1 to 2.8 moving, run 12 at 9 to 22.

**Run 13** (run 12's settings plus the target pose below) was stopped after
six minutes: run 12's result says those settings lose the pose whatever the
policy sees.

**Run 14 (256x2): the same hunched result, so that was not the cause.**
Target pose seen, `reward_speed 0.4`, `pose_each 1` with `reward_pose 0.3,
k_pose 10`, `reward_ee 0.1, k_ee 25`, `reward_effort 0.1, effort_scale 3`,
`kernel 1`, `cadence 1.5`. At 598M steps: joints 33.7 degrees off, step rate
2.77x, cost of transport 37.

**What it was: the actions sat at their clamp.** Measured on the final
checkpoints, mean (noise-free) actions on the walk and run clips:

| Run | Learned noise per axis | Mean offset from the clip | Joint steps at the +-29 degree clamp |
|---|---|---|---|
| 10 (plain tracking) | 5.1 deg | 6.0 deg | 0% |
| 12 | 14.3 deg | 24.0 deg | 68% |
| 14 | 15.0 deg | 25.3 deg | 73% |

In run 12 the spine is at the clamp on 100% of steps, a thigh on 99%, the
forearms on 97 to 99%. The trainer's entropy shows when: it stays near its
start in runs 9 and 10 (98, peak 116, back to 108) and climbs all run long
in 11, 12 and 14 (98 to 143 to 164), with the pose error climbing beside it
(14 to 21 to 26 to 29 to 32 degrees). Even the 26 actions this rig does not
use end with a std of 2.8 (run 10: 1.9): where no reward depends on an
action, its noise grows. An offset is clamped to +-action_scale, and past
the clamp an action changes nothing, so nothing points back. In runs 2 to
10 the sharp pose term (weight 0.65) kept every action inside its range;
the speed-first runs loosened it. So runs 11 to 14 say little about reward
kernels or per-joint terms: all of them were saturated.

`reward_action` (off by default): a term 1 / (1 + m), m the mean squared raw
action in units of its clamp, taken before the clamp, so it slopes back from
anywhere and costs the sampled action, noise included. The log's
`action_size` is m (0.04 at the start of training).

**Run 15 (256x2): the cost was too weak.** Run 14 with `reward_action 0.2`
(`reward_speed 0.35`, `reward_effort 0.05`). From its log: entropy still
climbs (98 to 158), `action_size` grows from 0.04 to about 1.4 (a root mean
square past the clamp), joints 32 degrees off, step rate 1.75x, cost of
transport 66. The policy gives up 0.12 of reward a step to keep its actions
large. A cost of transport of 66 on a 302 kg animal at 2.8 m/s is about
550 kW of joint power: the springs have no torque limit, so slamming every
target from one end of its range to the other is a strong and free motor.
A guess from that, not traced: with speed paid for and the pose loose,
thrashing unlimited actuators beats moving like the clip. (Box3D's
spherical joint spring has no cap; only its motor has `maxMotorTorque`.)

**Run 16 (256x2, live): strict pose, free tempo.** Runs 11 to 15 loosened
the pose, added the speed term and added the step rate all at once. Run 16
is run 10's rewards unchanged (`reward_pose 0.65, k_pose 2`, exp kernel,
which kept every action inside its range) plus only `cadence 1.5` and
`reward_speed 0.3`: the one way to gain speed without leaving the clip is to
play it faster. Not evaluated yet.

## The policy sees the target pose (2026-11-18)

Until here the policy was told which clip (its speed) and where in the
stride (the phase) and had to recall the pose from its weights: a new clip
means retraining. DReCon (Bergamin, Clavet, Holden, Forbes, SIGGRAPH Asia
2019) does it the other way: the game's animation system produces the pose
it wants, that pose is the springs' target, and the policy sees it and adds
balance corrections. No phase; any animation the system can produce is
followed. Their best network is 2 layers of 128, queried at 30 Hz.

The observation now ends with that pose: for every body, where the
reference has it one policy step from now (position from the pelvis and two
rotation columns, laid out like the body block, in the animal's own heading
frame, so target minus body is the correction still to make). LOCO_OBS is
574; checkpoints of runs 2 to 12 still load and act as before.

Other things that reading turned up, not built:
- **The reference should be physically right.** DeepMimic, AMP and DReCon
  all start from motion capture, where the body travels as fast as its legs
  carry it. The raptor's clips are slid at speeds their feet do not produce.
- **Nothing limits effort here.** DReCon clamps each motor at 200 Nm on a
  90 kg human; our springs have no cap and no energy cost (run 11 crossed
  the ground at 8 m/s). Box3D reports a joint's constraint torque
  (`b3Joint_GetConstraintTorque`), which an effort term could use. Fu et al.
  (CoRL 2021) get walk, trot and gallop from a speed command plus an energy
  cost alone.
- **AMP** (Peng et al. 2021) is the published loose guide: a learned critic
  scores whether motion looks like the clips, the task reward sets the
  speed. It cannot leave the speed range of its data.

## The research, briefly

**DeepMimic** (Peng, Abbeel, Levine, van de Panne, SIGGRAPH 2018,
arXiv 1804.02717) is the method used here.

- A physics character is driven by PD controllers. A policy sets their target
  rotations at 30 Hz, and the simulation runs at 1.2 kHz.
- The reward is how closely the body matches a reference clip:
  `0.65 exp(-2 Σ pose²) + 0.1 exp(-0.1 Σ joint-velocity²) + 0.15 exp(-40 Σ end-effector²) + 0.1 exp(-10 |COM|²)`.
  The COM term compares positions. (Ours compares COM velocities and uses
  k = 10 for the end effectors; see the table below.)
- Two tricks matter most:
  - Reference state initialisation: start each episode at a random point of the clip.
  - Early termination: end the episode when the torso or head touches the ground.
- The paper trained a T-Rex (20 links, 55 DoF, 54.5 kg) and a dragon from
  artist keyframes, because no motion capture exists for them.
  - T-Rex walk: 140 M samples.
  - Dragon walk: 139 M samples.
  - Humanoid walk: 61 M samples.
  - A typical humanoid skill took about 60 M samples and 2 days on an 8-core CPU.
- PPO settings: γ = λ = 0.95, clip 0.2, 4096 samples per update, two hidden
  layers of 1024 and 512 units.
- The paper says nothing specific about the T-Rex's tail.

**AMP** (Peng et al., SIGGRAPH 2021, arXiv 2104.02180) replaces the
hand-made tracking reward with a learned discriminator.

- The discriminator scores state transitions (not actions) as "from the clips"
  or "from the policy", using a least-squares GAN loss.
- The style reward is `max(0, 1 - 0.25 (D - 1)²)`. It is added 50/50 to a task
  reward such as "go this way at this speed".
- A gradient penalty (weight 10) is the part the authors call most vital.
- No clip is followed frame by frame, so one policy can blend walk, trot, run
  and turns from a pile of clips.
- Training took 100-300 M samples. The paper also trained a 59-DoF T-Rex walk.

**ASE** (Peng et al., SIGGRAPH 2022, arXiv 2205.01906) goes one step further.

- It pre-trains a low-level controller on a latent "skill" space, combining an
  AMP-style reward with skill discovery. It learned from 187 unlabelled clips.
- High-level task policies then steer the latent.
- Scale: over 10 billion samples, about 10 days on one V100 with Isaac Gym.
  This is a later-stage idea, not stage 2.

**ARC Raiders (Embark).** What I could confirm, from interviews only:

- A design director said their larger multi-legged drones walk with
  reinforcement learning (PCGamesN, 2025-11-03).
- Embark's ML research lead said:
  - Their ML is "strictly for locomotion".
  - "For legged robots, we haven't found any other way that works."
  - They use adversarial motion priors (AMP).
  - Behaviour trees and utility AI make the decisions.
  - Training randomises object placement.
  - Non-physical helper forces can stop robots from falling.
  - The team was 5-10 people over several years.

  (80.lv, 2026-03-31.)
- There was a GDC 2026 talk, "Learning to Move: Physics-Based Enemy Locomotion
  in ARC Raiders". I could not read its page.
- Not confirmed anywhere I could read: their physics engine, their RL
  algorithm (PPO is my guess), and whether policies run live on server or
  client. The Unreal deployment mention suggests live, but that is a guess.

So "Arc Raiders did it" holds, and the method they name is AMP, which is our
stage 2. Their robots are physics-driven enemies whose decisions come from
ordinary game AI. That split, physics policy for the legs and game AI for the
brain, is the one recommended for stage 3 below.

**Tails.**

- Persons and Currie (Anatomical Record, 2011) found the T. rex tail held a
  huge leg-retracting muscle, the M. caudofemoralis, that powered the stride.
- Libby et al. (Nature, 2012) showed lizards swing their tails to control body
  pitch in a leap. A robot with an active tail did the same, and a
  Velociraptor model's tail was more effective still.

So a theropod tail is both a counterweight and part of the motor.

Body mass: G. S. Paul's volumetric table gives Deinonychus specimens of about
40-80 kg and Velociraptor about 8-28 kg. Our raptor model is scaled to 3.0 m,
which is Deinonychus-sized.

## Your three questions

**What does walking well look like?** Our own clips. The reward is DeepMimic's
imitation reward against the walk clip, so "walking well" means moving the way
the animator made it move, under real gravity, contacts and joint limits. The
four terms, as built:

| term | what it compares | weight | scale k in exp(-k·error) |
|---|---|---|---|
| pose | every joint's rotation (child relative to parent), plus the pelvis's world rotation; squared angles summed | 0.65 | 2 |
| velocity | every joint's relative angular velocity, plus the pelvis's | 0.10 | 0.1 |
| end effectors | the tips of the feet, the head and the tail, relative to the pelvis in its heading frame, height absolute (m²) | 0.15 | 10 |
| centre of mass | velocity, including the clip's forward speed ((m/s)²) | 0.10 | 10 |

I kept DeepMimic's weights and its pose and velocity scales. They are
`[env]` keys, so tuning them needs no code. Two terms changed after run 2,
whose walk earned almost nothing from them. The COM term compared positions,
so run 2's slightly fast walk (29 m to the clip's 27 m) had drifted metres
ahead and scored 0 on it within seconds; a velocity cannot drift. DeepMimic's
k = 40 for the end effectors suits a 1.7 m human; on the 3 m raptor, whose
head and tail tip sit far from the pelvis, run 2's 19 cm of error scored
0.003, which gives no gradient, so k is 10. A perfect track scores exactly 1; the test checks this. The
clips are in place, so the reference walks forward at the clip's own speed
(`DinoClip.speed`, the one `dino.c` measures from the feet), and the COM term
charges an animal that walks in place, too slowly or too fast.

**How do we get the tail?** The tail is part of the body: 4 or 5 rigid
segments (the raptor's ten tail bones merge in pairs) on ball joints. They are
driven and tracked like every other joint, and the tail tip is an end
effector. Each segment's mass comes from the volume of the mesh skinned to it,
so the tail genuinely balances the head and chest. On the stand-in it is 12%
of the body (8.8 kg); `loco_play --rig` prints the real raptor's share.

For the experiment you asked about, set `passive_tail = 1` (or
`loco_play --passive-tail`). The tail then gets no actions: its springs pull it
toward its mean pose at 20 Hz with light damping (0.4), it swings under its
own inertia, and it drops out of the reward. The first data point is
interesting:

- Plain PD tracking stays up longer with the passive tail: 0.92 s vs 0.79 s on
  the stand-in raptor, 0.98 s vs 0.94 s on the fixture.
- A soft tail seems to absorb the body's errors instead of fighting them.

The real test is training both and comparing.

**State diversity.**

- Every episode starts at a random phase of the clip, in the clip's exact pose
  and velocities (reference state initialisation). So the policy sees the
  whole stride from step one, not just its first second.
- Any body touching the ground other than the feet, or the pelvis dropping
  below half its clip height, ends the episode (early termination). So no time
  is wasted learning to wriggle on the ground.
- DeepMimic found early termination essential for most skills. RSI made
  little difference for walking but mattered for anything with flight phases
  (a trot or a run).
- Stage 2 adds random shoves, slopes and the island's terrain on top.

## The body (stage 1 as built)

`loco/loco_rig.h` builds the ragdoll from any `.dino` plus a bone-group table:

1. Each table group (for example `tail_2` = `Tail_3, Tail_4`) becomes one
   rigid body.
2. Bones not in the table go to their nearest named ancestor (fingers, toes,
   jaw, tongue).
3. Groups without mesh merge into their parent.
4. Joints the clips never move by more than 2° are welded (see "What could go
   wrong").
5. Each group's shape is fitted to the vertices skinned to it:
   - a capsule along the long axis, with the mesh's cross-section area;
   - a box for the feet, so they stand flat.
6. Mass is the shape's volume at 1000 kg/m³.
7. Each body's pose in any frame is exactly its bone's skinning matrix, so the
   reference motion comes straight from the clip.
8. Joints are Box3D ball joints (cone and twist limits). Knees, ankles and
   toes become hinges when 85% of their clip motion is about one axis.
9. Limits are the range the walk, trot, run and idle clips use, plus 14° each
   side.

Two tables exist so far:

- **`raptor`**: the licensed model, with bone names as in
  `tools/dino_retarget.py`. It should give 23 bodies:
  - pelvis, spine, chest, two neck segments, head;
  - five tail segments;
  - thigh, shin, metatarsus and foot for each leg;
  - upper arm and forearm for each side.

  It is untested because the file is not here; the test checks it builds
  whenever the file exists.
- **`testrig`**: the stand-ins.

The other five species need a table each (about 25 lines).

The stand-in numbers (`loco_play --rig`):

| rig | bodies | joints | actions | mass | tail | notes |
|---|---|---|---|---|---|---|
| stand-in raptor (dino_testrig) | 9 | 8 | 20 | 73.4 kg | 8.8 kg, 4 segments | its clips hold spine, neck, head and toes rigid, so those weld into the pelvis |
| same, every joint kept (`--no-weld`) | 15 | 14 | 34 | 73.4 kg | 8.8 kg | |
| test fixture | 10 | 9 | 19 | 97.6 kg | 17.6 kg, 3 segments | |

73 kg against a real Deinonychus's 60-80 kg is a coincidence of the stand-in's
box sizes. The real mesh will give its own number; check it with `--rig`.

## Physics and stability

- One Box3D world per env, holding 64 ragdolls on one flat ground.
- Every body collides only with the ground, never with another ragdoll or
  itself.
- Physics runs at 120 Hz with 4 substeps (480 Hz solver). The policy runs at
  30 Hz.
- Each joint's spring is the PD controller.
  - Its target rotation is the clip's pose at this instant plus the policy's
    offset.
  - The target is updated every physics step, so the clip is followed
    smoothly between actions.
  - The target is clamped inside the joint's limits.
- Gains:
  - legs 100 Hz, body 60 Hz, tail 40 Hz, damping ratio 1;
  - joints as stiff as the substep rate allows.

Stability checks (`loco_play --stability`, also in the test suite):

1. Pinned: the clip's first pose, held with the pelvis pinned 1 m up for 3 s.
2. Free: the same pose standing free on the ground.
3. Dropped: the same pose dropped from 0.5 m.

| rig, substeps | pinned: drift, separation | free: stood, separation | dropped: separation, fastest body | cost per ragdoll step |
|---|---|---|---|---|
| stand-in, 4 | 0.28° mean (0.8° worst), 0.03 cm | 1.15 s, 0.48 cm | 1.9 cm, 10 m/s | 13.5 µs |
| stand-in, 8 | 0.28°, 0.01 cm | 1.17 s, 0.22 cm | 1.6 cm, 11 m/s | 22.6 µs |
| stand-in 15 bodies, 4 | 0.29° (0.8°), 0.04 cm | 1.03 s, 0.89 cm | 2.3 cm, 10 m/s | 20.9 µs |
| fixture, 4 | 0.24° (1.0°), 0.89 cm | 1.40 s, 0.85 cm | 2.0 cm, 7 m/s | 21.3 µs |
| fixture, 1 | 0.21°, 1.8 cm | 0.22 s, 7.4 cm (fails) | 9.8 cm (fails) | 9.3 µs |

- One substep fails, 2 is borderline, and 4 passes everything. So 4 is the
  default. Raise it if the real raptor's heavier pelvis or lighter toes need
  it.
- "Stood" only says how long the pose stays up open-loop. Nothing balances it,
  so it is expected to tip over.
- Box3D holds these chains at a usable speed, so MuJoCo was not needed.

## Throughput

`loco_play --bench`, one core, 32 ragdolls per world, random actions,
including observations:

- stand-in raptor, 9 bodies: **18,300 agent steps/s**;
- fixture, 10 bodies: 16,100;
- stand-in with all 15 bodies: 10,700.

Cost grows roughly with body count, so the real 23-body raptor should land
near 7,000 agent steps/s per core. That is my estimate, not a measurement. On
8 threads that is about 55,000 steps/s, so 300 M steps is roughly 1.5 hours of
env time. The trainer's GPU side should not be the bottleneck.

## PD-tracking baseline (no learning)

`loco_play --baseline`: actions zero, so the springs simply chase the clip;
32 animals for 20 s.

| rig | mean time up | tracking error | reward per step |
|---|---|---|---|
| stand-in raptor | 0.79 s | 8.5° per joint, end effectors 40 cm | 0.55 |
| same, passive tail | 0.92 s | 8.1°, 30 cm | 0.57 |
| fixture | 0.94 s | 7.5°, 35 cm | 0.56 |

Every episode ends in a fall within about a second. That is the expected
starting point: tracking a clip open-loop cannot balance a biped, which is
exactly what the policy learns. The end-effector error is large because it
is averaged over episodes that are mostly falling.

## Training

On the GPU box, from the repository root:

```
ocean/cretaceous/loco/train.sh                     # = build in loco mode, then train
ocean/cretaceous/loco/train.sh --print             # just show the two commands
ocean/cretaceous/loco/train.sh train --train.total_timesteps=5e7 --env.passive_tail=1
```

How it fits the trainer without touching it:

- The trainer fixes observation and action sizes at compile time. Loco mode is
  therefore a build flag passed through build.sh's own `NVCC_EXTRA`.
  `cretaceous.h` then includes `loco/loco_binding.h` instead of the game's
  binding.
- The trainer reads `config/<env name>.ini` and rejects any override whose
  key that file lacks, and the env name is a build macro. The same
  `NVCC_EXTRA` renames it to `cretaceous_loco`, so the trainer reads
  `config/default.ini` then `config/cretaceous_loco.ini` (never the game's
  `config/cretaceous.ini`) and logs its runs under that name. Your own
  `--section.key=value` arguments work as usual.
- The binary is `build/cretaceous/puffer_loco`, so it never clobbers the
  game's `./puffer`.
- Keep `policy.cretaceous_net = 0`: the game's structured encoder only reads
  game observations.

The interface the policy sees:

- Observation: 364 floats.
  - Per body: position and 6D rotation relative to the pelvis heading frame,
    centre-of-mass velocity and angular velocity, padded to 24 bodies.
  - Then pelvis height, phase (sin, cos) and target speed.
- Action: 69 continuous heads.
  - Each joint's offset from the clip's pose: 3 per ball joint, 1 per hinge.
  - `action_unit` (0.1) radians per unit, clamped to ±`action_scale` (0.5)
    rad. The trainer starts every action's noise at std 1, so the unit sets
    the starting exploration: 0.1 rad, near DeepMimic's fixed 0.05. (At 0.5
    rad per unit, the first smoke run shook every joint by about 29°.)
  - A rig uses the first `ndof` of them and the rest are ignored.

Recommended settings (already in the ini):

- PPO with γ 0.99, λ 0.99, learning rate 0.0087, momentum 0.884, clip 0.32:
  `config/drone.ini`'s swept values. The first run used DeepMimic's γ/λ 0.95 and
  Adam's 3e-4, and learned nothing in 300M steps; PufferLib 5.0's optimizer
  wants learning rates near 1e-2.
- Horizon 64, minibatch 32768.
- Entropy 0 (continuous actions carry their own learned std).
- 64×2 hidden, fixed (far smaller than DeepMimic's 1024 and 512; cheap to
  run in the game later).
- 4096 agents.
- 3e8 steps.

The minibatch size follows the repo's `robot_arm.ini`; the optimizer
settings follow `drone.ini`. The sample budget follows DeepMimic, where the
T-Rex walk took 140 M.

What to expect, if it behaves like DeepMimic:

- `env/episode_length` climbs from about 1 s toward the 15 s cap.
- `env/perf` (share of episodes ending without a fall) rises from 0 toward 1.
- `env/pose_deg` falls toward a few degrees.
- The paper's walks reached a normalised return of about 0.98.

If `episode_length` is still near 1 s after 5e7 steps, see below.

Not done here:

- The trainer builds (2026-11-16, on Felix's machine) and trains: a smoke
  run on the real raptor did two epochs with finite losses and the env's
  logs. It ran at only about 1,000 steps/s. The GTX 1060 has 3 GB, which was
  full with another job on it, and the 4 cores were shared. A real run needs
  a box with many cores, because physics, not the GPU, sets the pace.
- `loco/loco_policy.h` (the CPU runner behind `--policy` in loco_play and
  loco_view) follows the documented checkpoint layout. It is tested only on
  hand-made files. Compare it with `./puffer eval` on the first real
  checkpoint.

## What could go wrong

- **Box3D's limits are soft.** A light body hinged to a heavy one can be
  forced through its limit.
  - The stand-in's 0.8 kg toes, which the clips never move, flipped up to 90°
    under a 98 kg body in stance.
  - Fixes in place:
    - joints the clips do not use are welded (`weld_range`);
    - PD targets never point outside a limit;
    - joints are as stiff as the substeps allow.
  - Under random full-scale actions on the fixture, 0.3% of joint samples
    still exceed a limit by over 10° (worst 31°).
  - If the real raptor's toes misbehave, raise `substeps` to 8 or give the
    feet a mass floor.
- **No torque cap.** Box3D's joint spring has no maximum torque (its motor
  does). A policy can therefore ask for any torque, which can look snappy or
  jittery.
  - If it does, lower the leg gains, or add an action-rate penalty.
  - Or drive joints with the capped motor (a velocity servo toward the target)
    instead of the spring. That is about 20 lines in `loco_sim.h`.
- **Jitter.** 30 Hz targets can chatter. A small penalty on action change, or
  a low-pass filter on actions, is the usual fix.
- **Foot sliding.** The reference walks at `DinoClip.speed`. If that does not
  match the feet, the COM term pulls the body forward while the feet plant
  slower, so the animal shuffles.
  - Check the clip with `tools/animcheck.c` first; its foot-slide figure is
    the number to look at.
  - The stand-in's procedural clip slides its stance feet by up to 0.6 m/s.
    The real clips should be better.
- **Falling at the phase wrap.** If the clip's last frame and first frame do
  not quite match, the reference jumps. `animcheck` reports loop seams.
- **Tail whipping.** Light end segments with stiff springs can whip; the tail
  tip hit 10 m/s in the drop test. The passive tail is softer.
  - If the tail tip flails, lower `hertz_tail`. Or weight the tail less in the
    reward (it is part of the pose sum like any joint).
- **Exploits.** Early termination only catches non-foot contact. If policies
  learn to kneel on the metatarsals (allowed contacts), tighten `fall_height`.

## Stage 2 (not built): one policy, many gaits, any ground

- **AMP instead of per-clip tracking.**
  - A discriminator learns from all of a species' clips (walk, trot, run,
    turns, idles) and gives the style reward.
  - A task reward gives the speed and heading the game asks for, so one policy
    walks, trots, runs, turns and blends between them.
  - This is what Embark says they use.
  - It needs a discriminator trained alongside PPO, which is a core change to
    the trainer: a second network, a replay of policy transitions, and the
    style reward added to the env reward each step. That is your call. The env
    side is ready for it: it already exposes the state the discriminator would
    see.
- **Robustness:**
  - random shoves (impulses on random bodies);
  - slopes, then the island's heightfield (a Box3D heightfield shape);
  - varied speed commands;
  - getting up after a fall: start some episodes lying down, and drop early
    termination for them.
- **All six species,** one table each. The quadrupeds (stego, sauropelta)
  should be easier to balance and harder to make look right.

## Stage 3: how it reaches the game

Option 1 is built for hit reactions and the walk ("New clips from physics"
above); options 2 and 3 are not.

1. **Bake new clips offline.**
   - Run trained policies on slopes, turns and stumbles, record them, and
     convert them into `.dino` clips the current animation system plays.
   - Zero runtime cost and no determinism risk.
   - But it covers only the situations baked, and the animals still slide on
     terrain the clips never saw.
2. **An active ragdoll on the client only.**
   - For animals near the camera, and for knockdowns and deaths, a policy
     drives a client-side Box3D ragdoll that follows the server's kinematic
     mover.
   - The server sim stays kinematic and deterministic, and the hashes do not
     change.
   - Cost (estimate): the trainer's net at 64×2 (linear encoder, two MinGRU
     layers, decoder) is about 60 K multiply-adds per animal per step, so
     1.8 M/s per animal at 30 Hz. A dozen nearby animals come to about 22 M
     multiply-adds per second: well under a millisecond per frame. Add about 20-40 µs of physics per animal step.
   - Risk: the ragdoll drifts from the server's position. It has to be
     tethered, with a soft pull on the pelvis toward the mover; Embark
     mentions helper forces like this.
3. **Physics-driven animals in the sim.**
   - The real thing, and what ARC Raiders appears to do for its legged enemies.
   - But it changes the server sim, its determinism, its cost (every animal,
     every tick) and the netcode (state to replicate grows from a capsule to a
     body).

**My recommendation.**

- Start with option 2 for knockdowns, deaths and the closest few animals,
  where scripted animation looks worst and physics looks best.
- Use option 1 to fill the clip library meanwhile.
- Leave option 3 until the client ragdoll has shown the policies are robust.

## Open questions for you

1. The real raptor comes to 302 kg against the game's 80 kg adult and a real
   Deinonychus's 60-80 kg: its capsules and foot boxes are fatter than the
   mesh (the feet are 0.30 m wide boxes of 18 kg each). Uniformly lighter
   bodies move the same under frequency-tuned springs, so this matters only
   once torque caps or shoves are absolute. Fix the shapes, or add a target
   mass to scale to?
2. Do you want the stage 2 discriminator in the trainer (a core change), or
   should stage 2 start with more tracking work (more clips, shoves, terrain)?
3. Spring (uncapped, smoother) or capped motor (torque-limited, more
   physical) for the PD controllers? This is worth an A/B in the first real
   training runs, alongside the passive-tail comparison.
