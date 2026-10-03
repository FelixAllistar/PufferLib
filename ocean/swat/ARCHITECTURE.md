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
slots 1/2/3 use actors 3/4/5. House NPCs also occupy actors 6/7/8. The
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
moving to an occupied slot on disconnect. Only the leader can restart. A new
round epoch invalidates queued old inputs/snapshots. There is no host migration:
closing the listen host ends the session. Leaving online play restores solo.

## Protocol and presentation

`protocol.c` explicitly encodes big-endian integers and IEEE float32, validates
version/type/length/ranges and decodes into temporary storage before applying.
C layouts, pointers and platform bool representations never cross the wire.
Protocol v2 includes mission/room data, framed-wall part/material metadata,
rotated door bases, kit/tool commands, regional injuries and restraints. The
1,100-object house uses a reliable map baseline and fragmented state packets.

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
without advancing physics; destroyed cover loses its collider. Old revisions
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

Planning's cutaway shows authored geometry without hidden actors. Fixed
overwatch previews use real exterior camera positions and ordinary depth
occlusion. They are initial preview tools, not a piloted drone or autonomous
sniper. Optiwand cameras sweep a small sphere through actual geometry; the
officer's hearing position stays at their body.

## Modifying the game with ordinary code

Keep additions in straightforward C until real usage justifies a data format
or editor. These are the intended entry points for a developer or coding agent:

| Change | Entry point |
| --- | --- |
| House walls, openings, room dimensions, staging or camera posts | `mission.c` builders and mission table |
| Add a scenario | `SwatMission`/`SwatMissionDef`, build/spawn branch in `sim.c`; update protocol bounds/tests |
| Material color, projectile resistance, absorption or transmission | `materials.c`; material enum in `materials.h` |
| Kit mass, speed, torso protection and tools | `equipment.c` and authority equipment step in `sim.c` |
| Weapon timing, capacity, spread and impact behavior | `weapons.c`; authority hit processing in `sim.c` |
| Injury/compliance/arrest/objective behavior | `sim.c`; add scenario checks in `tests/test_mission.c` |
| Acoustic routing/room approximation/source sound | `acoustics.c` / `audio_dsp.c`; compare with `audio_lab.c` |
| New remote command/state | `SwatInput` and `protocol.c`, with protocol version/round-trip tests |

Change simulation rules once so solo, co-op and future policy actors agree.
Do not put authoritative damage or kit changes in the UI. Protocol v2 peers
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
   behavior, evidence, rules of engagement, equipment slots and overwatch orders.
3. Produce material/weapon/footstep audio and calibrated room acoustics before
   audio policy training.
4. Improve remote movement presentation, measure loss/bandwidth, and qualify
   a real two-machine LAN session before Internet service features.
5. Version multi-role observations, measure scripted/frozen baselines, then
   build curricula and multiple fidelity tiers against game behavior.
