# Cretaceous netcode

The server owns the sim. Clients send inputs, predict their own animal, and draw everyone else from a snapshot buffer.
Solo play never touches the network.
Code: `net.c` (codec, server, client transport), `netclient.c` (prediction, reconciliation, interpolation), `netsim.c` (link conditioner), `net_priv.h` (internals shared with the tools).
Tools: `tools/netbench.c` (scenario benchmark) and `tools/netfuzz.c` (codec round trip and fuzzer).

## Protocol v6

All messages are bit-packed (MSB first) through a bounds-checked reader and writer.
A short or malformed message sets a flag rather than reading past the end, and the receiver refuses the whole message.
C structs never cross the wire.

| Message | Channel | Content |
|---|---|---|
| connect | ENet connect data | `CRET_NET_VERSION`. A server refuses any other version and disconnects with `'V' << 24 \| its version`; the client prints both versions. |
| WELCOME | reliable | version, world seed, world size, cell, agent count, slot, tick rate, snapshot divisor |
| INPUT | unsequenced, 30 Hz | snapshot ack (newest received seq, plus a 32-bit mask of the ones before it); newest input seq; 1..32 inputs, newest first. Each input is 32 bits (forward, strafe and turn as signed bytes, sprint, jump, action), or 1 bit when it repeats the next newer one. |
| SNAPSHOT | unsequenced, 15 Hz per client | seq, server tick, last input applied, server input queue depth, time of day, cloud cover; own state at full precision; entity updates |
| REQUEST | reliable | respawn as a species, or as the same one (255) |

Snapshots are per client, so each client gets its own content:

- **Own state**: alive, ground, swim and sprint flags, action, action ticks, attack cooldown, flinch, food, water, then 11 floats: position, heading, velocity, speed, stamina, broken-bone time and growth. This is what the client rewinds to. About 50 bytes.
- **Entities** (agents 0..127, carcasses 128..159): id (8 bits), baseline age (5 bits, 0 = from zero), then each field against the baseline.
  - Quantisation: positions in 1/256 m (21 bits for the 8 km island, sized from the world), height from −2048 m, heading in 12 bits, speed in 1/16 m/s, health, growth and meat in 8 bits.
  - Flinch and death are sent as the tick they end or start (16 bits), so they do not change every tick.
  - Delta fields (position, heading, speed) use a 2-bit code: same, small signed delta, medium signed delta, or full value. Other fields use one changed bit.
  - The decoder range-checks species, action, group and the carcass's agent.
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

Compared with v5: v5 sent every agent at full precision in every snapshot to everyone, about 33 bytes per agent. At 64 agents that is 2.1 KB per snapshot, about 32 KB/s per client. At 128 agents it would be 4.2 KB, a fragmented datagram that dies if any fragment is lost. v6 measures 4–5 KB/s at 64 agents and about 8 KB/s at 128 (table below).

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
  - Prop colliders stream in around the animal as on the server. Other agents' collision bodies sit at their latest snapshot positions (`cret_sim_place`).
- **On a snapshot**: compare the prediction made after the acknowledged input with the server's own state.
  - If position, heading, velocity, flags, action, cooldown and stamina match (within 2 mm), nothing happens.
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
- The clients play scripted inputs: trot, sprint, walk, back up, strafe, stand, eat, with a wandering turn, jumps and attacks. Their clocks drift by ±0.02–0.08%, and each starts at a random phase against the server.
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

Results at 4 clients, 15 s each (2026-11-08):

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
   - A fake server encodes snapshots of a randomly evolving sim for a fake client: moves, deaths, teleports, out-of-range positions, every action, rallies and carcasses.
   - The channel loses, duplicates and delays packets, at 10% loss and at 30% loss with packets up to 40 snapshots late. The client's acks and inputs come back over the same kind of channel.
   - Every accepted snapshot must decode to exactly what the server quantised, no valid snapshot may be refused, and every input must arrive as sent.
2. **Fuzz**:
   - Inputs: valid snapshots (against the kept client's baselines), inputs and welcomes, plus purely random datagrams.
   - Mutations: bit flips, byte overwrites, 0x00/0xff runs, truncation, extension and splices.
   - Accepted state goes through apply, reconcile (`cret_sim_move` on a real client sim) and draw.

Result: 1,000,000 datagrams with no sanitizer findings and no round-trip failures. About 121k mutated snapshots were accepted and drawn.
The fuzzer found two bugs, both fixed:
- a late snapshot whose baseline slot had been overwritten (hence the 31-snapshot age bound);
- an out-of-range float-to-int conversion of a garbage render tick.

## Sim changes

- `CRET_MAX_AGENTS` goes from 64 to 128. Bodies are created for `num_agents` only, so the trainer pays nothing for this.
- `CretSim.hold` skips an agent's movement and movement timers for a tick.
- `cret_sim_move(s, i, in, predict)` runs one agent's movement and timers.
- `cret_sim_place(s, i)` moves an agent's collision body and capsule to its state, for clients.

## Open

- Prediction covers movement only. Your own attack's hit, eating and drinking gains, and fall injuries come from the server (about one round trip later). Hits on remote agents are not lag-compensated: the server resolves them at its present, not at what you saw about 100 ms ago.
- Other agents in the prediction's collision world are at their latest snapshot positions, not at where the server will have them. Pushing into a moving animal corrects.
- Inputs are applied per tick at the server's pace. A held tick shows as a small hitch of that player for everyone else (under 1% of ticks in every scenario above).
- The player's join still blocks while the client loads or bakes the world. ENet keeps the connection alive through a load from cache, but a first bake (about 45 s) can time out on the server side.
- Snapshot rate is fixed at 15 Hz (`CRET_SNAPSHOT_DIV`). 30 Hz would roughly halve the input-to-ack wait at about twice the bandwidth.
- The ENet round trip that `cret_net_client_stats` reports includes the server's 30 Hz service loop. It is a rough number.
