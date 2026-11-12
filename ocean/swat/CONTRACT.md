# SWAT annex contract v3

This section defines the annex training/policy contract, selected by
`env.task=annex` in `config/swat.ini` and rebuilding `puffer`. The default
movement task uses the v2 contract below; `env.task=roles` selects the
multi-role contract v2 at the end of this file. Human house play adds kit,
inspection, command, melee, throwable, taser, door-tool and sniper fields to the internal
`SwatInput`, but the annex action heads and 167-float observations do not expose
those features or hearing. Generated buildings use a separate layout policy.
A house/arrest/audio policy requires a new contract and corresponding training;
network protocol v23 is separate from the RL contract version. Human tool
inventory, retained cut profiles, latch failure, moving door leaves and bounded wall debris
do not add annex action heads or observation fields; perception still comes
from the shared physical queries.

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
The armed-target terms count every suspect hit and death, without the role
task's protected-person check. That is deliberate, not a missed fix: the annex
officer's 14 heads expose no command, taser, less-lethal kit, throwable or
device, so the training annex suspect cannot become surrendered or restrained,
and civilians never count as armed targets. Annex reward is unchanged; a
future annex action that can cause surrender needs a new contract that
excludes protected people, as the role task does with `SwatForce`.
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

## Role task contract v2

`env.task=roles` selects this contract (`roles_env.h`, build flag
`SWAT_ROLES_TRAINING`). Annex v3 and movement v2 headers, observations, heads,
rewards and checkpoints are unchanged.

**SWAT_ROLES_TRAINING build note.** `swat.h` includes `roles_env.h` only when
`SWAT_ROLES_TRAINING` is defined, so the trainer build must map the task to
that flag. `build.sh` needs this case in its swat task switch (native and
profile modes), beside the `movement)` mapping; until it is added, `build.sh`
exits with "env.task must be movement or annex" for `task = roles`:

```sh
roles) EXTRA_CFLAGS+=(-DSWAT_ROLES_TRAINING) ;;
```

`build.sh` is owned outside `ocean/swat`; this contract records the required
line rather than editing it. The headless role tests need no build.sh change:
`tests/test_roles.c` defines the flag before including `swat.h`.

Officers, suspects and civilians are learned actors in one authoritative
`SwatSim`. Every controlled actor reaches the game through the ordinary
per-actor `SwatInput` array and `swat_sim_step_inputs`, so weapons, doors,
cuffing, surrender, injuries and outcomes are the rules human and scripted
actors use. Uncontrolled actors keep their scripted inputs. Learned actors get
no navmesh, route or cover planner: movement, cover and tactics come from the
policy. Scripted motel occupants still use their own navigator.

`roles_mission=annex` (default) uses the training annex. Agents per env are
`roles_officers` officers (stable slots: actors 0, 3, 4, 5), then the suspect
(actor 1) if `roles_suspects=1`, then the civilian (actor 2) if
`roles_civilians=1`. The default is four agents on actors `[0,3,1,2]`.
`roles_mission=motel` uses Briar Court with its tactical rules and no squad
bots; standard occupancy gives seven controlled actors on `[0,3,1,8,2,6,7]`.
The roster is fixed per config and checked on every reset. All agents share
policy 0; the role is an observation, not a separate network.

Agents per env (`num_agents`) are the roster followed by inactive padding
agents, set by `roles_agents`. The default 0 pads to the next power of two
(the 4-agent annex is unpadded; the 7-actor motel becomes 8), so any
power-of-two per-buffer share of `vec.total_agents` divides evenly (8192 / 2
buffers / 8 = 512 envs per buffer). A positive `roles_agents` (at most 16) is
the exact count and must be at least the roster. Padding was chosen over
changing the role counts so the motel keeps its full occupancy. A padding
agent controls no actor and never touches the sim, so the real agents'
observations, rewards and RNG are identical to an unpadded env. It observes
144 zeros (no role bit, unlike every real agent), its mask allows exactly one
value per head (neutral motor values, gaze yaw bin 4 and pitch bin 2), its
actions are ignored, its reward is 0, and it receives the shared terminal.
Logged returns average over the roster only. A trainer that ignores
`action_mask` still gets zero reward from padding; its value head simply
learns zero for the all-zero observation.

Each decision repeats for `roles_repeat` 60 Hz ticks (default 4, at most 8).
Rewards sum over those ticks. Edge-triggered inputs (semi fire, interact,
reload, jump, selector, command) see one press per decision.

### Actions

32 categorical heads, 155 logits. Heads 0-13 decode exactly as annex v3
(`swat_decode_action`), including their neutral values:

| Index | Size | Meaning |
| --- | --- | --- |
| 0-13 | 5,5,3,3,3,2,3,2,2,2,3,2,2,2 | Annex v3: yaw, pitch, forward, strafe, lean, crouch, gait, aim, trigger, reload, weapon, interact, jump, selector |
| 14 | 2 | Command (compliance shout): released, pressed. Officers only |
| 15 | 2 | Comply (voluntary surrender): no, yes. Suspects and civilians only |
| 16+2k | 9 | Gaze ray k yaw offset: -80, -50, -25, -10, 0, 10, 25, 50, 80 degrees |
| 17+2k | 5 | Gaze ray k pitch offset: -40, -15, 0, 15, 40 degrees |

k runs over 8 gaze rays. Neutral motor actions are
`[2,2,1,1,1,0,1,0,0,0,0,0,0,0,0,0]`; gaze heads have no neutral value.
Positive yaw offsets look right and positive pitch offsets look up, relative to
the achieved view (recoil and lean roll included). Comply sets the actor's
surrendered state: it is irreversible, like a commanded surrender, and officers
must still cuff. Restrained actors keep their scripted (escort) input; the
policy's motor actions are ignored while restrained.

Legal actions arrive in `action_mask` (one byte per logit, refreshed with each
observation). The decoder enforces the same role rules when no mask is used.

| Role / state | Heads fixed to one value |
| --- | --- |
| Officer | comply = 0 |
| Suspect | command = 0 |
| Civilian | command, aim, trigger, reload, selector = 0; weapon = keep |
| Dead, restrained, surrendered or stunned | every motor head (0-15) = neutral |

Gaze heads are never masked.

### Observations

144 float32 values: 45 self values, then 9 rays of 11 channels. Ray 0 is a
fixed reticle ray along the achieved view; rays 1-8 use the gaze bins chosen by
the previous action (a default fan of yaw bins 0-7 at pitch 0 after reset).

| Index | Value |
| --- | --- |
| 0-13 | As annex v3 0-13: local velocity, vertical velocity, yaw/pitch sin/cos, grounded, crouch, lean, ADS, sprint, stamina, health/100 |
| 14 | Alive |
| 15-17 | Role one-hot: officer, suspect, civilian |
| 18-21 | Surrendered, restrained, stunned ticks/240, escorted |
| 22 | Own cuffing progress/72 ticks |
| 23-33 | Weapon: active slot, magazine/capacity, chambered, reserve/(3*capacity), reload, equip, selector/2, cooldown, recoil pitch/12 deg, recoil yaw/6 deg, muzzle obstructed. Zero for civilians |
| 34 | Jump cooldown fraction |
| 35-36 | Previous trigger held, fired this tick. Zero for civilians |
| 37 | Remaining episode fraction |
| 38-41 | Role objective: local forward/right/30 m, vertical/10 m, distance/30 m, clamped |
| 42-43 | Officers only: suspects and civilians in team custody (alive and cuffed)/4 |
| 44 | Own health lost during the last decision/100 |

The role objective is extraction for officers and civilians (the safe exit) and
the mission staging point for suspects when `suspect_escape=1`; otherwise
suspects get zeros. A dead actor observes only its role, alive=0 and time; its
rays are not cast.

Ray offset is `45 + ray*11`:

| Channel | Value |
| --- | --- |
| 0-1 | Ray yaw offset/90 deg, pitch offset/45 deg (0 for the reticle) |
| 2 | Nearest physical hit depth/30 m; 1 for no hit or an unseen person without a nearer pane |
| 3-8 | One-hot class: solid, destructible, door, officer (or sniper), suspect, civilian |
| 9 | Seen person: 1 hands up (surrendered, not cuffed), 0.5 cuffed (restrained), 0 free |
| 10 | Remaining object integrity; 1 for solid or a visible person |

Rays use the shared physical world ray from the collision-limited eye and ignore
the actor's own shapes. Clear glass, darkness and opaque cover follow annex v3:
a person behind clear glass shows identity with the pane's depth; hidden people
expose neither identity nor depth. Per agent and decision the cost is at most 9
physical rays, 9 glass sight rays and the bounded room-light queries, below the
annex 45-ray grid.

No observation contains another actor's position, health, ammunition, target
or memory, and nothing hidden is exported for a critic. The trainer's value
head shares the actor's input, so a privileged critic needs a separate trainer
input and is not part of v2.

**v2 changes (2026-11-11).** v1 gave officers the true number of armed
suspects and unsecured civilians still at large (obs 42-43), which revealed
people no officer had perceived: killing an unseen suspect changed every
officer's observation. v2 replaces them with team custody, which the officers
themselves create by cuffing, and makes ray channel 9 show cuffs, so a
policy (and the officer baseline) can tell who still needs cuffing from what
it sees. Obs 44 is now the agent's own health drop instead of the reward
ledger's value; the numbers agree up to float rounding. Sizes, heads, masks
and rewards are unchanged, but v1 weights are not valid for v2 (none were
trained). `make roles-eval-test` audits this property (see "Held-out
evaluation suite").

### Rewards

Every coefficient is a non-negative magnitude in `[env]` (defaults in
parentheses); costs are subtracted. Damage is in health points, capped at the
victim's remaining health. Protected people are civilians and anyone
surrendered or restrained before the hit, as recorded by the sim's per-actor
force ledger (`SwatForce`). Every agent pays `roles_time_cost` (0.001) per tick.

Officers share the team terms and keep their own individual terms:

```text
team  +officer_arrest (1.5)            per suspect newly restrained
      +officer_neutralize (1)          per suspect newly killed while a threat
      +officer_secure_civilian (0.25)  per civilian newly restrained
      -officer_civilian_harm (0.1)     per point of civilian damage from anyone
      -officer_roe (0.1)               per point any officer dealt to protected people or officers
own   -officer_damage (0.01)           per point taken
      -officer_shot (0.0005)           per round fired
      -officer_down (1)                on death or falling out of the world
end   +officer_success (5)             on mission success
      -officer_failure (5)             on annex civilian-harm failure or a suspect escape
```

Suspects:

```text
own   +suspect_officer_damage (0.01)   per point dealt to officers
      -suspect_harm (0.05)             per point dealt to protected people
      -suspect_damage (0.01)           per point taken
      -suspect_arrested (1)            when restrained
      -suspect_down (2)                on death
end   +suspect_free (1)                alive, unrestrained and not surrendered
      +suspect_escape_reward (2)       instead, for the suspect that escaped
```

Killing a protected person is never rewarded. The ledger's victim-side
`downed_protected` marks a death whose fatal impact found the victim already
civilian, surrendered or restrained, and such a suspect does not count for
`officer_neutralize`; the damage, killing blow included, is charged through
the protected-force terms (`officer_roe` for officers, `suspect_harm` for
suspects). A hit that itself causes surrender is still judged as a threat hit.

Surrender then arrest (-1) beats death (-2) for a suspect. The harm cost
outweighs the free bonus, so ending an annex episode by shooting the civilian
does not pay.

Civilians:

```text
own   -civilian_damage_cost (0.02)     per point taken
      -civilian_down (2)               on death
      +civilian_safe (1)               once, alive within 2.5 m of extraction or rescued
      +civilian_comply (0.1)           once, on voluntary surrender
end   +civilian_survive (1)            alive
```

### Termination and logs

The episode is shared: every agent receives terminal=1 on the same transition.
The sim's ends apply unchanged (annex: success with no armed suspects and all
living officers within 1.4 m of extraction, and any civilian damage fails;
motel: the tactical secured/evacuated/evidence rules), plus all officers down,
a fall, or `max_ticks` (ticks, not decisions). With `suspect_escape=1`, a free
suspect within 1.4 m of the mission staging point also ends the episode as an
officer failure. The adapter reports the final rewards with terminal=1 and
returns the **new episode observation and mask**, as annex v3 does.

Episode seeds come from `roles_split.h`: each reset seeds the sim with
`swat_roles_train_seed(env seed, episode)` and sets the layout seed to
`layout_seed` (default 1, at most 2^31-1) or, with `roles_layouts=N` (at most
2^20), one of 1..N drawn from the episode seed. Training seeds never have bit
31 set; evaluation seeds always do. Scripted-baseline traits and the
scripted-episode draw derive from the episode seed.

Logs per completed episode: `perf` (officer success rate, the sweep metric),
`episode_return` (mean over the learned roster, padding and scripted agents
excluded), `episode_length`
(decisions), `score` and `arrests` (suspects restrained), `suspects_down`, `civilian_damage`,
`protected_damage` (officer force on protected people and officers),
`officer_deaths`, `suspects_free`, `civilians_safe`, `escapes`, per-role
`officer_return`, `suspect_return`, `civilian_return` (learned agents only),
`scripted_episodes` (share of episodes that ran the scripted baselines),
`scripted_perf` (officer success rate within those episodes, 0 when none ran),
and `n`.

### Scripted baselines

`roles_baselines.h` holds scripted policies for all three roles, so learned
policies can be measured against fixed opponents (ROADMAP "Train and evaluate
the AI"). Each is a pure function `swat_baseline_act(obs, seed, action)` of
one agent's 144-float role observation above and a per-episode trait seed,
and writes all 32 heads. It sees nothing else: no positions, health, ammo,
minds or hearing of other actors, and no command or sound events. The header
includes only `roles_layout.h` (the observation/ray/head constants, shared
with `roles_env.h`), never `sim.h`. A baseline remembers things only through
its own previous gaze choice, which the observation reports back: it always
uses the fan yaw bins -80,-50,-25,-10,+10,+25,+50,+80 (the +-10 rays dip to
-15 deg pitch on about half the decisions), and gaze ray 0's pitch bin is a
five-state memory cell (2 = idle). Coin flips hash the observation and seed,
so identical inputs give identical actions. Its actions are always legal
under the role mask: held states give neutral motor heads, and each role
uses only its own heads. Turn steps are tuned for `roles_repeat=4`.

| Role | Behaviour |
| --- | --- |
| Suspect | Per episode either holds standing or ambushes crouched, aiming at the nearest visible door within 12 m. Turns onto visible non-compliant officers (24 or 4 deg per decision; a target lost between the +-10 rays is followed two more decisions via memory) and fires a semi shot every other decision while the reticle is on one. Reloads when empty (or below a third with nothing in view), leaves safe, switches to the sidearm when dry, and turns round when hit by an unseen shooter. A yielding suspect (40% of episodes) complies on about one decision in four while it sees two or more distinct officers, or one while below half health. With `suspect_escape=1` a runner (50%) sprints for the staging point when no officer is in sight. |
| Civilian | Complies (voluntary surrender) when an officer is within the 9 m command range; commands themselves are not observable, and the sim's command rule already surrenders commanded civilians. On seeing a non-compliant suspect within 15 m, or taking damage, it panics for the rest of the episode (memory cell 0) and either freezes crouched (60% of episodes) or sprints for the exit (40%), turning away when the suspect is within 40 deg of that heading. Otherwise it stands still. |
| Officer | (Annex; motel officers run the motel officer below.) Walks toward the role objective (extraction) with door opening (interact on a door within 1.6 m) and a turn toward the more open side when anything is within 1 m ahead. Engages non-compliant suspects in view, alternating a compliance shout and a semi shot while the reticle is on one within 9 m; never fires with a civilian or compliant person on the reticle. Shouts at non-compliant civilians on the reticle. Cuffs (holds interact) a person within reach whose hands are up and who is not cuffed yet (ray channel 9 = 1), suspects first. Scans in place once within 1.5 m of extraction. |

Known limits: there is no route planning (the officer stalls on geometry
the single-ray avoidance does not clear), and fire is limited to one semi
press every other decision. In motel episodes scripted officers therefore
run the motel officer below instead.

**Motel officer** (`roles_motel_officer.h`). In motel episodes a scripted
officer runs a route-following team baseline, so the motel bar is more than
the random floor. It acts on the same 144-float officer observation plus
what an officer is briefed with before the call-out: the static building
plan (`SwatMotelMap`: the ground floor of the game's own navigation grid,
built once per env with every door closed and nobody placed, the room boxes,
the closed door leaves and the extraction point; `swat_roles_motel_map` in
`roles_env.h`) and its team slot and size. The plan is the same for every
motel episode: occupancy and layout seeds place people, who are not on it.
The header includes only `roles_baselines.h`, never `sim.h`. Unlike the pure
baselines it keeps per-episode state, built only from its own observations
and actions (route progress, weapon spots, its previous presses), so it is
deterministic given its observation sequence and replays from that sequence
alone. It localizes itself from obs 3-4 and 38-39: the extraction point is on
the plan. Each decision, in priority order: (1) engage a free suspect in view
with the pure baseline's lawful engagement (turn on, semi shot while the
reticle is on a free suspect; never with a civilian, officer or compliant
person on the reticle); (2) walk to and cuff anyone seen with hands up and no
cuffs, suspects first, pathing round furniture on the plan; (3) take a
civilian it has just cuffed by the arm, so they follow it out; (4) recover
weapons: where it cuffed a suspect, or where a suspect it fired on dropped
from sight, crouch and sweep the likely drop points with the reticle,
pressing interact; (5) follow its route. The two officers split the rooms
east to west (slot 0 takes 104, 103 and 102; slot 1 takes 101, reception and
the back office). For each room it stacks 1.3 m outside the door, crouched
and aimed, opens it, looks in left and right, enters crouched and aimed
(its eye is then level with a crouched ambusher's), turns through the room
once level and once 35 deg down, and moves on; then it goes to extraction,
where its followers are counted rescued, and holds. If the mission is still
running 30 s later it sweeps every room again. It calls out a compliance
command on every other decision: the sim's command rule makes civilians and
low-resolve suspects in its aim cone within 9 m put their hands up. Walking
uses a breadth-first path on the plan, lines up on door axes before
crossing, opens only closed leaves on the plan (never re-presses a swinging
leaf), waits for free people in its way and side-steps when stuck.
Limits: the officers do not coordinate beyond the room split; scripted
suspects aimed at a door still often win the entry; cuffed civilians follow
at the bots' slow walk (about 0.8 m/s), so evacuation can outlast the time
limit; and weapon recovery is a blind floor sweep, because dropped weapons
are not observable.

`roles_scripted` (default `none`) picks roles that run the baseline instead
of the policy: `officers`, `suspects`, `civilians`, joined with `+`, `,` or
spaces, or `all`. `roles_scripted_rate` (default 1) is the probability that
an episode uses them, drawn at each reset from the env seed and episode index
(the sim's RNG stream is untouched, and `none` is exactly the unscripted env).
In a scripted episode those agents keep their slots, so the agent layout,
`num_agents` and checkpoints are unchanged and rates below 1 can mix
self-play and baseline episodes in one run, but the trainer sees them exactly
as padding agents: zero observation, one legal value per head, reward 0 and
the shared terminal; their trainer actions are ignored. The env computes each
scripted agent's real role observation at the start of the decision (before
any agent's voluntary surrender is applied, so it is what a learned agent
would have seen), runs the baseline, and feeds the result through the same
decoder, gaze update, comply and `SwatInput` path as a learned action. Rewards
are still computed internally but neither returned nor logged. Typical eval:
`roles_scripted = suspects` with rate 1 makes `perf` (equal to
`scripted_perf`) the officers' win rate against scripted suspects; rate 0.2
logs it alongside 80% self-play. `roles_scripted = officers` measures learned
suspects and civilians against scripted officers (lower `scripted_perf` is
better for them).

Policy size follows the project rule: 256x2 for tests and 1024x2 for real runs,
passed as `--policy.hidden_size`; `config/swat.ini` keeps 64x2 for the movement
checkpoints. No role checkpoint existed for v1 (the 2026-11-07 amendments,
protected kills never counting for `officer_neutralize` and `roles_agents`
padding, revised v1 in place). v2 (2026-11-11) changes observation meaning
only; any later change to the layout, observation meaning or reward needs a
v3 and fresh weights. Tests: `make roles-test` (annex: shapes, masks,
decoder, gaze, paired determinism, relocation, rewards; part of `make test`) and
`make roles-motel-test` (adds the padded 8-agent motel roster and a timeout
reset). `make roles-test` also covers protected kills and padding (inactive
agents pair exactly with an unpadded env), and the scripted baselines: role
selection and per-episode rate, scripted slots presented as padding, `none`
pairing exactly with the unscripted env, every baseline decision legal under
the env's mask and replayed bit-exactly from its recorded observation after
the sim is freed, paired all-scripted determinism, and the `scripted_perf`
log. It first runs `make roles-baselines-test`, which builds
`tests/test_roles_baselines.c` with only `roles_baselines.h` and
`roles_motel_officer.h` (no `sim.h`, no physics) and checks mask legality on
20,000 random observations, purity, and each role's rules on synthetic
observations; for the motel officer, on a synthetic plan: localization,
the room split, 20,000 legal decisions with state carried, lawful fire,
cuff and escort, the route walk and determinism. `make roles-motel-test`
adds an all-scripted motel: legal actions, officers replayed from the plan
and their observation sequences alone after the sim is freed, and one full
episode in which an officer works its route through the rooms. The 2026-11-07 scripted baselines add config keys and
two log fields only; observations, heads, masks and rewards of learned agents
are unchanged.

### Held-out evaluation suite

`roles_eval.h` defines suite v1 (`SWAT_ROLES_EVAL_SUITE`), run on CPU by
`make roles-eval` (`roles_eval.c`; about 5 minutes on 4 cores for the
scripted and random-floor runs). Every case has hostile fire, randomized
annex placement, default rewards and `roles_repeat=4`:

| Case | Mission | Occupancy | Officers | Escape | max_ticks | Episodes |
| --- | --- | --- | --- | --- | --- | --- |
| annex | annex | - | 2 | 0 | 1800 | 64 |
| annex_escape | annex | - | 2 | 1 | 1800 | 64 |
| motel_standard | motel | standard | 2 | 0 | 5400 | 32 |
| motel_dispersed | motel | dispersed | 2 | 0 | 5400 | 32 |
| motel_mixed | motel | mixed | 2 | 0 | 5400 | 32 |
| motel_busy | motel | busy (held out) | 2 | 0 | 5400 | 32 |

**Held out by construction.** Episode `e` of case `c` uses sim seed
`swat_roles_eval_seed(suite, c, e)` and layout seed
`swat_roles_eval_layout(suite, c, e)` (`roles_split.h`); both always have
bit 31 set, and training seeds and layout seeds never do. Motel layout seeds
choose the occupant bays, so held-out motel episodes place people where no
training layout does; the busy occupancy (six occupants) is rejected by
training configs and curriculum stages. The annex has one geometry, so annex
cases are held out by seed (suspect placement, suspect resolve and baseline
traits) only. Only `swat_roles_init(..., SWAT_ROLES_EVAL)`, called by the
suite, builds an evaluation env; `puf_init` always trains. Any change to a
case, the seed derivation or a metric needs a new suite version.

**Matchups.** `scripted`: every role runs the scripted baseline (the bar).
`policy_officers`: a policy controls the officers against scripted suspects
and civilians; `random` (uniform over legal actions, seeded per episode) is
the floor. `policy_occupants`: scripted officers against a policy's suspects
and civilians. A policy is a `SwatRolesPolicy` (`begin` per episode, `act`
per decision on the observations and masks of the agents it controls; all
other slots observe zeros, as in training). `roles_eval --checkpoint FILE.bin
--hidden H --layers L` (or `make roles-eval ROLES_EVAL_ARGS=...`) loads the
trainer's flat FP32 export and runs it with the shared CPU inference in
`src/puffercpu.c` (argmax under the role mask), so no trainer code is needed.

**Metrics** per episode (the env's ground-truth record, never observed):
mission completion (sim success and no escape), civilians harmed (count, and
whether any was), civilians killed, suspects killed, arrested and free,
officer casualties, ROE violations (engagements of officer force on
protected people or officers, merged per officer while hits follow within
`SWAT_ROE_ENGAGEMENT_TICKS`), unlawful damage, time to clear (seconds,
completed episodes only), officer and suspect rounds fired, episode length,
and sim cost (wall-clock ms per decision and us per tick excluding the
terminal decision, and the terminal decision's time, which includes the
world reset). Aggregates are means with 95% intervals: Wilson for
completion and any-civilian-harmed, Student t otherwise. The JSON report
has every episode's record; the Markdown table has the headline columns.
Timing is not deterministic; every other value is reproducible.

**Information-leak audit.** `make roles-eval-test` builds annex and motel
episodes and, for every unperceived occupant (no observation ray reaches
it), changes its identity, kills it, changes its health, ammunition, mind or
surrender, and separately scrambles the force ledger, ROE records, debrief,
event totals, reward accounting and evaluation record. Every agent that does
not perceive the changed person must observe and be masked exactly as
before. Restoring the v1 threat count makes the audit fail. Custody (obs
42-43) is checked to count only cuffed people and obs 44 to equal the
agent's own health drop.

### Curriculum

`roles_curriculum.ini` lists training stages in order; `roles_stage=N` (-1
off) in `[env]` runs stage N, whose keys override `[env]`, and
`roles_curriculum` names another file. Stages may only set difficulty keys
(`roles_mission`, `motel_occupancy`, `roles_layouts`, `hostile_fire`,
`max_ticks`, `roles_scripted`, `roles_scripted_rate`, `suspect_escape`,
`roles_officers`, `roles_suspects`, `roles_civilians`) plus
`promote_metric` (perf, scripted_perf, arrests or civilians_safe),
`promote_at` and `promote_window`, which tell the operator when to move on
and are not applied by the env. Reward coefficients, seeds, layout seeds,
`roles_agents`, `roles_repeat` and the held-out occupancy are rejected, so a
stage cannot change what a reward means or touch the evaluation suite.
Stages: `locomotion_doors` (annex, officers only, occupants disarmed under
sim control), `weapon_handling` (annex, scripted occupants firing back),
`target_discrimination` (mixed motel over 4096 training layouts, disarmed
sim occupants), `cover` (dispersed motel, scripted occupants firing back),
`extraction_selfplay` (standard motel, suspect escape, 80% self-play).
Stages control different rosters, so set `roles_agents=8` to keep one agent
count, and one checkpoint, across stages.
