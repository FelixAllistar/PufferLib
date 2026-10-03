# SWAT contract v1

This is the annex training/policy contract. Human Cedar House play adds kit,
inspection, command and melee fields to the internal `SwatInput`, but v1's
action heads and 167-float observations do not expose those features or hearing.
A house/arrest/audio policy requires a new contract and corresponding training;
network protocol v2 is separate from the RL contract version.

Co-op and the shared acoustic system preserve this single-officer v1 layout.
No audio cues/waveform fields have been added. The scripted guard consumes a
separate delayed hearing API; trained listening or multi-role actors require a
new observation contract. Network replica state includes hidden world truth
for presentation and must not be used directly as policy perception.

The game runs at 60 Hz with four Box3D substeps. One agent currently controls
the officer. A decision applies for one simulation tick. `swat.h` declares the
native Ocean interface; `sim.h` declares `SWAT_CONTRACT_VERSION = 1`.
Changing ordering, dimensions, semantics, reward, or task distribution requires
an explicit contract/config revision and a checkpoint migration decision.

## Actions

Actions are 14 floats interpreted as categorical integer indices. Nonfinite
values become zero; values are clamped to the head's range before conversion.
These are the head order and sizes in `SWAT_ACTION_SIZES`:

| Index | Size | Meaning of indices in order |
| --- | --- | --- |
| 0 | 5 | Yaw delta: -6, -1, 0, +1, +6 degrees |
| 1 | 5 | Pitch delta: -4, -0.5, 0, +0.5, +4 degrees |
| 2 | 3 | Forward: backward, neutral, forward |
| 3 | 3 | Strafe: left, neutral, right |
| 4 | 3 | Lean: left, centered, right |
| 5 | 2 | Crouch: released, held |
| 6 | 3 | Gait: slow, walk, sprint |
| 7 | 2 | Aim: released, held |
| 8 | 2 | Trigger: released, held |
| 9 | 2 | Reload: released, held |
| 10 | 3 | Weapon: keep current, primary, sidearm |
| 11 | 2 | Interact: released, held |
| 12 | 2 | Jump: released, held |
| 13 | 2 | Selector cycle: released, held |

Neutral is `[2,2,1,1,1,0,1,0,0,0,0,0,0,0]`, not an all-zero array.
Semi fire, reload, interaction, jump, and selector cycling require a rising
edge. Auto fire follows the held trigger. Releasing/pressing too quickly during
cooldown does not buffer a semi shot. Stance and movement are held requests;
clearance and controller/weapon gates determine the actual state.

## Observations

167 float32 values: 32 state values followed by a 5-row by 9-column forward ray
grid with three channels per ray. Units below describe the stored values.

| Index | Value |
| --- | --- |
| 0–1 | Local forward/right velocity divided by 4.6 m/s |
| 2 | Vertical velocity divided by 15, clamped to [-1,1] |
| 3–4 | Sine/cosine of yaw |
| 5–6 | Sine/cosine of pitch |
| 7–8 | Grounded, actual crouch |
| 9 | Achieved lean in [-1,1] |
| 10–13 | ADS blend, sprinting, stamina, health/100 |
| 14 | Active weapon: 0 primary, 1 sidearm |
| 15–17 | Magazine/capacity, chamber present, reserve/(3*capacity) |
| 18–19 | Reload remaining/empty reload ticks, equip remaining/equip ticks |
| 20–21 | Selector/2 (safe=0, semi=0.5, auto=1), cooldown/shot ticks |
| 22–23 | Vertical recoil/12 degrees, horizontal recoil/6 degrees |
| 24–25 | Muzzle obstructed, jump cooldown/cooldown duration |
| 26 | Remaining episode fraction |
| 27–28 | Local extraction forward/right displacement/30 m, clamped [-1,1] |
| 29 | Remaining armed threats (currently 0 or 1) |
| 30–31 | Previous trigger held, fired this tick |

Local movement and extraction axes use body yaw. Ray axes use the actual
view, including recoil and achieved lean roll; origin is the collision-limited
eye. Vertical FOV interpolates from 70 degrees to 45 with ADS, aspect 16:9,
range 30 m. Rows run top to bottom, columns left to right. Ray offset is
`32 + (row*9 + column)*3`:

| Channel | Value |
| --- | --- |
| 0 | Hit depth/30 m; 1 for no hit |
| 1 | Semantic: 0 empty, 0.2 solid, 0.4 destructible, 0.6 door, 0.8 armed actor, 1 civilian |
| 2 | Remaining object integrity; 1 for solid or visible actor, 0 for empty |

Own shapes are ignored. Actors do not expose their health through rays. Dead
actors have disabled colliders and no longer appear in sensors. The mission
provides extraction direction, remaining threat count, and time; this is
explicit mission telemetry. Enemy positions, visibility through opaque cover,
and behavior internals are not observation features. Glass currently occludes
query visibility even though its presentation is translucent.

## Reward and episode lifecycle

Per tick:

```text
-0.001 time
-0.0005 per officer shot
+0.01 per point of officer-inflicted armed-target damage
+1 per armed target down
-0.1 per point of civilian damage
-0.01 per point of officer damage
+5 on successful extraction
-5 on civilian-harm failure
-1 on officer death or falling out of the world
```

Damage is capped at remaining health. Destroying cover has no direct reward.
Raw game reward is not clipped by the SWAT config (`train.reward_clip=0`).
Success requires no living armed targets and officer feet within 1.4 m of
extraction. Any civilian damage fails the mission. Death, feet below -8 m,
or `max_ticks` also terminate. Failure takes priority over simultaneous success;
success takes priority over the time limit. Default `max_ticks=1800` is exactly
1,800 game steps. `randomize=1` varies cover and guard placement; `hostile_fire=0`
disables the baseline guard's reactions for practice/curriculum use.

`SwatSim` freezes at a terminal state for human presentation. The Ocean adapter
reports terminal=1 and the final reward, records metrics, resets all scene,
controller, weapon, and actor state, and returns the **new episode observation**
on that transition. The next step clears terminal. No separate truncation flag
or terminal observation is currently exported. Recurrent CPU playback resets
hidden state at that boundary.

Logs are accumulated per completed episode and normalized by the native
logging path: `perf` (success rate), `episode_return`, `episode_length`, `score`
(armed targets down), `shots`, `hostile_damage`, `civilian_damage`, `destroyed`,
and `n` (completed episodes). The sweep target is `perf`; compare policies on
fixed evaluation seeds and task settings, not return across changing contracts.
