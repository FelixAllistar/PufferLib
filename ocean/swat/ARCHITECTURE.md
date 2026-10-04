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
harm by any member fails. The house also requires all living civilians cuffed;
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
Protocol v4 includes mission/room data, framed-wall part/material metadata,
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
contract and curriculum. Human play and dedicated servers default to the house;
policy playback and the current training adapter default to the annex.

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
person and additionally checks a visible 15-degree forward cone, so a shout is forgiving without commanding
occupants behind the officer. The annex v1 interaction remains a direct ray.
F/middle mouse selects use or compliance; RMB latches cuff, pick or aim on
press. Lost targets cancel progress, cannot transfer the held cuff to another
person, and finishing does not change that press into ADS or a weapon click.
These bindings reuse existing protocol v4 commands; the server still computes
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
| Weapon timing, capacity, spread and impact behavior | `weapons.c`; authority hit processing in `sim.c` |
| Injury/compliance/arrest/objective behavior | `sim.c`; add scenario checks in `tests/test_mission.c` |
| Acoustic routing/room approximation/source sound | `acoustics.c` / `audio_dsp.c`; compare with `audio_lab.c` |
| New remote command/state | `SwatInput` and `protocol.c`, with protocol version/round-trip tests |

Change simulation rules once so solo, co-op and future policy actors agree.
Do not put authoritative damage or kit changes in the UI. Protocol v4 peers
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
