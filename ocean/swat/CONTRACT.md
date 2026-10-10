# SWAT annex contract v3

This section defines the annex training/policy contract, selected by
`env.task=annex` in `config/swat.ini` and rebuilding `puffer`. The default
movement task uses the v2 contract below. Human house play adds kit,
inspection, command, melee, throwable, taser, door-tool and sniper fields to the internal
`SwatInput`, but the annex action heads and 167-float observations do not expose
those features or hearing. Generated buildings use a separate layout policy.
A house/arrest/audio policy requires a new contract and corresponding training;
network protocol v9 is separate from the RL contract version.

Co-op and the shared acoustic system preserve this single-officer layout.
No audio cues/waveform fields have been added. The scripted guard consumes a
separate delayed hearing API; trained listening or multi-role actors require a
new observation contract. Network replica state includes hidden world truth
for presentation and must not be used directly as policy perception.

The game runs at 60 Hz with four Box3D substeps. One agent currently controls
the officer. A decision applies for one simulation tick. `swat.h` declares the
native Ocean interface; `sim.h` declares `SWAT_CONTRACT_VERSION = 3`.
Changing ordering, dimensions, semantics, reward, or task distribution requires
an explicit contract/config revision and a checkpoint migration decision.

Revision 3 (2026-10-10) preserves the 167/14/39 dimensions but changes visual
semantics: clear glass transmits actor identity/presence, nearest physical pane
depth remains available, and room lighting bounds distant actor detection.
Start fresh annex training rather than continuing v1 weights. Old annex weights
remain shape-compatible and the raw FP32 loader cannot identify their semantic
revision; loading them is not evidence of compatible behavior. Movement v2 and
its checkpoints are unchanged. No automatic checkpoint conversion is provided.

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
| 0 | Nearest physical hit depth/30 m; 1 for no hit or an unseen actor without a nearer pane |
| 1 | Semantic: 0 empty, 0.2 solid, 0.4 destructible, 0.6 door, 0.8 armed actor, 1 civilian |
| 2 | Remaining object integrity; 1 for solid or visible actor, 0 for empty |

Own shapes are ignored. Actors do not expose their health through rays. Dead
actors have disabled colliders and no longer appear in sensors. The mission
provides extraction direction, remaining threat count, and time; this is
explicit mission telemetry. Enemy positions, visibility through opaque cover,
and behavior internals are not observation features. Clear glass transmits sight
but remains physical cover for movement and ballistics. When a visible actor lies
behind glass, channels 1–2 describe that actor while channel 0 retains the nearest
pane depth. If that actor is hidden by darkness or opaque cover, all three channels
describe the physical pane instead. Otherwise unseen actors supply neither identity
nor body depth. Actor detection uses the shared bounded room-source approximation,
not renderer pixels; darkness halves maximum detection range, with close sight/FOV
still required. Navigation, hands, bullets and nonlethal tools use physical queries.

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

## Movement contract v2

`env.task=movement` in `config/swat.ini` selects this contract when building the
normal native Puffer binary. The compiled binary rejects an incompatible task
setting; rebuild after changing task. There is no separate config or trainer.

The movement interface has 32 float observations and six categorical action heads
of sizes `[5,3,3,2,2,3]`: turn, forward/backward movement, strafe, crouch, jump and
gait. Observations expose goal-relative direction/displacement, own velocity,
ground/stance/stamina, role, 15 local obstacle rays and three floor probes. They
contain no hidden enemy positions. Each decision advances four 60 Hz physics ticks.

The five course initial conditions are goal approach, stairs, crouch clearance,
an operable door and an already-breached wall. `env.stage` and `env.role` choose
individual cases or mixed episodes. `env.max_steps` sets the decision limit;
reward coefficients are `progress_reward`, `step_cost`, `success_reward` and
`fall_penalty`. Reaching the goal within 0.5 m and 0.3 m vertically succeeds;
timeout, falling or a terminal game state ends the episode. The Ocean adapter
exports one terminal/reward transition, resets, and returns the next observation.

Weights use native FP32 PufferNet encoder/MinGRU/decoder order, with no custom MLP
format. Default 64x2 architecture has 27,840 parameters. Annex and movement weights
are incompatible. Training and player inference use the same native weights;
the game keeps recurrent state separately for each officer/suspect, resets on a
new scenario, and holds each action for four ticks. Navigation and door handling
remain separate systems. Movement learning stays opt-in, and civilians stay scripted.
