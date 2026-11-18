# Teaching the dinosaurs to walk

This is stage 1 of physics-based locomotion for Cretaceous: an environment in
which a ragdoll raptor, built from our own rig, learns to walk by imitating
its walk clip. It plugs into the PufferLib trainer without changing it. It
has not been trained yet (this sandbox has no GPU); everything below about
learned walking is the plan and what the papers report, not a result.

- Code: `ocean/cretaceous/loco/` (header-only), `tools/loco_play.c`,
  `tools/loco_view.c`, `tests/test_loco.c`, `config/cretaceous_loco.ini`.
- The game itself is untouched: the sim, its hashes and the determinism suite
  are the same. Loco mode is a separate build of the binding.

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

## The research, briefly

**DeepMimic** (Peng, Abbeel, Levine, van de Panne, SIGGRAPH 2018,
arXiv 1804.02717) is the method used here.

- A physics character is driven by PD controllers. A policy sets their target
  rotations at 30 Hz, and the simulation runs at 1.2 kHz.
- The reward is how closely the body matches a reference clip:
  `0.65 exp(-2 Σ pose²) + 0.1 exp(-0.1 Σ joint-velocity²) + 0.15 exp(-40 Σ end-effector²) + 0.1 exp(-10 |COM|²)`.
  The COM term compares positions.
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
| end effectors | the tips of the feet, the head and the tail, relative to the pelvis in its heading frame, height absolute (m²) | 0.15 | 40 |
| centre of mass | position, including the forward travel of the clip (m²) | 0.10 | 10 |

I kept DeepMimic's weights and scales. They are `[env]` keys, so tuning them
needs no code. A perfect track scores exactly 1; the test checks this. The
clips are in place, so the reference walks forward at the clip's own speed
(`DinoClip.speed`, the one `dino.c` measures from the feet), and the COM term
charges an animal that walks in place or falls behind.

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

## Stage 3 (not built): how it reaches the game

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
