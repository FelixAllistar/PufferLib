# SWAT: Gold Element architecture

The north star is a portable co-op tactical shooter with persistent local
destruction and convincing RL-controlled characters. Develop the game first,
using scripted NPCs to qualify its rules before investing in trained policies.

## Technology

Keep C, Raylib 5.5 for presentation/audio/input, the pinned Box3D fork for
physics, ENet 1.3.18 for direct UDP co-op and optional Steam Audio 4.8.1 for
headphone HRTF. The player and dedicated server
require no CUDA. Linux and Windows use the same gameplay source; macOS needs
qualification before claiming support.

ENet is a small MIT-licensed C transport with reliable packets and independent
channels over sockets; see its [design](https://github.com/lsalzman/enet/blob/master/docs/design.dox).
It fits the current engine and self-hosting without a platform account.
It provides no authentication, encryption, NAT traversal or relay.
[GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)
offers encryption and optional ICE integration with C++/crypto/protobuf build
dependencies. Its open-source transport can operate without Steam; Steam
Datagram Relay is a separate service.
[Yojimbo](https://github.com/mas-bandwidth/yojimbo) is another C++ option with
encrypted connection tokens. Revisit them when Internet connectivity needs
justify the added integration. `net.c` contains ENet; the wire codec and game
rules remain independent of the transport.

## Authority

`SwatSim` owns rules at 60 Hz with four physics substeps. Humans, remote peers,
scripted NPCs and future policies supply `SwatInput` through the same controller,
weapon, collision, damage and objective rules. Solo remains entirely offline.

A listen host runs authority in the player's process. The dedicated server
runs the same game without Raylib, a window, audio device or GPU. There are
four officer slots: slot 0 uses actor 0; actors 1/2 remain suspect/civilian;
slots 1/2/3 use actors 3/4/5. House NPCs also occupy actors 6/7/8; sniper actors
use 9/10 and the complex generated mission can use suspect actor 11. The
single-officer annex Ocean adapter is preserved.

The server chooses the map, rolls randomness, advances doors, accepts shots,
damages cover/removes colliders, applies loadouts/injuries/surrender/restraints
and decides outcomes. Clients send inputs and
render authoritative replicas. Initial reliable map/snapshot transfer includes
holes and doors when joining mid-round. All living officers must extract;
an individual death leaves survivors playing, squad death fails, and civilian
harm by any member fails. Squad bots need a living commander, so in tactical
scenarios the death of the last human officer also fails. The house also requires all living civilians cuffed;
an armed suspect remains unsecured until restrained or killed.

The listen host leads; the first occupied dedicated slot leads, with leadership
moving to an occupied slot on disconnect. Only the leader can restart, choose
a generated scenario or issue sniper commands. A new
round epoch invalidates queued old inputs/snapshots. There is no host migration:
closing the listen host ends the session. Leaving online play restores solo.

## Protocol and presentation

`protocol.c` explicitly encodes big-endian integers and IEEE float32, validates
version/type/length/ranges and decodes into temporary storage before applying.
C layouts, pointers and platform bool representations never cross the wire.
Protocol v8 includes mission/room data, framed-wall part/material metadata,
rotated door bases, kit/tool commands, regional injuries and restraints,
throwable flight/effect state, stocks/exposure, independent wand pose and
sniper assignments/rifles/targets/travel state.
Door snapshots carry lock state, mounted-charge owner and the short breach effect;
actor equipment carries pick/mount progress and finite charges. Door tool input
is validated as a bounded command before authority applies it. Generated maps
include seed, accepted tokens, model ID, staging and post geometry. The 1,109-object Cedar
House and up-to-1,528-object accepted generated houses use a reliable map
baseline and fragmented state packets.

| Channel | Content | Delivery |
| --- | --- | --- |
| 0 | Control, map and initial snapshot | Reliable |
| 1 | Sequenced input bound to connected slot | Reliable |
| 2 | Complete state snapshots at 30 Hz | Unreliable sequenced, including fragments |

Reliable packets are acknowledged/retransmitted, and delivered in order within
their channel. Independent channels mean an unacknowledged map packet does not
hold the input stream in that channel's ordering queue. They still share the
socket and available bandwidth. Snapshots can be replaced by a newer complete
state, so stale ones can be dropped. ENet's explicit unreliable-fragment flag
prevents a large snapshot silently becoming reliable when it exceeds the MTU.

Snapshots carry input acknowledgements, actor pose/weapon/equipment state, cover HP,
doors and recent sounds. Replica application restores stance/lean colliders
without advancing physics; destroyed cover loses its collider. Replicated
canisters carry visual state without creating a second simulated projectile.
Reliable scenario requests are bound to the connected leader and current epoch;
changing mission clears prior inputs, canisters and sniper assignments. Old revisions
are discarded. Input queues, packet sizes and waiting data are bounded. Missing
input becomes neutral after 250 ms. Online menus/focus loss submit neutral
controls while authority continues; solo menus freeze simulation.

Unacknowledged mouse look appears immediately in the local camera and is
removed on acknowledgement. Movement, firing and damage are snapshot-driven.
Full movement prediction/reconciliation, interpolation, bandwidth profiling
and high-latency/loss qualification remain future work.

The replica protocol sends global world state, including NPCs and sound source
positions. This is trusted co-op presentation data, not competitive anti-cheat
or an appropriate perception feed for deployed RL actors.

## Perception and training fidelity

See [AUDIO.md](AUDIO.md) for shared acoustic paths. NPC hearing exposes coarse
audible cues rather than hidden source coordinates/identity. Player volume
affects presentation only. Contract v1 remains 167 floats/14 heads and has no
audio observations; hearing/equipment/house policies need a new versioned
contract and curriculum. Human play and dedicated servers default to the motel (Standard occupancy);
policy playback and the current training adapter default to the annex.

The role task (contract v4) keeps the same boundary for learned actors: an
actor observes its own state, team custody it created, and what its own rays
see, including what it sees of a seen person's hands (`swat_sim_hands_seen`,
the test the ROE observers use: a weapon in hand, hands hidden, or empty; a
weapon kept under a feigned surrender does not show). The annex
(contract v4) observes, in obs 29, only the suspects the team has seen at
large and not secured (`SwatSim.sighted`, authority-only and not
replicated), never the true number at large. Ground truth needed for rewards and evaluation (the force ledger, ROE
records, the per-episode evaluation record in `roles_env.h`) is never
observed, and `tests/test_roles_eval.c` audits that by changing hidden state
of unperceived people and checking every observation, the weapon channel of
perceived people and annex obs 29. Training and the
held-out evaluation suite (`roles_eval.h`) drive the same env on disjoint
seed streams (`roles_split.h`); only the suite can build an evaluation env.
Scripted motel officers (`roles_motel_officer.h`) add only the static
building plan an officer is briefed with: the navigation grid's ground floor
built once per env with doors closed and nobody placed, room boxes, door
leaves and extraction, plus their own ticks per decision. Decisions about
people come from their observation, including when lethal force is lawful
under `roe.h`: the officers read no ROE state, and fire only on a person their
reticle shows with a weapon seen in hand, never one whose hands they cannot see.

Planning's cutaway shows geometry without hidden actors. Overwatch previews
and controllable snipers use exterior posts and ordinary depth occlusion.
Target marking permits optical glass visibility, while firing uses shared
material penetration. An explicit hold/execute command, ammunition, cooldowns
and a conservative friendly/compliant-person corridor govern fire. Moving
posts is a timed relocation that preserves health and ammunition. Snipers
are physical actors, but do not join the entry squad's extraction requirement.

The live sniper inset renders the actual actor camera into one reusable texture.
Taking control animates that panel into a centered scope over a dimmed officer
view; the officer camera stays at the body while mouse input drives the sniper.
Holding Tab frees the pointer and sends neutral officer input while simulation
continues. Preview/close/feed selection are local presentation choices; sniper
assignment and firing remain commander-only authoritative commands.
Live cycling keys and previous/next buttons reopen a hidden panel without
changing the officer's weapon. The gameplay HUD uses Roboto text with a fine
shadow for contrast, compact edge status and one contextual action near the
reticle. There are no opaque status or interaction cards. The camera shows its
video with a one-pixel edge; unassigned/down cameras collapse to text. Holding
Tab reveals borderless camera controls and equipment counts. Actual menus are
reserved for planning, settings and pause. Make, CMake and the native Windows
builder copy the existing shared font beside the player; missing fonts use
Raylib's default. The earlier generated panel/icon assets remain an experiment
in source, and the player no longer loads them.

Contextual use shares a three-degree ray fan between player prompts and
authoritative physical tools. Direct hits take priority; all samples trace real
cover and still enforce door/cuff reach. Compliance accepts the actual aimed
person and additionally checks a visible 15-degree forward cone. In tactical
missions that cone is only a floor: the compliance shout itself is heard (see
"Heard commands" below). The annex v1 interaction remains a direct ray.
F/middle mouse selects use or compliance; RMB latches cuff, pick or aim on
press. Lost targets cancel progress, cannot transfer the held cuff to another
person, and finishing does not change that press into ADS or a weapon click.
These bindings reuse existing protocol v8 commands; the server still computes
targets and validates physical actions independently of client prompts.

Optiwand cameras sweep a small sphere through actual geometry. Near a closed
door, G chooses the low lens and crouch automatically; corner/over-cover modes
use two-segment sweeps. Lens yaw/pitch is independent of the officer/stem, and
inspection locks body movement/fire. Hearing stays at the officer's body;
direct sniper control listens at that sniper.

## Materials, throwables and generation

One material table supplies color, damage resistance, transmission/absorption,
density, contact friction/restitution/rolling resistance and impact/footstep
timbre. Box3D shape materials use those values; restitution combines the two
surfaces geometrically. A hollow-canister mass overrides solid steel density
for its spherical body. Canisters use gravity and bullet CCD; contact events
produce impacts using the struck material. Flash/CS exposure traces through
live world geometry, so broken cover changes exposure. Taser hits use a short
checked trace. All equipment rules run on authority and preserve finite stocks.

Reusable lockpicks unlock a closed leaf after an interruptible hold. A separate
mount hold consumes a charge; an edge-triggered remote detonates only charges
owned by that officer. Breaching removes the same collision object seen by all
actors and clients. Frames remain physical. The simplified blast uses live
world visibility for exposure, injury and material-aware sound; it does not
simulate explosive chemistry, pressure waves, fragmentation or flying debris.

The building policy chooses twelve categorical grammar tokens with a tiny
49→64→3 tanh network in C. Validation precedes physical construction, including
the exact wall-piece count. A shipped bootstrap model was trained from authored
scores; explicit local player comparisons feed an optional offline reward-model
and policy-update pipeline. [GENERATION.md](GENERATION.md) records the format,
data provenance, tests and limits. The layout model does not control NPCs.

## Modifying the game with ordinary code

Keep additions in straightforward C until real usage justifies a data format
or editor. These are the intended entry points for a developer or coding agent:

| Change | Entry point |
| --- | --- |
| House walls, openings, room dimensions, staging or camera posts | `mission.c` builders and mission table |
| Add a scenario | `SwatMission`/`SwatMissionDef`, build/spawn branch in `sim.c`; update protocol bounds/tests |
| Material color, resistance, contact physics, absorption/transmission or impact timbre | `materials.c`; material enum in `materials.h` |
| Canister motion, flash/CS exposure, masks or taser | `tactical.c`; shared geometry and material tables |
| Sniper assignments, visibility, interlocks and orders | `overwatch.c`; rifle profiles in `weapons.c` |
| Generated room grammar, acceptance rules or learned policy | `generation.c`, `layout_tool.c`, `train_layout.py` |
| Player comparisons | `feedback.c`, Houses tab; no automatic upload or training |
| Kit mass, speed, torso protection and tools | `equipment.c` and authority equipment step in `sim.c` |
| Weapon timing, capacity, spread and impact behavior | `weapons.c`, or an optional `--weapon-data` file (see [EQUIPMENT.md](EQUIPMENT.md)); authority hit processing in `sim.c` |
| Injury/compliance/arrest/objective behavior | `sim.c`; add scenario checks in `tests/test_mission.c` |
| Acoustic routing/room approximation/source sound | `acoustics.c` / `audio_dsp.c`; compare with `audio_lab.c` |
| New remote command/state | `SwatInput` and `protocol.c`, with protocol version/round-trip tests |

Change simulation rules once so solo, co-op and future policy actors agree.
Do not put authoritative damage or kit changes in the UI. Protocol v8 peers
must agree on behavior; mod compatibility negotiation/hot reload is future work.

| Tier | Purpose | Agreement to verify |
| --- | --- | --- |
| Fast tasks (future) | Large locomotion/perception/coordination curricula | Input semantics, material/weapon rules, outcomes |
| Authoritative Box3D (current) | Reference training, evaluation and replay | Exact game mechanics and seeded state |
| Rich perception game (future) | Audio/rendered policy transfer and qualification | Gameplay plus sensor timing/occlusion/noise |

Faster tiers must earn compatibility through paired input replays, collision,
damage/acoustic cases and held-out missions. Final game evaluation determines
policy quality. There is no second approximate training simulator yet.

## Next useful slices

1. Integrate art/animation with actual eye, muzzle and collider poses; build an
   interaction/test range and saved input replay.
2. Expand the house into replayable missions with useful suspect/civilian
   behavior, evidence, rules of engagement and equipment slots; evaluate generated
   houses and sniper sight lines with players.
3. Produce material/weapon/footstep audio and calibrated room acoustics before
   audio policy training.
4. Improve remote movement presentation, measure loss/bandwidth, and qualify
   a real two-machine LAN session before Internet service features.
5. Version multi-role observations, measure scripted/frozen baselines, then
   build curricula and multiple fidelity tiers against game behavior.

## Weapon pose and replay foundation

`pose.c` defines eye, shoulders, hands, sight and muzzle from the achieved
controller state and the active weapon definition. The render model and
authoritative eye-to-muzzle sphere sweep use this same pose. High/low ready
raise before a buffered trigger is permitted to fire. Reload removal/insertion/
chamber events conserve ammunition even when interrupted at any tick. Optional
retained magazines and sight profiles are replicated; profile changes require
an unused officer in staging. Protocol v8 includes these fields and pitched
world objects for the controller range.

`replay.c` stores explicit portable config/seed/input/state-hash records and
`replay_tool.c` verifies them in the same simulation. It records only solo
officer input from round start. It does not serialize live physics warm starts,
custom layout-model weights, or multiplayer authority. A journal recorded with
loaded weapon data stores only that table's id, and it opens only when the
same data is loaded. F3 debug rendering is
read-only and uses actual actor/muzzle/sensor geometry.

`pacing.c` is the only place rendered frames become simulation ticks. It keeps
time in integer units (14400 per second), so frame rates that resolve every
input change produce bit-identical tick inputs, and presses shorter than a
tick are latched into the next tick. The viewer, host and client loops share
it; `tests/test_pacing.c` and the weapon range test hold it to that. The
`weapon-range` mission (`swat_mission_build_weapon_range`) is the fixed scene
for weapon/controller determinism checks; its target and cover coordinates are
exported from `mission.h` for tests.

## Tactical encounter authority

`encounter.c` owns cached planar clearance navigation and scripted role state.
Its grid is built from actual Box3D overlap queries on supported flat ground;
world generation invalidates it after destruction or wedge/tool changes.
Planning allows an unwedged door, while movement must open/pick its physical
leaf. This first navigator does not handle stair floors or flying actors.

Tactical behavior is opt-in in `SwatConfig`, preserving the v1 annex/checkpoint
defaults. Sight checks use the first physical ray hit. Hearing gives a bearing
and a coarse investigation point; no hidden actor position is copied into
NPC memory. Squad orders are authority-derived from the commander's sight ray,
delayed 18 ticks and optionally queued. Only bot officers accept orders. Human
joins replace bots; departure/restart restores configured vacant-slot bots.
Public behavior/order/escort state, evidence and debrief totals are replicated
in v6; private targets, memory and personality are excluded from the wire.

Door wedges, partial peeks and trap knowledge share the same authoritative
objects as hinges/destruction. Spray uses finite stock, a short forward cone,
real cover and masks. Evidence is collected once; rescue and force violations
are debrief outcomes. A force violation is one engagement: an officer's or
sniper's hits on the same protected person merge while each follows the previous
within one second, and unlawful damage still sums every hit. Under tactical
rules a wounded suspect or civilian flinches: a deterministic aim kick away from
the shooter, then 0.3 s (`SWAT_FLINCH_TICKS`) without firing. Officers and the
annex contract are unchanged. `gear.flinch_ticks` replicates from wire version
50, and `swat_actor_flinch_weight` is the presentation hook. The force record currently classifies civilian or
surrendered/restrained harm, rather than a complete legal judgment model.

Free civilians react only to what they perceive: their own wound, a heard
shot/breach/flash bearing, or a visible suspect who is firing or aiming at
someone. Each fear episode picks freeze, cower (crouch) or flight from a hash
of the round seed, layout seed, actor and episode, biased by resolve; no wall
clock or shared RNG stream is used. A heard bearing makes a fleeing civilian
back up to 1.8 m away from it without turning, then stand. A seen gunman makes
it turn and move to floor the gunman cannot see, else through one of its room's
open doorways on the far side, then crouch there. Refuges stay in the occupied
room, outside leaf swings and fire lanes, with a lane-clear straight approach.
Panic routes treat visible suspects' lanes as officer lanes and never use doors.
Seeing police keeps the earlier back-away response; a panicked civilian that
has seen police in the last ten seconds then stands still for commands.
A calm civilian always obeys a command. A panicked one obeys a first command
with chance 0.35 + 0.3 * resolve, each further command in the 10 s window adds
0.25, and the third always lands. An order that does not land stops any flight
(cower in place). A civilian who obeys an order heard only (they cannot see the
officer, and the officer cannot see them) walks, never runs, to the best spot
within 4 m against a wall or in a corner, away from the direction the voice
arrived from and with the refuge planner's room, leaf and fire-lane limits,
and with clear floor 1 m away in at least 5 of 16 directions so an officer
can reach them to cuff (`civilian_cover_spot`; the dead end of an aisle
beside a bed is not a spot), then kneels with hands shown; with no such spot, or
after 4 s, they kneel where they are. A gunman in sight breaks off that walk.
Restrained and escorted civilians are unchanged.
The fear state is private (`SwatMind.panic*`), reconstructed by input replay;
the public behavior stays `SWAT_FLEE`.

### Rules-of-engagement state machine

`roe.h`/`roe_impl.h` (included by `sim.c`) keep one perceived ROE state per
suspect and civilian in `SwatSim.roe`. After every tactical tick, each living
officer and sniper (24 m, sniper 120 m, 60 degree half field, the shared optical
sight check) looks at each person; the nearest observer judges motion. Only what
they see changes the state: hidden intent, ammunition, resolve and AI decisions
never do, so a feigned surrender reads as a surrender.

The head does not decide. A person is seen when any of three body probes
reaches them (`swat_sim_actor_visible`: head, chest at 60% and hips at 35% of
the way from feet to eye, tried in that order, each with the range, field and
lighting tests), which is enough to see posture, movement and muzzle flash.
Whether they are armed is decided by the hands (`swat_sim_hands_seen`): a
sight ray from the observer's eye must reach a hand or the muzzle of the drawn
weapon pose (`swat_sim_weapon_pose`) for WEAPON, or both empty hands for
EMPTY (raised beside the head for a suspect who threw the weapon down, at the
hips for a civilian; hands up or cuffed are always EMPTY to anyone who sees
the person); the person's own body never hides their hands, other people and
the world do. Otherwise the hands are HIDDEN. Each tick the team's evidence
is WEAPON if any observer sees a weapon, else EMPTY if any sees empty hands,
else HIDDEN; WEAPON sets `SwatRoePerson.armed_seen`, EMPTY clears it.
`swat_sim_observed_weapon_threat` (a raised weapon) also needs that
observer's hands reading to be WEAPON. A person whose hands have never been
seen stays unknown: suspicious, never a lawful lethal target on that basis.

Classification, first match wins: surrendered; seen firing within 2 s and
the hands not seen empty (hostile-firing, and the weapon is on the record; a
shooter seen dropping the weapon has stopped firing and falls through); a raised weapon seen
(hostile-armed); on the record as armed and already hostile (hostile-armed);
flight (fleeing); on the record as armed after a challenge has run 1.5 s
(hostile-armed); hands seen empty (compliant); otherwise unchanged. The state is
authority-only: not observed by any policy, not replicated, rebuilt by input
replay.

| State | Seen as |
| --- | --- |
| unknown | never assessed, hands never seen, or seen armed with the weapon low and not yet challenged |
| compliant | hands seen empty (civilian, or a suspect who threw the weapon down), not running |
| surrendered | kneeling with hands shown (`gear.surrendered`), genuine or feigned |
| restrained | cuffed; applied even when no officer is looking, since hands were on them |
| fleeing | moving away from the nearest observing officer faster than 1.6 m/s without a shown threat |
| hostile-armed | weapon seen in hand raised and aimed at a person (`swat_sim_observed_weapon_threat`), seen armed after a shown threat, or seen armed and still not compliant 1.5 s (`SWAT_ROE_CHALLENGE_TICKS`) after a command they heard |
| hostile-firing | seen firing within 2 s (`SWAT_ROE_FIRING_TICKS`) and hands not seen empty; unseen for 2 s, it decays to hostile-armed |
| down | dead; terminal |

Legal transitions (`swat_roe_transition_legal`): any state may stay; nothing
leaves down and nothing returns to unknown; restrained leads only to down, and
any living state may become restrained; surrendered leads only to restrained,
down, hostile-armed, hostile-firing or fleeing (the last three mean the
surrender was feigned and are counted in `debrief.feints`); compliant never
becomes hostile (civilians carry nothing and a dropped weapon cannot be picked
up). Every other pair is legal: 43 of 64. A perceived change the table rejects
is refused and counted in `SwatSim.roe_refused`, which the tests require to stay
zero.

Lethal force is authorised only against hostile-armed and hostile-firing.
`swat_roe_force` runs before each hit is applied: a lethal (not less-lethal)
hit by an officer or sniper on a suspect or civilian in any other state is a
lethal violation, merged per shooter and victim inside the one-second engagement
window like `roe_violations`, and counted per hit on the shooter's force
ledger (`SwatForce.lethal`), which the role env merges with protected force
into the held-out suite's ROE violations (suite v2). The existing protected-person ledger is unchanged,
so a carbine round into a civilian counts in both; a round into an armed suspect
who has not been challenged, has not raised the weapon and has not fired counts
only as lethal. Explosive charge blasts are lethal force. Unattributed and
suspect-inflicted harm is never an officer violation.

### Suspect surrender

`surrender_impl.h` (included by `encounter.c`) decides what a scripted suspect
does under pressure. A command the suspect hears (below) and, without any
order, a fresh wound or running the weapon dry while an officer is in sight
force one decision. The suspect perceives (`swat_suspect_pressure`):

```text
pressure = 0.18*min(commands,3) + 0.12*min(officers,3) + 0.16*min(aimed,3)
         + 0.55*wounds + 0.40*(1-ammo) - 0.15*min(allies,2)
```

`commands` are orders heard in the last 10 s; `officers` are officers/snipers
the suspect sees, plus the one whose order it just heard; `aimed` are seen
officers whose aim ray reaches the suspect first; `wounds` is 1 - health/100;
`ammo` is rounds left over a full load; `allies` are free armed suspects seen
within 10 m. `will` is the suspect's resolve (0.2-0.95, seeded per actor).

With `margin = pressure - will` and a roll hashed from round seed, layout seed,
actor and decision count (`swat_suspect_choose`):

- margin >= 0 breaks the suspect. If no gun is aimed at them, they hold ammunition
  and are wounded under half, they feint with chance 0.1 + 0.35 * will; with no
  officer within 4 m they drop the weapon and run with chance 0.25 * (1 - wounds);
  otherwise they comply.
- -0.25 <= margin < 0 makes them waver: flee with chance 0.6 (0.25 if a gun is on them), else fight.
- lower margins fight on.

A stunned, badly hurt (health below 40) or already disarmed suspect always
complies. Compliance kneels and drops the weapon as evidence. Dropping sets the
replicated `gear.disarmed`: the weapon becomes evidence, the suspect cannot aim,
fire or reload, runs for 4 s, then hides and complies with the next order. A
feint kneels like a genuine surrender but keeps the weapon hidden (no evidence)
for at least 2-5 s, and draws only when no officer is within 3 m and no gun the
suspect can see covers them; cuffing a feint finds the weapon. An armed runner
uses the civilian refuge planner (cover in the room, else a far open doorway),
sprinting, for 4 s, then turns back toward the officer last seen and fights.
A suspect who hears an order, fights on or flees, and sees no officer
investigates a coarse point 4 m along the heard bearing, as for a heard shot:
shouting gives the police away. Squad officers never fire at a disarmed suspect. Suspects whose inputs come from
a policy (`mind.external`, set by the role environment) keep the fixed rule
(surrender when resolve < 0.48, stunned or health below 40) and take no scripted
decisions.

### Heard commands

A tactical command is heard, not aimed (`swat_sim_command_heard` in `sim.c`).
The officer's compliance shout (strength 1.2, range 22 m) or the communication
ball's (range 20 m) is emitted as an ordinary `SWAT_SOUND_COMMAND` event, read
back from the sound log, and delivered to every present, living, unrestrained
suspect and civilian not yet surrendered whose acoustic path
(`swat_acoustic_path`) to their eye carries at least `SWAT_COMMAND_HEARD_GAIN`
(0.01). The path is one straight ray without diffraction, so a beam between
mouth and ear would silence a voice at arm's length: anyone the officer's
aimed reach names (`swat_sim_command_reaches`) also hears it. Everyone who
hears is challenged on the ROE record, but only an order addressed to the
person (that aimed reach, or a ball they can see) restarts a response time
already running: otherwise one officer's routine calls would keep resetting
the clock of a suspect a teammate has challenged, and the teammate's lawful
shot after 1.5 s would land on a person the record still holds unknown. Everyone who hears is weighed by the surrender model
(`swat_person_commanded`), with `heard_only` set when the person cannot see
the speaker (for the ball: no clear ray between ball and eye within 9 m) and
no officer speaking in person has them in sight. The ball's owner is its
commander. The training annex keeps its aimed reach and its rule.

Measured gains for the shout in the motel (Standard), house and building
missions, sampled on every pair of door-approach, room-grid and outdoor points
(minimum of the heard cases, maximum of the unheard ones):

| Route | Gain |
| --- | --- |
| Same room or open ground, 15 m | 0.075 |
| One open doorway, bent route of 11-12 m | 0.053 |
| One closed timber door, crossed within 45 degrees, 5-6 m | 0.017 (motel), 0.019 (house, building) |
| Two closed doors, up to 6 m | at most 0.007 |
| Exterior wall, 9-11 m | at most 0.005 (motel), 0.007 (house), 0.008 (building) |

Tests: `make encounter-test` runs `tests/shout_hearing.h` (three civilians at
0, 50 and 120 degrees from the aim, an open door, two closed doors, a suspect
behind a wall, and the ball). `make roe-test` (part of `make test`, about 3.5 s) covers the table,
every perceived transition, lethal and less-lethal force, the pressure terms,
each decision and its behaviour (comply, drop, feint, flee, fight, spontaneous
yield), the policy rule, civilian orders, rescue, a blocked firing line,
determinism and replication, and in real geometry: a head hidden behind a
beam with torso and rifle seen (hostile-armed when raised, lawful), only the
head over a low wall (unknown through a challenge and its expiry, a lethal
hit is a violation), stepping out and back, a civilian over the wall
(unknown) and in the open (compliant), and a weapon dropped behind full
cover (no probe sees, the record holds; empty hands then read compliant), and
a shooter dropping the weapon inside the 2 s window: in plain view compliant
at once (a lethal hit is a violation), behind a slit wall that shows only the
head hostile-firing, then hostile-armed (lawful). Every motel completion run (`make motel-completion-test`)
checks each perceived transition against the table and requires zero lethal
violations and zero refused transitions; the interaction-isolation runs
finish with both suspects arrested and none killed.

## Physical payloads and remote devices

`devices.c` uses authoritative CCD bodies for throwable cameras, robots,
communication balls and drones. Camera yaw/pitch is stabilized independently
of the body's tumble. Slow robot/drone velocity requests still collide with
the same world. Remote commands are owner-validated; the officer receives a
neutral stance input while its body remains present. Batteries and damage
limit feeds. A communication ball has no optical camera and emits command
audio from its physical position; its orders are heard and weighed like an
officer's (see "Heard commands"). The player mixer can listen at a device
location using the same acoustic paths.

Projectile kinds now include PepperBall, impact, CS/flash launcher, probes and
experimental tether rounds. Actual swept flight resolves contacts; payload
effects use the existing cover/mask/region rules. CEW probes attach in actor
local coordinates with stance-relative height. Two separated contacts require
clear world-space tether paths before applying a temporary stun. Primary
payload profiles are selected before deployment; reserve rounds retain the
profile's type rather than changing on a UI selection. Protocol v8 replicates
these states and validates bounded inventories/remote inputs. Replica devices
render public state without creating an independent dynamic authority.

## Rigid weapon calibration and solo persistence

The private rigid carbine is an optional presentation asset, with the measured
interface documented in [WEAPON_ART.md](WEAPON_ART.md). Its points also define
authoritative carbine clearance and firing origins without the asset installed.
That semantic change bumps peers and journals to v8; old v7 clients/recordings
are rejected. Full skeletal character playback remains pending.

Solo F5/F9 checkpoints copy a journal atomically and restore into a temporary
simulation by verified replay. Physics body tags are retargeted after transfer;
failure preserves the live world. Continued recording copies the verified prefix
and appends future input. The frontend reconciles the restored kit and profiles,
resets outstanding UI actions, and invalidates presentation caches. Journals
require the same procedural model and load by replaying every recorded tick.
Co-op persistence and bounded-time snapshot restoration remain future work.

## Training throughput and shared static geometry

Training steps many independent `SwatSim` instances on CPU worker threads.
Box3D reserves world slots from a process-wide pool, so `b3CreateWorld` and
`b3DestroyWorld` run under `omp critical(swat_world_lifecycle)`. Everything
between them, including building the map, spawning and settling, belongs to
one sim and runs in parallel with other resets.

Static motel collision meshes are pure functions of compiled data. A
process-wide cache in `world.c` returns one immutable `b3MeshData` per distinct
`b3CreateMesh`/`b3CreateMeshSubset` input, keyed and compared byte for byte,
so every motel world and reset shares it. Box3D only reads mesh data after
creation. Worlds keep their own pointer lists but never destroy cached meshes,
which live until process exit. The cache has its own critical section;
concurrent first builds may each build a mesh, and the first insert wins. A
mesh that varies at runtime must not use the cache. Fractured or rubble
geometry, storefront meshes and hulls are still created per world.

Throughput work must keep the simulation bit-identical. `make determinism-test`
replays recorded per-tick state hashes; see README "Role and motel simulation
throughput" for the benchmark, measurements and profile.
