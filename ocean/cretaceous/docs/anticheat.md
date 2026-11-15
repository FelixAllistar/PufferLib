# Cretaceous anti-cheat

This is the groundwork for keeping a multiplayer island fair: what a cheater can do today, what the server already stops, and three things this branch (`cret/anticheat`, protocol v23) adds so the rest can be built on them:

1. an **area of interest** that limits what each client is told, not just how often (`netaoi.c`);
2. **server checks** for things an unmodified client never sends (`netcheck.c`), log-only unless you ask for more;
3. a **telemetry log** per player for training classifiers later (`nettelem.c`, read by `tools/cret_telemetry.py`).

It ends with the plan for the "landmine" classifiers. Nothing here changes the sim: relevance, checks and telemetry live in the net layer and only read the server's sim. `rlbench --hash` and the determinism suite are unchanged (Tests, below).

How to use it:

```bash
./build/cretaceous/play server --checks log --telemetry runs/day1.ctl   # area of interest is on by default (--aoi off: everyone gets everything, as before v23)
python3 ocean/cretaceous/tools/cret_telemetry.py runs/day1.ctl          # a summary per player
python3 ocean/cretaceous/tools/cret_telemetry.py runs/day1.ctl --aim    # the aim-snap example
```

`play host` takes the same flags. On a listen host the host's own animal is local and is neither filtered nor logged: whoever runs the server can always see and change everything, so competitive play needs a dedicated server you run.

## 1. Threat model

The server owns the sim (docs/net.md). A client sends only inputs and a few requests; everything else it learns from snapshots. A modified client can read every byte it receives and send any byte it likes, and anything that runs on the player's machine can be read and patched. So the question for each threat is what the server accepts and what it tells.

### Speed and teleport

**Enforced today.** The client never sends a position, a velocity or a health value. An input is five bytes, and the server turns them into motion with its own movement code (`sim.c`, `move_agent`):

```c
a->yaw += clampf(in->turn, -1, 1) * yaw_rate * dt;
```

The input decoder clamps every axis to -1..1 and refuses an action, ability slot or aim byte out of range (`net.c`):

```c
bool netp_input_valid(uint64_t v) {
    uint32_t fl = (uint32_t)(v >> 8) & 0xff;
    return (v >> NET_INPUT_BITS) == 0 && ((fl >> 2) & 7) <= CRET_ACT_MAX && ((fl >> 5) & 3) <= CRET_AB_SLOTS && (uint8_t)v != 0x80;
}
```

Top speed, turn rate, stamina, sprint, the leg wound's limp, the carried load and knockdowns all come from the server's state. Teleporting is impossible: a snapshot's own state is the server's, and prediction snaps to it.

**Still exploitable: the catch-up.** The server applies one input a tick, but drains a deep queue to keep latency down (`cret_net_server_poll`):

```c
int extra = queued - 1 > NET_BUFFER_MAX ? queued - 1 - NET_BUFFER_MAX : 0;
...
if (extra > 2) extra = 2;
for (int e = 0; e < extra; e++) { ... cret_sim_move(s, slot, in, false); ... }
```

A client whose clock runs fast numbers more inputs than real time allows, keeps its queue deep, and gets up to three moves a tick: a speed hack of up to 3x that needs nothing but a faster tick loop. Netbench's `--cheat speed` (one client's clock 10% fast) shows it: the server applied 1.099 of its inputs a tick.

**The fix (this branch).** A client numbers its inputs by its own ticks from the welcome, so the newest one it claims can lead the server's ticks since the join only by clock drift and what queued while its link was slow. `CRET_AC_INPUT_CLOCK` flags a claim more than 0.5% plus 45 ticks (1.5 s) ahead; with `--checks enforce` the catch-up never applies an input past that allowance, so a sped-up client's extra inputs wait their turn at one a tick (`netp_check_clock`). Netbench shows it (section 4): a client running 10% fast is flagged about 16 s after it joins (when its lead outgrows the allowance) and no clean client ever is; with ENFORCE the server applied 1.027 to 1.032 of its inputs a tick over the next minute (the allowance's 45 ticks spent, then 1.005).

### Input forging

**Enforced today.** Every action goes through the server's own rules: an attack's cooldown and the queue, an ability's readiness (`abilities.c`, `cret_ability_ready`: alive, cooldown, stamina, not stunned, latched or down), the loadout from the server's progress, eating only where there is food, carrying only what lies at the mouth. Respawns: the server picks the place and refuses one in combat (docs/net.md, respawn requests). Slot-3 choices: the server's marks and unlocks (`cret_choice_take`). A forged input can only press buttons faster than a hand would; it cannot do anything a hand could not.

**Still exploitable.** Perfect timing: pressing an ability the tick it is ready, dodging the tick a swing starts. These are legal inputs; only their statistics give a bot away (telemetry, section 3).

**Added.** `CRET_AC_BAD_BYTE`: a movement byte of -128. The client's quantiser stops at -127 (`sbyte`), so only a hand-made packet carries it. It decodes the same as -127, so it gains nothing; it is a fingerprint of a custom client.

### Aim assist

**Enforced today.** The turn and the head's aim are inputs. The server bounds the turn by the species' turn rate (above) and the aim to the neck's arc; a blow is judged by the server's own reach, arc and height test.

**Still exploitable.** Everything within those bounds. The aim comes from the camera relative to the body (`cretaceous.c`, `player_aim`), so a mouse flick moves it from one side to the other in a tick, and an aim-bot's snap looks the same in a single tick. There is no certain check here: a threshold strict enough to catch a bot would flag fast human hands. This is the classifiers' job, and the telemetry carries what they need: the aim and turn of every tick, when each target came into sight, and every swing with its target.

### ESP and wallhacks: what a client learns

This is the largest hole, and the area of interest (section 2) is the fix.

**Before v23 (and with `--aoi off`)**, a modified client learns from its snapshots:

- **every animal on the island**, at least about once a second however far (`netp_interest`: 0.06 a snapshot beyond 800 m), to 1/256 m, with heading, speed, species, growth, health, group, the eight status flags (stunned, afraid, latching, latched, defensive, marked, burst, mending) and who marked it, bleeding, broken bones, the three wound severities, a knockdown, its aim, and the rally point of every group that has one;
- **every carcass** on the island: where, its species, growth, how much meat is left and who carries it;
- **every nest**: position, species, group, owner and egg count;
- the **hits** whose victim is within 160 m: who hit whom, how hard, what kind;
- the 24 nearest **critters** within 80 m; every **prop** a physics event moved, anywhere;
- the time of day and the cloud cover (these are shown to everyone anyway).

A minimap of everything, a list of every wounded animal with its distance, every nest's eggs: all from bytes the client already has. Netbench (section 2) measures it: with players spread over the island, a client held 61.6 of the 62.3 other animals alive, 54.4 of them more than 300 m away.

**With v23** a client is told only what its animal could plausibly perceive now or within a fraction of a second (section 2). What is still sent and could still be abused:

- the margins: the 60 m around the animal regardless of the ground (the camera orbits there, prediction collides there), 0.75 s of both animals' motion, the 15% plus 10 m an animal keeps after it came in, the 1 s it lingers after it went out, the sight line from the highest place the camera can reach rather than where it is, hearing at 1.5 times a bot's, and smell for carnivores. Those are the price of no popping and a smooth screen. A wallhack can still show an animal crouching just behind a ridge within those margins;
- nests, still all of them (the map shows your own and the HUD those within 150 m; the others could go the same way);
- moved props anywhere, which say where something heavy fell or was pushed;
- the rally points of animals outside your group that are relevant to you;
- critters within 80 m regardless of the ground.

### Packet replay and injection

**Enforced today.** Inputs carry the client's tick number and the server applies each number once, in order (`have_input`, `NetPeer.applied`): a replayed or duplicated packet changes nothing. Snapshot acks only move the sender's own delta baselines. Requests carry a sequence number and the server answers each once.

**Still exploitable.** Nothing is authenticated or encrypted. Anyone on the path (a shared Wi-Fi, a compromised router) can read a player's traffic and inject inputs as them; ENet's session ids stop blind spoofing from elsewhere, not someone who sees the packets.

**The fix (not built).** A login that hands the client a per-session key, and a short keyed MAC on every datagram (or DTLS). This matters before any money or ranking rides on the game.

### Timing and lag abuse

**Enforced today.** Lag compensation is bounded (docs/net.md, Lag compensation; `netp_take_view`):

```c
v = fmax(fmin(v, now), known - CRET_REWIND_MAX);
n->rewind.back[np->slot] = (float)fmin(now - v, CRET_REWIND_HIST - 1);
```

never the future, at most 200 ms behind the newest snapshot the client had acknowledged, at most 500 ms in all. A missing input holds the animal still instead of guessing (no "extrapolate the cheater through a wall"), and a held animal gets nothing done.

**Still exploitable.** `known` is the newest snapshot the client *says* it has. A client that holds its acks back makes itself look further behind and can push the rewind toward the full 500 ms (its own snapshots go staler, but it sees well enough to aim). A lag switch (stop sending, then burst) only freezes the cheater for everyone and delays its own blows.

**Added.** `CRET_AC_VIEW_AHEAD` (a view past the server's present: impossible, the client draws behind its estimate of the server's clock) and `CRET_AC_VIEW_BEHIND` (a view more than 2 s behind the newest snapshot it had). The telemetry's ACK records (the ack's age against the round trip) are what a classifier needs to see ack withholding. **The fix**: cap the rewind by the server's own measure of the round trip plus the largest interpolation delay, instead of the client's claimed acks.

`CRET_AC_FLOOD`: more than 2.5 input packets a tick over 5 s (a client sends one a tick; duplicates on the link add a few percent).

### Scripted bots

Nothing certain: a bot sends legal inputs. Signs for classifiers: inhuman regularity (press intervals, exactly ±127 on every axis, no jitter in the turn), reactions faster than about 150 ms to a target appearing, perfect stamina and cooldown use, and play that never pauses. All of it is in the telemetry.

### Save and progress tampering

**Enforced today.** In multiplayer the host keeps every character (`cretaceous.c`, `server_characters`; files under `~/.config/cretaceous/characters`). The client sends a name and nothing else about its progress; growth, marks and unlocks are the server's. Solo play saves on the player's own machine: editable, but only for their own solo game.

**Still exploitable.** The name is the only key. A client can name another player's character and play it with that player's growth and marks, and a client could send a second name mid-session to load a character again (its own saved state after a lost fight, or someone else's).

**Added.** `CRET_AC_RENAME`: a second character name in one connection (the client sends one, as it joins). With `--checks enforce` the server ignores it. **The fix** for the first: bind a character to a secret. On creation the server gives the client a random key, stores its hash with the character and wants the key with the name from then on.

### What the server trusts that a client reports

| Client sends | What the server does with it | Status |
|---|---|---|
| Inputs (axes, buttons, aim) | Its own movement and rules on them, in range | Enforced; timing and aim go to the classifiers |
| Input numbers | Order, once each, the catch-up | Checked: `CRET_AC_INPUT_CLOCK`; ENFORCE caps the catch-up |
| The view at a press | Clamped (never the future, 200 ms behind the newest ack, 500 ms in all) | Checked: `CRET_AC_VIEW_*`; the ack it is measured against is still the client's word |
| Snapshot acks | Its own delta baselines, and lag compensation's `known` | Checked: `CRET_AC_ACK_FUTURE`; withholding: telemetry |
| Respawn requests | Species checked; the server picks the place; refused in combat | Enforced |
| Character name | Loads that character | Checked: `CRET_AC_RENAME`; no ownership yet |
| Slot-3 unlock | The server's marks and unlocks (`cret_choice_take`) | Enforced |
| LOADING | Holds the animal and sends no snapshots, at most 300 s, once per connection (a LOADING after the first input is playing) | Enforced; stage text sanitised |

## 2. Area of interest (protocol v23)

Before v23 interest management (`netp_interest_at`) rated entities only by distance and still sent far ones about once a second. Now an entity goes to a client only while that client's animal could plausibly perceive it, and the client is told to drop what it no longer could.

### The rule (`netaoi.c`, `netp_aoi_judge`)

Per client, at every snapshot, for every animal and carcass. It is a superset of the sim's own senses (`eco.c`: sight, hearing, calls; `scent.c`: smell), widened for a person at a screen: the renderer draws animals out to 900 m through haze, the camera orbits up to 2.5 body lengths away and above, and what is drawn lags the server by the round trip and the interpolation delay. An entity is relevant when any of these holds:

- **Near**: within 60 m (`NET_AOI_NEAR`), whatever is in between.
- **Sight**: within its sight range and with a clear line over the terrain. The range grows with the target's length: 150 m plus 40 m a metre of length, at most 700 m (a hatchling raptor about 190 m, an adult raptor 270, a gallimimus 390, a stegosaur 490, an adult tyrannosaur 630). It is scaled by the viewer: at night a diurnal species sees half as far (`ECO_NIGHT_SIGHT`, as the bots) and a raptor or tyrannosaur 0.8; full overcast or rain takes half away (the renderer's haze is 3.5 times as thick then); a head wound takes its share as for the bots (`cret_wound_sense`). The line runs from the highest place the camera can be (the pivot over the animal, the longest boom, looking down as far as it goes) to a metre over the target's top, sampled every 25 m or so; the terrain may rise 0.5 m over it before it hides an entity coming in, 3 m for one already relevant.
- **Hearing**: a moving animal within 1.5 times the bots' hearing (`cret_eco_hear_m`: by speed squared and mass, at most 150 m, halved in rain), a call within 300 m, and any animal that dealt or took a blow in the last 3 s within 160 m (the same range as the hit events).
- **Smell** (carnivores): another animal within the nose's range for animals, a carcass within its range for carrion (scent.c), stretched downwind and shortened upwind by the same lobe the sim uses. A tyrannosaur smells a big carcass from far off, as in solo play.
- **Kept for gameplay** whatever the senses say: the viewer's group (the compass and minimap dots, nameplates out to 300 m), animals marked for it by its group's alert call (the map's red dots), the animal its companion quest is about (the quest marker), and what it latches onto or what latches onto it. Carried food goes with its carrier. Quest spots, named places, the nests, the own rally point and the own state are not entities and are sent as before.

Every range gets a lead of 0.75 s of both animals' speeds (`NET_AOI_LEAD_S`), for prediction and the round trip.

**Hysteresis.** An entity already relevant stays so out to 1.15 times each range plus 10 m (`NET_AOI_KEEP`, `NET_AOI_KEEP_M`) and with the looser sight line. When it fails even that, it lingers for 1 s (`NET_AOI_LINGER`) before it leaves; coming back in that second cancels the leave. An entity found more than twice past every range leaves at once: it respawned or was moved, and its new place is not shown.

**Leaving.** Each snapshot lists, before the entities, the ids the client may still hold that are no longer relevant (8-bit count, 8-bit ids). The first time an entity is listed the server forgets its delta baseline, so it comes back from zero; it is listed in every snapshot until one carrying it is acknowledged. The client sets the entity to nothing (not alive, an inactive carcass), drops its interpolation samples and ignores anything about it from older snapshots, so a late packet cannot bring it back. The HUD, renderer and scent trails all skip it. The hit events go out only when their attacker and victim are each the client or relevant to it.

**Server flag.** `cret_net_set_aoi(n, on)`; `play host|server --aoi off` turns it off (everyone gets everything, as before v23). On by default: the tests pass with it. Netbench takes `--aoi on|off` too.

### Measured

Netbench, 12 clients (6 fighters), 60 s per run, every link profile (LAN; 80 ms; 150 ms with 20 ms jitter; 250 ms with 5% loss; 80 ms with 1% loss in bursts; 120 ms with jitter, 2% loss, duplicates and reordering), at 64 and 128 animals, the rule on and off. Ranges are over the six profiles. "Held" counts the other animals alive whose position the client had to within 25 m, sampled twice a second; "not perceivable" those of them the rule's looser (keep) test says the client's animal could not perceive. "Server ms" is the server's whole tick (bots included) on this machine.

Players spread over the island (`--spread 1000`: each fighter and its partner at their own place, at least 1 km from the others, the bots shared out among the places), which is what a real server looks like:

| Animals | Rule | Down KB/s per client | Entities / snapshot | Others held (of alive) | Held beyond 300 m | Held, not perceivable | Leaves /s | Flags | Server ms/tick |
|---|---|---|---|---|---|---|---|---|---|
| 64 | off | 5.3–6.3 | 8.1–10.0 | 61.3–62.1 of 62 | 52.4–55.8 | 53.2–57.5 | 0 | 0 | 0.37–0.46 |
| 64 | on | 4.6–5.6 | 3.7–5.7 | 5.3–8.2 | 0.4–1.0 | 0.37–0.99 | 0.22–1.26 | 0 | 0.41–0.48 |
| 128 | off | 6.4–7.2 | 15.2–17.4 | 124.8–125.9 of 127 | 107.4–115.5 | 109.1–115.8 | 0 | 0 | 0.68–0.77 |
| 128 | on | 5.2–5.9 | 7.0–9.4 | 10.7–15.9 | 0.5–0.9 | 0.53–0.90 | 0.25–1.74 | 0 | 0.62–0.79 |

Netbench's default, where the sim spawns everyone in one crowd (it spawns herbivores by their herd and hunters by their prey) and every animal is within 300 m of every client:

| Animals | Rule | Down KB/s per client | Entities / snapshot | Others held (of alive) | Held, not perceivable | Leaves /s | Flags | Server ms/tick |
|---|---|---|---|---|---|---|---|---|
| 64 | off | 9.1–9.6 | 30.9–36.2 | 60.5–61.6 of 62 | 8.9–10.6 | 0 | 0 | 0.53–0.60 |
| 64 | on | 8.7–9.3 | 27.5–31.2 | 48.4–53.1 | 1.1–1.7 | 1.0–5.5 | 0 | 0.57–0.66 |
| 128 | off | 13.3–14.1 | 62.3–68.5 | 122.1–124.7 of 125 | 13.7–20.5 | 0 | 0 | 0.95–1.10 |
| 128 | on | 12.7–13.6 | 56.0–64.7 | 100.9–109.0 | 1.9–2.8 | 1.9–6.8 | 0 | 1.01–1.13 |

Reading it:
- **What a client learns.** Spread out, a client held 5 to 8 of the 62 other animals (64 on the server) instead of all of them, and 11 to 16 of 127 instead of 125: about 88% and 90% less. Before, it held 52 to 56 (64 animals) and 107 to 116 (128) animals more than 300 m away; now under one. In the crowd, where everything is near, it holds 15 to 20% fewer, and the animals it held though it could not perceive them (behind a ridge, too small to see that far) fall from 9 to 21 to 1 to 3. What is left of those are the 1 s linger, group mates and the other animals kept for gameplay.
- **Bandwidth** falls 10 to 13% (64, spread) and 17 to 19% (128, spread), 3 to 5% in the crowd. It falls less than what a client learns because far animals cost little already (about once a second each) and the own state, the progress and nest blocks and the near animals are most of a snapshot.
- **Server cost**: within the run-to-run spread (the rule's work is a distance per entity per client and a sight line for the ones in range, at the snapshot rate).
- **Leaves**: 0.2 to 1.7 a second a client spread out, 1 to 7 in the crowd, each one byte.

### Prediction, lag compensation and the replays

The rule changes only what clients are sent, never the server's world. Paired replays show it: one 120 s recording per link profile (64 animals, the crowd, 12 clients), replayed with the rule on (its own setting: `--check` confirms the replay is the recording) and off. The recorded inputs are the same, so the server's world must be too, and every swing can be compared one to one (`--pair`).

| Profile | Server world, on vs off | Swings | Judged differently | Agreement % | Down KB/s, on / off | Prediction error mm, on / off | Corrections /min, on / off | Remote error cm, on / off |
|---|---|---|---|---|---|---|---|---|
| LAN | identical (`cd467df58105b51d`) | 147 | 0 | 97.9 / 97.9 | 8.7 / 9.1 | 5.5 / 5.4 | 41.4 / 41.3 | 0.9 / 0.8 |
| 80 ms | identical (`8376933f9d420ec2`) | 169 | 0 | 96.2 / 96.2 | 8.5 / 9.0 | 3.7 / 3.8 | 33.5 / 33.5 | 0.9 / 0.9 |
| 150 ms, jitter | identical (`f6b8ee251dffc2c3`) | 148 | 0 | 96.3 / 96.3 | 8.8 / 9.2 | 3.6 / 3.6 | 39.6 / 39.3 | 0.9 / 0.8 |
| 250 ms, 5% loss | identical (`8760e758e4265015`) | 176 | 0 | 96.8 / 96.8 | 8.8 / 9.3 | 4.6 / 4.6 | 46.8 / 46.2 | 2.1 / 2.1 |
| 120 ms, mixed | identical (`fae3e3fc8040bb74`) | 180 | 0 | 98.4 / 98.4 | 8.8 / 9.2 | 3.7 / 3.7 | 47.5 / 47.5 | 1.2 / 1.1 |

- The server sim's hash is the same with the rule on and off in every profile: the world is untouched.
- Every one of 820 fighter swings was judged the same by the client (seen to land or not) and landed the same on the server: lag compensation and the client's view of its fights are unchanged. Input-to-ack latency, the prediction error's 95th percentile (2 mm in every profile) and snaps are the same; the means differ by at most 0.1 mm, corrections by at most 0.6 a minute, the drawn remote animals by 0.1 cm.
- `make test`'s replay step records with the rule on and replays twice; both match.

## 3. Telemetry for classifiers

`play host|server --telemetry FILE` (or `cret_net_telemetry_open`) writes one binary log of every client's player; `cret_net_telemetry_slot(n, slot, true)` adds a slot no client plays (a bot or a policy, from the inputs given to the poll) in the same records, so human and bot data share one format. Netbench writes it with `--telemetry FILE` (its clients) and `--telemetry-bots N` (the first N bots), next to its replay recordings. Off by default. No personal data: a slot, and a player id that counts joins since the server started.

The layout is at the top of `nettelem.c`. In short: a 32-byte header (format, protocol, rates, agent count, world seed and size), then records of an 8-byte head (type, slot, payload length, server tick) and a payload:

| Record | When | What |
|---|---|---|
| JOIN, LEAVE | a player joins or leaves; a bot slot's first input | player id, person or bot, species |
| INPUT | every tick per logged slot | the input as received (5 bytes, `netp_pack_input`), its seq, whether it was applied normally, at once to catch up, or held (none had come); the press's view; the animal's position and heading before it |
| VIS | every snapshot | what the slot could perceive (the rule) and what was in plain sight (within the sight range with a clear line, no margins): two 160-bit masks |
| APPEAR | an entity came into plain sight | its id, distance and bearing from the animal's heading |
| WORLD | every snapshot | every animal: position (1/8 m), heading, alive, species, action, health, speed |
| SWING | a blow resolved | the action, landed or not, the animal nearest the strike point, whether it was relevant and in plain sight, its distance, how far back lag compensation judged it |
| ACK | every input packet | the snapshot acknowledged and its tick, the newest input, how many inputs, the server's queue, ENet's round trip |
| FLAG | a check's finding | the kind, the count so far, a value |

`tools/cret_telemetry.py` reads it (`read(path)` gives the header and a list of dicts) and has three modes: a summary per player, `--check` (every record well formed; `make test` runs it on the file `test_net_aoi` writes), and `--aim`, the example: per player, how the aim moves per tick (median, 95th, 99th percentile, largest), snaps (a change of a whole aim range in one tick) a minute and how many a swing followed, swings at a target out of plain sight, and the reaction time from a target coming into plain sight to the first tick the player turns or aims toward it.

Size: netbench at 80 ms with 64 animals, 12 clients and 6 bots logged wrote 3.27 MB in 63 s: 52 KB/s, of which 7.8 KB/s is the world (WORLD grows with the animals: 8 bytes each 15 times a second) and about 2.4 KB/s each logged player (INPUT 1.2, VIS 0.7, ACK 0.5). That is about 190 MB an hour for such a server, and it gzips to 36% (1.17 MB). The records the classifiers need least often (WORLD every snapshot, an ACK per packet) are where to thin it if that matters.

## 4. Server checks

`play host|server --checks log` (or `cret_net_set_checks(n, CRET_AC_LOG)`) counts each finding per player, prints a line (`cretaceous: check: slot N (player P): ...`, at most one per player and kind every 10 s) and writes a FLAG record. `--checks enforce` also refuses what the finding would gain: the catch-up past real time, and a second character name. Nothing kicks: that is for a person or a classifier with more evidence. Off by default.

| Check | What an unmodified client does | Flagged when |
|---|---|---|
| `CRET_AC_ACK_FUTURE` | acks only snapshots it received | an ack of a snapshot the server never sent |
| `CRET_AC_BAD_BYTE` | quantises axes to -127..127 | a forward, strafe or turn byte of -128 |
| `CRET_AC_INPUT_CLOCK` | numbers inputs by its ticks since the welcome | the newest claimed is more than 0.5% plus 45 ticks ahead of the ticks since the join |
| `CRET_AC_VIEW_AHEAD` | draws behind its estimate of the server's clock | a press's view more than 3 ticks past the server's present |
| `CRET_AC_VIEW_BEHIND` | draws within its interpolation delay (at most 500 ms) of its newest snapshot | a view more than 60 ticks behind the newest snapshot it had |
| `CRET_AC_FLOOD` | sends one input packet a tick | more than 2.5 a tick over 5 s |
| `CRET_AC_RENAME` | names its character once, as it joins | a second name |

What the checks deliberately do not do: they never look at timing that loss or jitter move. A laggy client's inputs come late, in bursts, reordered and duplicated, but their numbers never run ahead of real time, its acks are of snapshots it got, and its views are behind the server, never ahead.

### Measured: clean clients draw no flags

Every netbench run in section 2's tables ran with `--checks log`: 48 runs of 12 clients for 60 s, every link profile including 5% loss, loss bursts, jitter, duplicates and reordering, 64 and 128 animals, the rule on and off. Not one flag (576 client-minutes). `make test`'s replay recording runs with the checks on and fails if they flag anyone.

A sped-up client (`--cheat speed`: client 1's clock 10% fast), 12 clients, 64 animals, 60 s:

| Profile | Checks | Flags | Flagged | The fast client's inputs applied a tick |
|---|---|---|---|---|
| LAN | log | 1553 | that client only | 1.099 |
| LAN | enforce | 1553 | that client only | 1.027 |
| 250 ms, 5% loss | log | 1385 | that client only | 1.099 |
| 250 ms, 5% loss | enforce | 1385 | that client only | 1.032 |

The count is per input packet once its lead passed the allowance (about 16 s after it joined); the log printed one line per 10 s. With LOG the server played the client's extra inputs out (10% fast); with ENFORCE it applied about one a tick and the extra inputs waited in the queue instead: the cheater's own latency grew, nobody else's game changed.

## 5. The landmine classifiers

The plan: collect human play (real players, telemetry on), bot and policy play (the same records via `cret_net_telemetry_slot`), and deliberately cheating bots (aim-bots that snap to the nearest target, wallhack bots that track what is relevant but not in plain sight); train many small classifiers on windows of that; embed them in the server where they wake now and then, look at a player's recent data and, on a confident hit, flag or ban with no telltale timing.

### Where they run

Only on the server. Anything on the client can be read, stepped through and patched, and a check that runs there tells the cheat author exactly what to avoid. On the server:

- **The data**: an in-memory ring per player of the telemetry records (the same ones the file gets: no file needed), say the last 10 minutes. The checks read only this, never the live sim.
- **When**: each landmine draws its next wake-up per player from an exponential distribution (mean say 20 minutes of that player's play), seeded from a server secret, not from the tick or the player id. No fixed period, so nothing correlates with a heartbeat.
- **Where in the loop**: off the tick. A worker thread takes a copy of the window and runs the model; the tick never waits, so the server's timing and snapshot cadence show nothing. Nothing a model concludes changes what the player is sent or how the world treats them until an action is taken.
- **The action**: not at once. A confident score goes to a review queue with the evidence window; bans go out later and in batches (a "wave"), so a cheater cannot learn which session, which feature or which build of their cheat was caught.
- **Variety**: many small models over different features and windows, swapped with server updates. A cheat tuned to slip past one does not know the others.

### Features they need (all in the telemetry)

- **Aim**: per-tick aim and turn changes, snaps and what follows them (a swing within a few ticks, a target near the new bearing), how a snap ends (humans overshoot and correct; bots stop dead), the bearing error to the nearest target over time.
- **Information**: reaction time from APPEAR to the first turn toward the target; tracking of entities that are relevant but not in plain sight (the VIS masks: an honest player cannot see what is behind the ridge, so turning toward it, or swinging at it, before it appears is the wallhack's signature). The area of interest's margins are a honeypot for this: they are sent but not visible.
- **Timing**: press intervals, the regularity of inputs, the input packets against the ack rate and the round trip (ack withholding, lag switching).
- **Outcomes**: swings and hits against target visibility, the rewind each swing was judged at.
- **Context**: species, latency bucket (the ACK records' round trip), touch or mouse (the analog axes), to compare like with like.

### Avoiding false bans

- **Shadow mode first.** Landmines log scores and nothing else until their false-positive rate on known-clean play is measured: telemetry from trusted testers, from your own sessions, and from the netbench clients and bots at every latency profile, including loss and jitter. A legitimate laggy player must score clean.
- **Thresholds from data**: set each model's threshold for a target rate (say one false flag per 100,000 player hours on the clean set), not from a guess, and keep a held-out clean set to check it after every retrain.
- **More than one witness**: act only when several independent landmines (different features) agree, across more than one session.
- **A person reviews** every ban before it lands, with the evidence window replayed (the telemetry has every input, the world and what the player could see). Keep the model version and the window with the decision, and an appeal path.
- **The certain checks are separate**: `CRET_AC_*` findings are facts about packets, not scores. They can feed a classifier as features but should not ban alone either until they have been watched in the field for a while (a bug in a client build could trip one).

### What this branch leaves for that work

- The telemetry is in a file, not a ring yet; the reader and its formats are what the ring would hold.
- Netbench can make a sped-up client (`--cheat speed`). Aim-bot and wallhack bots for training data are the next step: a client mode that aims at the nearest drawn target with no delay, and one that reads every entity it was sent (including the relevant but unseen) and tracks it.

## Tests

`make -C ocean/cretaceous test` (docs/testing.md):

- `tests/test_net_aoi.c` (new): the rule (an animal past its sight range is not sent, a near one and a far group mate are, a ridge hides one the same distance of open ground shows, night halves a diurnal viewer's sight, the rule off sends everything); hysteresis (an animal pacing 5 m about the edge for 300 snapshots never leaves, one that walks out leaves after 1 s and not before, one moved far leaves at once and the client never learns where it went); leaves (a late snapshot from before a leave cannot bring the animal back, it re-enters from zero with no snapshot refused); each check on crafted packets, nothing on honest ones, ENFORCE's refusals; telemetry written and read back (every record well formed, the client's inputs byte for byte as sent, the bot's as passed). Then `tools/cret_telemetry.py --check` reads the same file. Also built with sanitizers (`make asan`); clean under gcc's AddressSanitizer and UBSan.
- `tests/test_net_codec.c`: its round trip learned that a snapshot can tell an entity to leave (it checks the client dropped it instead of comparing it).
- `tools/netfuzz.c`: the hit block is checked against what the client is told. 100,000 datagrams under gcc's ASan and UBSan: no findings, no valid snapshot refused.
- The netbench replay step: recorded with the checks on, no flags, and both replays match.
- The sim is untouched: `build/cretaceous/rlbench --hash --threads 1 --ticks 2000` prints `9f89e7db0fecb005` before and after. `test_sim` gives the same results as on unmodified 5.0 in the cloud sandbox, where two of its checks fail with or without this branch (no record for the dropped boulder; nothing moved in the determinism run).

## Open

- The rule's margins are generous on purpose (section 1, ESP). Once there are real players, the telemetry's VIS and APPEAR records say how often the margins mattered; they can be tightened from that.
- The sight line is the terrain's. Trees, rocks and the low cover (`cret_eco_cover_h`, which hides animals from the bots) do not hide anything from a client: the camera sees over ferns and between trunks, and a rule that hid what the screen would show would be a bug for everyone.
- Nests, moved props and other groups' rally points still go to every client (section 1). Each could follow the rule; nests need a decision about what the map should show.
- Scent trails: a client records trails only of the animals it is told of (`cret_scent_record` on the client), so a raptor sniffing no longer finds the trail of an animal it never perceived. Smell is in the rule, so animals whose scent reaches it are known; a trail left earlier by one now out of range is not. If that matters in play, the server can send the trail points within the nose's range instead of the animals.
- Lag compensation trusts the client's acks for its 200 ms cap (section 1, timing); capping by the server's measured round trip is the fix.
- No authentication or encryption of the link, and characters are keyed by name only (section 1).
- The telemetry is a file; the landmines want a per-player ring in memory (section 5). Aim-bot and wallhack bots for training data are not built yet; `--cheat speed` is the only cheating client netbench has.
- The listen host's own player is neither filtered nor logged.
