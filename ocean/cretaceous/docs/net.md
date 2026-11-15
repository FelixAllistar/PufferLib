# Cretaceous netcode

The server owns the sim. Clients send inputs, predict their own animal, and draw everyone else from a snapshot buffer.
Solo play never touches the network.
Code: `net.c` (codec, server, client transport), `netprops.c` (v11 movable props, included by `net.c`), `netcritters.c` (v12 critters, included by `net.c`), `netjoin.c` (v20 joining while the world loads, included by `net.c`), `netclient.c` (prediction, reconciliation, interpolation), `netsim.c` (link conditioner), `net_priv.h` (internals shared with the tools).
Tools: `tools/netbench.c` (scenario benchmark, with recorded replays) and `tools/netfuzz.c` (codec round trip and fuzzer).
Tests (`make test`, docs/testing.md): `tests/test_net_codec.c` (the net code against an in-memory ENet: round trips, atomic snapshots, fuzz), `tests/test_net_respawn.c` (respawn, profile and status protocol over loopback ENet), `tests/test_net_join.c` (joining during a slow world load, over loopback ENet) and the netbench replay check (a short recording replayed twice must give its hashes, section Replay).

## Protocol v22

Versions:
- **v6** (5.0): byte codec; respawn requests carry a sequence number and the server answers each one it settles.
- **v7** (cret/net): this bit-packed codec with per-client delta snapshots, prediction and interpolation, carrying v6's held requests and replies. cret/progression also called its own byte codec "7"; neither shipped alone.
- **v8**: v7 plus progression. Abilities ride in inputs; snapshots carry statuses, the own status timers and cooldowns, the own progress block and the nests; profile and unlock requests.
- **v9** (cret/controls): aimed attacks and the secondary attack. Inputs carry the head aim and `CRET_ACT_ATTACK2`; every agent carries its aim; the own state adds the secondary cooldown, the queued attack and the aim.
- **v10** (cret/feel): hit events for combat feedback (`render/fx.c`). Each snapshot ends with the recent hits near the client.
- **v11** (cret/physics): movable props. A prop block follows the hits: the boulders, stones and logs the server's physics moved, acknowledged per prop. Hit kinds grow to 4 bits (`CRET_HIT_CRUSH`).
- **v12** (cret/critters): small prey. A critter block follows the props: the client's nearest critters, whole every snapshot. Quests gain the catch kind and a spot flag (a hatchling's quest leads to a point, sent as x, z and radius, instead of a named place).
- **v13** (cret/nests-carry): carrying food. Inputs carry the carry press in the top bit of the flags; the own state ends with the carried slot and load; carcass entities gain the item kind, the critter kind and the carrier.
- **v14** (cret/quests2): quests. The quest kind grows to 4 bits (companion, ability, explore, nest, rest, scavenge, raid and guard, `docs/progression.md`), and each quest ends with its first-steps step (3 bits, 0 for an ordinary quest). The progress block ends with the first steps done (3 bits) and the places visited (32 bits), so a client's map fills in and its tracker labels first steps. A quest with a step past `CRET_CHAIN_STEPS`, or a block with more steps done, is refused.
- **v15** (cret/combat-feel): the attack queue holds 0.3 s (`CRET_ATTACK_BUFFER` 9 ticks, was 6), so the own state's queued-attack ticks grow from 3 to 4 bits (up to 10). A queue longer than `CRET_ATTACK_BUFFER + 1` is refused as before. Nothing else on the wire changed: the lunge (`sim.c`, `lunge_step`) moves the attacker, which snapshots already carry, and client prediction runs the same lunge against the agents it has.
- **v16** (cret/ecosystem): energy (`docs/ecosystem.md`, section 1). The own state adds the fat reserve (8 bits, after food and water), which the HUD draws as a line under the food bar.
- **v17** (cret/ecosystem): plants as a resource (`docs/ecosystem.md`, section 2). The own state adds the biomass of the plant cell at the mouth (8 bits, after the fat); the client writes it into its own plant table so the graze prompt sees what grazing left. Carcass entities may carry a fourth item kind, a piece of meat torn off a body (`CRET_ITEM_MEAT`, the 2-bit kind's last value), which a v15 client would refuse.
- **v18** (cret/lag-comp): lag-compensated hits (section Lag compensation below). An input that presses an attack, the secondary attack or an ability slot carries the client's view after it: a presence bit, then the server tick its remote agents were drawn at, as 32 bits of tick and 4 of fraction (1/16 tick, about 2 ms). 37 bits a press (1 without a view); every other input pays nothing.
- **v19** (cret/knockdown2): knockdowns (README, Knockdowns). Agent entities gain a knockdown field (the tick the animal is up again, the fall's length and side); the own state ends with the lying, get-up and immunity timers and the fall's length and side.
- **v20** (cret/async-join): joining while the world loads (section Join and loading below). A new client message, LOADING, reports the load's stage; the server holds a loading client's animal, sends it no snapshots and drops it with `'L' << 24 | seconds` after `CRET_NET_LOAD_LIMIT_S` (300 s). Nothing else on the wire changed.
- **v21** (cret/wounds): wounds (README, Wounds). Agent entities gain a 15-bit wound field: the leg, body and head severity in twentieths, 5 bits each, rounded down, any wound at least one twentieth. The client draws the limp (`CRET_WOUND_LIMP`, 0.2) and drips blood on the trails it records (`CRET_WOUND_DRIP`, 0.25) from it; both are multiples of 0.05, so they read on the client exactly as on the server. The own state ends with the three severities at full precision, each a presence bit and a float (prediction slows the animal by its leg wound); a severity outside 0..1 is refused.
- **v22** (cret/net-replay): the snapshot rate is a server setting (section Snapshot rate). The welcome's last byte carries the rate in Hz (15, 20 or 30) where it carried the snapshot divisor (2), which a v21 client read and ignored; nothing else on the wire changed.

All messages are bit-packed (MSB first) through a bounds-checked reader and writer.
A short or malformed message sets a flag rather than reading past the end, and the receiver refuses the whole message.
C structs never cross the wire.

| Message | Channel | Content |
|---|---|---|
| connect | ENet connect data | `CRET_NET_VERSION`. A server refuses any other version and disconnects with `'V' << 24 \| its version`; the client prints both versions. |
| WELCOME | reliable | version, world seed, world size, cell, agent count, slot, tick rate, snapshot rate in Hz (v22; the divisor before). The client refuses a rate of 0 or above the tick rate. |
| INPUT | unsequenced, 30 Hz | snapshot ack (newest received seq, plus a 32-bit mask of the ones before it); newest input seq; 1..32 inputs, newest first. Each input is 40 bits (v9; v8: 32): forward as a signed byte, then strafe and turn as signed bytes, sprint, jump, a 3-bit action, a 2-bit ability slot and the carry press (v13, the top bit; 0 before), and the aim as a signed byte (-127..127 for -1..1), or 1 bit when it repeats the next newer one. A press (an attack, the secondary attack or an ability slot) is followed by its view (v18): 1 bit, then when set the render tick at the press, 32 bits of tick and 4 of sixteenths. The server refuses the packet when any input has an action past `CRET_ACT_MAX` (`CRET_ACT_ATTACK2`), an ability slot past `CRET_AB_SLOTS` or an aim byte of -128; it never refuses a view, it clamps it (Lag compensation). |
| SNAPSHOT | unsequenced, 15 Hz per client (the default; 20 or 30 with `--snapshot-hz`) | seq, server tick, last input applied, server input queue depth, time of day, cloud cover; own state at full precision; progress and nest blocks; hits; movable props (v11); critters (v12); entity updates |
| REQUEST | reliable | 8-bit request seq; species, or 255 for the same one |
| SPAWN_REPLY | reliable | once per settled request: seq, result (ok, bad species, in combat), species after the reply, seconds until the combat lock lifts |
| PROFILE | reliable | character name: 5-bit length (up to 23), then the bytes. The host loads and saves that character (`character.c` cleans the name). |
| UNLOCK | reliable | 1 bit: buy or equip slot-3 ability choice A or B (only while alive) |
| LOADING | reliable, 2 Hz while loading | v20: 1 bit done, the stage's fraction (8 bits), the stage name (5-bit length up to 31, then the bytes). The server prints the stage; it replaces anything outside printable ASCII. |

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
  - v19 adds the knockdown (sim.h, `CRET_KD_*`): ticks left lying (7 bits), getting up (5), before it can be knocked down again (8), the fall's length in ticks (7) and its side (1). 28 bits. The decoder refuses more ticks lying than the fall lasts, a get-up longer than `CRET_KD_GETUP_TICKS`, and lying and getting up at once. Prediction gates the input as the server does (nothing while lying, only the turn while getting up: `cret_down_input`), runs the timers down in `agent_timers`, and compares all three timers exactly, so the predicted animal stops the moment a snapshot says the server has it down, and the knockdown itself (set in `damage()` on the server) corrects it one round trip after the blow.
- **Own progress** (v8): marks, unlocked slot-3 choices, quests done, the three loadout abilities, the quest (kind, places, step, prey species, progress and goal as floats, rewards) and the group's quest. About 30–50 bytes, behind a presence bit: sent for 8 snapshots after it changes and once a second regardless (it is unreliable, like the rest of the snapshot). A quest in progress therefore goes out in every snapshot.
- **Nests** (v8): all active nests (species, group, owner, eggs, position), on the same change-or-once-a-second rule. A client therefore sees the same nests and egg counts as the server, at most about a second late; the renderer draws them from these (`render/nest.c`). Build progress is sent only for the client's own animal (`nest_ticks` in the own state); other animals' nests rise on the client's own clock while they hold the nest action.
- **Entities** (agents 0..127, carcasses 128..159): id (8 bits), baseline age (5 bits, 0 = from zero), then each field against the baseline.
  - Quantisation: positions in 1/256 m (21 bits for the 8 km island, sized from the world), height from −2048 m, heading in 12 bits, speed in 1/16 m/s, health, growth and meat in 8 bits.
  - Flinch and death are sent as the tick they end or start (16 bits), so they do not change every tick.
  - Delta fields (position, heading, speed) use a 2-bit code: same, small signed delta, medium signed delta, or full value. Other fields use one changed bit.
  - v8 adds two agent fields: 8 status flags (stunned, afraid, latching, latched, defensive, marked, sprint burst, mending) and who marked it. Remote animals get status timers of 1 or 0; only the local animal gets real timers.
  - v9 adds the aim: 6 bits, 0..62 for -1..1 in steps of 1/31 (the head turns toward it, `render/dino.c`). It changes only with the bit-per-field change flag.
  - v13 widens the carcass kind field from 4 to 9 bits (active, species, then the item kind in 2 bits: body, critter or egg, and the critter kind in 3) and adds the carrier (8 bits, agent + 1, 0 on the ground). A carried item's position is the carrier's mouth on the server; the client draws it at the drawn carrier's jaws (`cret_carry_follow` on the interpolated copy), so it does not lag behind the animal.
  - v19 adds the knockdown: 25 bits, 0 while the animal stands; else the tick it will be up again (16 bits, as flinch and death are sent, so it does not change every tick), the fall's length in ticks (7), the side it fell toward (1) and a set bit. The client turns it back into the lying and get-up timers at its own reference tick (`netp_ent_to_agent`: more than `CRET_KD_GETUP_TICKS` left is lying), so a remote animal's fall is posed from the same numbers as the server's (`cret_down_pose`, `render/dino.c`). The decoder refuses a set field with a zero length and a clear one with other bits. It costs a bit per entity update (the changed flag) and 25 more bits twice per fall.
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
  - Hunting (cret/hunt-sense) changed nothing on the wire; the version stays 13. Critters gained two fields the server alone uses (flee legs, jink timer); a critter's watch (head up or feeding, `cret_critter_watch`) is computed from its pool slot, state and the sim tick, which the client already has, so the client draws the same head as the server decides by (to within the interpolation delay). Scent trails are recorded by each client from its snapshots (`cret_scent_record(s, true)` in `netp_client_apply`), and sniffing is local: the creep it imposes is in the inputs sent, and the scent view and panel are drawn from the client's own sim.
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
  - Statuses gate it as on the server (stunned or latched: no movement; slowed, defensive or burst: speed), and their timers and the ability cooldowns run down.
  - Ability casts are predicted (section Predicted casts below): the press starts the cast on the client's own animal at once, through the server's own cast rules.
  - A bite's lunge reaches for the target where the client drew it at the press, as the server's does (Lag compensation, the lunge).
  - Prop colliders stream in around the animal as on the server. Other agents' collision bodies sit at their latest snapshot positions (`cret_sim_place`).
- **On a snapshot**: compare the prediction made after the acknowledged input with the server's own state.
  - If position, heading, velocity, flags, action, cooldown, stamina and the statuses and ability cooldowns (within 0.1 s) match (within 2 mm), nothing happens.
  - Otherwise the animal is reset to the server's state and the newer inputs are replayed.
  - The visible animal keeps its place: the difference becomes an offset that decays with a 100 ms time constant. Only a jump of more than 4 m (a respawn) snaps.
  - The drawn animal is also interpolated between its last two predicted ticks, for frame rates above 30 Hz.

## Predicted casts

Before, a cast started on the server and the client's animal followed a round trip later: the client sent the slot, and the next snapshot brought the cooldown, the stance or the leap, which reconciliation then smoothed in. A pounce or dodge leap was pulled 0.4 to 1.8 m along its path, and every cast showed 30 to 55 ms (LAN) to 320 to 345 ms (250 ms round trip) after the press.

Now the client runs the server's cast rules on its predicted animal, in the same code (`abilities.c`):
- `cret_status_input(s, i, in, predict)` and `cast` take a `predict` flag. `cret_sim_move(..., predict = true)` calls them as the server's step does, so readiness (`cret_ability_ready`: alive, cooldown, stamina, not stunned, latched or down), the carry rule, the per-kind conditions (a strike not mid-swing, swimming or afraid; a leap only from the ground and not limping; mend not with raptors on) and the effect on the caster (cooldown, stamina, the strike, the stance or buff, the leap's launch) are the server's own lines.
- Predicting, a cast does nothing to other animals: `area` applies only the caster's own share, a dodge leap does not shake latched raptors off, and no counts or quest progress are kept. Letting go of a latch (Space) stays the server's too: the victim's state changes.
- The gating before it is shared too: a stun now also ends the predicted swing (the server's rule, which prediction lacked before), a latch or a knockdown holds the input as before.
- `cret_leap_step(s, i, predict)`, split out of `cret_status_step`, runs a leap's timer and its end on landing in prediction as well; only the pounce's catch (`try_latch`) is left out.
- The client knows its loadout from the progress block and its cooldowns, statuses and stamina from its own state, so nothing new is on the wire: the protocol stays v19, and `CRET_NET_VERSION` is unchanged.
- The server stays authoritative. A cast the server refuses (its stamina a little lower, a stun the client did not know of yet) or one it carries out that the client did not, differs in the own state (cooldowns, statuses, leap, strike) and reconciliation replays from the server's state as for movement.

What is predicted, per ability:
- **Strikes** (hamstring, crushing and rending bite, tail slam, spike sweep, charge headbutt, shoulder shove): the swing starts on the press, with its cooldowns and stamina, the charge's run-in at 1.25 times sprint speed, and the lunge of the front blows. The hit and all it does (damage, bleeding, slow, stun, broken bone, knockback) are the server's and arrive with the snapshot, as for a plain bite.
- **Pounce**: the leap (launch speed, the arc at 1 g, the 1.2 s timer, the landing). Catching hold of an animal the head reaches, riding its flank and the bites after are the server's: when the server latches on, the next snapshot moves the raptor to the flank.
- **Dodge leap**: the hop (to the side with the strafe keys, back with S, else forward) and the 0.6 s in which attacks miss it. Shaking off raptors latched on is the server's.
- **Sprint burst, defensive stance, hunker, mend**: the status on the caster and what movement reads (the burst's speed, the stances' speed factor and armour, mend's stopped bleeding; mend's healing itself comes with the health in snapshots).
- **Fear roar, herd bellow, alert call**: the call (the roar and the bellow stop the animal for the call, as on the server), the cooldown and stamina, and the bellow's defence on the caster itself. Fear, marks and the bellow on herd-mates are the server's.

**Measured** (netbench, below: 12 clients of which 6 fighters, 300 s per scenario, 64 agents; every client casts its ready abilities about every 1.5 s, the odd clients play the six species in turn and switch their slot-3 choice every 20 s). Start is the time from the press to the first frame whose predicted animal shows the cast; the correction is what reconciliation smoothed into the drawn animal over the 0.5 s after the first snapshot that acknowledged the press. Means over two runs per build and scenario (four after at 80 ms); before is the code before this change with the same netbench.

Press to start, ms, before → after (the bite was predicted already):

| Ability | Casts (before / after) | LAN | 80 ms | 150 ms | 250 ms |
|---|---|---|---|---|---|
| Pounce | 270 / 340 | 46 → 9 | 124 → 8 | 228 → 8 | 329 → 8 |
| Pounce, caught hold | 97 / 124 | 35 → 8 | 130 → 7 | 225 → 6 | 337 → 12 |
| Dodge Leap | 183 / 234 | 44 → 8 | 133 → 8 | 224 → 8 | 328 → 8 |
| Charge Headbutt | 192 / 223 | 56 → 8 | 130 → 9 | 221 → 9 | 321 → 9 |
| Hamstring Bite | 141 / 168 | 44 → 8 | 114 → 8 | 227 → 8 | 330 → 7 |
| Crushing Bite | 193 / 236 | 50 → 8 | 118 → 8 | 216 → 9 | 328 → 7 |
| Rending Bite | 65 / 88 | 44 → 7 | 132 → 8 | 234 → 9 | 331 → 25 |
| Tail Slam | 160 / 176 | 46 → 7 | 126 → 7 | 217 → 7 | 319 → 7 |
| Spike Sweep | 37 / 49 | 49 → 7 | 120 → 7 | 233 → 6 | 339 → 10 |
| Shoulder Shove | 135 / 171 | 29 → 8 | 116 → 8 | 227 → 8 | 340 → 8 |
| Sprint Burst | 463 / 549 | 48 → 8 | 121 → 8 | 222 → 8 | 329 → 9 |
| Defensive Stance | 91 / 109 | 48 → 9 | 132 → 9 | 246 → 9 | 333 → 8 |
| Hunker | 85 / 108 | 34 → 8 | 121 → 8 | 230 → 8 | 322 → 8 |
| Mend | 347 / 444 | 45 → 10 | 125 → 8 | 223 → 9 | 336 → 9 |
| Fear Roar | 165 / 201 | 47 → 8 | 132 → 8 | 230 → 9 | 332 → 9 |
| Herd Bellow | 152 / 188 | 41 → 7 | 127 → 8 | 224 → 8 | 341 → 10 |
| Alert Call | 78 / 97 | 48 → 8 | 144 → 10 | 226 → 8 | 344 → 9 |
| Bite (lunge) | 3771 / 4681 | 8 → 8 | 8 → 8 | 8 → 8 | 8 → 8 |

Prediction error at the first snapshot acknowledging the press, cm, before → after:

| Ability | Casts (before / after) | LAN | 80 ms | 150 ms | 250 ms |
|---|---|---|---|---|---|
| Pounce | 270 / 340 | 35.7 → 10.2 | 29.9 → 3.6 | 38.2 → 1.6 | 34.2 → 5.5 |
| Pounce, caught hold | 97 / 124 | 162.6 → 143.9 | 149.2 → 117.8 | 139.2 → 174.0 | 137.0 → 124.9 |
| Dodge Leap | 183 / 234 | 30.4 → 0.1 | 30.3 → 0.0 | 29.9 → 0.0 | 29.8 → 0.0 |
| Charge Headbutt | 192 / 223 | 1.8 → 0.1 | 1.5 → 0.1 | 1.1 → 0.4 | 1.7 → 0.1 |
| Hamstring Bite | 141 / 168 | 1.4 → 0.3 | 0.8 → 0.0 | 1.1 → 0.1 | 1.3 → 0.1 |
| Crushing Bite | 193 / 236 | 1.4 → 0.2 | 0.9 → 0.1 | 0.9 → 1.1 | 1.3 → 0.2 |
| Rending Bite | 65 / 88 | 1.3 → 0.0 | 0.9 → 0.0 | 1.0 → 0.2 | 1.2 → 0.1 |
| Tail Slam | 160 / 176 | 0.7 → 0.1 | 0.7 → 0.1 | 0.6 → 0.0 | 0.7 → 0.1 |
| Spike Sweep | 37 / 49 | 0.8 → 0.0 | 0.6 → 0.5 | 0.7 → 0.1 | 0.9 → 0.1 |
| Shoulder Shove | 135 / 171 | 0.4 → 0.0 | 0.4 → 0.0 | 0.6 → 0.2 | 0.5 → 0.1 |
| Sprint Burst | 463 / 549 | 0.8 → 0.1 | 0.7 → 0.1 | 3.2 → 0.1 | 0.6 → 0.3 |
| Defensive Stance | 91 / 109 | 0.7 → 0.1 | 0.8 → 0.1 | 0.8 → 0.0 | 1.1 → 0.1 |
| Hunker | 85 / 108 | 0.7 → 0.1 | 1.0 → 0.1 | 1.0 → 0.1 | 0.8 → 0.1 |
| Mend | 347 / 444 | 0.1 → 0.1 | 0.1 → 2.3 | 0.3 → 0.1 | 0.0 → 0.3 |
| Fear Roar | 165 / 201 | 0.9 → 0.1 | 1.1 → 0.1 | 0.8 → 0.2 | 1.1 → 0.3 |
| Herd Bellow | 152 / 188 | 1.5 → 0.3 | 1.5 → 0.1 | 1.2 → 0.3 | 2.5 → 0.0 |
| Alert Call | 78 / 97 | 0.0 → 0.0 | 0.1 → 0.0 | 0.0 → 0.1 | 0.0 → 0.0 |
| Bite (lunge) | 3771 / 4681 | 1.0 → 0.6 | 0.6 → 0.9 | 0.7 → 0.7 | 1.0 → 0.7 |

Correction smoothed into the drawn animal over the 0.5 s after that snapshot, cm, before → after:

| Ability | Casts (before / after) | LAN | 80 ms | 150 ms | 250 ms |
|---|---|---|---|---|---|
| Pounce | 270 / 340 | 42.9 → 10.0 | 83.2 → 4.1 | 140.3 → 5.4 | 177.0 → 5.6 |
| Pounce, caught hold | 97 / 124 | 296.6 → 297.9 | 310.0 → 272.5 | 327.0 → 303.7 | 264.2 → 304.4 |
| Dodge Leap | 183 / 234 | 36.8 → 0.0 | 78.1 → 0.5 | 123.6 → 0.0 | 154.9 → 0.0 |
| Charge Headbutt | 192 / 223 | 3.1 → 0.8 | 10.5 → 3.4 | 22.1 → 0.7 | 45.3 → 13.2 |
| Hamstring Bite | 141 / 168 | 2.9 → 6.0 | 7.0 → 1.1 | 10.4 → 0.0 | 22.7 → 14.5 |
| Crushing Bite | 193 / 236 | 2.7 → 1.4 | 7.7 → 2.7 | 11.2 → 10.4 | 22.9 → 3.6 |
| Rending Bite | 65 / 88 | 2.1 → 2.3 | 6.7 → 0.4 | 6.3 → 0.4 | 18.5 → 8.3 |
| Tail Slam | 160 / 176 | 1.0 → 0.3 | 4.0 → 0.2 | 7.8 → 0.0 | 13.4 → 0.5 |
| Spike Sweep | 37 / 49 | 1.3 → 0.0 | 3.1 → 0.6 | 5.7 → 0.0 | 10.4 → 0.0 |
| Shoulder Shove | 135 / 171 | 0.4 → 0.1 | 2.2 → 0.0 | 6.6 → 1.2 | 7.5 → 0.2 |
| Sprint Burst | 463 / 549 | 8.4 → 4.2 | 4.6 → 6.0 | 17.6 → 3.9 | 17.1 → 4.2 |
| Defensive Stance | 91 / 109 | 1.1 → 0.8 | 4.2 → 0.0 | 11.3 → 0.0 | 20.8 → 0.0 |
| Hunker | 85 / 108 | 0.8 → 0.5 | 5.2 → 0.9 | 16.1 → 2.0 | 19.7 → 0.0 |
| Mend | 347 / 444 | 0.8 → 4.2 | 2.2 → 7.8 | 9.2 → 5.8 | 4.1 → 5.0 |
| Fear Roar | 165 / 201 | 2.2 → 0.9 | 10.7 → 2.4 | 10.8 → 3.7 | 16.0 → 8.0 |
| Herd Bellow | 152 / 188 | 3.6 → 1.4 | 15.3 → 5.6 | 16.3 → 3.0 | 44.1 → 1.2 |
| Alert Call | 78 / 97 | 1.8 → 0.0 | 0.6 → 0.0 | 21.9 → 3.8 | 18.8 → 0.1 |
| Bite (lunge) | 3771 / 4681 | 8.7 → 5.9 | 12.7 → 9.6 | 16.8 → 11.0 | 18.8 → 10.2 |

Reading it:
- Every cast now shows on the first frame after the press: 6 to 12 ms (a mean of 25 ms for the rending bite at 250 ms, where a few presses waited for a stamina or cooldown the client still had to regain), against one round trip and the input queue before (29 to 56 ms on LAN, 114 to 144 ms at 80 ms, 216 to 246 ms at 150 ms, 319 to 344 ms at 250 ms).
- The leaps no longer rubber-band. The dodge leap's error at the ack falls from 30 cm to under 1 mm and the 37 to 155 cm pulled along its path to nothing. A pounce that catches nothing falls from 30 to 38 cm and 43 to 177 cm to 2 to 10 cm and 4 to 10 cm. A pounce that catches hold is the server's (the raptor jumps to the victim's flank a round trip after the catch), so its correction stays about 3 m, as before.
- The self casts (stances, burst, mend), the calls and the strikes agreed to about 1 cm at the ack already (their effect on movement starts slowly), but the stance's slowdown, the burst's speed and the charge's run-in were corrected over the next 0.5 s by 10 to 45 cm at 150 and 250 ms; that is mostly gone (0 to 14 cm, the rest from blows by bots in the same window).
- Of 8186 casts in the after runs, 92 were mispredicted (the client showed a cast the server refused: a stun or a knockback it did not know of yet, or a slot-3 press just after a switch); 49 of them are bites, which were predicted before too. Reconciliation corrects them as any other mismatch.
- The bite's corrections are a third smaller at every latency (lunge, below).
- Over everything the client corrects (netbench's main table, same runs), corrections fall from 47 to 54 a minute to 38 to 56, their mean size at 150 and 250 ms from 13 to 16 cm to 9 to 15 cm. Bandwidth (8.1 to 9.1 KB/s down, 1.8 to 2.9 up), input-to-ack latency and the mean prediction error (4 to 8 mm) are unchanged within noise.

In play: `CRET_NETSIM="lat=75 jit=14" ./build/cretaceous/play join HOST` as a raptor or a gallimimus and press 1. The pounce or the dodge leap leaves the ground on the key press; before, the animal stood for about 0.2 s and was then pulled along the leap. The ability bar's cooldown starts on the press too. A pounce that catches hold still moves the raptor to its victim's flank a round trip after the catch.

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

## Lag compensation

A client draws other animals at its render tick: the estimated server tick minus the interpolation delay (section above). Its press then travels to the server and waits in the input queue. By the time the server applies it, what the client saw is a round trip, an interpolation delay and the queue old: measured by netbench (below), a mean of 131 ms on LAN, 221 ms at 80 ms, 344 ms at 150 ms with 20 ms jitter and 438 ms at 250 ms. Fast prey covers 2–3 m in that time, so before v18 a bite that closed on a running gallimimus on screen often landed on air at the server. Since v18 the server judges a client's swing where the client drew its targets.

- **What the client sends** (`netclient.c`, `net.c`): `cret_net_client_draw` keeps the render tick of the last frame (`NetClient.view`), and every input that presses an attack, the secondary attack or an ability slot carries it (Protocol, v18). Near agents are drawn at exactly that tick (far ones further back, but they are out of reach anyway).
- **The pose history** (`sim.h`, `CretRewind`): the net server owns it (`CretNet.rewind`) and points the sim at it (`CretSim.rewind`). At the start of every `cret_net_server_poll`, before the tick's step, it stores each agent's position, heading, capsule radius and height, growth, species, alive flag and age in ticks (`cret_rewind_record`): 16 ticks (533 ms) of 128 agents, 74 KB. The server's tick cost did not change measurably (0.61–0.64 ms in netbench, before and after).
- **The rewind** (`netp_take_view`, when the press's input is applied, by the step or by the catch-up of a deep queue): the view is clamped
  - to no later than the state this tick makes (never the future);
  - to at most `CRET_REWIND_MAX` (6 ticks, 200 ms) behind the newest snapshot the client had acknowledged when the press first arrived. The server knows that snapshot's tick from its own record of what it sent (`NetPeer.sent_tick`), not from anything the client claims; without an acknowledged snapshot the press is judged at the present;
  - the slot's rewind is then the state's tick minus the view, at most 15 ticks (the history; `CRET_REWIND_HIST - 1`).
  - A press without a view rewinds nothing, inputs without a press keep the slot's rewind (so a swing, queued or not, is judged with the press that started it), and a disconnect clears it (the bot that takes over swings at the present).
- **The hit test** (`sim.c`, `rewound`, used by `resolve_attack` and `resolve_alt`): for an attacker with a rewind, each target is tested at its pose that many ticks before the state the step makes, interpolated between the stored ticks (a jump of more than 5 m in one tick, an animal put somewhere, is not blended). The attacker stands where it is now. The height test, reach, the arc, the choice of the nearest body and the packmate rule (by the present group) are unchanged; only where the targets stood moves back. Damage, knockback (its direction from the present positions), bleeding, statuses, armour, thorns, bone breaks and the hit log all apply to the present animals.
- **What is rewound**: the primary blow (bite, headbutt, kick, the tail blows of stegosaurs and sauropeltas), every strike ability, because they resolve through `resolve_attack` (hamstring bite, crushing bite, rending bite, tail slam, spike sweep, charge headbutt, shoulder shove), and the secondary attack through `resolve_alt` (tail swipe, slash, kick).
- **The lunge** (cret/predict-casts): through the wind-up a front blow carries the attacker toward its target (README, Lunge). `lunge_target` reads the targets through `rewound` as the hit test does, so for a client with a view the server's attacker lunges toward where the target stood at the view, the pose the blow is then judged against, and the queued bite's check (does the target draw away faster than the lunge could follow) reads it too. `rewound` gives that pose's own velocity (from the two stored ticks), which the lunge reads. The client predicts the same lunge (`netp_client_predict`): each input carries the tick the server will judge it at (`NetClient.in_rv`: the press's view, clamped as `netp_take_view` clamps it, then a tick later for each input after), and the client sim rewinds to the drawn poses around it, from its interpolation buffer (`draw_agent`, extrapolated past the newest sample exactly as the screen does). Bots, the trainer and a press without a view lunge at the present as before.
- **What is not, and why**:
  - The pounce is not a swing judged at a hit time: through the whole leap the raptor's head is tested for contact every tick (`try_latch` in `abilities.c`), and once it holds on it rides the victim's present flank. Judging that contact in the past would latch a raptor onto an animal that is no longer under it.
  - Fear roar, alert call and herd bellow act on a radius around the caster at the cast, not on a blow; dodge leap, sprint burst, the stances and mend hit nothing.
  - The snap at small prey (`resolve_snap`) and a bite's catch of a critter: critters live only on the server (the client eases them toward each new position, `render/critters.c`), and the server keeps no history of them. The snap's "an animal is in the way" check stays at the present too.
  - Latched bites (they hold on, there is no reach test) and crush damage from props.
- **Knockdowns** (v19, README Knockdowns): a knocked-down animal's history is its real pose, because the sim never moves or reshapes its capsule for the fall: the roll onto the side is drawn (`render/dino.c`, `render/view.c`), not simulated. A lying animal is hit with the same standing capsule (radius, height) as before it fell, at the position the knockback slid it to, so the rewound hit test needs nothing new. Whether a blow knocks down, and how long it lies, is judged on the present animal in `damage()` (its present timers, immunity and water), like the rest of what a blow does. Its drawn pose on the client comes from the snapshot's knockdown field at the same interpolated tick as its position, so what the client aims at lies where the history has it.
- **Limits** (against a client claiming an older view to hit what it never saw): at most 200 ms behind the newest snapshot the server had sent it and it acknowledged, which covers the adaptive interpolation delay (107–149 ms in every scenario) but not a made-up past; never the future; never more than the 16-tick history (500 ms) even with acknowledgements held back on purpose (that only makes its own snapshots staler); reach, arc and height unchanged; a target that was dead in any stored tick since, or respawned (its age dropped), cannot be hit; an agent that did not exist then is not in the history.
- **The sim stays neutral**: `CretSim.rewind` is NULL for the trainer, bots, solo play, the listen host's own animal and replays, and a bot slot's rewind is 0. Then `rewound` returns the present agent and the code path is the one before. `build/cretaceous/rlbench --hash --threads 1 --ticks 2000` prints `96d771ee056376c0` before and after, and the determinism suite passes.

**Design decision, the 200 ms cap.** The cap is taken against the newest snapshot the client had, not against the server's present, because the server's present is a round trip later than anything the client could show: a total cap of 200 ms would compensate 80 ms fully and 150 ms by only 60%. Measured with that total cap instead (same runs): agreement 98.5% on LAN, 96.9% at 80 ms, 89.7% at 150 ms and 84.3% at 250 ms. With the cap as built, a client at 150 ms gets the 344 ms it needs and still cannot claim to have seen anything older than its own interpolation shows.

**Measured** (netbench, below; 12 clients of which 6 fighters, 300 s per scenario, 64 agents). Agreement is the share of swings the client saw land that the server landed; false hits the share of swings it saw miss that the server landed.

Before (protocol 17, the same netbench):

| Scenario | Agents | Attacks | Seen hit | Server hit | Agreement % | False hits % | Left out: prey / dodged / out of step | View age ms (mean / p95) |
|---|---|---|---|---|---|---|---|---|
| LAN | 64 | 383 | 268 | 256 | 93.7 | 4.3 | 0 / 8 / 26 | 132 / 154 |
| 80 ms RTT | 64 | 422 | 275 | 250 | 88.7 | 4.1 | 1 / 5 / 4 | 222 / 238 |
| 150 ms RTT, 20 ms jitter | 64 | 493 | 316 | 257 | 76.9 | 7.9 | 0 / 10 / 5 | 344 / 362 |
| 250 ms RTT, 5% loss | 64 | 447 | 288 | 193 | 62.8 | 7.5 | 0 / 5 / 8 | 438 / 454 |

After (protocol 18):

| Scenario | Agents | Attacks | Seen hit | Server hit | Agreement % | False hits % | Left out: prey / dodged / out of step | View age ms (mean / p95) | Views clamped % |
|---|---|---|---|---|---|---|---|---|---|
| LAN | 64 | 450 | 332 | 329 | 99.1 | 0.0 | 0 / 6 / 28 | 130 / 154 | 0.0 |
| 80 ms RTT | 64 | 391 | 259 | 254 | 97.7 | 0.8 | 1 / 9 / 5 | 221 / 238 | 0.0 |
| 150 ms RTT, 20 ms jitter | 64 | 452 | 299 | 292 | 96.7 | 2.0 | 4 / 5 / 12 | 344 / 362 | 0.0 |
| 250 ms RTT, 5% loss | 64 | 467 | 286 | 269 | 92.7 | 2.2 | 3 / 6 / 5 | 438 / 462 | 0.0 |

Reading it:
- At 150 ms, agreement goes from 77% to 97% and false hits from 8% to 2%. At 250 ms with loss it goes from 63% to 93%. An earlier run of the same size gave 74.9 → 97.1% at 150 ms and 58.1 → 95.8% at 250 ms, so the run-to-run spread is about 2 points.
- Even on LAN agreement was 94% before: the view is 130 ms old there too (the interpolation delay). Now it is 99%.
- What still disagrees was not separated in the measurement. The likely sources: the lunge (fixed since, see the lunge results below: the server's attacker lunged toward where the target was then, the predicted one toward the latest snapshot), interpolation and extrapolation error on the drawn target under loss, and the client judging on the first frame after the predicted blow, up to half a tick later.
- No view was clamped in any scenario: the interpolation delay stays well under 200 ms.
- Nothing else in the benchmark moved: bandwidth up and down, latency, prediction error, corrections and remote error are the same within run-to-run noise (a press costs 37 more bits, and fighters press about once a second).

**The lunge at the drawn target** (cret/predict-casts). Two ways to make the client's and the server's lunge agree were measured: (a) the server lunges toward the target's rewound pose and the client toward its drawn one (built), and (b) the server stays at the present and the client lunges toward the target extrapolated to the tick the server will apply the input. Same netbench, 300 s per run, pooled over the runs (before: 4 runs at 150 and 250 ms; casts predicted with the old lunge: 3; (b): 2; (a): 3):

| Variant | Scenario | Runs | Attacks | Agreement % | False hits % | Bites | Bite: error at the ack cm | Bite: largest error in 0.5 s cm | Bite: corrected in 0.5 s cm |
|---|---|---|---|---|---|---|---|---|---|
| Before | 150 ms RTT, 20 ms jitter | 4 | 1807 | 96.2 | 2.7 | 1470 | 1.3 | 5.2 | 19.2 |
| Before | 250 ms RTT, 5% loss | 4 | 1921 | 95.5 | 1.3 | 1340 | 0.8 | 3.1 | 16.9 |
| Casts predicted, lunge at the present | 150 ms RTT, 20 ms jitter | 3 | 1382 | 95.9 | 1.7 | 1498 | 1.1 | 4.4 | 16.5 |
| Casts predicted, lunge at the present | 250 ms RTT, 5% loss | 3 | 1363 | 95.4 | 1.5 | 1355 | 0.9 | 3.5 | 18.5 |
| (b) client extrapolates, server at the present | 150 ms RTT, 20 ms jitter | 2 | 867 | 97.3 | 1.2 | 949 | 1.2 | 4.9 | 16.3 |
| (b) client extrapolates, server at the present | 250 ms RTT, 5% loss | 2 | 912 | 95.4 | 1.7 | 984 | 1.9 | 4.5 | 22.0 |
| (a) both at the drawn pose (built) | 150 ms RTT, 20 ms jitter | 3 | 1232 | 97.9 | 1.6 | 1386 | 1.3 | 3.9 | 11.1 |
| (a) both at the drawn pose (built) | 250 ms RTT, 5% loss | 3 | 1331 | 95.5 | 0.8 | 1321 | 0.8 | 3.8 | 12.1 |

(a) agrees best at 150 ms and as well as any at 250 ms, has the fewest false hits, and the bite's corrections are 30 to 40% smaller: both ends now carry the attacker toward the pose the blow is judged against. (b) has to extrapolate the target 100 to 330 ms ahead, and the server's attacker then lunges toward a pose the rewound hit test does not use. The deterministic loopback (`test_lag_comp`) puts the predicted lunge within 4.9 mm of the server's at every blow with the view, and 200 mm off without it.

The hit-registration table before and after the whole change (casts predicted and lunge (a)), same netbench, pooled over the runs (12 clients, 300 s each; the pooled agreement and false hits count every swing of every run):

| Scenario | Runs (before / after) | Attacks (before / after) | Agreement % | False hits % | Agreement per run % (before; after) | False hits per run % (before; after) |
|---|---|---|---|---|---|---|
| LAN | 9 / 8 | 3654 / 3027 | 98.4 → 99.0 | 1.5 → 1.8 | 97.3–99.6; 98.3–100.0 | 0.0–4.0; 0.7–3.3 |
| 80 ms RTT | 9 / 10 | 3802 / 4131 | 98.1 → 97.3 | 2.2 → 2.5 | 96.7–100.0; 96.3–99.2 | 0.6–4.1; 1.1–3.9 |
| 150 ms RTT, 20 ms jitter | 6 / 7 | 2706 / 2975 | 96.7 → 97.7 | 2.3 → 1.7 | 93.9–98.5; 95.9–98.8 | 1.6–4.6; 0.0–3.0 |
| 250 ms RTT, 5% loss | 6 / 7 | 2804 / 3223 | 95.9 → 96.3 | 1.5 → 1.0 | 94.1–97.3; 94.3–97.9 | 0.0–2.9; 0.4–2.2 |

Reading it:
- The run-to-run spread is wide: the same build's agreement varies by 2 to 5 points between 300 s runs (per-run ranges in the table), so the pooled numbers come from 6 to 10 runs of each build per scenario.
- At 150 and 250 ms, where the lunge mattered, false hits fall (2.3 → 1.7%, 1.5 → 1.0%) and agreement rises (96.7 → 97.7%, 95.9 → 96.3%).
- On LAN and at 80 ms the point estimates move the other way by less than half a point in false hits (1.5 → 1.8%, 2.2 → 2.5%) and, at 80 ms, by 0.8 points in agreement. None of the false-hit changes is significant (two-proportion z between -1.1 and +0.6); the 80 ms agreement drop is z = +1.8 over 9 and 10 runs, down from +2.7 over 5 and 6 runs. A debug pass over the 80 ms misses found the predicted attacker within a few millimetres of the server's and the drawn target within a few millimetres of the server's rewound pose, so nothing points at the lunge there. It is listed under Open.
- View ages and clamps did not change (no view clamped in any run).

In play: `CRET_NETSIM="lat=75 jit=14" ./build/cretaceous/play join HOST` and bite a gallimimus or raptor running past. The bite lands when the jaws close on it on screen; the hit feedback (`render/fx.c`) still comes a round trip later, with the server's snapshot. Nothing is drawn differently.

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

Each datagram's draws (lost or not, its delay, reordered, duplicated) come from a hash of the relay's seed, the direction, the time the datagram entered (in 1/4 ms) and its place among the datagrams entering that direction then (cret/net-replay; before, one running sequence for both directions). What one direction carries never shifts the other's draws, and one datagram more or less (an ENet acknowledgement a tick earlier or later) changes only its own fate. Netbench's replays need this: before it, the downstream jitter decided when the client's ENet acknowledged a reliable message, that shifted every later upstream draw, and an input arrived a tick late at 30 Hz where it was on time at 15 Hz. The conditions themselves are unchanged (the same distributions).

## netbench

```bash
make -C ocean/cretaceous netbench CC=clang          # headless; no raylib
./build/cretaceous/netbench --agents 64,128 --clients 4 --seconds 15 --md table.md
./build/cretaceous/netbench --connect 127.0.0.1:27480 --clients 2 --scenario lan,mixed   # against `play server`
./build/cretaceous/netbench --scenario 80ms --agents 64 --clients 12 --seconds 120 --seed 11 --record 80.rec   # keep the fights (Replay)
./build/cretaceous/netbench --replay 80.rec --snapshot-hz 30          # the same inputs at another setting
./build/cretaceous/netbench --replay 80.rec --check                   # exit 1 unless it reproduces the recording
```

- `--seed N`: the server sim's seed (default 7). `--snapshot-hz 15|20|30`: the server's snapshot rate (Snapshot rate). `--no-view`: presses carry no view, so the server judges every swing at its present (lag compensation off).
- `--record FILE`, `--replay FILE`, `--check`, `--hash`, `--window T W`, `--swings FILE`, `--pair A B`: section Replay.

- One process runs the server sim (bots everywhere else) and N clients, each with its own client sim, behind its own relay.
- The clients play scripted inputs: trot, sprint, walk, back up, strafe, stand, eat, with a wandering turn, jumps, attacks and secondary attacks, and (v9) a head aim that sweeps side to side. Their clocks drift by ±0.02–0.08%, and each starts at a random phase against the server.
- **Virtual time**: it runs on a virtual clock in 0.25 ms steps (`cret_netsim_set_clock`), so the results do not depend on how busy the machine is.
  - Packets still cross real localhost sockets and ENet.
  - ENet's own timers (pings, retransmits, its RTT estimate) run on the virtual clock too (cret/net-replay, Replay), so the RTT it reports is now the conditioned one (about 270 ms at 250 ms). Before, they ran on wall time and reported a few tens of ms.
  - Each client's link draws come from its relay's seed, which follows `--seed` (cret/net-replay). With one fixed set for every seed, ENet's pings, whose times do not depend on the sim, met the same losses in every run: at 250 ms with 5% loss one ping and its retransmissions, or their acknowledgements, were lost six times in a row and ENet timed client 3 out after 93 s in every run, at every seed and snapshot rate. Six losses in a row at about 10% each is a one-in-a-million chain, which a fixed seed then repeats every time. Every seed, the default 7 too, meets other link draws than before this change.
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

Hit registration (v18, `--hits FILE` writes the table; Lag compensation above has the results):
- Half the clients (the even ones) are fighters. They ask for a raptor, a tyrannosaur, a raptor, a pachycephalosaur and so on, and chase the nearest animal in their own drawn world that is not a packmate, counting a running one as nearer by 1.5 s of its speed (fast prey first). They attack when it looks in reach (the secondary attack now and then) and, with nothing within 60 m, move like the other clients. Each other client starts beside a fighter, so clients fight clients too.
- Every 4 s the server puts two bots (gallimimus, then raptors, first) 4–8 m in front of each fighter, running across at 60–100% of their sprint.
- For every swing, the client judges at its own hit moment (the first frame after the predicted blow) whether its predicted animal's blow reaches any animal it draws (`cret_swing_reaches`: remote agents at their render time). The server's hit log says whether the same swing landed (a blow by that animal at the tick the server applied that input).
- **Attacks**, **Seen hit**, **Server hit**; **Agreement**: server hits among the swings seen to land; **False hits**: server hits among the swings seen to miss.
- **Left out** (not counted in the columns before): swings that caught small prey at the snap, swings the server had cut short before the blow (a stun or a throw: the prediction's miss, not the hit test's), and blows at an animal in the middle of a dodge leap (attacks miss it by rule, and the client is not told about the leap).
- **View age**: the server's tick when it applies a press minus the view the client had at the press. **Views clamped**: presses whose view was more than 200 ms behind the newest snapshot the client had.
- The fights add corrections (bites and knockback, about 40 a minute) and a few centimetres of mean prediction error to the main table.

Casts (cret/predict-casts, `--casts FILE` writes the table; Predicted casts above has the results):
- Every client presses one of its three ability slots about every 1.5 s when its own (predicted) animal has that ability ready, fighters too. The fighters ask for their species as before; the odd clients ask for a stegosaur, sauropelta, gallimimus, raptor, tyrannosaur and pachycephalosaur in turn, so all six species and all 16 abilities are cast. The server unlocks every slot-3 choice for the clients, and each client switches its slot-3 choice (`cret_net_client_unlock`) every 20 s. Fighters bite while chasing, the others now and then while moving.
- Per ability, and for fresh bites (a front blow that can start: the lunge): **Casts**, the presses the server carried out (its own state at the first snapshot acknowledging the press shows the slot's cooldown, or the bite's, started at that input); **Refused**, those it did not (a leap pressed in the air, a strike mid-swing, a stun the client did not know of); **Mispredicted**, refused ones the client had shown.
- **Press to start**: from the press to the first frame (60 Hz) whose predicted animal shows it (the slot's cooldown running; for a bite, the swing).
- **Error at the ack**: the prediction error at the first snapshot that acknowledges the press (the predicted animal after the newest input that snapshot applied, against the server's); **Largest error** in the 0.5 s after it; **Corrected**: the sum of the shifts reconciliation smoothed into the drawn animal over those 0.5 s.
- A pounce whose raptor the server had caught hold of an animal within the window counts apart (**Pounce, caught hold**): the catch is the server's, and the raptor moves to the victim's flank a round trip later. Presses whose window held an error of more than 4 m (the client snaps those instead of smoothing them: netbench's prey drops sometimes move a bot a raptor rides) or no prediction to compare (a respawn) are not counted.

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

## Replay

`netbench --record FILE` keeps a run's fights, and `netbench --replay FILE` plays them back, so two builds or two settings meet the same fights (cret/net-replay).

- **A recording** holds what the run needs to start the same way: the protocol version, agent and client counts, the server sim's seed, the warmup and the length, whether props drop, the snapshot rate, and the scenario's name and netsim spec (the latency profile). Then every client's input at every one of its ticks, as the server receives it (the 40-bit packed input, `netp_pack_input`), and the slot-3 switch it sent before that tick: 6 bytes a client tick, about 260 KB for 12 clients over 120 s. Last, the recording run's two hashes. The layout is in `tools/netbench.c` (`rec_write`).
- **A replay** sends the recorded inputs in place of the scripted and fighting clients. Everything else is the same code with the same seeds: the bots, the prey brought to the fighters every 4 s, the boulder drops, the species requests, the clients' clock drift and phase, the relays' seeds.
- **It is exact.** A run on netbench's virtual clock is now a function of its inputs and seeds, sockets included:
  - netbench links with `-Wl,--wrap=enet_time_get` (Makefile, `NETBENCH_LD`), so ENet's own timers (pings, retransmits, its round trip, its bandwidth throttle) run on the virtual clock instead of the wall clock;
  - the link conditioner draws per datagram (Link conditioner, above), so an ENet acknowledgement a little earlier or later does not shift the delays of every input after it.
  - The hashes: one over the server sim after every tick (every agent's position, heading, health, alive flag and action, the hit count), one over every client's predicted animal after each of its ticks, what it had received and what it judged of each swing. `--hash` prints them; `--check` compares them with the recording's and exits 1 if they differ.
  - Measured: recordings of 6 s at 250 ms with 5% loss and at 120 ms with jitter, loss, duplication and reordering, each replayed 4 times, 4 processes at once on a loaded 4-core machine: all 8 matched their recording. `make test` records 4 s at 150 ms with 20 ms jitter (4 clients, 32 agents) and replays it twice; both must match (about 8 s, docs/testing.md).
- **What a changed setting does to the fights.** A setting that never changes a blow leaves the server's world exactly as recorded: with views off, replays at 15 and 30 Hz gave identical server sims over a whole 60 s run. A setting that does change a blow (the view, the lunge, the snapshot rate through the view the press carries) changes the world from that blow on, by a few millimetres of lunge at first, and the bots' choices spread it. The recorded inputs are open loop: a minute on, they steer the fighters at targets that are no longer where they were. One 60 s recording at 80 ms: 61 swings seen to land at its own settings, 21 and 23 replayed at 20 and 30 Hz (before the near-band fix, Snapshot rate).
  - So a whole replay at another setting is a paired measure of what does not hinge on the fights (bandwidth, the input-to-ack wait, interpolation), and a poor one of what does (hits, corrections from blows): its fighters fight less. Snapshot rate (below) measures both ways.
  - **Windowed replays** (`--window T W`) pair the hits: the replay plays the recording's own settings, the same run exactly, until T seconds into the statistics, switches to its own settings (`--no-view`, `--snapshot-hz`, or a build's change) for W seconds, and stops. At a rate switch the server takes the new rate and each client the matching snapshot interval, as a welcome at that rate would give it; the clients' interpolation delay then settles to the new rate within about a second. Up to the switch every swing is the recording's, so a swing in the window met the same fight in both runs and differs only by the setting. Swings whose blow lands at least 0.5 s into the window are logged (the 0.5 s covers a wind-up pressed before the switch).
  - `--swings FILE` writes every fighter swing as a line: the recording's id (its FNV-1a), the client, the input whose predicted blow lands, the action, seen to land, landed on the server, and why it was left out (0 counted, 1 small prey, 2 out of step, 3 dodged, 4 never applied, 5 latched: the server's raptor held on after a pounce, so its bites had no reach test). The recording run itself logs id 0 (its file is written at the end), so the reference side of a pair is a replay of the recording at its own settings, which `--check` also confirms is the recording. `--pair A B` matches two such files swing by swing and prints agreement, false hits and mismatches in each, the discordant pairs with an exact McNemar test, and the per-recording differences with a paired t.

**First use: hit registration, paired.** The open question was whether hit registration on LAN and at 80 ms had got worse with the lunge change (Lag compensation): pooled over 8 to 10 unpaired runs of 300 s, false hits were 0.3 points higher and agreement at 80 ms 0.8 points lower, against a run-to-run spread of 2 to 5 points. Paired replays put every swing of one condition against the same swing of the other. The build before the lunge change has no replays and the lunge at the present has no switch, so it could not be replayed itself; what was replayed is lag compensation on (the recording) against off (`--no-view` windows). 24 recordings, 6 per scenario (12 clients of which 6 fighters, 64 agents, 240 s, seeds 21 to 26), each replayed at its own settings for the reference (all 24 reproduced their recording's hashes) and in 12 windows of 20 s with views off:

| Scenario | Window | Swings paired | Judged differently | Agreement %, recording → window | False hits %, recording → window | Mismatch % (discordant: recording only / window only, exact McNemar p) | Seen to land in both: landed in the recording only / the window only | Per recording, window − recording: agreement, false hits points (paired t; recordings) |
|---|---|---|---|---|---|---|---|---|
| LAN | lag compensation off | 1590 | 373 | 97.72 → 93.35 | 1.03 → 1.93 | 1.82 → 4.03 (20 / 55, p = 6.5e-05) | 33 / 4 of 672 (p = 1.1e-06) | -4.35, +0.97 (-4.03, 1.24; 6 recordings) |
| 80 ms | lag compensation off | 1968 | 601 | 96.71 → 85.16 | 0.40 → 1.61 | 2.18 → 6.50 (28 / 113, p = 2.6e-13) | 76 / 11 of 671 (p = 4.2e-13) | -11.31, +1.25 (-9.87, 2.60; 6 recordings) |
| 150 ms, 20 ms jitter | lag compensation off | 1976 | 673 | 97.51 → 73.61 | 2.07 → 2.75 | 2.33 → 10.73 (33 / 199, p = 4.2e-30) | 138 / 5 of 599 (p = 8.6e-35) | -24.00, +0.65 (-18.57, 1.04; 6 recordings) |
| 250 ms, 5% loss | lag compensation off | 1937 | 652 | 94.61 → 69.93 | 1.82 → 4.38 | 3.87 → 12.49 (50 / 217, p = 5.7e-26) | 133 / 4 of 537 (p = 1.7e-34) | -25.02, +2.52 (-12.88, 3.21; 6 recordings) |

Reading it:
- Agreement is the share of swings the client saw land that the server landed, false hits the share of those it saw miss that the server landed, mismatch the share where the two differ. "Judged differently": swings the client itself saw differently in the two runs (without a view its predicted lunge goes elsewhere).
- Lag compensation is worth 4.4 points of agreement on LAN, 11.5 at 80 ms, 24 at 150 ms and 25 at 250 ms with loss, on the same swings: of the swings the client saw land in both runs, 33 on LAN landed only with views, 4 only without (p = 1e-6). It also halves the mismatches on LAN and cuts them to a third at 80 ms.
- With views, 1.8% of swings on LAN and 2.2% at 80 ms still land differently from what the client saw. Between the two runs of a pair a change of about 1 point in mismatches is significant at these sizes (some 30 discordant swings for 2,000), against 2 to 5 points between unpaired runs. So the lunge's 0.8 points of agreement at 80 ms would show at about these sizes, its 0.3 points of false hits only with about ten times as many swings; the unpaired runs could see neither. A build change replayed this way is measured at that resolution. The lunge change itself stays open (Open).

## Snapshot rate

The server sends 15 snapshots a second by default; `play host` and `play server` take `--snapshot-hz 15|20|30` (cret/net-replay, protocol v22). The default stays 15.

- **The schedule** (`cret_net_snapshot_due`): a snapshot after tick t when `t * hz mod 30 < hz`. 15 Hz goes out on the even ticks as before, 20 Hz alternates gaps of 1 and 2 ticks, 30 Hz every tick. `cret_net_set_snapshot_hz` takes 15, 20 or 30 and nothing else.
- **The welcome** carries the rate in Hz where it carried the divisor (v22); the client refuses 0 or a rate above the tick rate and keeps the interval (`NetClient.snap_iv`: 2, 1.5 or 1 tick).
- **What scales with the rate**, so that each keeps its meaning in seconds:
  - interest management (`netp_interest_at`): animals within 40 m go out in every snapshot at any rate; every farther band keeps its rate a second (an animal 80 m off 7.5 times a second at any rate), so the extra bandwidth goes to what is near;
  - the client's sample gaps and how far back it draws far animals (`gap`, `extra`), from the same function;
  - the interpolation delay: its floor is 1.5 snapshot intervals (100 ms at 15 Hz, 75 ms at 20, 50 ms at 30) and its target one interval, a tick and the jitter, as before;
  - the resends: a progress or nest block that changed goes out for 8 snapshots' worth of time after it (0.53 s) and is refreshed once a second; an unacknowledged entity or prop is resent after 8 snapshots' worth of time (`net_snaps`).
  - Lag compensation's cap stays 200 ms behind the newest acknowledged snapshot, at any rate.
- **A bug the replays found, fixed**: the first version scaled the near band too, so at 30 Hz an animal 10 m off went out every other snapshot (15 a second, as at 15 Hz) and at 20 Hz three of four, while the client, reading the same function, drew it half a tick (20 Hz) or a tick (30 Hz) further back than the view its presses carry. The server then judged swings against poses the screen never showed: closed-loop agreement on LAN was 99.5% at 15 Hz against 97.2% and 97.5% at 20 and 30 Hz (5 runs each, paired t of the whole replays -1.8 and -2.1); with the fix 98.9%, 98.7% and 98.2% (the closed-loop runs below; they met other link draws, Link conditioner). `test_snapshot_rate` now checks the bands. Nothing changed at 15 Hz: the make test replay gives the same hashes before and after.

**Measured** with netbench (12 clients of which 6 fighters, 64 agents, 150 s, 5 seeds). For each scenario and seed one recording was made at each rate, and each was replayed at the other two, so every rate has 15 runs: 5 recordings of its own (the fighters steer by what they see) and 10 replays of the other rates' recordings (the fighters' recorded inputs, open loop). Every rate meets the same 15 recordings, so the per-recording differences are paired. Spreads are the range over the 15 runs.

| Scenario | Rate Hz | Runs | Down KB/s | Up KB/s | Input to ack ms (mean / p95) | Prediction error mm (mean / p95) | Corrections /min (mean size cm) | Swings | Agreement % | False hits % | Range over the runs: down KB/s; ack ms; corrections /min |
|---|---|---|---|---|---|---|---|---|---|---|---|
| LAN | 15 | 15 | 8.8 | 1.90 | 45 / 79 | 2.7 / 2.0 | 23.0 (5.3) | 2837 | 99.1 | 0.7 | 8.3–9.2; 44–46; 8–46 |
| LAN | 20 | 15 | 10.4 | 1.90 | 39 / 78 | 2.4 / 2.1 | 27.0 (5.0) | 2883 | 98.5 | 0.6 | 9.8–10.9; 38–40; 10–67 |
| LAN | 30 | 15 | 14.7 | 1.89 | 28 / 48 | 1.3 / 1.9 | 32.6 (3.8) | 2883 | 98.4 | 0.7 | 14.2–15.2; 28–29; 14–71 |
| 80 ms | 15 | 15 | 8.8 | 2.20 | 127 / 161 | 2.3 / 2.1 | 25.5 (7.6) | 3498 | 97.7 | 0.4 | 8.3–9.4; 126–128; 11–48 |
| 80 ms | 20 | 15 | 10.4 | 2.20 | 122 / 158 | 2.3 / 2.0 | 25.6 (6.8) | 3518 | 97.3 | 0.3 | 9.8–11.0; 120–123; 12–55 |
| 80 ms | 30 | 15 | 14.7 | 2.16 | 111 / 130 | 1.6 / 2.0 | 34.4 (5.7) | 3438 | 97.2 | 0.3 | 13.7–15.6; 109–112; 16–79 |
| 150 ms, 20 ms jitter | 15 | 15 | 8.9 | 2.60 | 227 / 268 | 2.5 / 2.0 | 25.6 (9.1) | 3436 | 97.2 | 0.6 | 8.3–9.6; 227–228; 13–46 |
| 150 ms, 20 ms jitter | 20 | 15 | 10.8 | 2.60 | 222 / 262 | 2.3 / 2.0 | 25.1 (9.4) | 3416 | 96.8 | 0.6 | 10.1–11.4; 221–222; 15–55 |
| 150 ms, 20 ms jitter | 30 | 15 | 15.2 | 2.51 | 211 / 242 | 1.6 / 2.0 | 30.1 (7.8) | 3436 | 95.8 | 0.7 | 14.3–15.8; 211–211; 14–74 |
| 250 ms, 5% loss | 15 | 15 | 8.7 | 3.00 | 333 / 372 | 2.1 / 2.0 | 24.9 (11.8) | 3564 | 95.5 | 0.4 | 8.3–9.0; 332–334; 15–44 |
| 250 ms, 5% loss | 20 | 15 | 10.3 | 3.00 | 327 / 368 | 2.1 / 2.0 | 31.4 (11.0) | 3468 | 95.7 | 0.5 | 9.7–10.9; 326–327; 16–57 |
| 250 ms, 5% loss | 30 | 15 | 14.7 | 2.91 | 315 / 340 | 1.4 / 2.0 | 32.3 (9.3) | 3545 | 95.3 | 0.5 | 13.9–15.7; 314–316; 19–59 |

The recordings alone (closed loop: the fighters steer by what they see at that rate; 5 per cell, different fights at each rate):

| Scenario | 15 Hz: agreement % (swings seen to land; range over the runs), false hits % | 20 Hz | 30 Hz |
|---|---|---|---|
| LAN | 98.9 (658; runs 98.2–100.0), 2.8 | 98.7 (679; runs 97.2–99.5), 1.3 | 98.2 (675; runs 97.6–99.3), 2.4 |
| 80 ms | 97.7 (687; runs 95.7–99.3), 1.5 | 97.2 (635; runs 96.0–97.9), 0.8 | 97.2 (634; runs 95.8–99.2), 0.7 |
| 150 ms, 20 ms jitter | 97.3 (640; runs 95.5–100.0), 2.3 | 97.7 (596; runs 96.0–98.6), 2.1 | 96.0 (668; runs 93.5–97.5), 2.1 |
| 250 ms, 5% loss | 95.3 (635; runs 94.2–97.0), 1.8 | 96.0 (674; runs 94.0–98.6), 1.8 | 95.9 (629; runs 92.1–98.0), 0.9 |

Paired, per recording (15 recordings per scenario; mean difference and paired t; the prediction error's p95 is read off 1 mm bins, so a constant difference there means none):

| Scenario | Pair | Blocks | Down KB/s | Input to ack ms | Prediction error p95 mm | Corrections /min | Agreement points | False hit points |
|---|---|---|---|---|---|---|---|---|
| LAN | 20 vs 15 | 15 | +1.63 (t 26.9) | -6.00 (the same in every pair) | +0.13 (t 1.0) | +3.99 (t 0.6) | -0.92 (t -2.1) | -0.39 (t -0.9) |
| LAN | 30 vs 15 | 15 | +5.94 (t 69.5) | -16.60 (t -126.8) | -0.13 (t -1.0) | +9.61 (t 1.3) | -0.77 (t -1.3) | -0.06 (t -0.1) |
| 80 ms | 20 vs 15 | 15 | +1.65 (t 25.3) | -5.80 (t -54.3) | -0.13 (t -1.0) | +0.06 (t 0.0) | -0.20 (t -0.2) | -0.22 (t -0.8) |
| 80 ms | 30 vs 15 | 15 | +5.92 (t 42.2) | -16.60 (t -126.8) | -0.13 (t -1.0) | +8.85 (t 1.0) | -0.91 (t -0.7) | -0.18 (t -0.7) |
| 150 ms, 20 ms jitter | 20 vs 15 | 15 | +1.86 (t 24.4) | -5.80 (t -54.3) | +0.00 (the same in every pair) | -0.49 (t -0.1) | -1.00 (t -0.7) | -0.12 (t -0.2) |
| 150 ms, 20 ms jitter | 30 vs 15 | 15 | +6.27 (t 61.2) | -16.40 (t -125.3) | +0.00 (the same in every pair) | +4.49 (t 0.7) | -1.63 (t -1.5) | +0.08 (t 0.2) |
| 250 ms, 5% loss | 20 vs 15 | 15 | +1.58 (t 13.8) | -6.40 (t -48.9) | +0.00 (the same in every pair) | +6.52 (t 1.3) | -1.17 (t -0.9) | +0.16 (t 0.3) |
| 250 ms, 5% loss | 30 vs 15 | 15 | +6.03 (t 40.0) | -18.40 (t -140.5) | +0.00 (the same in every pair) | +7.43 (t 1.6) | -1.73 (t -1.6) | -0.07 (t -0.2) |

Hit registration on the same fights: windowed replays (Replay) of 24 recordings at 15 Hz (240 s each, 6 per scenario, 12 windows of 20 s each), the window at 20 or 30 Hz against the recording's own swings. A swing in a window met the same fight at both rates up to the switch.

| Scenario | Window | Swings paired | Judged differently | Agreement %, recording → window | False hits %, recording → window | Mismatch % (discordant: recording only / window only, exact McNemar p) | Seen to land in both: landed in the recording only / the window only | Per recording, window − recording: agreement, false hits points (paired t; recordings) |
|---|---|---|---|---|---|---|---|---|
| LAN | 20 Hz | 1645 | 289 | 97.73 → 97.43 | 1.02 → 0.48 | 1.82 → 1.52 (17 / 12, p = 0.46) | 5 / 2 of 792 (p = 0.45) | -0.32, -0.56 (-0.76, -1.29; 6 recordings) |
| LAN | 30 Hz | 1632 | 339 | 97.71 → 97.55 | 1.03 → 0.82 | 1.84 → 1.59 (18 / 14, p = 0.6) | 6 / 3 of 742 (p = 0.51) | -0.02, -0.19 (-0.03, -0.62; 6 recordings) |
| 80 ms | 20 Hz | 1979 | 435 | 96.63 → 96.91 | 0.39 → 0.72 | 2.22 → 1.77 (25 / 16, p = 0.21) | 8 / 10 of 827 (p = 0.81) | +0.29, +0.31 (0.51, 0.80; 6 recordings) |
| 80 ms | 30 Hz | 2007 | 478 | 96.70 → 96.45 | 0.39 → 0.52 | 2.19 → 1.79 (26 / 18, p = 0.29) | 6 / 7 of 805 (p = 1) | -0.25, +0.14 (-0.56, 0.59; 6 recordings) |
| 150 ms, 20 ms jitter | 20 Hz | 2029 | 424 | 97.50 → 96.37 | 2.03 → 0.89 | 2.32 → 2.12 (29 / 25, p = 0.68) | 10 / 8 of 862 (p = 0.81) | -1.16, -1.17 (-1.95, -3.57; 6 recordings) |
| 150 ms, 20 ms jitter | 30 Hz | 2027 | 460 | 97.42 → 96.84 | 2.04 → 0.88 | 2.37 → 1.87 (36 / 26, p = 0.25) | 13 / 6 of 834 (p = 0.17) | -0.54, -1.17 (-0.77, -1.90; 6 recordings) |
| 250 ms, 5% loss | 20 Hz | 2001 | 426 | 94.68 → 95.65 | 1.80 → 1.45 | 3.85 → 2.65 (47 / 23, p = 0.0056) | 7 / 7 of 784 (p = 1) | +0.70, -0.33 (0.84, -1.04; 6 recordings) |
| 250 ms, 5% loss | 30 Hz | 1996 | 479 | 94.59 → 95.81 | 1.68 → 1.08 | 3.86 → 2.30 (55 / 24, p = 0.00064) | 10 / 10 of 736 (p = 1) | +1.10, -0.59 (1.30, -1.13; 6 recordings) |

Reading it:
- **Bandwidth** down per client: 8.7–8.9 KB/s at 15 Hz, +1.6 to +1.9 KB/s (+19%) at 20 Hz, +5.9 to +6.3 KB/s (+68%) at 30 Hz, in every scenario (paired t 14 to 70). Each snapshot carries the own state and blocks whatever is near, and the near animals go out in every one. Up is unchanged (2–3 KB/s; inputs go at 30 Hz at any rate). With 15 clients a 30 Hz server sends about 1.8 Mbit/s against 1.1 at 15 Hz.
- **Input to ack**: −6 ms at 20 Hz and −16 to −18 ms at 30 Hz, the same in every recording (the wait for the next snapshot, on average half an interval: 33, 25 and 17 ms). The 30 Hz gain is 37% of LAN's 45 ms and 5% of the 333 ms at 250 ms.
- **Corrections** rise with the rate, +4 to +10 a minute at 30 Hz, but not significantly (paired t 0.6 to 1.6; the runs spread from 8 to 79 a minute): a faster rate shows the server's state sooner, and so a mismatch sooner. They are smaller (5.3 → 3.8 cm on LAN, 11.8 → 9.3 cm at 250 ms).
- **Prediction error** stays under 2 mm at the 95th percentile at every rate; the mean (outliers from bites and knockback) falls from 2–3 mm to 1.3–1.6 mm at 30 Hz.
- **Hit registration**, paired on the same fights (the windows): on LAN, at 80 ms and at 150 ms with jitter, 20 and 30 Hz change nothing measurable (mismatch differences of 0.2 to 0.5 points, McNemar p 0.2 to 0.7; agreement per recording within ±1.2 points, paired t under 2). With 5% loss at 250 ms the faster rates help: mismatches fall from 3.9% to 2.7% at 20 Hz (p = 0.006) and 2.3% at 30 Hz (p = 0.0006), agreement +0.7 and +1.1 points. A lost snapshot leaves a gap of 4 ticks at 15 Hz and 2 at 30; at 30 Hz the client draws its targets from fresher samples.
- The whole replays and the recordings alone say the same within their spread (agreement within 1.7 points at every rate, paired t under 2.2).

**Recommendation: keep 15 Hz as the default.** On links without loss a faster rate buys 6 or 17 ms of input-to-ack wait, small next to the 45 to 330 ms it is part of, and changes neither the hits nor the prediction measurably, for 19% or 68% more downstream bandwidth. The one measured gain in play is under loss: at 250 ms with 5% loss, 30 Hz cuts the swings that land differently from what the player saw by 40%. A host whose players come over poor links can pass `--snapshot-hz 20` (most of that gain at 19% more bandwidth: 2.7% mismatches against 2.3% at 30 Hz) or `30`. Not measured here: a real network's own loss and jitter, more than 12 clients, and 128 agents.

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

## Join and loading

A client that has the welcome loads the server's world before it can play: from the cache in a few seconds, or a first bake of about 45 s (85 s at 2048² on 4 threads), and then the view's own stages.
Before v20 the client did not service ENet during that time. ENet drops a peer whose reliable packets (the server's pings) go unacknowledged for 30 s at most, so a first bake timed the player out on the server side; `tests/test_net_join.c --long 45` shows it with ENet's own timeouts: the server had dropped the silent client by the end of the 45 s load.

Now (`netjoin.c`):
- After the welcome the client calls `cret_net_client_load_begin`. A thread of its own then owns the connection until `cret_net_client_load_end`: every 5 ms it services ENet (acknowledging the server's packets and running the link conditioner's relay), and twice a second it sends LOADING with the load's stage and fraction. The stage comes from one process-wide load status (`cret_load_status` in `world.h`), which the world's load and bake (`world.c`, including terra's bake stages) and the view's stages (`loading_screen` in `render/view.c`) write.
- The main thread loads as before. The world loads on its own thread (`sim_init_thread`), and the window now draws the loading screen meanwhile with the current stage and progress (`sim_init_wait` in `cretaceous.c`, through `cret_view_loading_screen`, the view's own loading screen): "Loading the island", "Baking the island: hydraulic erosion" with the bake's fraction, "Shaping beaches, gorges and grassland", "Placing colliders", then the view's stages as before. Solo and host starts show the same screen instead of a window that stays black while the world loads.
- `cret_net_client_load_end` stops the thread, checks the connection and sends LOADING with the done bit; the server starts the snapshots. Called after the view, HUD and audio are up, just before the first frame (`play join`) or before leaving the title (the title's Join). When the connection was lost it prints why and returns false: `play join` exits, the title shows "Lost HOST:PORT while loading" and stays on the island it showed.
- The server (`netp_server_loading`, `netp_server_join_wait`): a peer that sends LOADING is loading until LOADING with the done bit or its first input. Meanwhile its animal is held (as for a missing input; it does not count as a held tick in the stats), it gets no snapshots, and its requests wait. The server never waits for it: the tick and every other client go on at their own pace. It prints when the client starts loading, every 10 s while it loads (stage and fraction), and when it is done.
- The bound: a client still loading after `CRET_NET_LOAD_LIMIT_S` (300 s, `CRET_NET_LOAD_LIMIT=SECONDS` overrides it for tests) is dropped with the disconnect data `NET_REJECT_LOAD | seconds`. The server prints "dropped slot N: still loading the world after 300 s (stage, fraction)"; the client prints "the server stopped waiting after 300 s for this client to load the world".
- A client that sends no LOADING (netbench, the tests, a tool) joins exactly as before: snapshots from the welcome on.
- The load's thread uses the connection alone: no other `cret_net_*` call on it until the end. The snapshot decoder keeps its scratch buffers in statics, so two threads must never decode at once; with one client per process the hand-over at `pthread_join` is enough (`tests/test_net_join.c` runs its other clients in a child process for this reason, and is clean under ThreadSanitizer).

Measured with `tests/test_net_join.c` (part of `make test`): a server ticking at 30 Hz with 8 animals on its own thread, two clients playing in a child process, and a third client joining with a slow load (`CRET_TEST_SLOW_LOAD=SECONDS`, test-only: every world load waits that long first, reporting progress as a bake). The quick run cuts the server's ENet timeouts to 2 s so that a 3 s load stands for a first bake:

| case | result |
|---|---|
| 3 s load, connection not serviced (the old join) | dropped by the server |
| 3 s load, v20 join | joined; 0 snapshots sent while it loaded; 14 snapshots in its first second of play; its slot held 0 of the first 30 ticks |
| still loading after the limit (5 s in the test) | dropped after 5.1 s; the client sees the load reason |
| the other two clients during the load | 15.0 snapshots/s on average and at least 15 in every whole second (15 sent); longest server tick 1.0 to 1.9 ms |
| `--long 45`: 45 s load with ENet's own timeouts | not serviced: dropped; v20: joined, 15 snapshots in the first second; the other clients 15.0 snapshots/s over 88 client-seconds, at least 15; the server printed the stage every 10 s |

Three quick runs took 15.75 to 15.76 s of wall time each (most of it sleeping) and under 1 s of CPU, with the same numbers. A LOADING message is about 35 bytes, so a loading client sends about 70 bytes a second plus ENet's headers.

In play: start a server with an empty world cache (`rm -r build/cretaceous/worlds`, or a new `--seed`) on one machine and `play join HOST` on another with its own empty cache. The loading screen shows "Baking the island: ..." with the bake's progress, then the view's stages; the server prints the client's stage every 10 s; the client gets in when it is done. `play scenario` and `capture` are unchanged.

## Predicted slot-3 switch

Before, buying or equipping a slot-3 ability while joined sent UNLOCK and the client's loadout changed with the next progress block, a round trip later; a slot-3 press in that window was predicted with the old ability and corrected.
Now `cret_net_client_choose` (`netclient.c`) sends UNLOCK and takes the choice in the client's sim at once with the server's own rule (`cret_choice_take`: the marks, the unlock and the loadout), if the animal is alive and the client's marks allow it. The HUD says "... ready on key 3" as in solo play.
Progress blocks the server wrote before it took the choice still show the old loadout, so the client takes the choice again on top of each new block (on the server's marks), until a block shows it or a second of server ticks has passed (the server refused: its block stands). Nothing changed on the wire.
`tests/test_net_respawn.c` checks that the loadout changes on the press, stays through the following blocks and matches the server's, and that a choice the server refuses (a dead animal the client still saw alive) is undone within a second.

## Sim changes

- `CRET_MAX_AGENTS` goes from 64 to 128. Bodies are created for `num_agents` only, so the trainer pays nothing for this.
- `CretSim.hold` skips an agent's movement (and its status input) and movement timers for a tick.
- `cret_sim_move(s, i, in, predict)` runs one agent's tick as `cret_sim_step` does: `cret_status_input` (stun, latch, casting), then movement unless latched, then its timers. Predicting, it applies the stun and latch gating without casting and runs the status timers and ability cooldowns down.
- `cret_sim_place(s, i)` moves an agent's collision body and capsule to its state, for clients.
- `move_agent` treats an action past `CRET_ACT_NEST` as none.
- Wounds (v21): `CretAgent.wound[CRET_WOUND_PLACES]` (severity and the tick of the last blow); `move_agent` reads the leg's (top speed and turn) and the body's (stamina recovery), so `cret_sim_move` predicts a wounded animal as the server moves it (`tests/test_sim.c`, wound effects).
- Knockdowns (v19): `CretAgent.down_ticks`, `getup_ticks`, `kd_immune`, `down_len`, `down_side`; `cret_down` and `cret_down_input` (sim.h) gate the input the same way in `cret_status_input` and in prediction (`cret_sim_move`), and `agent_timers` runs the three timers down in both.
- Lag compensation (v18): `CretRewind` and `CretSim.rewind` (NULL outside the net server), `cret_rewind_init` and `cret_rewind_record`; `resolve_attack` and `resolve_alt` test targets through `rewound`. `cret_swing_reaches(a, b)` is the same reach and arc test, read-only, for judging what a client saw.
- Predicted casts (cret/predict-casts): `cret_status_input(s, i, in, predict)` and `cast` take a `predict` flag (the caster only); `cret_leap_step(s, i, predict)` runs a leap's timer and end, from `cret_status_step` and from `cret_sim_move` when predicting. `lunge_target` reads targets through `rewound` and returns how fast the target draws away; `rewound` gives the pose's velocity. The client sim's `rewind` points at the client's drawn poses only for each predicted move (`netp_client_predict`). Without a net server nothing changes: `build/cretaceous/rlbench --hash --threads 1 --ticks 2000` prints `24f9406a7d909357` before and after, and the determinism suite passes.

## Open

- Prediction covers movement and the player's own casts (Predicted casts). Your own attack's hit, what a cast does to others (fear, marks, a pounce's catch), eating and drinking gains, and fall injuries come from the server (about one round trip later). A pounce that catches hold is corrected to the victim's flank a round trip after the catch.
- A slot-3 switch is predicted (Predicted slot-3 switch). A choice the server refuses shows for up to a second before the loadout goes back.
- Hit registration with lag compensation: on the same swings, 1.8% on LAN and 2.2% at 80 ms land differently from what the client saw (Replay, first use; without views 4.0% and 6.5%). Whether the lunge change moved that by the 0.3 and 0.8 points the unpaired runs showed cannot be replayed: the build before it has no replays, and the server's lunge toward the rewound pose has no switch. A netbench-only switch for the lunge's target would let a windowed replay settle it at about a point's resolution.
- Other agents in the prediction's collision world are at their latest snapshot positions, not at where the server will have them. Pushing into a moving animal corrects.
- Inputs are applied per tick at the server's pace. A held tick shows as a small hitch of that player for everyone else (under 1% of ticks in every scenario above).
- While a client loads the world (Join and loading) its animal stands held where it spawned, and other animals can hurt or kill it; it respawns as any dead animal does. Spawning it only when the load is done would also have to move the character the host restores at the join.
- The snapshot decoder (`netp_client_packet`) keeps its scratch buffers in statics: clients in one process must not decode on two threads at once.
- Props are not predicted: a boulder you shove moves one round trip later, and until then your animal leans on it where the server last had it.
- The snapshot rate is a server setting, 15, 20 or 30 Hz (Snapshot rate); the default stays 15. Not measured: a real network's loss and jitter, more than 12 clients, 128 agents. A client learns the rate only from the welcome: a server cannot change it for clients already in (netbench's windows set the clients' interval themselves).
- Wounds (v21) are predicted from the own state, but the client does not heal or worsen them between snapshots (that is `survive()`, which prediction does not run). Its severities lag the server's by what healed since the last reconciliation: at most 0.006 in netbench runs at 80 and 150 ms (a body or head wound; under 0.25% of stamina recovery or the senses), and a new wound always came with a correction, so every one reached the prediction at once.
- The ENet round trip that `cret_net_client_stats` reports includes the server's 30 Hz service loop. It is a rough number.
