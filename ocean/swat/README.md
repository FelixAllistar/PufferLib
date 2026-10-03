# SWAT: Gold Element

A standalone tactical game within Ocean, with environment/config name `swat`.
The first playable is a small training annex for developing the character,
weapons, destructible cover, and shared human/policy simulation. The longer-term
game is a cooperative tactical shooter with trained RL actors; see the
[development roadmap](ROADMAP.md).

This is an early graybox foundation. The armed guard currently uses a visible-target
script and the civilian is stationary. Native training and checkpoint playback
work, but no capable trained opponent or squad policy ships with this commit.
This scenario tests threat removal and civilian protection; a full policing,
surrender, arrest, and rules-of-engagement system is still to come.

## Play

Run commands from the repository root. With Box3D and Raylib installed:

```sh
make -C ocean/swat viewer
./build/swat/swat play
```

The standard build entry point also creates the game:

```sh
bash build.sh swat --cpu
./swat play
```

Open or destroy the wooden door, deal with the armed guard in the far room,
keep the blue civilian unharmed, and reach the gold extraction circle. It turns
green when the threat is down. The default annex has a 30-second limit, shared
with training. For controller/weapon practice with a longer limit:

```sh
./swat play --env.hostile_fire=0 --env.max_ticks=7200 --env.randomize=0
```

| Control | Action |
| --- | --- |
| WASD / mouse | Move / look |
| Q / E | Hold left / right lean |
| Ctrl or C | Hold crouch; standing waits for clearance |
| Shift / Alt | Sprint / slow walk |
| Space | Jump; release before jumping again |
| Right / left mouse | Aim / fire |
| R | Reload |
| 1 / 2 | Carbine / sidearm |
| V | Cycle selector; carbine starts in semi, then auto, then safe |
| F | Toggle the door while looking at it within 2.2 m |
| Backspace / Tab / Esc | Restart / release or capture cursor / quit |

The graybox weapon model, actors, and HUD use procedural Raylib geometry.
No assets from Ready or Not or SWAT 4 are included.

## Implemented systems

- **Character:** dynamic Box3D body, separate feet and upper capsule, normalized
  movement, stair/ground handling, crouch clearance, grounded jump, pitch limits,
  walk/slow/sprint, stamina, and movement/aim/fire gates. Lean shifts the actual
  upper collider and eye by up to 0.42 m; it is swept against cover. Standing
  checks the leaned head as well as the feet hull. Aim, roll, and sensor rays
  follow the achieved pose.
- **Weapons:** carbine and sidearm, chamber plus magazine/reserve counts,
  semi/auto/safe selectors, timed tactical/empty reloads, reload cancellation on
  swap, equip delay, recoil, movement/air/aim spread, and deterministic actor-local
  weapon RNG. Reserve ammunition is pooled; individual spare magazines and
  staged reload animation are not modeled yet.
- **Hits and cover:** eye-to-muzzle volume check, muzzle-origin hitscan, nearest
  collision damage, bounded thickness/material penetration, head damage bonus,
  breakable wood/glass and independent drywall cells. Destroyed objects lose
  their physics colliders and disappear from sensors immediately. This is
  modular destruction, not structural fracture or simulated debris.
- **Doors:** an authored hinged, damageable door with real collision. Its short
  swept rotation stops for actors and resumes once they clear it. It currently
  moves under game control rather than a motorized rigid-body hinge.
- **Mission:** armed target, protected civilian, clear-and-extract success,
  injury/death, civilian-harm failure, fall/timeout, restart, and episode metrics.
  Small layout and guard-position variations are seeded at reset.

The carbine has a 30-round magazine plus chamber, 90 reserve rounds, a 6-tick
fire interval, 120/156-tick tactical/empty reload, and 24-tick equip delay.
The sidearm uses 15 plus chamber, 45 reserve, 10-tick fire interval,
90/120-tick reload, and 18-tick equip delay. These are fictional game tuning.

## Shared simulation

| File | Responsibility |
| --- | --- |
| `body.c`, `body.h` | Local low-level character fork and collision queries |
| `controller.c`, `controller.h` | Tactical input, stance/lean/aim, movement gates, recoil |
| `weapons.c`, `weapons.h` | Weapon state and shot requests |
| `world.c`, `world.h` | Box3D scene, queries, material damage, doors |
| `sim.c`, `sim.h` | Actors, fixed update, ballistics, mission, observations/actions |
| `swat.h` | Ocean adapter, rewards, logging, automatic reset |
| `render.c`, `swat.c` | Game presentation, human input, CPU policy playback/evaluation |

Both humans and policies submit `SwatInput` to the same **60 Hz** game update
with **four Box3D substeps**. Rendering does not own physics or weapon state.
`swat_sim_step_inputs` accepts per-actor input for future policy-controlled
suspects and teammates. The initial Ocean adapter exposes only the officer.

The simulation is heap-owned because native vector setup may relocate `Env`;
Box3D actor/object metadata must keep stable addresses. World create/destroy
operations are serialized for the Box3D world registry; separate worlds step
on the vector workers. Headless evaluation opens no graphics window. The
adapter binary still links Raylib for its optional render hook.

The character ancestry and dependency revision are recorded in
[PROVENANCE.md](PROVENANCE.md). SWAT owns its controller fork and does not import
Shenaniguns gameplay code, assets, checkpoints, observations, or tasks.

## Dependencies

Use the existing sibling Box3D checkout if it is already at this revision.
For a fresh dependency checkout:

```sh
git clone https://github.com/FelixAllistar/box3d.git ../box3d
git -C ../box3d checkout --detach c4a414fcfe612a704dcd06ce921348d441271fc7
cmake -S ../box3d -B ../box3d/build -DCMAKE_BUILD_TYPE=Release \
  -DBOX3D_SAMPLES=OFF -DBOX3D_UNIT_TESTS=OFF
cmake --build ../box3d/build --parallel 2
```

`build.sh` uses the repository's Raylib 5.5 download/setup path. The local
Makefile expects `raylib-5.5_linux_amd64` and Clang on Linux; override `RAYLIB`,
`BOX3D`, or `CC` when needed. `build.sh` accepts `BOX3D_DIR` for a different
physics checkout. CUDA and the repository's native trainer dependencies are
needed for training, not for the standalone player or core simulation tests.

## RL contract and training

[CONTRACT.md](CONTRACT.md) defines the initial versioned interface: **167 float
observations**, **14 discrete action heads**, and **39 total logits**. The
observation combines character/weapon state, mission telemetry, and 45 forward
visibility rays. It does not include hidden enemy positions. Visible ray classes
are semantic labels, not rendered RGB; human and policy physics are shared,
while their observation modalities differ.

The baseline is the standard linear encoder and recurrent MinGRU policy with
width 64 and two layers. SWAT does not use the Shenaniguns spatial encoder.
Run training and evaluate/play back its FP32 checkpoint with matching settings:

```sh
mkdir -p build/swat
bash build.sh swat build/swat/puffer --float
./build/swat/puffer train
./swat --eval /path/to/checkpoint.bin 32 --deterministic
./swat watch /path/to/checkpoint.bin --deterministic
```

Configuration lives in `config/swat.ini` and inherits `config/default.ini`.
`--section.key=value` overrides work for both the player and trainer. Use the
checkpoint's matching architecture and scenario settings. The viewer checks
weight count against the architecture; the raw native checkpoint does not
embed a SWAT contract identifier. Keep config/contract metadata with checkpoints.
The default 64x2 policy has 37,824 FP32 parameters.

The environment is CPU Box3D, with CUDA policy training. A separate CUDA
environment, BF16 qualification, self-play league, multi-agent adapter, and
production learning benchmarks are not implemented in this foundation.

## Verification

```sh
make -C ocean/swat test
make -C ocean/swat sanitize
./swat --capture build/swat/first-playable.png --env.randomize=0 --env.hostile_fire=0
```

Checks cover movement speed and gates, jump edges, crouch/lean collision and
combined stand clearance, ammunition conservation and cadence, reload/swap,
muzzle obstruction, cover removal and penetration, door obstruction, and guard
visibility/reaction. A test-only driver completes eight randomized missions with
enemy fire enabled using ordinary movement, interaction, and weapon inputs.
It has known waypoints/target poses; it is a solvability check, not learned AI.

Adapter checks cover 2,048 paired seeded transitions, 33 paired resets,
finite observations, relocated environment storage, exact timeouts, civilian
penalty, extraction reward, and preservation of terminal reward across reset.
ASan/UBSan instrument the game/controller and adapter, not the separately built
Box3D archive. Offscreen rendering has also been captured and visually inspected.

A native FP32 smoke run completed 2,048 transitions with finite losses and
saved checkpoints; loading those weights into another 2,048-step training run
also passed. CPU deterministic evaluation loaded the result successfully;
the tiny smoke policy solved 0/4 short episodes. This verifies execution and
checkpoint compatibility, not policy quality. Reproduce a short run with:

```sh
./build/swat/puffer train --vec.total_agents=16 --vec.num_threads=2 \
  --train.horizon=16 --train.minibatch_size=128 --train.total_timesteps=2048 \
  --env.max_ticks=32 --env.hostile_fire=0 --base.run_id=smoke \
  --base.checkpoint_dir=build/swat/checkpoints --base.log_dir=build/swat/logs \
  --base.checkpoint_interval=1
./swat --eval build/swat/checkpoints/swat/smoke/0000000000002048.bin 4 \
  --deterministic --env.max_ticks=32 --env.hostile_fire=0
```
