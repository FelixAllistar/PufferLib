# Cretaceous netcode

The server owns the sim. Clients send inputs, predict their own animal, and draw everyone else from a snapshot buffer.
Solo play never touches the network.
Code: `net.c` (codec, server, client transport), `netprops.c` (v11 movable props, included by `net.c`), `netcritters.c` (v12 critters, included by `net.c`), `netclient.c` (prediction, reconciliation, interpolation), `netsim.c` (link conditioner), `net_priv.h` (internals shared with the tools).
Tools: `tools/netbench.c` (scenario benchmark) and `tools/netfuzz.c` (codec round trip and fuzzer).
Tests (`make test`, docs/testing.md): `tests/test_net_codec.c` (the net code against an in-memory ENet: round trips, atomic snapshots, fuzz) and `tests/test_net_respawn.c` (respawn, profile and status protocol over loopback ENet).

## Protocol v13

Versions:
- **v6** (5.0): byte codec; respawn requests carry a sequence number and the server answers each one it settles.
- **v7** (cret/net): this bit-packed codec with per-client delta snapshots, prediction and interpolation, carrying v6's held requests and replies. cret/progression also called its own byte codec "7"; neither shipped alone.
- **v8**: v7 plus progression. Abilities ride in inputs; snapshots carry statuses, the own status timers and cooldowns, the own progress block and the nests; profile and unlock requests.
- **v9** (cret/controls): aimed attacks and the secondary attack. Inputs carry the head aim and `CRET_ACT_ATTACK2`; every agent carries its aim; the own state adds the secondary cooldown, the queued attack and the aim.
- **v10** (cret/feel): hit events for combat feedback (`render/fx.c`). Each snapshot ends with the recent hits near the client.
- **v11** (cret/physics): movable props. A prop block follows the hits: the boulders, stones and logs the server's physics moved, acknowledged per prop. Hit kinds grow to 4 bits (`CRET_HIT_CRUSH`).
- **v12** (cret/critters): small prey. A critter block follows the props: the client's nearest critters, whole every snapshot. Quests gain the catch kind and a spot flag (a hatchling's quest leads to a point, sent as x, z and radius, instead of a named place).
- **v13** (cret/nests-carry): carrying food. Inputs carry the carry press in the top bit of the flags; the own state ends with the carried slot and load; carcass entities gain the item kind, the critter kind and the carrier.

All messages are bit-packed (MSB first) through a bounds-checked reader and writer.
A short or malformed message sets a flag rather than reading past the end, and the receiver refuses the whole message.
C structs never cross the wire.

| Message | Channel | Content |
|---|---|---|
| connect | ENet connect data | `CRET_NET_VERSION`. A server refuses any other version and disconnects with `'V' << 24 \| its version`; the client prints both versions. |
| WELCOME | reliable | version, world seed, world size, cell, agent count, slot, tick rate, snapshot divisor |
| INPUT | unsequenced, 30 Hz | snapshot ack (newest received seq, plus a 32-bit mask of the ones before it); newest input seq; 1..32 inputs, newest first. Each input is 40 bits (v9; v8: 32): forward as a signed byte, then strafe and turn as signed bytes, sprint, jump, a 3-bit action, a 2-bit ability slot and the carry press (v13, the top bit; 0 before), and the aim as a signed byte (-127..127 for -1..1), or 1 bit when it repeats the next newer one. The server refuses the packet when any input has an action past `CRET_ACT_MAX` (`CRET_ACT_ATTACK2`), an ability slot past `CRET_AB_SLOTS` or an aim byte of -128. |
| SNAPSHOT | unsequenced, 15 Hz per client | seq, server tick, last input applied, server input queue depth, time of day, cloud cover; own state at full precision; progress and nest blocks; hits; movable props (v11); critters (v12); entity updates |
| REQUEST | reliable | 8-bit request seq; species, or 255 for the same one |
| SPAWN_REPLY | reliable | once per settled request: seq, result (ok, bad species, in combat), species after the reply, seconds until the combat lock lifts |
| PROFILE | reliable | character name: 5-bit length (up to 23), then the bytes. The host loads and saves that character (`character.c` cleans the name). |
| UNLOCK | reliable | 1 bit: buy or equip slot-3 ability choice A or B (only while alive) |

Respawn requests (from 5.0's v6, unchanged in meaning):
- The server picks every spawn point; a request carries no position.
- A request within 1 s of the client's last respawn is held until the second is up. A newer request replaces a held one, and only the newest gets a reply.
- A living animal hit in the last 10 s is refused (`CRET_SPAWN_IN_COMBAT`, with the seconds left). A dead one always respawns.
- An unknown species is refused at once (`CRET_SPAWN_BAD_SPECIES`).
- Joining and leaving clear the slot's species pick, so a bot taking over gets the default mix.

Snapshots are per client, so each client gets its own content:

- **Own state**: alive, ground, swim and sprint flags, action, action ticks, attack cooldown, flinch, food, water, then 11 floats: position, heading, velocity, speed, stamina, broken-bone time and growth. This is what the client rewinds to. About 50 bytes.
  - v8 adds what movement and the HUD read: the special strike in progress, latch target, raptors latched on, leap and its time left, nest-building ticks, slow amount, defensive speed, each status timer (1 bit when off, else 20 bits in ms) and the three ability cooldowns (ms). About 10 bytes with no status on.
  - v9 adds the secondary attack's cooldown (8 bits, ticks, capped at 255), the queued attack (3-bit action, 0 or an attack, and 3-bit ticks left, both 0 when nothing is queued) and the aim (the input's signed byte). 22 bits. The decoder refuses a queued action that is not an attack, more than 7 ticks, an action without ticks (or ticks without one) and an aim of -128. Prediction compares the cooldown and the queue too.
  - v13 adds what the animal carries: the carcass slot + 1 (6 bits, 0 for nothing) and, when it carries something, the load in kg (32-bit float), which slows it. 6 bits, or 38 while carrying. The decoder refuses a slot past `CRET_MAX_CARCASSES` and a negative load. Picking up and putting down are the server's (prediction never changes them); prediction compares the slot and the load, so the next snapshot after a pickup corrects the predicted speed.
- **Own progress** (v8): marks, unlocked slot-3 choices, quests done, the three loadout abilities, the quest (kind, places, step, prey species, progress and goal as floats, rewards) and the group's quest. About 30–50 bytes, behind a presence bit: sent for 8 snapshots after it changes and once a second regardless (it is unreliable, like the rest of the snapshot). A quest in progress therefore goes out in every snapshot.
- **Nests** (v8): all active nests (species, group, owner, eggs, position), on the same change-or-once-a-second rule. A client therefore sees the same nests and egg counts as the server, at most about a second late; the renderer draws them from these (`render/nest.c`). Build progress is sent only for the client's own animal (`nest_ticks` in the own state); other animals' nests rise on the client's own clock while they hold the nest action.
- **Entities** (agents 0..127, carcasses 128..159): id (8 bits), baseline age (5 bits, 0 = from zero), then each field against the baseline.
  - Quantisation: positions in 1/256 m (21 bits for the 8 km island, sized from the world), height from −2048 m, heading in 12 bits, speed in 1/16 m/s, health, growth and meat in 8 bits.
  - Flinch and death are sent as the tick they end or start (16 bits), so they do not change every tick.
  - Delta fields (position, heading, speed) use a 2-bit code: same, small signed delta, medium signed delta, or full value. Other fields use one changed bit.
  - v8 adds two agent fields: 8 status flags (stunned, afraid, latching, latched, defensive, marked, sprint burst, mending) and who marked it. Remote animals get status timers of 1 or 0; only the local animal gets real timers.
  - v9 adds the aim: 6 bits, 0..62 for -1..1 in steps of 1/31 (the head turns toward it, `render/dino.c`). It changes only with the bit-per-field change flag.
  - v13 widens the carcass kind field from 4 to 9 bits (active, species, then the item kind in 2 bits: body, critter or egg, and the critter kind in 3) and adds the carrier (8 bits, agent + 1, 0 on the ground). A carried item's position is the carrier's mouth on the server; the client draws it at the drawn carrier's jaws (`cret_carry_follow` on the interpolated copy), so it does not lag behind the animal.
  - The decoder range-checks species, action (up to `CRET_ACT_MAX`), aim (up to 62), group and the carcass's agent, item kind, critter kind and carrier; the v8 blocks check ability ids, quest kind, step and prey species, group and nest owner, and refuse non-finite floats.
- **Hits** (v10): the sim logs every landed blow in a ring it never reads (`sim.h`, `CretHit`: tick, attacker, victim, kind, damage as a fraction of the victim's max health in 1/255). Each snapshot repeats the hits of the last `NET_HIT_WINDOW` ticks (6, so one lost snapshot loses none) that involve the client, then those whose victim is within 160 m, at most 24, newest first.
  - Per hit: a 5-bit count, then age in ticks before the snapshot (7 bits), attacker and victim (7 each), kind (4 since v11; 3 before) and damage (8, at least 1). 35 bits a hit; a quiet snapshot pays 5 bits.
  - The decoder refuses a count over 24, an age past the window, an attacker or victim at or past the agent count, an animal hitting itself, an unknown kind and zero damage.
  - The client drops repeats against the last 64 hits it saw and appends new ones to its sim's hit log, where the feedback code reads them as it reads the local sim's.
  - Hits are presentation only: prediction ignores them and nothing in the sim reads the log.
- **Movable props** (v11, `netprops.c`): the sim keeps a record for every boulder, stone, log or driftwood it moved (`CretSim.dyn`, `dynprops.c`); the rest sit at their scattered pose, which client and server derive from the world seed.
  - Per prop: id (20 bits, the collider index), a gone bit (the server dropped the record: the prop is back at its scattered pose), else x and z (`pos_bits`, 1/256 m), y (21 bits from −2048 m), the rotation as smallest three (2-bit index and 3 × 10 bits, within about 0.3°) and a rest bit. 109 bits on the 8 km island; a 5-bit count first.
  - The server keeps, per peer, the state each prop's client acknowledged and the last one sent (up to 512 props). A prop goes out when its quantised state differs from the acknowledged one, or from an unacknowledged send; a send in flight is repeated only after 8 snapshots. Props at rest and acknowledged cost nothing.
  - Priority accumulates by distance as for entities; coming to rest or going away adds 1. At most 31 props and 300 bytes per snapshot, written before the entities.
  - The client keeps 8 samples per prop by server tick. Applying a snapshot puts each prop at its newest pose in the client sim (asleep: collisions for prediction, no physics there); the draw copy interpolates them at the render tick, as remote agents. Gone props are remembered (1024 recent ids with their seq), so a late snapshot cannot bring one back.
  - Nothing in the client sim simulates props. Pushing a boulder is the server's; the client's animal leans on the boulder where the last snapshot left it.
- **Critters** (v12, `netcritters.c`): the small prey of `critters.c` live only in the server's sim, around people.
  - Each snapshot carries the client's 24 nearest critters within 80 m, whole (no baselines or acks): a 5-bit count and a reference point (whole metres near the client's animal, 16 bits each for x and z), then per critter its pool slot (9 bits), kind (3), state (3), x and z as 1/16 m offsets from the reference (12 signed bits each, ±128 m), heading (6), height above the ground (4, at 0.5 m: a bird flying off), mass within its kind's range (6) and speed (4, at 0.5 m/s). 59 bits a critter, at most 177 bytes; a snapshot with none pays 37 bits.
  - The decoder refuses a count over 24, a slot past the pool, an unknown kind or state.
  - The client replaces its critters with the newest set and puts each on its own terrain (fish under the surface); the renderer eases each pool slot toward its new position (`render/critters.c`), so the 15 Hz steps do not show. A critter left out (eaten, gone, or no longer among the nearest) vanishes until it is sent again.
  - Catching is the server's: a client's bite resolves there, and the eaten critter is gone from the next snapshot.
- **Quest spots** (v12): the progress block's quests add a spot bit; with it set, three floats (x, z, radius) follow. A radius outside 0..1000 m is refused. The client keeps a spot quest even though it names no place.
- **Baselines are per entity**: the newest state of that entity that the client acknowledged.
  - The server records what each of its last 64 snapshots held. An ack moves those entities' baselines forward.
  - The client keeps the last 64 received states of every entity, indexed by seq.
  - Baselines are at most 31 snapshots old, and a client accepts a snapshot at most 32 behind its newest. Together these guarantee that a late snapshot's baseline is still stored (the round-trip test found this bound).
  - A snapshot whose baseline is missing is refused and not acknowledged, so it is never used as a baseline.
- **Interest management**:
  - Each entity accumulates priority per snapshot by distance from the client's animal: 1 within 40 m, 0.5 to 120 m, 0.25 to 300 m, 0.12 to 800 m, and 0.06 beyond, which is about once a second.
  - A change of alive, flags or action adds 1, so it goes out at once.
  - Entities at priority 1 or more, ordered by priority, are written until the 1200-byte budget is spent (one datagram under ENet's 1392 MTU). Whatever does not fit keeps its priority for the next snapshot.
  - Unchanged entities are not sent. An unacknowledged send that differs, or is more than 8 snapshots old, is sent again.
- **ENet**:
  - Snapshots and inputs are sent unsequenced, so late packets still count; ordering is handled by seqs.
  - ENet's unreliable-packet throttle is disabled (`enet_peer_throttle_configure`, deceleration 0). Otherwise ENet drops unreliable packets on purpose whenever the round trip varies, which under jitter would throw snapshots away.

Compared with v5 (and 5.0's v6, the same layout plus request replies): v5 sent every agent at full precision in every snapshot to everyone, about 33 bytes per agent. At 64 agents that is 2.1 KB per snapshot, about 32 KB/s per client. At 128 agents it would be 4.2 KB, a fragmented datagram that dies if any fragment is lost. v6 measures 4–5 KB/s at 64 agents and about 8 KB/s at 128 (table below).

## Server: input queue

- Inputs carry the client's tick sequence. The server applies each one exactly once, in order: one per tick into `inputs[slot]`.
- If the next input has not arrived, the animal is **held** (`CretSim.hold`). That tick skips its movement and the timers that gate it, so the client's prediction stays exact instead of the server guessing an input.
- The queue is drained when it is deeper than needed:
  - more than 4 waiting inputs are applied at once through `cret_sim_move`, up to 2 a tick;
  - a queue that never dropped below 1 for 2 s gives up one input of latency.
- Inputs the client stopped resending (more than 32 behind) are skipped and counted as lost.

## Client: prediction and reconciliation

- **Each client tick**: the input is quantised exactly as the server will see it, sent together with every input not yet applied (up to 32), and applied to the local animal with `cret_sim_move(..., predict = true)`.
  - This is the server's own `move_agent` plus its per-tick timers.
  - Prediction leaves out effects on others: attacks landing, calls, and fall injuries.
  - Statuses gate it as on the server (stunned or latched: no movement; slowed, defensive or burst: speed), and their timers and the ability cooldowns run down. Casting an ability is the server's: the client sends the slot and the next snapshot brings the effect.
  - Prop colliders stream in around the animal as on the server. Other agents' collision bodies sit at their latest snapshot positions (`cret_sim_place`).
- **On a snapshot**: compare the prediction made after the acknowledged input with the server's own state.
  - If position, heading, velocity, flags, action, cooldown, stamina and the statuses and ability cooldowns (within 0.1 s) match (within 2 mm), nothing happens.
  - Otherwise the animal is reset to the server's state and the newer inputs are replayed.
  - The visible animal keeps its place: the difference becomes an offset that decays with a 100 ms time constant. Only a jump of more than 4 m (a respawn) snaps.
  - The drawn animal is also interpolated between its last two predicted ticks, for frame rates above 30 Hz.

## Client: remote agents

- Each agent has a buffer of 16 samples, sorted by server tick, so reordered snapshots slot in.
- **Clock**: the client estimates the server tick from snapshot arrivals. The offset follows the earliest arrivals (quickly when packets come early, slowly when they come late), and jitter is the mean lateness against it.
- **Render time** is the estimated server tick minus an adaptive delay:
  - one snapshot interval, plus a tick of margin, plus 2.5 × jitter, plus a boost of 0.5 ticks after each underrun that wears off at 0.05 ticks/s;
  - clamped to 100–500 ms, and changed at no more than 10% (growing) or 5% (shrinking) of real time.
- **Far agents** are updated less often, so they are drawn further back, by their expected gap (the same distance rule the server uses).
  - The server does not send an agent while it stands still. After a silence longer than 1.5 gaps, a hold sample is inserted one gap before the new sample, so a walk starting is not smeared over the silence.
- **Past the newest sample**: the agent moves on in the direction of its last two samples at its own sent speed, for at most 200 ms, then holds. An agent that stopped is not carried on.
- **Carcasses** appear when the death is drawn, not 100 ms early.

## Link conditioner (netsim)

`netsim.c` is a UDP relay inside the client process (no threads):
- the client's ENet host connects to the relay's localhost port;
- the relay forwards to the server and back;
- it is pumped from the client's poll, every frame in the player.

Set `CRET_NETSIM` or pass `join --netsim "..."`. Every value applies to each direction:

| Key | Effect |
|---|---|
| `lat=MS` | one-way delay (round trip grows by 2 × lat) |
| `jit=MS` | standard deviation of a normal delay variation. Jitter alone never reorders: queueing keeps order. |
| `loss=PCT`, `burst=N` | loss rate; Gilbert–Elliott bursts with a mean length of N datagrams (1 = independent losses) |
| `dup=PCT` | datagrams delivered twice |
| `reorder=PCT` | datagrams held back 10–40 ms, so later ones overtake them |
| `up=0`, `down=0` | condition only one direction |

```bash
CRET_NETSIM="lat=60 jit=10 loss=2 dup=0.5" ./build/cretaceous/play join 192.168.1.20
```

## netbench

```bash
make -C ocean/cretaceous netbench CC=clang          # headless; no raylib
./build/cretaceous/netbench --agents 64,128 --clients 4 --seconds 15 --md table.md
./build/cretaceous/netbench --connect 127.0.0.1:27480 --clients 2 --scenario lan,mixed   # against `play server`
```

- One process runs the server sim (bots everywhere else) and N clients, each with its own client sim, behind its own relay.
- The clients play scripted inputs: trot, sprint, walk, back up, strafe, stand, eat, with a wandering turn, jumps, attacks and secondary attacks, and (v9) a head aim that sweeps side to side. Their clocks drift by ±0.02–0.08%, and each starts at a random phase against the server.
- **Virtual time**: it runs on a virtual clock in 0.25 ms steps (`cret_netsim_set_clock`), so the results do not depend on how busy the machine is.
  - Packets still cross real localhost sockets and ENet.
  - Only ENet's own timers (pings, retransmits, its RTT estimate) run on wall time, so the RTT it reports is not shown.
  - `--realtime` and `--connect` use the wall clock.
- Statistics start after a 3 s warmup.

Columns:
- **Down**, **Up**: per client, UDP payload plus 28 bytes of IP/UDP header per datagram.
- **Snap/s**: snapshots accepted per second.
- **Input to ack**: from sending an input to receiving a snapshot that shows it applied. It includes the server queue, the wait for the next snapshot (15 Hz) and the client's frame.
- **Prediction error**: the client's predicted position after an input against the server's state after the same input. Histogram percentiles are bin centres, so 0.5 means "under 1 mm".
- **Corrections**: replays that moved the prediction, with the mean and largest shift that was smoothed.
- **Snaps**: corrections of more than 4 m.
- **Underrun**: render frames with no snapshot newer than the render time.
- **Extrapolated**: drawn frames of a moving remote agent past its newest sample.
- **Delay**: the interpolation delay at the end of the run.
- **Remote error**: drawn remote agents within 120 m against the server's true position at their render time.
- **Held**: peer-ticks with no input yet.
- **Refused**: snapshots refused by the client (duplicates and malformed ones).
- **Props / snapshot** (v11): prop states sent per snapshot and their bytes. Every 2 s the server drops a boulder or log (100 kg or more) within 40 m of each client from 3 m up with a tilt (`--no-props`: off); the bots also kick stones about.
- **Prop error**: drawn props within 120 m against the server's pose at the render time (interpolated between physics steps). Teleports (the drops themselves) dominate the mean; the 95th percentile is 2–8 cm.

Results at 4 clients, 15 s each (2026-11-08, protocol 7):

| Scenario | Agents | Down KB/s | Up KB/s | Snap/s | Input to ack ms (mean / p95) | Prediction error mm (mean / p95) | Corrections /min (mean, max) | Snaps | Underrun % | Extrapolated % | Delay ms | Remote error cm (mean / p95) | Held % | Refused |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LAN | 64 | 4.5 | 1.7 | 15.0 | 32 / 68 | 0.4 / 2 | 0.0 (0.0 cm, max 0 cm) | 0 | 0.00 | 0.20 | 109 | 0.3 / 0 | 0.0 | 0 |
| 80 ms RTT | 64 | 5.2 | 2.0 | 15.0 | 131 / 162 | 0.5 / 2 | 9.0 (5.3 cm, max 10 cm) | 0 | 0.00 | 0.12 | 119 | 0.4 / 2 | 0.1 | 0 |
| 150 ms RTT, 20 ms jitter | 64 | 4.4 | 2.3 | 15.0 | 230 / 268 | 0.8 / 2 | 7.0 (31.2 cm, max 152 cm) | 0 | 0.00 | 0.06 | 153 | 0.3 / 0 | 0.1 | 0 |
| 250 ms RTT, 5% loss | 64 | 4.8 | 2.6 | 14.1 | 332 / 372 | 0.9 / 2 | 6.0 (0.2 cm, max 0 cm) | 0 | 0.17 | 2.55 | 136 | 1.7 / 10 | 0.1 | 0 |
| 80 ms RTT, 1% loss in bursts | 64 | 5.1 | 2.1 | 14.8 | 165 / 278 | 0.6 / 2 | 4.0 (0.2 cm, max 0 cm) | 0 | 1.22 | 1.20 | 114 | 1.1 / 2 | 0.8 | 0 |
| 120 ms RTT, jitter, 2% loss, dup, reorder | 64 | 5.0 | 2.2 | 14.8 | 202 / 252 | 1.1 / 2 | 7.0 (19.0 cm, max 68 cm) | 0 | 0.06 | 0.70 | 148 | 0.8 / 2 | 0.2 | 0 |
| LAN | 128 | 8.0 | 1.7 | 15.0 | 32 / 68 | 0.4 / 0 | 2.0 (23.7 cm, max 47 cm) | 0 | 0.00 | 0.22 | 109 | 0.4 / 2 | 0.0 | 0 |
| 80 ms RTT | 128 | 8.3 | 2.0 | 15.0 | 131 / 162 | 0.3 / 2 | 2.0 (0.3 cm, max 0 cm) | 0 | 0.00 | 0.14 | 121 | 0.4 / 2 | 0.1 | 0 |
| 150 ms RTT, 20 ms jitter | 128 | 7.7 | 2.3 | 15.0 | 231 / 268 | 0.8 / 2 | 5.0 (4.6 cm, max 22 cm) | 0 | 0.00 | 0.09 | 150 | 0.4 / 2 | 0.1 | 0 |
| 250 ms RTT, 5% loss | 128 | 7.7 | 2.6 | 14.2 | 332 / 368 | 1.2 / 2 | 20.0 (16.0 cm, max 175 cm) | 0 | 0.11 | 2.51 | 131 | 2.0 / 10 | 0.1 | 0 |
| 80 ms RTT, 1% loss in bursts | 128 | 8.1 | 2.1 | 15.0 | 168 / 278 | 0.9 / 2 | 13.0 (9.6 cm, max 80 cm) | 0 | 0.00 | 0.23 | 117 | 0.3 / 0 | 1.3 | 0 |
| 120 ms RTT, jitter, 2% loss, dup, reorder | 128 | 8.2 | 2.2 | 14.7 | 201 / 242 | 0.6 / 2 | 7.0 (6.4 cm, max 23 cm) | 0 | 0.11 | 0.82 | 143 | 1.0 / 2 | 0.2 | 0 |

Protocol 8 (integration with progression, 2026-11-08), 4 clients, 20 s each, `--scenario lan,250l5`:

| Scenario | Agents | Down KB/s | Up KB/s | Snap/s | Input to ack ms (mean / p95) | Prediction error mm (mean / p95) | Corrections /min (mean, max) | Snaps | Underrun % | Extrapolated % | Delay ms | Remote error cm (mean / p95) | Held % | Refused |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LAN | 64 | 6.2 | 1.7 | 15.0 | 32 / 68 | 0.5 / 2 | 7.5 (2.5 cm, max 4 cm) | 0 | 0.00 | 0.14 | 109 | 0.6 / 2 | 0.0 | 0 |
| 250 ms RTT, 5% loss | 64 | 5.5 | 2.6 | 14.3 | 332 / 362 | 1.0 / 2 | 11.2 (34.8 cm, max 267 cm) | 0 | 0.12 | 2.18 | 127 | 2.5 / 10 | 0.0 | 0 |
| LAN | 128 | 9.7 | 1.7 | 15.0 | 32 / 68 | 0.1 / 0 | 2.2 (0.1 cm, max 0 cm) | 0 | 0.00 | 0.13 | 109 | 0.7 / 4 | 0.0 | 0 |
| 250 ms RTT, 5% loss | 128 | 9.2 | 2.6 | 14.2 | 332 / 368 | 1.0 / 2 | 16.5 (11.9 cm, max 89 cm) | 0 | 0.10 | 2.56 | 130 | 2.7 / 10 | 0.1 | 0 |

Against protocol 7, downstream grows by 1.5 to 1.7 KB/s per client (statuses, marks, the own-state extension and the progress and nest blocks); latency, snapshot rate and underruns are unchanged.

Protocol 9 (aim and the secondary attack, cret/controls, 2026-11-08), 4 clients, 20 s each, `--scenario lan,250l5`; the scripted clients now sweep their aim and use the secondary attack too:

| Scenario | Agents | Down KB/s | Up KB/s | Snap/s | Input to ack ms (mean / p95) | Prediction error mm (mean / p95) | Corrections /min (mean, max) | Snaps | Underrun % | Extrapolated % | Delay ms | Remote error cm (mean / p95) | Held % | Refused |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LAN | 64 | 6.1 | 1.8 | 15.0 | 32 / 68 | 0.4 / 0 | 3.7 (1.6 cm, max 4 cm) | 0 | 0.00 | 0.15 | 110 | 0.6 / 4 | 0.0 | 0 |
| 250 ms RTT, 5% loss | 64 | 5.8 | 3.1 | 14.2 | 329 / 368 | 1.1 / 2 | 27.0 (3.6 cm, max 22 cm) | 0 | 0.10 | 2.49 | 128 | 2.5 / 14 | 0.2 | 0 |
| LAN | 128 | 9.5 | 1.8 | 15.0 | 32 / 68 | 0.7 / 0 | 21.0 (2.4 cm, max 8 cm) | 0 | 0.00 | 0.20 | 109 | 0.5 / 2 | 0.0 | 0 |
| 250 ms RTT, 5% loss | 128 | 10.4 | 3.1 | 14.2 | 331 / 368 | 0.7 / 2 | 6.0 (0.2 cm, max 0 cm) | 0 | 0.17 | 2.49 | 131 | 2.8 / 16 | 0.2 | 0 |

Against protocol 8: upstream grows by 0.1 KB/s on LAN and 0.5 KB/s at 250 ms with 5% loss (8 more bits per input, and up to 32 unacknowledged inputs per packet there); downstream, latency, snapshot rate and prediction error are unchanged within run-to-run noise.
The scripted clients cast no abilities, so their larger corrections come from server-side events the client does not predict: hits, stuns, latches and slows from bots using abilities.

Protocol 11 (movable props, cret/physics, 2026-11-09), 4 clients, 15 s each, all scenarios, with a boulder or log dropped near each client every 2 s:

| Scenario | Agents | Down KB/s | Up KB/s | Snap/s | Input to ack ms (mean / p95) | Prediction error mm (mean / p95) | Corrections /min (mean, max) | Snaps | Underrun % | Extrapolated % | Delay ms | Remote error cm (mean / p95) | Held % | Refused | Props / snapshot (bytes) | Prop error cm (mean / p95) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LAN | 64 | 6.5 | 1.8 | 15.0 | 32 / 68 | 2.1 / 2 | 30.0 (5.0 cm, max 10 cm) | 0 | 0.00 | 0.28 | 109 | 0.5 / 2 | 0.0 | 0 | 4.19 (61.9 B) | 3.6 / 2 |
| 80 ms RTT | 64 | 6.6 | 2.2 | 15.0 | 132 / 162 | 1.0 / 2 | 5.0 (23.9 cm, max 119 cm) | 0 | 0.00 | 0.23 | 121 | 0.5 / 2 | 0.1 | 0 | 3.61 (53.4 B) | 4.9 / 2 |
| 150 ms RTT, 20 ms jitter | 64 | 6.1 | 2.6 | 15.0 | 230 / 268 | 0.6 / 2 | 7.0 (0.2 cm, max 0 cm) | 0 | 0.00 | 0.15 | 154 | 0.4 / 2 | 0.1 | 0 | 3.03 (45.0 B) | 2.9 / 2 |
| 250 ms RTT, 5% loss | 64 | 6.6 | 3.1 | 14.3 | 332 / 362 | 2.2 / 2 | 26.0 (13.6 cm, max 77 cm) | 0 | 0.25 | 2.15 | 131 | 2.5 / 12 | 0.1 | 0 | 3.29 (48.8 B) | 4.7 / 2 |
| 80 ms RTT, 1% loss in bursts | 64 | 6.8 | 2.4 | 15.0 | 167 / 278 | 1.0 / 2 | 27.0 (1.7 cm, max 16 cm) | 0 | 0.00 | 0.29 | 116 | 0.6 / 2 | 1.7 | 0 | 4.39 (64.8 B) | 4.7 / 2 |
| 120 ms RTT, jitter, 2% loss, dup, reorder | 64 | 7.1 | 2.5 | 14.7 | 199 / 238 | 1.2 / 2 | 9.0 (15.6 cm, max 139 cm) | 0 | 0.08 | 0.82 | 153 | 1.5 / 4 | 0.1 | 0 | 4.88 (71.9 B) | 5.6 / 2 |
| LAN | 128 | 10.4 | 1.8 | 15.0 | 31 / 68 | 0.6 / 2 | 2.0 (31.1 cm, max 62 cm) | 0 | 0.00 | 0.18 | 110 | 0.5 / 2 | 0.0 | 0 | 3.88 (57.4 B) | 3.9 / 2 |
| 80 ms RTT | 128 | 10.5 | 2.2 | 15.0 | 132 / 162 | 0.7 / 2 | 10.0 (1.3 cm, max 7 cm) | 0 | 0.00 | 0.14 | 120 | 0.6 / 2 | 0.1 | 0 | 5.30 (78.1 B) | 4.8 / 4 |
| 150 ms RTT, 20 ms jitter | 128 | 9.2 | 2.7 | 15.0 | 231 / 268 | 1.5 / 2 | 5.0 (43.8 cm, max 205 cm) | 0 | 0.00 | 0.21 | 153 | 0.4 / 2 | 0.0 | 0 | 3.52 (52.1 B) | 2.8 / 0 |
| 250 ms RTT, 5% loss | 128 | 10.0 | 3.1 | 14.2 | 332 / 368 | 0.8 / 2 | 9.0 (0.9 cm, max 5 cm) | 0 | 0.19 | 2.61 | 134 | 2.6 / 16 | 0.1 | 0 | 4.34 (64.1 B) | 7.6 / 2 |
| 80 ms RTT, 1% loss in bursts | 128 | 10.2 | 2.3 | 14.8 | 152 / 258 | 1.0 / 2 | 19.0 (2.4 cm, max 18 cm) | 0 | 0.72 | 0.72 | 115 | 0.9 / 2 | 0.2 | 0 | 5.07 (74.8 B) | 7.9 / 8 |
| 120 ms RTT, jitter, 2% loss, dup, reorder | 128 | 10.3 | 2.5 | 14.7 | 201 / 242 | 1.0 / 2 | 13.0 (0.9 cm, max 5 cm) | 0 | 0.11 | 1.38 | 148 | 1.3 / 4 | 0.2 | 0 | 5.41 (79.7 B) | 5.3 / 2 |

Without the drops (`--no-props`; bots still kick stones), 4 clients, 15 s:

| Scenario | Agents | Down KB/s | Up KB/s | Snap/s | Input to ack ms (mean / p95) | Prediction error mm (mean / p95) | Corrections /min (mean, max) | Snaps | Underrun % | Extrapolated % | Delay ms | Remote error cm (mean / p95) | Held % | Refused | Props / snapshot (bytes) | Prop error cm (mean / p95) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LAN | 64 | 5.9 | 1.8 | 15.0 | 31 / 68 | 2.0 / 2 | 28.0 (5.2 cm, max 10 cm) | 0 | 0.00 | 0.27 | 110 | 0.5 / 2 | 0.0 | 0 | 1.66 (24.9 B) | 2.5 / 4 |
| 250 ms RTT, 5% loss | 64 | 6.1 | 3.1 | 14.2 | 332 / 368 | 1.3 / 2 | 13.0 (10.1 cm, max 54 cm) | 0 | 0.14 | 2.30 | 133 | 2.8 / 18 | 0.1 | 0 | 0.69 (10.7 B) | 0.7 / 2 |
| LAN | 128 | 9.9 | 1.8 | 15.0 | 31 / 68 | 0.6 / 2 | 2.0 (31.1 cm, max 62 cm) | 0 | 0.00 | 0.18 | 111 | 0.5 / 2 | 0.0 | 0 | 1.63 (24.5 B) | 1.5 / 2 |
| 250 ms RTT, 5% loss | 128 | 9.9 | 3.1 | 14.3 | 331 / 362 | 0.7 / 2 | 5.0 (40.2 cm, max 200 cm) | 0 | 0.11 | 2.40 | 134 | 2.7 / 14 | 0.1 | 0 | 2.60 (38.7 B) | 13.6 / 4 |

Against protocol 9/10 at the same agent counts, downstream grows by about 0.5 KB/s per client with props tumbling near every client (the drops) and by under 0.1 KB/s without; latency, snapshot rate, underruns and remote-agent error are unchanged within run-to-run noise. The prop budget never filled.

Two-process smoke test: a headless dedicated server (`play server --port 27711 --agents 64`) and `netbench --connect 127.0.0.1:27711 --clients 2` in a second process, on wall time on a heavily loaded machine (load average about 20 on 4 cores):

| Scenario | Down KB/s | Up KB/s | Snap/s | Input to ack ms | Prediction error mm | Underrun % | Delay ms |
|---|---|---|---|---|---|---|---|
| LAN | 2.8 | 2.1 | 14.5 | 136 | 0.6 | 2.4 | 219 |
| 250 ms RTT, 5% loss | 2.8 | 2.9 | 13.9 | 412 | 0.7 | 2.5 | 244 |
| 120 ms RTT, jitter, 2% loss, dup, reorder | 2.9 | 2.5 | 14.7 | 269 | 0.9 | 1.6 | 265 |

The latencies there include the CPU starvation of both processes. The adaptive delay grew to cover the resulting jitter.

Reading it:
- **Bandwidth** is 4–5 KB/s down per client at 64 agents and about 8 KB/s at 128, with about 2 KB/s up. The budget was never full.
- **Input to ack** is the round trip plus about 30–80 ms (the server queue, the 15 Hz snapshot wait, the client frame).
- **Prediction** stays within a few millimetres of the server at the acknowledged input.
  - The mean is about 1 mm and the 95th percentile is under 2 mm.
  - Corrections are a few to about 20 a minute, mostly a few centimetres, and smoothed. The largest (up to 1.75 m) are bites and knockback.
  - They come from what prediction cannot know: bites and knockback from bots, and other agents moving into the path.
- **Interpolation** delay settles at 110–150 ms.
  - Underruns stay at or under about 1% of frames, even with 5% loss or loss bursts.
  - Remote agents are drawn within a few centimetres of where the server had them.

## netfuzz

```bash
make -C ocean/cretaceous netfuzz CC=clang     # -fsanitize=address,undefined, aborts on any finding
./build/cretaceous/netfuzz --packets 1000000
```

1. **Round trip**:
   - A fake server encodes snapshots of a randomly evolving sim for a fake client: moves, deaths, teleports, out-of-range positions, every action, rallies and carcasses, and (v8) statuses, latches, strikes, cooldowns, marks, the viewer's progress and quests, and nests.
   - The channel loses, duplicates and delays packets, at 10% loss and at 30% loss with packets up to 40 snapshots late. The client's acks and inputs come back over the same kind of channel.
   - Every accepted snapshot must decode to exactly what the server quantised, no valid snapshot may be refused, and every input (with its ability slot) must arrive as sent.
   - v8 blocks: on snapshots delivered in the round they were made, the own status timers and cooldowns, the progress block and the nests must decode to the server's state (about 5,000 checks per run).
2. **Fuzz**:
   - Inputs: valid snapshots (against the kept client's baselines), inputs, welcomes, respawn requests, profiles, unlocks and spawn replies, plus purely random datagrams.
   - Mutations: bit flips, byte overwrites, 0x00/0xff runs, truncation, extension and splices.
   - Accepted state goes through apply, reconcile (`cret_sim_move` on a real client sim) and draw.

Result (v7): 1,000,000 datagrams with no sanitizer findings and no round-trip failures. About 121k mutated snapshots were accepted and drawn.
Result (v8, 2026-11-08): 300,000 datagrams (`make asan` runs this size) with no sanitizer findings and no failures: 0 valid snapshots refused in 18,000 round-trip snapshots, about 16,000 v8 block checks, and of 254k fuzzed datagrams 35k mutated snapshots, 25k inputs, 6k welcomes and 6.4k requests or replies accepted.

Result (v11, 2026-11-09): the round trip adds props that appear, roll and turn, rest, wake, are dropped and stray out of range (up to 256 at once). Every prop state a snapshot carried must be in the client's samples (about 15,000 checks per round trip), and after a last 60 snapshots without loss the client's props must equal the server's quantised ones. 200,000 datagrams under ASan + UBSan, no findings. It found one bug, fixed: a late snapshot could bring back a prop the client had already been told was gone (hence the client's gone list).

Result (v9, 2026-11-08, aims, secondary cooldowns and queued attacks in the round trip): 300,000 datagrams with no sanitizer findings and no failures: 0 valid snapshots refused in 18,000 round-trip snapshots, about 15,900 v8 block checks, and of 254.6k fuzzed datagrams 33.3k mutated snapshots, 25.9k inputs, 6.1k welcomes and 6.4k requests or replies accepted.
The fuzzer found two bugs, both fixed:
- a late snapshot whose baseline slot had been overwritten (hence the 31-snapshot age bound);
- an out-of-range float-to-int conversion of a garbage render tick.

## Sim changes

- `CRET_MAX_AGENTS` goes from 64 to 128. Bodies are created for `num_agents` only, so the trainer pays nothing for this.
- `CretSim.hold` skips an agent's movement (and its status input) and movement timers for a tick.
- `cret_sim_move(s, i, in, predict)` runs one agent's tick as `cret_sim_step` does: `cret_status_input` (stun, latch, casting), then movement unless latched, then its timers. Predicting, it applies the stun and latch gating without casting and runs the status timers and ability cooldowns down.
- `cret_sim_place(s, i)` moves an agent's collision body and capsule to its state, for clients.
- `move_agent` treats an action past `CRET_ACT_NEST` as none.

## Open

- Prediction covers movement only. Your own attack's hit, eating and drinking gains, and fall injuries come from the server (about one round trip later). So do abilities: a cast (pounce, charge, sprint burst, dodge leap) starts on the server, and the client's animal follows one round trip later with a smoothed correction. Predicting casts would need the cast rules (stamina, cooldown, targets) on the client. Hits on remote agents are not lag-compensated: the server resolves them at its present, not at what you saw about 100 ms ago.
- Other agents in the prediction's collision world are at their latest snapshot positions, not at where the server will have them. Pushing into a moving animal corrects.
- Inputs are applied per tick at the server's pace. A held tick shows as a small hitch of that player for everyone else (under 1% of ticks in every scenario above).
- The player's join still blocks while the client loads or bakes the world. ENet keeps the connection alive through a load from cache, but a first bake (about 45 s) can time out on the server side.
- Props are not predicted: a boulder you shove moves one round trip later, and until then your animal leans on it where the server last had it.
- Snapshot rate is fixed at 15 Hz (`CRET_SNAPSHOT_DIV`). 30 Hz would roughly halve the input-to-ack wait at about twice the bandwidth.
- The ENet round trip that `cret_net_client_stats` reports includes the server's 30 Hz service loop. It is a rough number.
