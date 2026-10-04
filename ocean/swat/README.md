# SWAT: Gold Element

A standalone tactical game within Ocean, with environment/config name `swat`.
Human play defaults to Cedar House: a small hostage mission with planning,
equipment, arrests and layered construction. Planning also offers generated
residences, two controllable snipers, and local house comparisons. The training annex remains the
stable policy/test environment. The longer-term
game is a cooperative tactical shooter with trained RL actors; see the
[development roadmap](ROADMAP.md).

This is an early graybox foundation. Armed guards use scripted sight and
hearing reactions, and civilians stay in place and can comply. Native training and checkpoint playback
work, but no capable trained opponent or squad policy ships with this commit.
The house generator does ship a small trained neural policy; it is separate
from character AI and has learned authored layout scores, not player enjoyment.
Surrender, restraint and less-lethal tools work; full policing, suspect behavior,
evidence and rules-of-engagement systems still need development.

## Play

Run commands from the repository root. With Box3D and Raylib installed:

```sh
make -C ocean/swat viewer
./swat play
```

The standard build entry point also creates the game and launcher:

```sh
bash build.sh swat --cpu
./swat play
```

`./swat` opens a main menu with solo play, **Settings**, **Host co-op**,
**Join co-op**, and **Quit game**. **Plan the mission** opens Briefing, Loadout,
Snipers and Houses tabs; **Deploy** captures the mouse. **Escape** or **Tab** opens
the pause menu, releases the cursor, and freezes the solo simulation. Resume captures
it again; losing window focus automatically pauses. The pause menu also offers
restart, settings, return to the main menu, and quit. Menu clicks and capture
warps are discarded before accepting gameplay input.

On WSL, the launcher uses the **native Windows player** with the same Raylib
and Box3D code. This avoids the WSLg/RDP pointer path implicated in
[reported mouse lock and relative-motion problems](https://github.com/microsoft/wslg/issues/240).
The first Windows build is automatic if needed, or can be run explicitly:

```sh
bash ocean/swat/build-windows.sh
./ocean/swat/play.sh play
```

The script uses an installed MinGW cross-compiler or downloads and unpacks
Ubuntu/Debian tool packages under `build/swat/windows-deps`, without sudo or
system installation. It fetches the official Raylib 5.5 Win64 archive and
builds Box3D into a separate Windows build directory. The player and a
`Play SWAT.cmd` launcher live in `build/swat/windows`. Only generated files
are placed there. On non-Debian Linux, install MinGW-w64 C/C++ first.

To explicitly run the Linux player, use `SWAT_NATIVE_WINDOWS=0 ./swat play`
or `./build/swat/swat play`. CUDA training continues to run on Linux. The
launcher rebuilds stale player code and translates checkpoint/settings/capture/layout-model
paths from WSL for the Windows process.

At Cedar House, enter through doors or create openings, secure two suspects,
order the three civilians to comply, cuff them, and bring all surviving officers
back to staging. Any civilian harm fails the mission. Human play defaults to
five minutes. **P** opens planning: orbit the roof cutaway with A/D or inspect
three authored overwatch viewpoints. The overview shows geometry, not hidden
actor positions; optical previews show only what the camera can see through
the actual building. The overview is a planning camera; a piloted drone remains future work.

In **P > Snipers**, select A or B, choose an unoccupied post and a Precision or
Marksman rifle, then assign. **Take scope control** deploys queued assignments.
In the scope, mouse aims, **Y** marks an optically visible armed target, **Space**
orders both snipers to execute their marked shots, **H** clears marks, and
**LMB** fires the controlled rifle. **1/2** switches snipers; **Esc** returns to
your officer. From the officer, **X** executes and **H** clears. Snipers hold
fire until ordered; opaque cover and friendly/compliant people block a marked
shot. Wait for **TARGET READY**: the rifle must finish equipping and steady its
aim before a shot can fire. Glass uses the shared bullet penetration rules. Repositioning takes
three seconds and preserves wounds/ammunition; it currently moves the actor
to the new post after that delay. Your officer remains in the world while you
control a sniper, and online planning does not pause the mission.

Choose a kit before leaving staging, firing or using a consumable. Recon carries a carbine/optiwand
at full pace; Control trades some mobility for an impact launcher, optiwand and
light torso protection; Entry has a ram and stronger torso protection at a
larger movement cost. Control and Entry have gas masks. All have a sidearm and cuffs. The impact launcher forces
NPC surrender on a hit, but still causes injury and does not penetrate cover.
Y makes visible civilians comply; healthy armed suspects generally need to be
stunned or wounded first. Hold F within 1.7 m while aiming at a compliant person
for 1.2 seconds to restrain them. Release or lose the target to interrupt.

Hold **G** near a closed door: the officer crouches and inserts the lens through
the floor gap automatically. Mouse movement rotates the lens without moving its
stem. While holding G, **Ctrl/C** selects under-door, **Q/E** reaches around a
left/right corner, and **Space** reaches over cover. The HUD shows the selected
mode and whether reach is blocked. The lens sweeps against collision; movement
and firing are locked during inspection. Release G to return to normal controls.
Use B for a butt strike or the Entry kit's stronger ram hit.

**4** throws a flashbang and **5** throws CS gas. Canisters are dynamic Box3D
bodies with gravity, swept collision and material-dependent bounce/friction.
A 1.5-second fuse starts when thrown; holding a key throws once. Flash exposure
depends on distance, facing and cover. Gas grows for twelve seconds, respects
geometry, slows unmasked officers and can force NPC compliance. **T** fires the
limited-charge taser within 7 m through a checked short trace. It stops at cover,
stuns and can force surrender; it does not simulate a cable or flying darts yet.
These are fictional, non-damaging game effects; cuffs remain necessary.

| Kit | Flashbangs | CS canisters | Taser charges | Gas mask |
| --- | ---: | ---: | ---: | --- |
| Recon | 1 | 1 | 2 | No |
| Control | 1 | 2 | 3 | Yes |
| Entry | 2 | 1 | 0 | Yes |

In **P > Houses**, choose a seed, difficulty and Learned/Random generator, then
**Build this seed** or **Next house**. Accepted layouts have connected rooms,
clear door frames, reachable occupants and a checked collider budget. The three
arrangements have 3–5 rooms; difficulty places 1–3 suspects and three hostages.
After playing two different houses, the comparison buttons save your explicit
choice locally beside settings as `settings.ini.layouts.jsonl`. Nothing is
uploaded, and votes do not retrain the running game automatically. See
[generation, model training and player preferences](GENERATION.md).

```sh
./swat play --mission generated --layout-seed 42 --difficulty 1
./swat play --mission generated --generator uniform
./swat play --mission generated --layout-model build/swat/layout-experiment/policy.txt
```

To revisit the annex or practice without hostile fire:

```sh
./swat play --env.hostile_fire=0 --env.max_ticks=7200 --env.randomize=0
./swat play --mission annex
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
| 1 / 2 | Kit primary / sidearm |
| V | Cycle selector; carbine starts in semi, then auto, then safe |
| F | Door within 2.2 m; hold to cuff a compliant person within 1.7 m |
| Y | Order visible nearby occupants to comply |
| G | Hold optiwand; auto under-door, mouse lens aim; Ctrl/Q/E/Space select reach |
| 4 / 5 / T | Flashbang / CS gas / taser |
| X / H | Execute marked sniper shots / clear sniper marks |
| B | Melee / Entry kit ram |
| P | Briefing, equipment, sniper placement/control and generated houses |
| Backspace | Restart while playing |
| Esc or Tab | Pause/resume; back from Settings |

## Co-op and self-hosting

Host or join through the menu, or from the repository root:

```sh
./swat host --port 27474
./swat join 192.168.1.50 --port 27474
make -C ocean/swat server
SWAT_NATIVE_WINDOWS=0 ./swat server --port 27474
```

Four officers share server-owned movement, weapons, doors, damage, destruction
and mission outcomes. All surviving officers must extract. Only the leader can
restart, choose a house or command snipers. Online pause/settings release your controls while the session continues.
Solo/offline play remains available. Planning also releases controls online;
the session continues. The default hosted mission is Cedar House, five minutes;
`--mission annex` selects the old range. Hosts and peers must use protocol v3.

The dedicated server has no display, Raylib, audio-device or CUDA dependency.
On WSL, `./swat server` defaults to `build/swat/windows/swat-server.exe`;
the example explicitly selects Linux. Server options include `--seed 42`,
`--mission house|annex|generated`, `--layout-seed`, `--difficulty`, `--generator`,
`--layout-model`, `--max-ticks 18000`, `--hostile-fire 0`, `--randomize 0` and `--help`.

Connections currently use direct IPv4 addresses/DNS and UDP port 27474.
Internet hosting requires a reachable UDP port and appropriate router/firewall
configuration. There is no lobby service, NAT traversal, relay, authentication,
encryption or host migration yet. Movement uses authoritative snapshots;
high-latency presentation still needs prediction/interpolation. See
[architecture and next priorities](ARCHITECTURE.md).

When mixing native Windows and a WSL Linux server, use the WSL interface IP
and Windows host gateway IP respectively; UDP loopback forwarding is not
assumed. The default native Windows host/join path avoids that extra boundary.

The graybox weapon model, actors, and HUD use procedural Raylib geometry.
No assets from Ready or Not or SWAT 4 are included.

## Saved settings

The Raygui settings page has sliders for mouse sensitivity, vertical
sensitivity, aiming sensitivity, vertical FOV, frame limit, and master volume, plus independent
horizontal/vertical inversion. The default mouse sensitivity is **0.035 degrees
per pixel**, about one third of the original setting, with a 0.65 aiming
multiplier. Positive horizontal motion turns right; moving the mouse up looks
up unless inversion is enabled. Mouse displacement is independent of frame rate.

**Save & back** writes preferences immediately; Escape saves edited values
before returning. **Reload saved** restores the file, and **Defaults** restores
the starting values. Changes also save on orderly exit. Files are versioned,
validated, and replaced atomically; a failed save keeps the previous file and
shows an error in the menu. Preferences load at startup from:

- Windows: `%LOCALAPPDATA%\SWAT Gold Element\settings.ini`
- Linux: `$XDG_CONFIG_HOME/swat-gold-element/settings.ini`, falling back to
  `~/.config/swat-gold-element/settings.ini`

Use `--settings /path/to/profile.ini` for an independent profile. The user
preferences file stores controls/display settings. Gameplay/mission and policy
configuration continue to live in `config/swat.ini`; preference changes do not
change the RL action/observation contract. Human FOV is adjustable; policy
playback retains its sensor FOV. Raygui is already vendored at
`vendor/raygui.h`; no separate GUI library installation is required.

## Implemented systems

- **Character:** dynamic Box3D body, separate feet and upper capsule, normalized
  movement, stair/ground handling, crouch clearance, grounded jump, pitch limits,
  walk/slow/sprint, stamina, and movement/aim/fire gates. Lean shifts the actual
  upper collider and eye by up to 0.42 m; it is swept against cover. Standing
  checks the leaned head as well as the feet hull. Aim, roll, and sensor rays
  follow the achieved pose.
- **Weapons:** carbine, sidearm, impact launcher and two sniper rifles, chamber plus magazine/reserve counts,
  semi/auto/safe selectors, timed tactical/empty reloads, reload cancellation on
  swap, equip delay, recoil, movement/air/aim spread, and deterministic actor-local
  weapon RNG. Reserve ammunition is pooled; individual spare magazines and
  staged reload animation are not modeled yet.
- **Hits and cover:** eye-to-muzzle volume check, muzzle-origin hitscan, nearest
  collision damage, bounded thickness/material penetration, and head/torso/arm/leg
  damage. Leg wounds reduce pace, arm wounds widen spread, and plates reduce
  torso damage. Cedar House uses two thin board/plaster faces, a cavity, timber
  studs, plates and headers; breaking a face exposes what remains behind it.
  Supports are fixed rather than a structural collapse simulation. Destroyed objects lose
  their physics colliders and disappear from sensors immediately. This is
  modular destruction, not structural fracture or simulated debris.
- **Doors:** an authored hinged, damageable door with real collision. Its short
  swept rotation stops for actors and resumes once they clear it. It currently
  moves under game control rather than a motorized rigid-body hinge.
- **Mission:** house suspects/hostages, command/compliance, hold-to-cuff arrests,
  protected civilians, secure-and-extract success,
  injury/death, civilian-harm failure, fall/timeout, restart, and episode metrics.
  Annex variations, authored Cedar House and neural/uniform generated houses.
- **Materials and equipment:** shared density/friction/restitution/rolling values,
  CCD canisters, cover-sensitive flash/CS exposure, masks, finite taser/throwable
  stocks and collision-limited optiwand placement with independent lens aim.
- **Audio:** shared material/thickness/doorway propagation, delayed directional
  hearing, listener/source room reflections/tails, optional Steam Audio HRTF, procedural
  shots/steps/handling/doors/impacts/breakage and guard turning toward audible
  cues. Enable the local runtime with `python3 ocean/swat/setup_audio.py`.
  Production source recordings and richer propagation still need work; see
  [research, setup and listening comparisons](AUDIO.md).

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
| `mission.c`, `materials.c`, `equipment.c` | Authored house/framing/viewpoints and editable material/kit tables |
| `tactical.c`, `overwatch.c` | Physical canisters/effects/taser and authoritative sniper orders/control |
| `generation.c`, `layout_tool.c`, `train_layout.py` | Validated layout grammar, C inference and offline model training |
| `feedback.c` | Explicit local comparisons between played houses |
| `sim.c`, `sim.h` | Actors, fixed update, ballistics, mission, observations/actions |
| `swat.h` | Ocean adapter, rewards, logging, automatic reset |
| `render.c`, `swat.c` | Game presentation, fixed update loop, CPU policy playback/evaluation |
| `frontend.c`, `settings.c` | Main/pause/settings menus, mouse capture, saved player preferences |
| `protocol.c`, `net.c`, `server.c` | Versioned codec, UDP co-op authority/replica, headless server |
| `acoustics.c`, `audio_dsp.c`, `spatial_audio.c`, `sound_view.c` | Shared hearing, room mixer, optional HRTF, device stream |
| `audio_lab.c` | Offline WAV/CSV acoustic comparisons |

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

ENet is vendored at its pinned 1.3.18 revision. A standalone CMake build can
build the server and core/network tests without the training repository's tools:

```sh
cmake -S ocean/swat -B build/swat/portable -DSWAT_BUILD_PLAYER=OFF \
  -DSWAT_BOX3D_DIR=/absolute/path/to/box3d -DCMAKE_BUILD_TYPE=Release
cmake --build build/swat/portable --parallel 2
ctest --test-dir build/swat/portable --output-on-failure
./build/swat/portable/swat_server --port 27474
```

Enable `SWAT_BUILD_PLAYER` and set `SWAT_RAYLIB_DIR` to build `swat_player`.

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
make -C ocean/swat net-test
make -C ocean/swat sanitize
make -C ocean/swat audio-lab spatial-test
./swat --capture build/swat/first-playable.png --env.randomize=0 --env.hostile_fire=0
./swat --capture build/swat/settings.png --capture-screen settings
./swat --capture build/swat/house-plan.png --capture-screen plan
./swat --capture build/swat/overwatch.png --capture-screen overwatch
```

Checks cover movement speed and gates, jump edges, crouch/lean collision and
combined stand clearance, ammunition conservation and cadence, reload/swap,
muzzle obstruction, cover removal and penetration, door obstruction, and guard
visibility/reaction. A test-only driver completes eight randomized missions with
enemy fire enabled using ordinary movement, interaction, and weapon inputs.
It has known waypoints/target poses; it is a solvability check, not learned AI.

Co-op checks cover extraction/squad failure and guard targeting teammates.
Real UDP tests cover four players, capacity rejection, independent movement
and ammunition, shared door/destruction/audio state, late join, epoch resets,
leader authority, terminal menus, disconnect cleanup and listen-host shutdown.
Codec tests cover malformed/truncated packets, nonfinite fields and replica
colliders. Acoustic and PCM tests cover shared occlusion/timing and output.

House checks exercise independent board faces/studs, rotated door collision,
optiwand clearance, authority kit restrictions, impact rounds stopped by cover,
ram breakage, surrender, interrupted cuffs/fire isolation, civilian commands,
leg-injury mobility and secured-house extraction. This interaction driver uses
fixture placement to isolate rules; it is not a complete navigation policy.
Real UDP checks also transfer the full house and late-join wounds/restraints.
New checks cover canister mass/bounce/CCD, cover-sensitive effects, consumable
conservation, independent optiwand aim, sniper placement/mark/hold/execute and
friendly interlocks. Generated-house tests include 1,024 deterministic plans,
314 real standing door crossings across 24 houses, exact collider counting and
token/seed/model replication. UDP checks cover active gas at late join and
leader-only scenario changes. The optional Python smoke command exercises
preference reward training and policy updates using marked synthetic labels.
Native HRTF checks verify directional impulse differences; room mixer checks
verify longer decay retains more late energy.

Adapter checks cover 2,048 paired seeded transitions, 33 paired resets,
finite observations, relocated environment storage, exact timeouts, civilian
penalty, extraction reward, and preservation of terminal reward across reset.
ASan/UBSan instrument the game/controller and adapter, not the separately built
Box3D archive. Offscreen rendering has also been captured and visually inspected.

Settings tests cover save/load/replacement, preserving the old file after a
failed save, malformed values, unsupported versions, mouse directions,
inversion, and frame-independent sensitivity. The native Windows GUI regression
opens its own brief test window and checks the actual OS cursor confinement
rectangle, stationary mouse input, Escape/pause/resume/quit, focus loss,
click isolation, slider cancellation, host/join setup/cancel, leader restart
controls, active audio streaming, preferences surviving reinitialization,
automatic optiwand insertion, sniper scope/mark/execute and local house comparisons:

```sh
bash ocean/swat/build-windows.sh
./build/swat/windows/test_frontend.exe build/swat/windows/frontend-test-settings.ini
```

It uses only the supplied test settings path. For native Linux, build the same
GUI test with `make -C ocean/swat frontend-test-build` and run
`./build/swat/test_frontend build/swat/frontend-test-settings.ini`. That Linux
test can inspect Raylib capture state; the Windows test additionally checks
the OS clipping rectangle. Interactive play on WSL uses Windows by default.

Separate-process probes also passed from native Windows to the WSL Linux server
and from Linux to the Windows server using their interface IPs. A joining native
client moved/fired against the actual listen-host player with the host window
minimized. These verify this machine's transport/UI integration; a separate
two-machine LAN session and adverse network conditions remain to qualify.

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
