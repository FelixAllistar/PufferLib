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
| `netcode.c/h` | transport-independent pieces: command queue, snapshot ring, view interpolation, local predictor, tuning switches |
| `net.c/h` | ENet transport (server/client) using the above |
| `sim.c/h` | lag-compensation history and bounded rewind for hitscan |
| `tests/test_netcond.c` | deterministic network-conditions harness (`make net-conditions-test`, part of `make test`) |

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
instead of disconnecting.

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

## Known limits and next steps

* **Prediction model.** Prediction covers ground locomotion only. Jumps,
  clambers, step-ups and depenetration, door pushes and actor-to-actor shoves
  stay authoritative and are smoothed as corrections. One trace showed a
  single-tick authoritative move of 0.6 m at a lot obstacle; moves like that
  give the 60-90 cm maximum. Fuller prediction would re-simulate the body in
  the replica world.
* **Server starvation.** With no queued command, the server repeats the
  previous movement input (protocol 53 behaviour) and then executes the late
  command too. The drain removes the latency this adds, but the extra movement
  shows up as a correction.
* **Remote state timing.** Remote stance, weapon pose and animation states
  apply on arrival. Only the body pose, yaw and pitch are interpolated, so a
  crouch can lead the interpolated position by the display delay.
* **Doors.** Doors are applied immediately rather than interpolated, which
  keeps door state latency at one-way delay plus at most one snapshot interval.
* **Lag compensation scope.** It covers hitscan against actor bodies only.
  Doors, props and rubble are tested where they are now. Remote stance is not
  rewound, only position.
* **Rewind bound.** 400 ms covers the 250 ms scenario. Competitive modes may
  want a lower bound and per-player caps.
* **Input upstream size.** Redundancy grows upstream with RTT. Per-field
  quantisation would shrink it further.
