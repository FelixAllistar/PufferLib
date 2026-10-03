# SWAT: Gold Element architecture

The north star is a portable co-op tactical shooter with persistent local
destruction and convincing RL-controlled characters. Develop the game first,
using scripted NPCs to qualify its rules before investing in trained policies.

## Technology

Keep C, Raylib 5.5 for presentation/audio/input, the pinned Box3D fork for
physics, and ENet 1.3.18 for direct UDP co-op. The player and dedicated server
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
slots 1/2/3 use actors 3/4/5. The single-officer Ocean adapter is preserved.

The server chooses the map, rolls randomness, advances doors, accepts shots,
damages cover/removes colliders and decides outcomes. Clients send inputs and
render authoritative replicas. Initial reliable map/snapshot transfer includes
holes and doors when joining mid-round. All living officers must extract;
an individual death leaves survivors playing, squad death fails, and civilian
harm by any member fails.

The listen host leads; the first occupied dedicated slot leads, with leadership
moving to an occupied slot on disconnect. Only the leader can restart. A new
round epoch invalidates queued old inputs/snapshots. There is no host migration:
closing the listen host ends the session. Leaving online play restores solo.

## Protocol and presentation

`protocol.c` explicitly encodes big-endian integers and IEEE float32, validates
version/type/length/ranges and decodes into temporary storage before applying.
C layouts, pointers and platform bool representations never cross the wire.

| Channel | Content | Delivery |
| --- | --- | --- |
| 0 | Control, map and initial snapshot | Reliable |
| 1 | Sequenced input bound to connected slot | Reliable |
| 2 | Complete state snapshots at 30 Hz | Unreliable sequenced |

Snapshots carry input acknowledgements, actor pose/weapon state, cover HP,
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
audio observations; hearing-enabled policies need a new versioned contract.

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
2. Author a small multi-room mission with surrender/restraint, useful suspect
   and civilian states, and door equipment.
3. Produce material/weapon/footstep audio and calibrated room acoustics before
   audio policy training.
4. Improve remote movement presentation, measure loss/bandwidth, and qualify
   a real two-machine LAN session before Internet service features.
5. Version multi-role observations, measure scripted/frozen baselines, then
   build curricula and multiple fidelity tiers against game behavior.
