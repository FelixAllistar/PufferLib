# SWAT netcode

Server-authoritative co-op over ENet UDP, 60 Hz simulation, 30 Hz state.
The server's `SwatSim` is the only game. Clients keep a Box3D replica for
presentation and queries and never simulate gameplay. Everything below changes
transport and presentation only. Authority stays on the server, and anything
taken from the client (commands, acknowledgements, view ticks) is validated
and bounded there.

Code:

| file | role |
|------|------|
| `protocol.c/h` | wire encoders/decoders (version 54), byte deltas, input batches |
| `netcode.c/h` | transport-independent pieces: command queue and starvation policy, snapshot ring, view interpolation (actors, stance, door leaves), local predictor (private shared-controller world), tuning switches |
| `net.c/h` | ENet transport (server/client) using the above |
| `sim.c/h` | lag-compensation history (actors, door leaves) and bounded rewind for hitscan |
| `tests/test_netcond.c` | deterministic network-conditions harness (`make net-conditions-test`, part of `make test`) |
| `tests/test_net_udp.c` | two-process real-UDP test through a seeded impairment shim (`make net-udp-test`, part of `make test`) |

## What changed (protocol 53 -> 54)

**1. Snapshot deltas.** A motel snapshot is about 73 KB, so every state packet
used to need about 54 ENet fragments. At 2% packet loss only about 11 snapshots
per second survived, and at 5% only about 2. Each client now reports the
newest revision it holds. The server keeps its last 32 encoded snapshots
(`SwatSnapshotRing`) and sends each player `SWAT_MSG_DELTA`: the full
`SWAT_MSG_SNAPSHOT` bytes XORed with that baseline and packed as
(zero-run, literal-run) varint pairs. Expansion gives back the exact original
bytes, which then go through the unchanged `swat_decode_snapshot` validation
and `swat_apply_snapshot`. The harness checks every reconstruction byte for
byte against the server's encoding. If the baseline is unknown, too old (more
than 16 revisions) or from another epoch, the server sends a full snapshot. A
client that lacks a delta's baseline skips the packet, and the next one
targets a newer acknowledgement. Steady-state state traffic drops from about
2.2 MB/s to 9-14 kB/s per client, and each state packet fits in one datagram.

**2. Redundant unreliable inputs.** Inputs used to be one reliable ENet
command per tick. A lost packet waited for a retransmission timeout, and every
later command waited behind it (head-of-line blocking). `SWAT_MSG_INPUTS` is
sent unreliably every tick and carries every unacknowledged command, up to 64
(about one second). Consecutive command bodies are XORed and delta-packed, so a
typical packet is tens of bytes. The same packet carries the client's snapshot
acknowledgement and, per command, its view tick. The server drops duplicates
by sequence. Gaps beyond the window count as lost.

**3. Server jitter-buffer drain.** The server executes one command per tick.
After any delivery burst (a lost reliable packet, a jitter spike) the queue
stayed longer forever, and the player's input latency ratcheted up for the
rest of the session: 470 ms mean at 120 ms RTT. `swat_remote_take` now tracks
the queue's minimum depth over 60 ticks. Any surplus above two commands is
removed by merging consecutive pairs (`swat_input_merge`): look deltas are
summed, presses are ORed, and one-shot requests (weapon, tool, squad order...)
are kept. A full queue also merges its two oldest commands instead of
disconnecting the player with "Input queue exceeded".

**4. Remote interpolation (`SwatNetView`).** Snapshot arrival times give a
clock offset (the minimum of arrival minus server time) and a 95th-percentile
lateness estimate. The interpolation delay is one snapshot interval plus that
lateness, plus up to one more interval as measured snapshot loss rises,
clamped to 40-250 ms. Presentation time advances by the frame duration and is
steered toward the target with a bounded ±8% time warp, never a jump (it
resynchronises only beyond 250 ms of error). Positions use cubic Hermite
interpolation with the replicated body velocities, and yaw uses the shortest
arc. Past the newest snapshot the view extrapolates with the horizontal
velocity for at most 100 ms, then holds. When fresh data replaces an
extrapolation, the difference becomes an offset that decays with a 90 ms time
constant. Moves over 2.5 m (teleport, respawn) snap.
`swat_client_present` writes these poses into the replica's remote bodies just
before drawing, and the next snapshot overwrites them.

**5. Local prediction (`SwatPredictor`).** The local body is presented ahead of
authority. Starting from the newest authoritative local state, the predictor
replays every unacknowledged command with the controller's own ground model:
the same speeds (walk, slow, aim, sprint, crouch, times mobility), sprint and
stamina rules, and the 22 / 30 / 4.5 m/s² acceleration clamp. Horizontal moves
are swept with a read-only capsule cast against the replica. Reconciliation
compares last frame's prediction, continued through the same commands, with
the fresh re-simulation. Any difference is a correction: it joins an offset
that decays with a 100 ms time constant, and jumps over 1 m snap. The
presented position blends the last two predicted ticks by the pacer phase, so
60 Hz prediction stays smooth at any frame rate. Aim was already predicted
(`pending_yaw` and `pending_pitch`). Step-ups, clambers, collisions with other
bodies and stand-up clearance remain authoritative and appear as smoothed
corrections.

**6. Lag compensation.** Each command carries the server tick the client was
presenting remote actors at (`view_tick`). The sim records every actor's body
position after each tick (`lag_positions`, 32 ticks). For a remote player's
hitscan shot, `net.c` sets `lag_view_tick`. The sim then moves the other
living actors to their positions at that tick, runs the unchanged
`swat_world_ray` and `swat_sim_shoot`, and restores them. Projectiles and
melee are not rewound. The rewind is bounded: never more than
`SWAT_LAG_MAX_TICKS` = 24 ticks (400 ms), never into the future, and only for
non-host remote slots. Solo play, bots, training and replays leave
`lag_view_tick` at 0, so their steps are unchanged (the determinism hashes
still pass). The harness fires a real shot at a strafing target from 15 ticks
in the past: it hits with the rewind (34 damage) and misses without it.

### Wire format (version 54)

All integers are big-endian. The header is unchanged: magic, version, type,
two zero bytes, epoch.

* `SWAT_MSG_DELTA` (7): header, `u32 base_revision`, `u32 full_size`, then
  packed pairs until the end. A pair is `varint zeros` followed by
  `varint literals`, then that many XOR bytes. It expands to a complete
  `SWAT_MSG_SNAPSHOT` of the same epoch.
* `SWAT_MSG_INPUTS` (8): header, `u32 snapshot_ack`, `u8 count` (1-64),
  `u32 first_sequence`, then the packed run of `count` bodies. Each body is
  the `SWAT_MSG_INPUT` body after the sequence field, plus a `u32 view_tick`.
  Each body is XORed with the previous one before packing. Every command is
  re-validated through `swat_decode_command`.
* `SWAT_MSG_INPUT` (1) is still accepted, as a single command without a view
  tick. Replays still record it.
* `SwatCommand` gained `view_tick`. The replay format is unchanged, but the
  shared version number refuses older replays and peers.

### Switches

`SwatNetTuning` (`swat_net_tuning_default()` in production) has the switches
`delta`, `redundancy`, `drain`, `interpolate`, `predict` and `lag_comp`.
`swat_net_tuning_legacy()` turns them all off and reproduces the protocol-53
behaviour for the harness. The one exception is that a full queue merges
instead of disconnecting. The prediction pass added `controller`,
`starve_decay`, `view_state` and `lag_doors` (below);
`swat_net_tuning_pass1()` is the default with those four off.

## Harness (`make net-conditions-test`)

The harness runs one authoritative motel session (tactical rules, three squad
bots, hostile fire off) and one client in a single process, using simulated
time. It uses the production code paths: capture, encode, ring, packet,
expand, decode, apply, the queue, the view and the predictor.

* **Link.** Latency and jitter are seeded and applied per direction. The
  scenario RTT is split evenly, and jitter is uniform ±J per packet. The
  link also loses and duplicates packets, and jitter reorders them. State
  packets are fragmented at the ENet MTU (1364-byte payloads): a packet
  survives only if all its fragments do, and arrives with the last one. The
  protocol-53 reliable input channel is modelled as ENet: retransmit after RTT
  + 4×variance, doubling each retry, with ordered delivery. Byte counts
  include the IP/UDP and ENet headers.
* **Actors.** The client player (slot 0) follows a scripted walk, strafe,
  stop, back, sprint and jitter-strafe loop with a slow turn. A second human
  (slot 1) runs a phase-shifted script on the server. The bots react. A door
  operator toggles one ordinary door every 1.5 s.
* **Rendering.** Frames run at 120 Hz and commands at 60 Hz. The run lasts 35
  s, of which 3 s are warm-up.
* **Remote metrics.** These count actors moving faster than 0.2 m/s. *error vs
  truth (shown time)* is reconstruction error against the server's position at
  the presented time. *error vs truth now* includes the display delay. *jerk*
  is the second difference of presented positions per frame. The harness also
  counts extrapolated frames and snaps.
* **Local metrics.** These compare the presented position with the server's
  position after it executes the newest command the client has issued (where
  "my input should have put me"). Jerk is counted as for remote actors.
  *correction* is the per-event reconciliation jump, which before this change
  was the raw snap when a snapshot moved the camera.
* **Other metrics.** The harness also measures input send-to-execute latency,
  server starvation (ticks with no command) and merged commands; door state
  latency (server change to the client's replica showing it); hitscan target
  error (distance between where the client saw the target and where the server
  tests it); snapshots applied per second; and bytes per second per client.
  The bytes count steady state only: the initial map and baseline are excluded.
* **Checks.** The harness also runs codec unit checks, the end-to-end
  lag-compensation shot, and a repeated seeded run that must match exactly.
  Thresholds (`limits[]` in the test) fail the target on regression.

## Results (before = protocol 53 behaviour, after = protocol 54)

### 50 ms RTT, no jitter, no loss

| metric | before | after |
|---|---:|---:|
| remote error vs truth at shown time (cm, mean / p95) | 0.00 / 0.00 | 0.15 / 0.24 |
| remote error vs truth now (cm) | 4.8 | 8.2 |
| remote display delay (ms) | 41 | 66 |
| remote jerk RMS / p99 (mm/frame²) | 45.4 / 128 | 5.4 / 4.6 |
| local error vs own input (cm, mean / p95) | 16.3 / 30.6 | 4.9 / 24.6 |
| local jerk RMS / p99 (mm/frame²) | 67.8 / 153 | 3.1 / 10.6 |
| local correction mean / max (cm) | 8.0 / 75 | 6.9 / 63 |
| input latency mean (ms) | 42 | 42 |
| door state latency mean / max (ms) | 45 / 45 | 45 / 45 |
| hitscan target error mean / p95 (cm) | 19.4 / 35.4 | 0.9 / 1.4 |
| snapshots applied (per s) | 30.0 | 30.0 |
| downstream / upstream (kB/s per client) | 2183 / 6.2 | 8.9 / 6.8 |

### 120 ms RTT, ±30 ms jitter, 2% loss, 0.5% duplication

| metric | before | after |
|---|---:|---:|
| remote error vs truth at shown time (cm, mean / p95) | 0.00 / 0.00 | 0.17 / 0.38 |
| remote error vs truth now (cm) | 19.8 | 19.3 |
| remote display delay (ms) | 165 | 160 |
| remote jerk RMS / p99 (mm/frame²) | 80.1 / 373 | 4.0 / 3.9 |
| local error vs own input (cm, mean / p95) | 114 / 206 | 5.3 / 25.2 |
| local jerk RMS / p99 (mm/frame²) | 125 / 559 | 2.7 / 5.5 |
| local correction mean / max (cm) | 21.5 / 165 | 5.9 / 62 |
| input latency mean / p95 (ms) | 471 / 480 | 106 / 114 |
| server starvation (ticks) | 29 | 7 |
| door state latency mean / max (ms) | 177 / 411 | 79 / 95 |
| hitscan target error mean / p95 (cm) | 116 / 198 | 1.0 / 2.1 |
| snapshots applied (per s) | 10.7 | 25.7 |
| downstream / upstream (kB/s per client) | 2183 / 6.3 | 11.5 / 9.8 |

### 250 ms RTT, ±60 ms jitter, 5% loss, 1% duplication

| metric | before | after |
|---|---:|---:|
| remote error vs truth at shown time (cm, mean / p95) | 0.00 / 0.00 | 0.26 / 0.54 |
| remote error vs truth now (cm) | 57.3 | 26.3 |
| remote display delay (ms) | 564 | 230 |
| remote jerk RMS / p99 (mm/frame²) | 121 / 551 | 3.6 / 4.1 |
| remote extrapolated frames (%) | 0 | 2.2 |
| local error vs own input (cm, mean / p95) | 137 / 268 | 12.4 / 66.7 |
| local jerk RMS / p99 (mm/frame²) | 153 / 846 | 3.4 / 7.8 |
| local correction mean / max (cm) | 63 / 246 | 9.6 / 87 |
| input latency mean / p95 (ms) | 748 / 922 | 181 / 205 |
| server starvation (ticks) / merged commands | 719 / 445 | 15 / 3 |
| door state latency mean / max (ms) | 723 / 1536 | 141 / 186 |
| door changes never seen | 1 | 0 |
| hitscan target error mean / p95 (cm) | 181 / 307 | 5.5 / 16.2 |
| snapshots applied (per s) | 2.3 | 19.5 |
| downstream / upstream (kB/s per client) | 2182 / 5.8 | 13.6 / 13.8 |

For reference, the truth jerk of the moving actors at the same sampling is
about 4-7 mm/frame². Remote jerk after the change is at or below that, because
Hermite interpolation smooths the physics.

How to read these numbers:

* Before the change, remote reconstruction error was zero because the replica
  showed the newest snapshot exactly, but held it between arrivals. The jerk
  column shows the cost: frozen, then jumping.
* At 50 ms, interpolation adds about 25 ms of display delay, so the error
  against the server *now* rises slightly. That is the price of smoothness.
* In the "before" 250 ms run, 445 merges happened because the protocol-53
  queue overflowed. Protocol 53 itself would have disconnected the player at
  that point. The merge-on-full exception in the legacy switch kept the
  session alive so the run could be measured.

## Prediction pass (wire format unchanged, protocol 54)

The second pass closes the open problems of the first. The wire format is
unchanged: everything here is client presentation or server policy, so the
version stays 54, peers of the first pass interoperate, and the determinism
traces match unchanged (the one encoder fix below only changes bytes that
the decoder refused anyway). `swat_net_tuning_pass1()` turns the new
switches off (`controller`, `starve_decay`, `view_state`, `lag_doors`) and
is the harness's middle column.

**1. Shared-controller prediction.** The client keeps a private copy of the
world (`SwatPredictor.world`, built from the same map with
`swat_apply_map`). Every applied snapshot is also applied to it, so it holds
the authority after the acknowledged command. The predictor then replays
the unacknowledged commands through `swat_sim_step_movement`, the server's
own step: controller, stance, 4-phase step-up, clamber planning and path,
jump, door swing and use, and the Box3D contact solve. Step-ups, clambers,
shoves and door pushes are predicted rather than corrected. Between
snapshots only new commands are stepped (one movement step costs about
0.04 ms on the motel, the replay after a snapshot about 0.2-0.8 ms).
Details:

* Edge state the snapshot does not carry (`last_jump`, `last_interact`)
  comes from the acknowledged command, so a held jump or use is not a new
  press in the replay.
* Other actors are dead-reckoned through the same controller: each gets the
  movement input that keeps its snapshot velocity (forward/strafe at walk,
  sprint or crouch speed), so a body being shouldered has momentum.
* Measured with the server's actual inputs, the replay matches the
  authority exactly (single-tick and six-tick shadow checks: no difference
  above 0.5 cm in 2000 ticks). What remains are input differences (server
  starvation and drain merges, the other actors' real inputs) and rare
  discrete flips, for example a step-down taken one tick earlier at a
  platform edge (18 cm once in the 50 ms run before the starvation change).
* Presentation only: the controller's step-up lifts the body up to about
  0.3 m in one tick, and depenetration can push it sideways as far. The
  prediction keeps that, but presented motion faster than 6.5 m/s goes
  into the same decaying offset as corrections (stair smoothing).
  Corrections over 1 m still snap.
* `swat_client_present` writes the predicted stance, lean, eye height and
  clamber state into the replica's local actor, not only the position.
* If a snapshot is refused by the private world (the prediction moved
  something the authority never did) it is rebuilt from the kept map and the
  snapshot applied to it fresh. Without a world the protocol-54 kinematic
  predictor is the fallback.

**2. Starvation policy.** A starved server tick still steps every body, so
it cannot be skipped. Protocol 53/54 repeated the held input at full
strength, so the player moved one tick further than its own commands, and
the client saw that as a correction once the late command executed too.
Now the held movement decays toward neutral by a factor of 0.25 per
consecutive starved tick (`swat_starve_decay`). Buttons stay as held,
because every press in the sim is edge-detected (`last_jump`,
`last_interact`, `last_fire`, `last_throw`...): a held button never fires
again, while releasing it would make the next command a false press. Look
deltas and one-shot requests (weapon, loadout and profile requests) are
never repeated. Measured on the stall scenario with shared-controller
prediction, local correction mean / p95 / max (cm):

| starved movement kept per tick | stalls | 250 ms motel |
|---|---:|---:|
| 1.0 (repeat, protocol 54) | 8.1 / 15.1 / 83.8 | 2.5 / 4.7 / 9.3 |
| 0.75 | 5.5 / 15.1 / 23.7 | 3.5 / 6.4 / 6.4 |
| 0.5 | 5.8 / 10.7 / 48.5 | 3.6 / 8.0 / 8.0 |
| **0.25 (chosen)** | **5.1 / 9.5 / 20.3** | **1.8 / 4.7 / 4.7** |
| 0 (neutral movement) | 6.6 / 14.8 / 29.3 | 3.1 / 7.0 / 9.6 |

**3. Remote stance and animation state.** `SwatViewActor` now also keeps
stance, grounded, sprinting, upper-body (lean) offset, eye height, lean,
ADS and ready blends and the clamber state. `swat_net_view_sample_state`
samples them at the presented time: discrete fields switch when the
presented time reaches the snapshot that carries them (not when the packet
arrives), and continuous ones are interpolated. The stance height blends
between standing and crouched over one snapshot interval instead of
popping. `swat_net_view_present_actor` writes these into the replica actor
the renderer draws (presentation fields only: the next snapshot's
`swat_body_replica_pose` rebuilds the shapes whenever they differ).

**4. Doors.** Door leaves are interpolated on clients:
`swat_net_view_bind` records the map's doors (up to 96), each view frame
keeps their angle (ordinary leaves) or pose (hinged and broken leaves),
and `swat_net_view_present_doors` poses them at the presented time with the
same pose math as snapshot application (hinged leaves are nlerped). Past
the newest snapshot an ordinary leaf keeps its last swing rate for at most
100 ms. The authoritative door state (open, latch, angle) still applies on
arrival, so door state latency is unchanged, and the next snapshot
overwrites the presented pose. **Lag compensation** now also rewinds door
leaves: with `sim.lag_doors` (network server only) the first 64 doors'
poses are recorded each tick, and a rewound hitscan tests the leaves where
the shooter saw them and then restores them, both the bodies the rays hit
and the object pose fields the material pass reads. The harness's door shot
fires through a doorway whose leaf, 15 ticks earlier, still crossed the line
of fire: without the rewind the round passes the open doorway (34 damage),
with it the round goes through the wooden leaf the shooter saw (27). Props
and rubble are not rewound: they move only when shot or blasted, almost
never within the 400 ms window, and recording every dynamic piece would
cost history for hundreds of bodies each tick for no measured gain.

**5. Encoder fix.** The controller keeps `mantle.climb` set after a climb
ends. The encoder sent that bare flag and the decoder correctly refuses it,
so after anyone climbed onto an obstacle every later snapshot was
undecodable and clients failed with "incompatible game data". Found by the
course scenario. Only an active clamber now carries the flag, and
clamber-test checks a snapshot after a finished climb.

### Harness scenarios (pass 2)

`make net-conditions-test` now runs seven scenarios, each with protocol 53,
pass 1 and pass 2 behaviour (`NETCOND_ONLY=<name>` runs one):

* the three motel loops of pass 1;
* **course** (120 ms and 250 ms): the breach-lab floor with a lane of
  obstacles. A 0.25 m curb and a 0.28 m step block (step up, step down), a
  0.95 m wall to vault, a framed wall with a door the player opens with use
  (someone shuts it whenever the player is back at the start, and at 28 s it
  is breached: latch broken and knocked open), a framed wall with a free
  hinged leaf the body pushes through, and a 0.70 m crate to climb onto and
  drop off. The second human holds a post at the lane edge, so the player
  shoulders past it on every pass. The player goes out and back, turning in
  place, and reacts to what it sees: its presented position (jump at the
  wall, use the door when the replica shows it shut). The check also
  requires that the run really exercised clambers, step-ups, shoves, door
  opens and leaf pushes;
* **stance** (120 ms): the motel loop with the remote player toggling
  crouch and lean;
* **stalls**: the motel loop with a 150 ms uplink stall every 2 s (every
  packet sent in the window is released at its end), which starves the
  server and then floods it.

New metrics: per-event correction p95; remote stance height and lean offset
error against the authority at the shown time; door leaf angle error at the
shown time and the largest per-frame leaf step (the leaf swings 0.95° per
120 Hz frame); the local truth jerk at the same sampling, as a reference for
the local jerk; and the course event counts. A door-shot unit check covers
the door rewind.

### Results (proto 53 / pass 1 / pass 2)

#### 50ms (RTT 50 ms, jitter +-0 ms per direction, loss 0%, duplication 0.0%)

| metric | proto 53 | pass 1 | pass 2 |
|---|---:|---:|---:|
| remote jerk RMS (mm/frame²) | 45.4 | 5.40 | 7.07 |
| local error vs own input (cm, mean) | 16.3 | 4.93 | 2.60 |
| local error p95 (cm) | 30.6 | 24.6 | 7.66 |
| local jerk RMS (mm/frame²) | 67.8 | 3.13 | 3.24 |
|   truth jerk, own body (mm/frame²) | 13.0 | 13.0 | 12.9 |
| local correction mean (cm) | 7.96 | 6.93 | 0.72 |
| local correction p95 (cm) | 15.2 | 26.3 | 0.68 |
| local correction max (cm) | 74.9 | 62.6 | 3.38 |
| input latency mean (ms) | 41.6 | 41.6 | 41.6 |
| server starvation (ticks) | 3 | 3 | 3 |
| server merged commands | 0 | 0 | 0 |
| door leaf angle error (deg, mean) | 0.00 | 1.48 | 0.01 |
| door leaf angle error max (deg) | 0.00 | 4.42 | 0.68 |
| door leaf step per frame max (deg) | 3.82 | 3.82 | 0.96 |
| downstream (kB/s) | 2183 | 8.92 | 8.99 |
| upstream (kB/s) | 6.24 | 6.80 | 6.80 |

#### 120ms+-30 2% (RTT 120 ms, jitter +-30 ms per direction, loss 2%, duplication 0.5%)

| metric | proto 53 | pass 1 | pass 2 |
|---|---:|---:|---:|
| remote jerk RMS (mm/frame²) | 80.1 | 4.02 | 5.33 |
| local error vs own input (cm, mean) | 114 | 5.25 | 2.71 |
| local error p95 (cm) | 206 | 25.2 | 7.80 |
| local jerk RMS (mm/frame²) | 125 | 2.73 | 3.55 |
|   truth jerk, own body (mm/frame²) | 11.7 | 7.47 | 13.5 |
| local correction mean (cm) | 21.5 | 5.89 | 0.64 |
| local correction p95 (cm) | 65.2 | 21.1 | 1.48 |
| local correction max (cm) | 165 | 62.3 | 1.88 |
| input latency mean (ms) | 471 | 106 | 106 |
| server starvation (ticks) | 29 | 7 | 7 |
| server merged commands | 0 | 0 | 0 |
| door leaf angle error (deg, mean) | 0.00 | 4.76 | 0.03 |
| door leaf angle error max (deg) | 0.00 | 14.7 | 1.79 |
| door leaf step per frame max (deg) | 49.7 | 11.5 | 0.97 |
| downstream (kB/s) | 2183 | 11.5 | 11.7 |
| upstream (kB/s) | 6.34 | 9.80 | 9.80 |

#### 250ms+-60 5% (RTT 250 ms, jitter +-60 ms per direction, loss 5%, duplication 1.0%)

| metric | proto 53 | pass 1 | pass 2 |
|---|---:|---:|---:|
| remote jerk RMS (mm/frame²) | 121 | 3.63 | 2.78 |
| local error vs own input (cm, mean) | 137 | 12.4 | 2.81 |
| local error p95 (cm) | 268 | 66.7 | 7.66 |
| local jerk RMS (mm/frame²) | 153 | 3.41 | 3.43 |
|   truth jerk, own body (mm/frame²) | 9.94 | 13.0 | 13.0 |
| local correction mean (cm) | 63.2 | 9.63 | 1.82 |
| local correction p95 (cm) | 178 | 61.1 | 4.66 |
| local correction max (cm) | 246 | 87.1 | 4.72 |
| input latency mean (ms) | 748 | 181 | 181 |
| server starvation (ticks) | 719 | 15 | 15 |
| server merged commands | 445 | 3 | 3 |
| door leaf angle error (deg, mean) | 0.00 | 5.53 | 0.06 |
| door leaf angle error max (deg) | 0.00 | 20.9 | 3.78 |
| door leaf step per frame max (deg) | 90.0 | 15.3 | 1.60 |
| downstream (kB/s) | 2182 | 13.6 | 13.6 |
| upstream (kB/s) | 5.83 | 13.8 | 13.8 |

#### course 120ms+-30 2% (RTT 120 ms, jitter +-30 ms per direction, loss 2%, duplication 0.5%)

| metric | proto 53 | pass 1 | pass 2 |
|---|---:|---:|---:|
| remote jerk RMS (mm/frame²) | 0.00 | 1.01 | 0.31 |
| local error vs own input (cm, mean) | 110 | 19.1 | 4.89 |
| local error p95 (cm) | 161 | 74.3 | 19.3 |
| local jerk RMS (mm/frame²) | 71.8 | 60.6 | 18.2 |
|   truth jerk, own body (mm/frame²) | 13.0 | 23.7 | 23.7 |
| local correction mean (cm) | 9.19 | 18.2 | 1.73 |
| local correction p95 (cm) | 21.8 | 67.0 | 5.28 |
| local correction max (cm) | 123 | 71.6 | 5.28 |
| input latency mean (ms) | 549 | 108 | 108 |
| server starvation (ticks) | 109 | 7 | 7 |
| server merged commands | 75 | 0 | 0 |
| clambers (server) | 4 | 7 | 7 |
| step-up ticks (server) | 5 | 13 | 13 |
| shove contact ticks | 33 | 93 | 93 |
| use-door opens | 2 | 2 | 2 |
| free-leaf push ticks | 23 | 25 | 25 |
| downstream (kB/s) | 549 | 24.6 | 23.9 |
| upstream (kB/s) | 6.34 | 8.82 | 9.17 |

#### course 250ms+-60 5% (RTT 250 ms, jitter +-60 ms per direction, loss 5%, duplication 1.0%)

| metric | proto 53 | pass 1 | pass 2 |
|---|---:|---:|---:|
| remote jerk RMS (mm/frame²) | 1.01 | 0.20 | 2.40 |
| local error vs own input (cm, mean) | 180 | 28.8 | 4.93 |
| local error p95 (cm) | 370 | 102 | 20.5 |
| local jerk RMS (mm/frame²) | 103 | 92.2 | 18.2 |
|   truth jerk, own body (mm/frame²) | 18.9 | 23.7 | 23.7 |
| local correction mean (cm) | 14.3 | 20.7 | 2.18 |
| local correction p95 (cm) | 42.4 | 65.9 | 3.64 |
| local correction max (cm) | 108 | 67.1 | 6.29 |
| input latency mean (ms) | 829 | 182 | 182 |
| server starvation (ticks) | 336 | 16 | 16 |
| server merged commands | 255 | 5 | 5 |
| clambers (server) | 3 | 7 | 7 |
| step-up ticks (server) | 13 | 13 | 13 |
| shove contact ticks | 133 | 94 | 94 |
| use-door opens | 2 | 2 | 2 |
| free-leaf push ticks | 0 | 25 | 25 |
| downstream (kB/s) | 549 | 42.1 | 42.9 |
| upstream (kB/s) | 6.50 | 12.1 | 12.7 |

#### stance 120ms+-30 2% (RTT 120 ms, jitter +-30 ms per direction, loss 2%, duplication 0.5%)

| metric | proto 53 | pass 1 | pass 2 |
|---|---:|---:|---:|
| remote jerk RMS (mm/frame²) | 71.3 | 9.05 | 7.47 |
| remote stance height error (cm, mean) | 0.00 | 0.62 | 0.08 |
| remote stance height error max (cm) | 0.00 | 81.3 | 58.1 |
| remote lean offset error (cm, mean) | 0.00 | 0.66 | 0.01 |
| remote lean offset error max (cm) | 0.00 | 30.5 | 4.07 |
| local error vs own input (cm, mean) | 116 | 7.28 | 2.74 |
| local error p95 (cm) | 218 | 51.1 | 8.49 |
| local jerk RMS (mm/frame²) | 122 | 2.93 | 4.00 |
|   truth jerk, own body (mm/frame²) | 11.8 | 12.3 | 13.6 |
| local correction mean (cm) | 21.7 | 7.03 | 2.31 |
| local correction p95 (cm) | 56.4 | 27.2 | 9.30 |
| local correction max (cm) | 182 | 62.6 | 10.1 |
| input latency mean (ms) | 457 | 106 | 106 |
| server starvation (ticks) | 46 | 7 | 7 |
| server merged commands | 0 | 0 | 0 |
| door leaf angle error (deg, mean) | 0.00 | 4.52 | 0.01 |
| door leaf angle error max (deg) | 0.00 | 14.2 | 1.33 |
| door leaf step per frame max (deg) | 36.5 | 11.5 | 0.97 |
| downstream (kB/s) | 2182 | 11.2 | 11.2 |
| upstream (kB/s) | 6.37 | 9.76 | 9.76 |

#### stalls 120ms+-10, 150 ms uplink stall every 2 s (RTT 120 ms, jitter +-10 ms per direction, loss 0%, duplication 0.0%)

| metric | proto 53 | pass 1 | pass 2 |
|---|---:|---:|---:|
| remote jerk RMS (mm/frame²) | 43.6 | 5.08 | 5.27 |
| local error vs own input (cm, mean) | 60.5 | 14.4 | 5.70 |
| local error p95 (cm) | 124 | 59.8 | 30.5 |
| local jerk RMS (mm/frame²) | 65.9 | 5.29 | 8.55 |
|   truth jerk, own body (mm/frame²) | 11.8 | 10.3 | 10.2 |
| local correction mean (cm) | 7.94 | 8.26 | 5.06 |
| local correction p95 (cm) | 15.3 | 27.4 | 9.47 |
| local correction max (cm) | 76.7 | 99.9 | 20.3 |
| input latency mean (ms) | 223 | 204 | 204 |
| server starvation (ticks) | 14 | 132 | 132 |
| server merged commands | 0 | 119 | 119 |
| door leaf angle error (deg, mean) | 0.00 | 2.15 | 0.01 |
| door leaf angle error max (deg) | 0.00 | 7.38 | 0.80 |
| door leaf step per frame max (deg) | 3.82 | 3.82 | 0.96 |
| downstream (kB/s) | 2183 | 11.9 | 11.6 |
| upstream (kB/s) | 6.24 | 12.6 | 12.6 |

How to read these numbers:

* Local corrections fall from a 62-87 cm maximum (pass 1) to 2-6 cm on the
  motel loops and the course (10 cm in the stance run, where the player
  brushes the crouching remote player), and the course's p95 from 66 cm to
  3.6-5.3 cm,
  while the course really does 7 clambers, 13 step-up ticks and about 90
  ticks of shoulder contact per run. The remaining stall-scenario maximum
  (20 cm, from 100 cm) is the drain merging the burst that follows a stall.
* Local jerk is now set by the authority's own motion: the column
  "truth jerk, own body" is the server's trajectory at the same sampling.
  Before, the kinematic prediction looked smoother than the truth only
  because it did not show step-ups and clambers until a correction blended
  them in.
* Motel remote jerk and truth jerk both moved in pass 2 (5.4 to 7.1 at
  50 ms, with truth 7.1 to 10.0): the starvation change alters the local
  player's server trajectory and so what the bots react to. The ratio to the
  truth is unchanged.
* Remote stance: the mean error drops from 0.62 to 0.08 cm and the lean
  error from 0.66 to 0.01 cm. The stance maximum stays large because the
  authority's crouch is a step function of 81 cm; the presentation
  interpolates it over one snapshot interval at the right time instead of
  applying it up to the display delay early.
* Door leaves: angle error at the shown time drops to about 0.01-0.03°
  mean (from 1.5-4.8°), and the largest per-frame leaf step to 1-1.6°, the
  leaf's own swing rate, from 3.8-50°.
* Proto 53 shows zero remote stance and door errors because it presented
  the newest snapshot as is (its shown time *is* the newest tick); its jerk
  and delay columns show the cost.

### Real UDP (`make net-udp-test`)

Two processes on localhost: the server process runs `net.c`'s ENet server
at 60 Hz wall time on the motel; the client process runs `net.c`'s client
(at 120 Hz frames, commands at 60 Hz, the motel loop script) and a UDP relay
thread between its ENet socket and the server's. The relay delays each
datagram by RTT/2 ± jitter (120 ± 20 ms), drops 2% and duplicates 0.5%,
from a seeded generator. The server writes its truth (actor positions per
tick, its position after each executed client command) to a file and the
client compares its presentation with it after both exit. Wall-clock
scheduling varies the numbers slightly between runs, so the limits are
looser than the harness's. A typical run (and two more taken while `make
test -j4` was loading the machine):

| metric (real UDP, two processes) | value |
|---|---:|
| remote error vs truth at shown time (cm, mean / p95) | 0.11 / 0.25 |
| remote display delay (ms) | 134 |
| remote jerk RMS (mm/frame²) | 1.45 |
| local error vs own input (cm, mean / p95) | 3.9 / 12.9 (4.0-5.2 loaded) |
| local correction mean / max (cm) | 5.2 / 14.4 (max 4-8 loaded) |
| input latency mean (ms) | 95 |
| server starvation / merges | 0 / 0 |
| snapshot deltas applied (per s) | 29.0-29.1 |
| downstream ENet payload (kB/s) | 6.7 |

Limits: local error mean under 12 cm, correction max under 40 cm, remote
p95 under 4 cm, remote jerk under 20, input latency 40-260 ms, more than 18
snapshots per second, downstream under 40 kB/s, prediction on the shared
controller, and the shim must actually have dropped packets.

## Known limits and next steps

* **Prediction inputs.** The replay is exact for the authority's actual
  inputs. What it cannot know: other players' real inputs (they are
  dead-reckoned from velocity), server starvation and drain merges (each
  changes how many ticks the server ran), and the authority's door use by
  others before it arrives. These produce the remaining few-cm corrections,
  and occasionally a discrete flip (a step-down one tick apart at an edge).
* **Stalls.** After a stall the drain merges the burst; each merge drops a
  tick of movement and shows as a correction (stall maximum 20 cm). A
  merge that kept both ticks' displacement would need a server-side
  "catch-up" step and was not attempted.
* **Lag compensation scope.** It covers hitscan against actor bodies and
  the first 64 door leaves. Props and rubble are tested where they are now
  (see above). Remote stance is still not rewound, only position.
* **Rewind bound.** 400 ms covers the 250 ms scenario. Competitive modes may
  want a lower bound and per-player caps.
* **Input upstream size.** Redundancy grows upstream with RTT. Per-field
  quantisation would shrink it further.
* **Real networks.** The UDP test runs on localhost through a shim; it
  checks the transport, timing and metrics end to end, but not real Wi-Fi
  or WAN behaviour (bufferbloat, reordering at scale, NAT).
