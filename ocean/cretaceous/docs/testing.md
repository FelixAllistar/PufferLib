# Testing and evaluation

Everything here runs on the CPU, from `ocean/cretaceous`:

```bash
make test     # fast suite: determinism, ecosystem arithmetic, critters, quests, binding contract, physics, net codec, respawn protocol, eval smoke, camera probe, combat probe, ecosystem probe, tick bench
make asan     # 10k-tick bot soak, critters, binding run, codec fuzz, the camera probe and the combat probe under AddressSanitizer + UBSan
make fuzz     # long codec fuzz under ASan + UBSan (FUZZ_ITERS=2000000 by default)
make bench    # the tick benchmark alone; make bench-baseline rewrites tests/bench_baseline.txt
make eval     # policy baselines on the training world (EVAL_ARGS="--audit --episodes 6" for the reward audit)
```

Setup: Box3D (the fork) built at `../box3d` relative to the repository root, and the raylib headers in `raylib-5.5_linux_amd64/` (only `raylib.h` is read, through `src/pufferenv.h`; `./build.sh` downloads it).

```bash
git clone https://github.com/FelixAllistar/box3d ../box3d
cmake -S ../box3d -B ../box3d/build -DCMAKE_BUILD_TYPE=Release && cmake --build ../box3d/build -j4
```

Pass `BOX3D=...` or `RAYLIB=...` to `make` to use other locations.
The binaries go to `build/cretaceous/test/` and always run from the repository root, because the world cache (`build/cretaceous/worlds/`) and the props (`ocean/cretaceous/assets/props/`) are relative to it.

`make test` costs about 45 s of CPU. On an idle 4-core machine it takes under a minute.
On a box loaded by other jobs it took 65 s at load average 10 (bake 1 vs 2 threads, `OMP_WAIT_POLICY=PASSIVE`). With the old 4-thread bake check at load average 20+, it took 2 to 5 minutes.

The tests use a tiny world, 512² at 4 m (about 2 km across), which bakes in 2 to 3 s and is then cached.
`CRET_TEST_N`, `CRET_TEST_CELL` and `CRET_TEST_SEED` pick another world (terra accepts powers of two from 256).
Props block movement only when `assets/props` is present (`tools/fetch_props.sh`). Without them, the prop test runs only its synthetic wall.

## The suites

### `tests/test_sim.c`: determinism, invariants, physics, combat

- **Bake across thread counts.** A 256² world baked on 1 and on 2 OpenMP threads (`CRET_TEST_BAKE_THREADS`) must hash the same, over height, water, flow, soil, materials and plants.
- **Lockstep determinism.** Two sims with the same seed and inputs run for 5000 ticks with 32 agents. Every third agent acts at random and the rest are bots. Hunger and thirst are short, so animals starve, fight and respawn. The state hash must match on every tick.
  - The hash covers each agent's raw bytes except the Box3D ids, so new `CretAgent` fields are covered automatically. It also covers the carcasses, the sim rng, the weather and the prop stream.
  - On a mismatch, the test names the first agent and field that diverged.
  - The same run is repeated alone afterwards, then as 4 copies on 4 threads at once (sims sharing one `CretWorld`, as trainer threads do). All must match the first run.
  - It prints coverage (deaths, respawns, kills, hits, attacks, calls, drinking, eating, swimming) and fails if there were no deaths, respawns, hits or eating.
    Drinking used to be required here too, but only the random animals drank, by wading into water. With progression's 7-way action head they swim a third as much and never drank in 5000 ticks, so drinking has its own test (below).
- **Invariants** are checked on every tick of every run:
  - state is finite;
  - feet are no lower than `ground - 1` m, on the map;
  - health, food, water, growth and stamina are within their ranges;
  - species and act are valid (up to `CRET_ACT_MAX`);
  - on-ground agents are at the ground (a raptor latched onto another animal rides its flank and is exempt);
  - the aim is finite and within -1..1;
  - an agent that has been swimming for 3 s floats between `water - height - 0.5` and `water + 0.3`;
  - groups are no larger than their species cap;
  - carcasses are valid.
- **Falls.** A raptor, a tyrannosaur and a gallimimus are dropped from 40 m. Each must land within 4 s, rest on the ground and take damage. A 10 m-per-tick fall must stop at the ground.
- **Swimming.** A stegosaur in more than 8 m of water swims and floats within 0.3 m of its target depth, then swims around for 400 ticks.
- **Drinking.** A gallimimus that settles on a dry shore facing water (`cret_water_ahead`) and holds drink for 2 s starts drinking and gains water.
- **Prop colliders.**
  - A synthetic 24 m wall (a box hull in the prop category) stands across a sprinting raptor's, tyrannosaur's and sauropelta's path. Each must reach the wall and never pass its face.
  - A capsule placed inside the wall must register as an overlap, which validates the probe.
  - Then a raptor walks into 15 real boulders, logs and stumps. It must never be more than 0.15 m inside one.
- **Garbage inputs.** Out-of-range forward, strafe and turn values (±4) and random flags must keep the state valid.
- **Aimed attacks.** For every species, `cret_attack_dir` follows the aim across its neck arc (35 to 70 degrees; the stegosaur's behind it). A stegosaur standing off to one side of the attacker is hit when the attack is aimed at it and missed when it is aimed the other way (the miss is checked where the arc moves the strike point out of reach; at least 4 species).
- **Attacking on the move.** Every species trots, then attacks (and uses its secondary attack): the attack starts on the tick it is pressed, and the speed until the blow lands stays at or above the species' swing speed (primary 0.7 to 0.92, secondary 0.7 to 1.0) of the trot. Slashes and kicks never slow.
- **Secondary attacks.** Per species: tail swipes (stegosaur, tyrannosaur, pachycephalosaur, sauropelta) hit a stegosaur behind and miss one in front, and throw or stagger it; the raptor's slash and the gallimimus's kick hit at the snout and miss behind. Each uses its own cooldown and leaves the primary ready.
- **Attack buffer.** With a victim in reach, a press up to `CRET_ATTACK_BUFFER` ticks (0.3 s) before the cooldown ends starts the attack when it can; one pressed 15 ticks early is dropped. A queued bite with nothing in front lapses (tail blows still swing).
- **Lunge.** Each species' primary stands still and attacks a stegosaur half its lunge beyond reach (it lands, except the stegosaur's tail, which never lunges) and one past the lunge (it misses); the attacker moves no further than its lunge. With the lunge turned off the first check fails.
- **Carrying** (`test_carry_*`):
  - pickup by mass: an 80 kg raptor lifts at most 20 kg and a tyrannosaur 2 t; the raptor refuses a 30 kg body (and the HUD state names it as too heavy), takes a 15 kg catch and holds it at its mouth; the tyrannosaur takes a 400 kg body; a stegosaur lifts nothing;
  - slowdown: a raptor trotting with 16 kg runs at 1 - 0.8 x 16/80 = 84% of its free speed (measured 3.86 of 4.60 m/s), and client prediction (`cret_sim_move`) gives exactly the same speed;
  - drops: carrying blocks the bite and eating; a stun drops the load onto the ground (counted as forced), a scratch from a small animal does not, a grown tyrannosaur's bite does, and so does death;
  - eating: a carried catch keeps rotting, a tyrannosaur cannot eat from it in the raptor's jaws, and once put down the raptor eats it;
  - an egg taken from another group's nest (the nest loses it), and the 3 kg rest of a 5 kg fish that a fed raptor could not finish;
  - bots and determinism: a fed tyrannosaur parent with a hatchling at its nest picks up a juvenile gallimimus carcass 45 m away (at 10.3 s) and puts it down at the nest (at 23.4 s, 3.1 m from its centre); two copies of the run stay in lockstep on every tick.
- **Soak.** 3000 ticks of 32 bots. `test_sim --soak N` runs longer; `CRET_SOAK_TRACE=1` prints bot and sim ms per tick and the number of swimming and thirsty animals every 1000 ticks.

### `tests/test_eco.c`: the ecosystem arithmetic (`docs/ecosystem.md`)

Each rule in isolation: energy (Kleiber burn, a raptor's gut lasting `hunger_seconds`, trot 2x and sprint 4x basal, the fat store filling above 0.6 and burning on an empty gut), thirst (mass scaling, midday heat in the open), growth needing fat and the digestion pause; plants (logistic regrowth 0.2 to 0.8 in a day, the seed bank, Holling intake, eight pachycephalosaurs taking a rich cell to 0.3 in about four minutes, the plant table filling and dropping regrown cells); fear (D, the flight distance with hunger and cover, vigilance intervals, hearing, risk and flight from a hand-built memory, the landscape fading over a day); the hunt arithmetic (an unaware young gallimimus a good hunt, a noticed one worse, an adult stegosaur not worth it, a pack taking a pachycephalosaur, the ranking); the drive choice in hand-built scenes (hunt, rest while digesting, flee a raptor coming, keep grazing near an idle one, a stegosaur standing to a tyrannosaur and ignoring a lone raptor, a young one following its parent, a parent defending its young, water first); tearing a piece off a body; and the population director's targets, a held slot and the floor against extinction.

### `tools/ecoprobe.c --quick`: the ecosystem against a budget

`make test` runs the probe's quick set on the test world (24 animals, 2 game days, persons for 5 minutes, 4 stalking trials per case; about 7 s) and fails if any of its 146 results leaves `tests/ecoprobe_budget.txt` (each result of the stored run +-50%, at least +-2). It guards against regressions; the design's targets and the full run are in `docs/ecosystem.md`, section 11. `--write FILE` rewrites the budget after a deliberate change.

### `tests/test_critters.c`: small prey and the early game

The critter layer (`critters.c`) runs only around people, and `t_config` has none (`player_growth = -1`), so these tests make slot 0 a person.

- **Density.** Rings filled around a person at 40 spots on the test island: every critter stands where its kind can live (fish in fresh shallows, the rest on dry walkable ground), within the ring and the despawn range; small mammals and lizards are more than three times as common as the large mammal (Damuth's law); mice stand in thicker plants than the ring's average and birds on more open ground than mice. 20,000 random points never hold both fish and land habitat. A fresh hatchling has more than twice as many critters within 60 m after 3 s as an adult.
  The test island has sea but no rivers or lakes, so no fish spawn there (checked); `tools/critprobe.c` runs on the full island, where they do.
- **Food scales with the eater.** A real bite (`resolve_attack`) on a 2 kg mouse at the strike point gives exactly `min(kg / (0.25 x body mass), room left)`: a hatchling raptor fills up, an adult raptor gets 0.10, a hatchling tyrannosaur 0.023 and an adult 0.001; `ev_food` matches. A herbivore's bite kills it and feeds nothing. The HUD prompt reads 3 (bite it) with a lizard at the jaws and 4 (9 m away) further off. A fish at the jaws is out of reach from dry land and in reach from the water.
- **Fleeing.** An adult raptor trotting at a mouse, a lizard and a bird: each notices it (at 6 to 12 m), the mouse flees and hides, the lizard flees, the bird flies off and is gone. A juvenile walking up is noticed later than one trotting.
- **Biting back.** A 12 kg Repenomamus at a hatchling raptor's jaws bites it at least twice in 5 s, each bite 5 to 25% of its health with bleeding; it takes three bites from the hatchling, two from an adult raptor and one from an adult tyrannosaur, which loses under 0.5% to it.
- **Determinism with critters on.** Two sims with a hatchling raptor person (a bot drives it, hungry, so it hunts critters through `cret_critter_bot`) and 23 bots, in lockstep for 4000 ticks: the state hash (which covers every active critter and the quests' spots) matches every tick; the active count matches `ncrit`; every critter is finite and valid; something was caught. A sim without people has no critters.
- **First quest.** A raptor, tyrannosaur, stegosaur and gallimimus hatchling, 4 seeds each, gets a spot quest within 150 m in its first 10 s (its first step: catch for predators, graze for herbivores); an adult person past its first steps gets one in the adult band (250-500 m) when it leads somewhere.
- **Cost.** `cret_critters_step` with 128 critters and 48 animals must stay under 0.25 ms a tick (the target is 0.1; measured about 0.008).

### `tests/test_quests.c`: quests and first steps

Quest picking (`progress.c`) and the character save (`character.c`), on the test island with hunger and thirst off (so reserves never cut a quest short).

- **Rules.** The distance bands by growth (70-150, 150-300, 250-500 m); defend is refused to a hatchling stegosaur and a gallimimus and given to a grown stegosaur; the prey rule refuses an adult stegosaur to a raptor, allows a young gallimimus, and never a grown sauropelta.
- **Feasibility.** Every species at every stage (slot 0 a person past its first steps, 23 bots at the same growth), 150 s each with every quest withdrawn every 8 s, about 8000 quests: each new quest is checked as it appears. A hunt names a species with a living animal at most 3 times the hunter's mass (a group hunt: the members' combined mass), never a grown sauropelta for a raptor, and no hatchling raptor hunts; defend only for herbivores of 0.6 growth and up, never a gallimimus; nest only for grown animals without one; guard only with a nest with eggs; places and spots (drink, graze, rest, explore, catch, a patrol's first leg) within the stage's band, carcasses, nests and companions within its far end (slot 0 and bots outside packs and herds, whose errands are their leader's); no first steps for bots or a character past them.
- **Repeats.** Fewer than 15% of consecutive quests (slot 0 and bots choosing their own) are the same kind. Lone tyrannosaurs are left out: on the small test island a hunt is often their only feasible quest.
- **First steps.** A raptor hatchling person gets steps 1-5 in order (eat, drink, companion, ability 1, a new place), each finished as a player would or its count filled in; a death after step 2 keeps two steps done and the next life goes on with step 3; past step 5 a hatchling gets ordinary quests (the nest waits for growth); a save and a load into another sim keep `first_steps` and `visited`; grown, the nest step comes and building a nest finishes it; afterwards first steps never come back (300 s, a death included) and no bot ever gets one; a save written before first steps existed, with 9 quests done, loads as done.
- **Determinism.** Two sims with people of both diets on their first steps, quests reset now and then, in lockstep for 6000 ticks: `t_sim_hash` (which covers the quests, their first-steps step and last kind, steps done and places visited) and the group quests match on every tick.

### `tests/test_binding.c`: the PufferLib contract

The binding (`cretaceous.h`) is built with `[env]` from `config/cretaceous.ini` on the test world.

- **Sizes.**
  - `OBS_SIZE == CRET_OBS_SIZE == self + patch + neighbours + contract 4 self + contract 4 neighbours` (contract 5 kept the observations);
  - `NUM_ATNS == CRET_ACTION_HEADS == len(CRET_ACTION_SIZES)`;
  - the config states the same action heads (`{5, 5, 3, 2, 8, 5}`, contract 5) and obs count (`243 obs floats`), the README states `243 floats`, and both name the contract version.
- **Action decoding.** All 6000 action combinations decode to bounded inputs (action up to `CRET_ACT_MAX`, ability slot up to `CRET_AB_SLOTS`, aim within -1..1). Every head has an effect, out-of-range indices clamp, the no-op action stands still, action 7 is the secondary attack and the aim head maps to -1, -0.5, 0, 0.5, 1.
- **Rollout.** 20k steps of random actions (16 agents, 2000-tick episodes, so 10 episode ends). It checks:
  - no NaN or Inf in obs or rewards;
  - terminals are 0 or 1, and 1 exactly on a death or at the episode end;
  - guard floats after each agent's obs are intact (no write past `OBS_SIZE`);
  - the species one-hot sums to 1;
  - each reward equals the sum of its terms in `tests/reward_terms.h` (which the eval uses);
  - every obs feature stays inside its documented range (the `SELF`, `PATCH`, `NEIGH`, `SELF4` and `NEIGH4` tables at the top of the file; flags must be 0 or 1);
  - the log counts episodes, lengths and `perf` correctly, and `puf_log` has its keys.
- **Death and respawn.** A bled-out agent is dead and terminal with the death penalty, and observes `alive = 0`. Others are not terminal. The next step it is a fresh animal of the same species. At `max_ticks`, every agent is terminal and the sim resets.

`make test` prints only out-of-range features. `make test BINDING_ARGS=` or `test_binding` prints the whole obs-stats table (min, max and mean per feature, its documented range, and `const` for features that never changed).
Change those tables together with `cret_sim_observe`.

### Nest drawing smoke test (`make nest-smoke`)

Not part of `make test`, because it needs the player and a GL context (Mesa llvmpipe is fine; it runs under `xvfb-run` when there is no display).
The player (`capture DIR --nest-smoke`, `devmodes.c`) renders the gallery's raptor nest with five eggs, framed as Tab frames it, once with the nests and once without.
It passes when `render/nest.c` drew the nest and its five eggs and at least 1% of the frame changed, and writes both frames to `build/cretaceous/nest_smoke/`.
Measured in the sandbox at 640x360: 48 nests and 90 eggs drawn (every gallery nest in range is counted), 17.8% of the frame changed.

### `tests/test_net_codec.c`: net codec (protocol 17)

`net.c`, `netclient.c` and `netsim.c` are compiled unchanged against an in-memory ENet (`tests/fake_enet/enet/enet.h`). Events are queued by the test, and sent packets are captured as bytes.
If the net code starts using another ENet call, add a stub there (sockets are stubs that fail: the link conditioner is not used here).

- **Round trips.**
  - Welcome: slot and world parameters.
  - 60 snapshots over a 3000-tick bot run, with forced deaths, carcasses, rallies, long flinches, statuses, progress and a nest. The client's input (with its acks) reaches the server before each snapshot, so baselines move and deltas are used.
  - Every entity a snapshot carries (interest management sends far ones less often) must match within its quantisation: 1/256 m positions, 12-bit heading, 1/16 m/s speed, 1/255 for q8 fields, 1/8 m rally points, 1/62 for the aim, exact flags, statuses and marks. The own state (with the v9 secondary cooldown, queued attack and aim) must match exactly (floats) or to the ms (status timers, cooldowns); the progress block and nests exactly.
  - 2000 random inputs over every action head, the secondary attack, the aim (including -1 and 1) and the carry press (v13): within 1/127; flags, action, ability and carry exact, in order. An aim byte of -128 and a bit past the 40 are refused; the top flag bit unpacks as the carry press; non-finite axes pack to 0.
  - Carrying (v13): during the status rounds the client's animal carries an egg and a fish's rest lies on the ground; the carcass entities' item kind, critter kind and carrier and the own carried slot and load round-trip exactly.
  - Hits (v10): the block keeps the client's own hits first, then near ones in the window, at most 24, dropping old, far and self hits, and round-trips exactly; every out-of-range field is refused. In the bot run a hit on the client is logged once although the next snapshot repeats it, and every hit the client logs is in the server's log.
  - Respawn requests: settled after the cooldown with a reply, a second one held until the second is up, an unknown species refused at once. Profile (character name) and slot-3 unlock reach the server.
  - Disconnects, a full server turning a peer away, and another protocol version refused.
- **Critters** (v12): a server with people steps bots for 3 s, then 40 snapshots reach the client while critters move and one is eaten. Every critter the client holds is the server's at that slot, kind and state exact, position within 1/32 m, heading within 3°; the set is the 24 nearest within 80 m; eaten ones vanish. A spot quest (catch, at 1234.5, 777.25, radius 40) arrives with its spot and the client finds its target. These snapshots join the fuzz seeds, and the fuzz checks every client critter and spot quest is valid.
- **Atomic snapshots.** Every truncation of a new snapshot must be refused and leave the client's state unchanged.
- **Fuzz** (`--fuzz N`, 100k in `make test`, 200k in `make asan`, 2M in `make fuzz`):
  - mutations: truncation, bit flips, byte writes, trailing junk, random bodies with a real message type, and swaps;
  - targets: the client's snapshot and spawn-reply decode (each snapshot made new to it), the server's input, request, profile, unlock and junk handling, and the join handshake;
  - afterwards, the client's agents, carcasses, nests, progress and hit log must be valid and finite, server inputs bounded on every action head, names terminated, and an accepted welcome must give a slot below `CRET_MAX_AGENTS` and a positive cell size.

`tools/netfuzz.c` is the codec's own fuzzer, which `make asan` also runs (300k packets): round trips over a lossy, duplicating, reordering channel at 64 and 128 agents (including the v8 blocks, the v9 aims, secondary cooldowns and queued attacks, the v10 hits, and the v12 critter block and spot quests, each of which must reach the client), then mutated snapshots, inputs, welcomes, requests and replies into the decoders. docs/net.md has the details.

### `tests/test_cache.c`: derived-data caches

The startup caches (`render/cache.c`; no GL, no world): an item stored in one part or several loads back byte for byte; another key (a changed input or version), a flipped payload or header byte, a truncated or extended file, a missing file and `CRET_CACHE=0` are all misses, and a store after a miss hits again, so a deleted or corrupt cache rebuilds itself; no temporary files are left. Content keys change with a file's bytes, its appearance and the order of files, and the threaded hash matches. The reader refuses short reads and `cret_parallel_for` runs every index once. Texture payloads (`render/texbuild.c`): BC3 blocks decode back near their texels (flat blocks to 565 rounding with exact alpha, two-colour gradients and alpha ramps within the format's quantisation, noisy blocks within a looser bound), mip chains have the right levels and sizes and average in linear light for sRGB, every block of every level and layer is written, float formats pass through as one level, unknown formats are refused, and a threaded build matches a single-threaded one byte for byte. `make test` and `make asan` run it.

### `tests/test_net_respawn.c`: respawn protocol over loopback ENet

A server and a client in one process over the real ENet on 127.0.0.1 (from 5.0's `tests/test_net.c`): species requests, held requests within the cooldown, the in-combat refusal and its lift, a dead animal respawning right after a hit, validation of bad and short requests and of a claimed position, profile and unlock, statuses, cooldowns and nests reaching the client, and leaving clearing the slot's pick. `make net-test` runs it alone.

### `tests/bench.c`: tick benchmark

It measures CPU ms per tick, the best of 6 repeats of 250 ticks, for:
- `step_16`, `step_64`, `step_256`: random actions, `cret_sim_step` and `cret_sim_observe` for every agent, which is one trainer step;
- `bots_16`, `bots_64`: `cret_bot_input` and the step, which is a server's cost.

`CRET_MAX_AGENTS` is 64, so 256 agents means four 64-agent envs on one thread.

`make test` fails when a case is slower than `tests/bench_baseline.txt` × `tolerance` (2.5).
If the load average is above 1.5× the core count, slow cases are reported but do not fail. `CRET_BENCH_STRICT=1` fails them anyway; `CRET_BENCH_SKIP=1` never fails.
A score normalised against a calibration loop was tried and was noisier than raw CPU time on a loaded box.

Baseline, rewritten at integration (2026-11-08, after progression) on the 4-core WSL box at load average 1.2:

| case | ms/tick | µs per agent |
|---|---|---|
| step_16 | 0.081 | 5.1 |
| step_64 | 0.52 | 8.1 |
| step_256 | 2.37 | 9.3 |
| bots_16 | 0.058 | 3.6 |
| bots_64 | 0.33 | 5.2 |

Progression's bots (quests, nests, abilities) cost about 2.4x more at 64 agents than the old baseline (0.14 ms, before progression), which sat at the tolerance edge, so the baseline was rewritten.
bots_16 is bimodal on this box, 0.022 to 0.026 or 0.057 to 0.059 ms from run to run with the same binary (pure progression too), so the stored value is the slow mode.

The same cases ran 0.04 to 0.09, 0.23 to 0.63 and 1.25 to 2.7 ms in other runs that day. Rewrite the baseline (`make bench-baseline`) on a quiet machine before relying on tight numbers.

### `tools/camprobe.c`: the third-person camera on hard ground

A headless probe of the camera rig (`camera.h`, the same code the player steps; no window or GPU).
Each species at growth 0.35 (the youngest the sim allows; the request said 0.3) and 1.0 stands on sampled spots:
flat ground, 15, 30 and 45 degree slopes facing up, down and across, ridge tops, gully bottoms, cliff feet and tops (facing the wall or drop and away from it), shorelines (facing the water and the land), shallow water, under trees, next to boulders and logs, and among dense props.
A scripted player then pitches through the whole range at four headings, turns the camera through 360 degrees high and low, flicks it (12 rad/s of yaw, 6 of pitch), and walks and runs in circles while turning the camera, with the look still and with the touch/drag swing.
The sim runs at 30 Hz and the camera at 15, 30 and 60 fps on the interpolated animal, as in the player's loop; every run starts from a fresh sim, so the counts do not depend on the thread schedule.

Every frame (after the first 0.3 s) it counts violations of:

| invariant | broken when |
|---|---|
| `ground` | the lens is less than 0.25 m from the terrain, square to the slope (the near plane is 0.3 m) |
| `water` | the lens is less than 0.25 m above the water as drawn (`shaders/water.vs`: the level blended between cells, sunk under the banks where the wet mask fades, plus the sea swell) |
| `los` | terrain or a prop blocks the line from the lens to the camera's focus (the pivot, as smoothed), unless the boom is at its minimum (0.75 m, or the lens within 0.5 m of the body: over the back) |
| `pop` | the lens moved further than the animal, the look input (1.3 x the turn x the boom) and 4 m/s plus 2 boom lengths a second explain |
| `cut` | not a failure: a jump in past a prop or trunk that came closer, or because the terrain (within 5 cm) or a prop hid the pivot from where the lens was or would be after the turn |
| `pitch` | the view pitch is not the pitch the player asked for for 0.5 s, or a limit the player pushes against is tighter than the ground ahead calls for (the flat limits widened by 0.8 x the fall or rise beyond 3 degrees, to 1.2 rad down and 0.95 up); the one known reason for a tighter upper limit is the lens sitting on the ground or water behind with the pivot already 0.38 rad below the centre |
| `stuck` | pitch input held one way for 0.25 s moved neither the view nor the lens while that range allowed more (and the known reason did not hold) |
| `short` | the boom is under 0.9 of its length although its straight line has been clear of props and 0.3 m clear of ground and water for 1 s |
| `osc` | with the look still, the lens height over the pivot or its distance turned back 5 times within 1 s, each swing at least 5 cm (not counting 0.5 s after a cut) |
| `frame` | the animal's pivot (unsmoothed) is off the 16:9, 60 degree picture, unless the boom is at its minimum |
| `inside` | the lens is within 0.15 m of the body (a capsule along the heading at 0.6 of the height) |
| `nan` | anything not finite |

```bash
make -C ocean/cretaceous camprobe                         # from the repository root, like every run below
./build/cretaceous/camprobe                               # the game world: 28 spots, 1656 runs, about 25 s on 4 cores
./build/cretaceous/camprobe --quick --check ocean/cretaceous/tests/camprobe_budget.txt   # what make test runs (2 s)
./build/cretaceous/camprobe --trace                       # the owner's case, frame by frame
./build/cretaceous/camprobe --only slope30 --dump 100     # the first 100 violation frames on one kind: view, boom, clearance, lens
./build/cretaceous/camprobe --paths-out a.bin; ...; ./build/cretaceous/camprobe --paths-ref a.bin   # compare flat-ground lens paths
```

`make test` runs the quick set (the 512² test world, one spot per kind, raptor and tyrannosaur, both growths, 15 and 60 fps: 184 runs) against `tests/camprobe_budget.txt` and fails if any invariant breaks more often than it allows.
The budget is each count of the fixed camera plus a quarter and 10 frames; invariants at 0 stay at 0. `--write FILE` rewrites it after a deliberate change.
Without `assets/props` the boulder, log and dense spots are skipped and the counts stay within the budget. `make asan` runs the same check under ASan and UBSan.

Totals before (the rig as it was, `camera.h` at the refactor commit) and after the fix, game world, 4,192,992 frames:

| | ground | water | los | pop | cut | pitch | stuck | short | osc | frame | inside | nan |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| before | 140 | 30562 | 20709 | 1055 | 9547 | 394121 | 303974 | 57 | 266 | 350018 | 86336 | 0 |
| after | 0 | 0 | 349 | 645 | 9311 | 22 | 11 | 111 | 273 | 758 | 0 | 0 |

Quick set: before 143,213 violation frames (ground 4883, los 5059, pitch 44362, stuck 34513, frame 44176, inside 10213), after 205.
Runs with at least one violation frame: 1597 of 1656 before (every run off flat ground), 466 after, most of those a handful of frames while running.

What is left (game world): `frame` and `pop` mostly while running fast down 45 degree slopes and off cliff tops (the pivot falls faster than the boom can follow) and among trees and rocks at speed; `los` mostly next to logs (139 frames) and in gullies; `short` mostly where a hatchling runs over a cliff top (75 frames) and the boom eases out behind the crest; `osc` mostly on shorelines (116), where the water floor and the ground alternate under a moving lens. `pitch` and `stuck` (22 and 11) are a hatchling at 15 fps on one shoreline.

The owner's case (`--trace`: an adult raptor on an even 30 degree slope, camera straight behind it, pitching up to the limit, then down, then back).
Before, with the camera on the uphill side (the animal facing downhill), the boom already met the slope at the default pitch: the lens sat on the ground with the pivot 6 degrees low. Pitching up, the lens stayed pinned to the slope and only the view turned, so the animal slid down the picture and left it at +0.30 rad (40 degrees below the centre at the +0.45 limit), and nothing moved after that; looking down stopped at -0.55 rad although the hill fell 30 degrees ahead, so the slope below could not be seen. With the camera on the downhill side the ground fell away behind the lens and never met the boom, but looking up the hill stopped at +0.45 and looking down at -0.55.
After, uphill: pitching up the lens rides the slope and the boom shortens (4.5 to 2.0 m) to keep the animal within 27 degrees of the centre, and the pitch stops there (+0.32); pitching down the lens comes back down and out, and the view reaches -0.97 rad, down the hill. Downhill: looking up the hill reaches +0.82, the lens settling onto the ground behind with the animal 12 degrees low. All four cases end with no violations.

Flat ground feels the same: over the 72 flat-spot runs the lens is a median 3 mm and a mean 3 cm from where it was (95% within 7 cm); the larger differences come from props and trees near the spots (the boom now waits behind an occluder instead of easing into it) and from hatchlings looking up, where the pivot sits lower than the clearance. The view pitch is never more than 0.06 rad apart.

### `tools/combatprobe.c`: how combat feels to a person

The owner's report was "combat feels clunky" and "everything's hard to catch". The probe turns that into numbers: a scripted player with human limits fights and chases the game's bots in the pairings that matter, and every attack it throws is followed from the click to the blow.

The player (the probe's header has the details):
- sees the world 0.2 s late (a reaction delay) and carries what it saw on by its velocity then, as a person tracks steady motion;
- turns the camera toward the target at most 10 rad/s, easing in over about 80 ms, with a wandering aim error of about 2.5 degrees; the attack aims where the camera looks and the body turns toward the camera only while a move key is held, as in `player_aim` and `player_input` (`cretaceous.c`);
- decides once a frame at 30 or 60 fps, latches presses to the next 30 Hz tick as the player does, and runs with no input latency and with 80 ms;
- clicks when the target, as seen, is within reach plus 0.3 m of the jaws at the current aim, at most every 0.25 s and not while its own blow is visibly winding up; it does not watch the cooldown;
- plays two styles: **press** keeps W held into the target, **stand** lets go of W once the target is in reach and bites standing. Defending herbivores walk only to turn toward the attacker (a stegosaur puts its tail to it and looks back); a fleeing gallimimus waits until the raptor commits (18 m, or coming at more than a walk), then sprints away.

Pairings (players first, bots after; adults unless a growth is given): raptor chasing a gallimimus, raptor against a pachycephalosaur, a pack of four raptors against a growth 0.6 stegosaur, the tyrannosaur against each species (gallimimus and pachycephalosaur as chases), each herbivore's defence (stegosaur against a tyrannosaur and against three raptors, pachycephalosaur against a raptor, sauropelta against a tyrannosaur, a growth 0.45 gallimimus fleeing a raptor), and same-size duels (raptor, pachycephalosaur, tyrannosaur). Each runs 6 trials (start direction and aim noise) for each of 8 settings (2 styles x 30/60 fps x 0/80 ms) on open, flat ground of the test world with trees and props walk-through (`CRET_NO_PROP_COLLIDERS`), at most 60 s (90 s for the pack).

Per pairing it prints: attacks thrown and landed; swings knocked out before the blow (stunned); why each miss missed: **passed** (the target alongside or behind: the attacker ran past it or it circled), **arc** (another aim would have reached it; degrees off), **moved** (in reach when the swing started, and the blow would have landed where it stood then; how far it moved), **self** (in reach at the start, but the attacker's own movement, usually a throw, took the jaws away; metres short), **short** (out of reach at the start and at the hit; metres short); clicks dropped (they reached the game while it could not attack and nothing swung or stayed queued); press to start and press to hit, from the click the swing answered; the wait from the first click since the last swing; time closing and in reach; time alongside and passes per minute; median time to kill; won, lost or drawn; and for chases whether the prey was caught (the first blow) or escaped (15 m further off than at the start), when, and how far the chaser ran.

```bash
make -C ocean/cretaceous combatprobe        # from the repository root, like every run below
./build/cretaceous/combatprobe              # the full table: 16 pairings x 8 settings x 6 trials, about 3 s
./build/cretaceous/combatprobe --only trex --style stand --fps 30 --latency 80 -v   # one setting, every miss and a trace once a second
./build/cretaceous/combatprobe --quick --check ocean/cretaceous/tests/combatprobe_budget.txt   # what make test runs (3 trials, 1.5 s)
./build/cretaceous/combatprobe --quick --write ocean/cretaceous/tests/combatprobe_budget.txt   # after a deliberate change
```

`make test` (and `make asan`) fail if, for any pairing in `tests/combatprobe_budget.txt`, the share of attacks that land falls more than 5 points below the stored value, or press to hit rises more than 40 ms above it. The runs are deterministic, so the margins only absorb deliberate small changes.

#### Before and after (cret/combat-feel)

Before is origin/5.0 with this probe; after is this branch. All settings together (both styles, 30 and 60 fps, 0 and 80 ms); "press / stand" splits the share landed by style. TTK is the median time to kill over the trials won; -1 means no kill (or no decided chase).

| pairing | landed | dropped clicks | press>hit | first click>start | TTK (median) | won/lost/draw | chase: caught/escaped/open, s, m | landed press / stand |
|---|---|---|---|---|---|---|---|---|
| raptor>galli | 0% → 0% | 0 of 0 → 0 of 0 | 0 ms → 0 ms | 0 ms → 0 ms | -1.0 s → -1.0 s | 0/0/0 → 0/0/0 | 0/8/40, 53.3 s, 258 m → 0/8/40, 53.3 s, 258 m | 0% / 0% → 0% / 0% |
| raptor>pachy | 75% → 80% | 472 of 956 → 303 of 976 | 587 ms → 579 ms | 94 ms → 266 ms | -1.0 s → -1.0 s | 0/48/0 → 0/48/0 |  | 71% / 80% → 77% / 84% |
| raptor4>stego | 76% → 88% | 1466 of 3586 → 407 of 3662 | 593 ms → 584 ms | 95 ms → 461 ms | 11.9 s → 11.1 s | 48/0/0 → 48/0/0 |  | 84% / 67% → 88% / 87% |
| trex>raptor | 100% → 100% | 0 of 48 → 0 of 48 | 908 ms → 908 ms | 75 ms → 75 ms | 3.1 s → 3.1 s | 48/0/0 → 48/0/0 |  | 100% / 100% → 100% / 100% |
| trex>stego | 98% → 95% | 378 of 791 → 114 of 919 | 922 ms → 936 ms | 101 ms → 533 ms | 12.8 s → 14.1 s | 48/0/0 → 48/0/0 |  | 100% / 96% → 100% / 89% |
| trex>pachy | 0% → 0% | 0 of 0 → 0 of 0 | 0 ms → 0 ms | 0 ms → 0 ms | -1.0 s → -1.0 s | 0/0/0 → 0/0/0 | 0/40/8, 24.8 s, 110 m → 0/40/8, 24.8 s, 110 m | 0% / 0% → 0% / 0% |
| trex>sauro | 60% → 87% | 766 of 1743 → 192 of 1358 | 938 ms → 938 ms | 112 ms → 411 ms | 16.7 s → 17.7 s | 42/0/6 → 48/0/0 |  | 100% / 42% → 100% / 77% |
| trex>galli | 0% → 0% | 0 of 0 → 0 of 0 | 0 ms → 0 ms | 0 ms → 0 ms | -1.0 s → -1.0 s | 0/0/0 → 0/0/0 | 0/48/0, 9.5 s, 50 m → 0/48/0, 9.5 s, 50 m | 0% / 0% → 0% / 0% |
| stego<trex | 100% → 100% | 406 of 1064 → 0 of 1064 | 659 ms → 664 ms | 126 ms → 1104 ms | -1.0 s → -1.0 s | 0/48/0 → 0/48/0 |  | 100% / 100% → 100% / 100% |
| stego<raptor3 | 87% → 88% | 470 of 1094 → 42 of 1064 | 644 ms → 660 ms | 123 ms → 954 ms | -1.0 s → -1.0 s | 0/48/0 → 0/48/0 |  | 87% / 87% → 88% / 88% |
| pachy<raptor | 86% → 86% | 260 of 504 → 156 of 508 | 801 ms → 807 ms | 97 ms → 266 ms | -1.0 s → -1.0 s | 0/0/48 → 0/0/48 |  | 86% / 86% → 86% / 86% |
| sauro<trex | 96% → 96% | 388 of 964 → 38 of 1026 | 504 ms → 520 ms | 107 ms → 794 ms | -1.0 s → -1.0 s | 0/48/0 → 0/48/0 |  | 96% / 96% → 96% / 96% |
| galli0.45<raptor | 0% → 0% | 0 of 0 → 0 of 0 | 0 ms → 0 ms | 0 ms → 0 ms | -1.0 s → -1.0 s | 0/0/0 → 0/0/0 | 48/0/0, 15.7 s, 85 m → 48/0/0, 15.4 s, 83 m |  |
| raptor=raptor | 75% → 82% | 300 of 701 → 109 of 706 | 590 ms → 580 ms | 93 ms → 292 ms | 39.1 s → 38.8 s | 42/3/3 → 45/2/1 |  | 79% / 73% → 86% / 79% |
| pachy=pachy | 59% → 73% | 2046 of 3536 → 312 of 3637 | 773 ms → 779 ms | 94 ms → 1230 ms | -1.0 s → 38.5 s | 0/19/29 → 13/13/22 |  | 92% / 30% → 100% / 50% |
| trex=trex | 94% → 95% | 641 of 1429 → 147 of 1489 | 928 ms → 926 ms | 126 ms → 645 ms | 42.2 s → 43.0 s | 11/16/21 → 12/16/20 |  | 98% / 90% → 99% / 91% |

Misses and movement (counts, and in brackets the mean degrees off or metres):

| pairing | thrown | knocked out | passed | arc (deg off) | moved (m) | self (m) | short (m) | closing / in reach (s) | alongside | passes/min |
|---|---|---|---|---|---|---|---|---|---|---|
| raptor>pachy | 240 → 225 | 11 → 5 | 0 → 0 | 0 (0) → 0 (0) | 0 (0.0) → 0 (0.0) | 38 (1.3) → 33 (1.4) | 11 (2.1) → 6 (2.1) | 4.6 s/6.0 s → 4.5 s/6.0 s | 2% → 2% | 0.0 → 0.0 |
| raptor4>stego | 1124 → 1016 | 2 → 0 | 0 → 0 | 16 (26) → 3 (30) | 26 (0.6) → 15 (0.5) | 92 (1.4) → 51 (1.1) | 101 (1.8) → 33 (2.0) | 42.1 s/23.9 s → 24.9 s/24.4 s | 2% → 3% | 0.0 → 0.0 |
| trex>raptor | 48 → 48 | 0 → 0 | 0 → 0 | 0 (0) → 0 (0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 2.2 s/0.9 s → 2.2 s/0.9 s | 9% → 9% | 0.0 → 0.0 |
| trex>stego | 172 → 179 | 0 → 0 | 0 → 0 | 0 (0) → 0 (0) | 0 (0.0) → 1 (1.4) | 0 (0.0) → 0 (0.0) | 3 (0.2) → 8 (1.0) | 15.9 s/6.0 s → 14.1 s/6.7 s | 5% → 5% | 0.0 → 0.0 |
| trex>sauro | 406 → 283 | 12 → 12 | 0 → 0 | 2 (5) → 0 (0) | 35 (2.1) → 25 (2.4) | 0 (0.0) → 0 (0.0) | 109 (0.2) → 0 (0.0) | 15.8 s/11.6 s → 10.5 s/10.7 s | 31% → 16% | 0.0 → 0.0 |
| stego<trex | 184 → 184 | 0 → 0 | 0 → 0 | 0 (0) → 0 (0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 4.4 s/7.2 s → 4.4 s/7.2 s | 0% → 0% | 0.0 → 0.0 |
| stego<raptor3 | 222 → 218 | 18 → 16 | 10 → 10 | 0 (0) → 0 (0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 4.6 s/6.8 s → 4.6 s/6.6 s | 15% → 15% | 10.6 → 10.5 |
| pachy<raptor | 168 → 168 | 0 → 0 | 24 → 24 | 0 (0) → 0 (0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 57.3 s/2.7 s → 57.3 s/2.7 s | 3% → 3% | 0.0 → 0.0 |
| sauro<trex | 224 → 228 | 8 → 6 | 0 → 0 | 0 (0) → 0 (0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 5.5 s/5.9 s → 5.3 s/6.1 s | 1% → 2% | 0.0 → 0.0 |
| raptor=raptor | 216 → 200 | 0 → 1 | 6 → 1 | 0 (0) → 0 (0) | 24 (3.8) → 15 (3.8) | 0 (0.0) → 0 (0.0) | 21 (2.3) → 15 (1.9) | 38.5 s/4.5 s → 39.6 s/4.5 s | 2% → 2% | 0.0 → 0.0 |
| pachy=pachy | 575 → 560 | 8 → 10 | 0 → 0 | 0 (0) → 0 (0) | 3 (1.4) → 0 (0.0) | 172 (1.0) → 126 (0.4) | 53 (1.0) → 15 (0.3) | 28.0 s/21.7 s → 24.5 s/23.0 s | 5% → 6% | 0.0 → 0.0 |
| trex=trex | 230 → 227 | 0 → 0 | 0 → 0 | 0 (0) → 0 (0) | 3 (3.9) → 5 (4.1) | 8 (2.4) → 6 (2.2) | 2 (1.4) → 0 (0.0) | 40.1 s/10.9 s → 40.5 s/11.1 s | 4% → 4% | 0.0 → 0.0 |

By setting (all pairings), before → after:

| style | fps | latency | landed | dropped clicks | press>hit |
|---|---|---|---|---|---|
| press | 30 | 0 ms | 89% → 95% | 55% → 10% | 703 → 673 ms |
| press | 30 | 80 ms | 89% → 91% | 41% → 13% | 729 → 754 ms |
| press | 60 | 0 ms | 89% → 93% | 54% → 9% | 699 → 675 ms |
| press | 60 | 80 ms | 89% → 92% | 32% → 9% | 699 → 700 ms |
| stand | 30 | 0 ms | 64% → 79% | 56% → 13% | 675 → 680 ms |
| stand | 30 | 80 ms | 66% → 82% | 42% → 13% | 744 → 733 ms |
| stand | 60 | 0 ms | 69% → 81% | 54% → 12% | 683 → 685 ms |
| stand | 60 | 80 ms | 68% → 81% | 35% → 11% | 703 → 720 ms |

Bots against bots (`balance --seeds 48 --minutes 60`, 16 agents), kills by row against column, before → after; the counts swing by 20 to 30% between runs of the same code, so only larger moves mean anything:

| killer \ victim | raptor | stego | trex | pachy | sauro | galli |
|---|---|---|---|---|---|---|
| raptor | 2 → 5 | 2 → 0 | 2 → 1 | 97 → 101 | 0 → 1 | 11 → 9 |
| stego | 101 → 95 | 0 → 0 | 27 → 21 | 1 → 2 | 0 → 0 | 0 → 0 |
| trex | 5 → 3 | 97 → 98 | 0 → 0 | 11 → 20 | 26 → 41 | 1 → 1 |
| pachy | 76 → 94 | 0 → 0 | 9 → 3 | 0 → 0 | 0 → 0 | 0 → 0 |
| sauro | 34 → 30 | 0 → 0 | 26 → 44 | 0 → 0 | 0 → 0 | 0 → 0 |
| galli | 0 → 0 | 0 → 0 | 1 → 0 | 0 → 0 | 0 → 0 | 0 → 0 |
| env | 68 → 62 | 4 → 5 | 46 → 36 | 18 → 5 | 30 → 20 | 7 → 16 |
| deaths | 286 → 289 | 103 → 103 | 111 → 105 | 127 → 128 | 56 → 62 | 19 → 26 |

Deaths per species are the same within the noise. The cells that move most are the pachycephalosaur's kills of raptors (76 → 94; 91 to 110 over the variants tried) and the tyrannosaur and sauropelta trading kills (26 ↔ 26 → 41 ↔ 44); both come from blows that now follow an animal stepping out of reach during the wind-up, and both stayed within the range seen across runs while tuning.

#### What the numbers said, and what was done

Confirmed causes:
1. **Clicks thrown away in the cooldown.** A blow can come every 1.2 to 2.6 s, but a player clicks about four times a second in a fight. With a 0.2 s queue, 32 to 56% of clicks did nothing and nothing showed it. The queue is now 0.3 s, so repeated clicks chain into the next blow (9 to 13% dropped, most of them bites that lapsed because the target had gone); the reticle shows the cooldown as a closing ring, lights up while a click is queued and flashes red on a refused one. Press to hit from the click the game answered is unchanged (about 0.7 s, most of it the wind-up); the wait from the first click of a burst is the cooldown, now shown (first click>start).
2. **Standing still to bite.** The blow lands where the jaws are at the hit, and nothing carried the attacker in: a player who stopped to bite landed 64 to 69%, against 89% for one who kept W held. The worst was a standing tyrannosaur on a walking sauropelta: 42% (it walked 2 m out of reach during the 0.85 s wind-up). The lunge carries the attacker in: 79 to 82% standing, 77% for that case.
3. **Queued blows fired at nothing.** A longer queue alone fired more blows after the target had left (a raptor on a pachycephalosaur went 75 → 72%). A queued bite now waits while its target draws away faster than the lunge could follow, and lapses if nothing is in front when it comes due; fresh clicks always swing.
4. **Thrown out of your own swing.** In a pachycephalosaur duel the bot's headbutt usually lands first and throws the player a metre or more; the player's swing then whiffed in the air (172 "self" misses). A queued bite now waits until the animal is on its feet again: pachycephalosaur duels 59 → 73% landed, and the player wins 13 of 48 instead of none. Today's value was itself the problem there: the bot reacts faster than a person, and the first blow decided every exchange.
5. **Misses that looked like lost clicks.** A bite that landed on nothing sounded like one that landed, minus the crunch. A whiff now shows an open ring on the reticle and plays a quiet whoosh.

Tried and dropped:
- **Knocking a thrown animal out of its swing** (with the cooldown back): tail swipes throw tyrannosaurs, and it cancelled blows that used to land (tyrannosaur on a stegosaur 98 → 71%).
- **A full quarter body length of lunge for every species**: bot defenders then followed a raptor that slipped out of reach during their wind-up, and raptors died about 12% more often (sauropeltas killed 35% more of them). The lunge is now capped by what each animal's acceleration covers in its wind-up, so the heavy, slow-starting animals lunge little.
- **Sprints that run until spent and need a quarter of the stamina to start**, against the stamina flicker in long chases (a spent sprinter alternated sprint and trot every tick): it did not settle the raptor's chase of a gallimimus, and raptors were winded at the wrong moments and died 13 to 20% more often in the balance runs. Slower regeneration while running made the gallimimus bot hover 12 to 16 m ahead instead of escaping.

Ruled out by the numbers:
- **Turn rate and aim**: 0 to 16 arc misses per pairing (the camera aims the head across the whole neck arc, and the mouse turns faster than any body); no soft tracking was added.
- **Reach measured from the centre**: reach was already measured from the jaws to the victim's body segment.
- **Tunnelling on a fast pass**: no attacker ran through its target; "passed" misses are at most 24 a pairing and nearly all in defence (below).
- **The 30 Hz tick and latching**: 30 vs 60 fps changes press to hit by at most 41 ms; 80 ms of input latency adds 0 to 80 ms of press to hit and does not change the share landed much.
- **Time to kill and wins** stay within 15% of today in every pairing except the pachycephalosaur duel (above): raptor pack on a stegosaur 11.9 → 11.1 s, tyrannosaur on a stegosaur 12.8 → 14.1 s, on a sauropelta 16.7 → 17.7 s (it now wins all 48), raptor duel 39.1 → 38.8 s, tyrannosaur duel 42.2 → 43.0 s.

What still feels wrong by the numbers:
- **The raptor never settles a chase with an adult gallimimus**: 40 of 48 are still open after 60 s, the gap swinging between 15 and 30 m. Once both are spent they average nearly the same speed (sprint 8.6 vs 9.0 m/s, trot 4.6 vs 5.0, the gallimimus burning stamina twice as fast), and the fleeing bot eases to a trot whenever the raptor is 25 m back. The bot's flight is the ecosystem branch's code; a stamina or speed retune should be measured with it. The other chases are decided in 10 to 25 s: a tyrannosaur loses a gallimimus in 9.5 s and a pachycephalosaur in 25 s, and a raptor catches a young gallimimus in 15.4 s.
- **A tyrannosaur on a stegosaur that walks away** lands 95% (96% standing before, 89% now): fresh clicks 0.3 m out of reach while the stegosaur walks off at 2.8 m/s open 2.4 m in the 0.85 s wind-up, more than its 0.9 m lunge. Time to kill rose 10%.
- **Defenders get circled**: a pachycephalosaur defending against a raptor still has 24 "passed" misses (the raptor is behind it when the butt lands), because the body turns toward the camera only while a move key is held.
- **Press to hit is still 0.5 to 0.95 s**, almost all of it the wind-up (0.3 to 0.85 s); a shorter wind-up would mean re-timing the attack clips in `render/dino.c`.
- **Fleeing duellists**: in raptor duels the losing bot runs, and misses of that kind are "moved" by 3.7 m, more than any lunge.

## Evaluation harness (`tools/cret_eval.c`)

```bash
make eval                                    # --baselines: bot, random and noop for every slot
make eval EVAL_ARGS="--audit --episodes 6"   # plus the reward audit
build/cretaceous/test/cret_eval --policy all=bot --policy carnivores=net:checkpoints/cretaceous/<run>/<step>.bin --episodes 8
```

It runs the trainer's env: `[env]` from `config/cretaceous.ini`, built with `puf_init` (16 agents, 18000-tick episodes, the 2048² world), with the same reset and respawn rules.
Each slot gets a policy. `--policy WHO=POLICY` can be repeated, and a later one overrides an earlier one.
- `WHO` is one of `all`, `herbivores`, `carnivores`, a species (`raptor`, `stego`, `trex`, `pachy`, `sauropelta`, `galli`) or `slotN`.
- Other flags:
  - `--episodes`, `--ticks`, `--agents` and `--seed`;
  - `--world-n` and `--world-cell` (for example `--world-n 512` for the test world);
  - `--quiet`.

| Policy | What it does |
|---|---|
| `bot` | Scripted bots (`cret_bot_input`) with continuous inputs |
| `botq` | The bots' inputs snapped to the policy's action heads, to show what the discrete action space costs the bots |
| `random` | Uniform over the action heads (an untrained policy) |
| `noop` | Stands still |
| `brawl` | Probe: walks at the nearest animal outside its group and attacks |
| `swim` | Probe: walks into deep water and floats |
| `net:FILE[:HIDDEN:LAYERS]` | Trained weights through PufferNet (`src/puffercpu.c`, the same forward pass as `./puffer eval`). See below. |

Output is Markdown:
- **Per species:** policy, slots, mean life in seconds, the share of slot-episodes with no death (the trainer's `perf`), and deaths, kills, food and water (stomachs), growth at the end of each life, damage dealt and taken (fractions of max health) and return, all per slot-episode.
- **Kill matrix:** the victim against the credited killer (`last_attacker`), with death causes:
  - attack: a hit within 3 s;
  - starved, thirst, bleeding;
  - fall or other.
- **Reward terms:** with `--audit` or a custom run.

The run is deterministic for a given seed. `make test` runs `cret_eval --smoke`: one short episode with every policy type, including a `net:` slot on all-zero weights of the current contract's size.

### Plugging in a trained policy

`net:FILE` loads a checkpoint written by the trainer (`checkpoints/cretaceous/<run>/<step>.bin`). The size comes from `[policy] hidden_size` and `num_layers`, or from `net:FILE:HIDDEN:LAYERS`.
- The file must hold exactly the weights for `CRET_OBS_SIZE` inputs and the action heads, or the eval stops and names both counts. Checkpoints from an older contract are refused: the ones in `checkpoints/cretaceous/` from 2026-11-07 hold 160 or 176 inputs plus actions, not contract 3's 197 + 19.
- Actions are sampled, as in the trainer.
- Every agent goes through one batched net, with recurrent state per slot, reset at each death and episode start. Only the slots assigned to the net use its actions.

To add another kind of policy (another architecture, a scripted variant, a remote policy):
1. Write `static void act_x(Policy* p, CretSim* s, const float* obs, const float* terminals, const bool* mask, CretInput* in)`. It fills `in[i]` for every `i` with `mask[i]`. `obs` holds `CRET_OBS_SIZE` floats per agent when `p->needs_obs` is set, and `terminals[i]` is 1 after a death.
2. Add a name for it in `make_policy`. Put any state in `Policy`, and free it in `p->close`.

To compare a policy against the bots, mix them, for example `--policy all=bot --policy raptor=net:FILE`. Raptor rows then show the policy's survival, kills and return against scripted prey and rivals.

### Baselines

Training world (2048² × 4 m, seed 1), 16 agents, 6 episodes of 18000 ticks (600 s), seed 1, after the fixes below. Figures are per slot-episode.

**All slots: bot**

| species | slots | life s | survived | deaths | kills | food | water | growth end | dealt | taken | return |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Velociraptor | 5 | 514 | 83% | 0.17 | 0.20 | 0.29 | 0.77 | 0.95 | 0.25 | 0.53 | 19.10 |
| Stegosaurus | 3 | 540 | 89% | 0.11 | 0.00 | 0.79 | 0.71 | 0.97 | 0.04 | 0.11 | 19.71 |
| Tyrannosaurus | 1 | 600 | 100% | 0.00 | 1.17 | 0.36 | 0.62 | 0.96 | 1.10 | 0.47 | 20.71 |
| Pachycephalosaurus | 3 | 470 | 78% | 0.28 | 0.17 | 0.77 | 0.69 | 0.94 | 0.59 | 0.57 | 19.51 |
| Sauropelta | 2 | 424 | 67% | 0.42 | 0.00 | 0.82 | 0.66 | 0.95 | 0.14 | 0.42 | 19.07 |
| Gallimimus | 2 | 514 | 92% | 0.17 | 0.00 | 0.77 | 0.81 | 0.92 | 0.00 | 0.18 | 19.71 |
| **all** | 16 | 501 | 83% | 0.20 | 0.17 | 0.60 | 0.73 | 0.95 | 0.28 | 0.40 | 19.46 |

Kill matrix for the bots (rows: victim, columns: credited killer):

| victim | raptor | stego | trex | pachy | sauropelta | galli | none | attack | starved | thirst | bleeding |
|---|---|---|---|---|---|---|---|---|---|---|---|
| raptor | 0 | 0 | 0 | 3 | 0 | 0 | 2 | 1 | 0 | 2 | 2 |
| stego | 0 | 0 | 2 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 1 |
| pachy | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 4 |
| sauropelta | 0 | 0 | 5 | 0 | 0 | 0 | 0 | 5 | 0 | 0 | 0 |
| galli | 1 | 0 | 0 | 0 | 0 | 0 | 1 | 1 | 0 | 1 | 0 |

**All slots: random** (an untrained policy)

| species | slots | life s | survived | deaths | kills | food | water | growth end | dealt | taken | return |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Velociraptor | 5 | 300 | 0% | 1.00 | 0.00 | 0.00 | 0.00 | 0.87 | 0.01 | 1.01 | 16.37 |
| Stegosaurus | 3 | 386 | 44% | 0.56 | 0.11 | 0.22 | 0.03 | 0.90 | 0.06 | 0.87 | 17.24 |
| Tyrannosaurus | 1 | 327 | 17% | 0.83 | 0.00 | 0.00 | 0.00 | 0.81 | 0.00 | 0.99 | 16.60 |
| Pachycephalosaurus | 3 | 338 | 22% | 0.78 | 0.06 | 0.18 | 0.01 | 0.88 | 0.00 | 0.96 | 16.88 |
| Sauropelta | 2 | 379 | 42% | 0.58 | 0.50 | 0.27 | 0.04 | 0.88 | 0.05 | 0.70 | 17.85 |
| Gallimimus | 2 | 379 | 42% | 0.58 | 0.42 | 0.21 | 0.02 | 0.91 | 0.01 | 0.84 | 17.57 |
| **all** | 16 | 341 | 24% | 0.76 | 0.15 | 0.13 | 0.02 | 0.88 | 0.02 | 0.91 | 16.98 |

Random animals almost all die of thirst. 72 of 75 deaths were from thirst, because they almost never drink (0.02 stomachs an episode).

**All slots: noop** (stand still)

| species | slots | life s | survived | deaths | kills | food | water | growth end | dealt | taken | return |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Velociraptor | 5 | 400 | 50% | 0.50 | 0.00 | 0.00 | 0.00 | 0.87 | 0.00 | 0.70 | 17.17 |
| Stegosaurus | 3 | 415 | 56% | 0.44 | 0.00 | 0.00 | 0.00 | 0.89 | 0.00 | 0.77 | 17.12 |
| Tyrannosaurus | 1 | 360 | 33% | 0.67 | 0.00 | 0.00 | 0.00 | 0.78 | 0.00 | 0.82 | 16.94 |
| Pachycephalosaurus | 3 | 372 | 39% | 0.61 | 0.00 | 0.00 | 0.00 | 0.89 | 0.00 | 0.82 | 16.96 |
| Sauropelta | 2 | 343 | 25% | 0.75 | 0.00 | 0.00 | 0.00 | 0.87 | 0.00 | 0.89 | 16.72 |
| Gallimimus | 2 | 360 | 33% | 0.67 | 0.00 | 0.00 | 0.00 | 0.87 | 0.00 | 0.84 | 16.84 |
| **all** | 16 | 381 | 43% | 0.57 | 0.00 | 0.00 | 0.00 | 0.87 | 0.00 | 0.78 | 17.01 |

Every noop death is thirst.
With `thirst_seconds = 600`, an animal that spawns with a full stomach outlives a 600 s episode without drinking. One that spawns at 0.75 dies at about 520 s.

Summary for RL: return per slot-episode is bot 19.46, noop 17.01 and random 16.98; survival is 83%, 43% and 24%.
A trained policy should beat noop on `perf` (43%) and approach the bots (83%).
Return is a poor yardstick, because 18.0 of it is the alive term, which every policy gets (see below).

## Reward audit

`make eval EVAL_ARGS="--audit --episodes 6"` runs the three baselines and three probes, and prints every reward term per species.
The probes are:
- bots with herbivores set to `brawl`;
- bots with carnivores set to `brawl`;
- everyone set to `swim`.

Weights are from `config/cretaceous.ini`: alive 0.001 per tick, food 1, water 1, damage dealt 0.5, damage taken 1, kill 1, death 1, growth 2.

Reward terms per slot-episode:

| species | policy | alive | food | water | dealt | taken | kill | death | growth | total |
|---|---|---|---|---|---|---|---|---|---|---|
| Velociraptor | bot | +18.000 | +0.294 | +0.773 | +0.125 | -0.529 | +0.200 | -0.167 | +0.407 | +19.104 |
| Stegosaurus | bot | +18.000 | +0.789 | +0.714 | +0.018 | -0.111 | +0.000 | -0.111 | +0.408 | +19.706 |
| Tyrannosaurus | bot | +18.000 | +0.364 | +0.616 | +0.548 | -0.474 | +1.167 | +0.000 | +0.487 | +20.707 |
| Pachycephalosaurus | bot | +18.000 | +0.765 | +0.686 | +0.296 | -0.569 | +0.167 | -0.278 | +0.440 | +19.508 |
| Sauropelta | bot | +18.000 | +0.820 | +0.662 | +0.069 | -0.421 | +0.000 | -0.417 | +0.357 | +19.071 |
| Gallimimus | bot | +18.000 | +0.768 | +0.806 | +0.000 | -0.180 | +0.000 | -0.167 | +0.480 | +19.707 |
| Velociraptor | random | +17.999 | +0.000 | +0.000 | +0.004 | -1.006 | +0.000 | -1.000 | +0.372 | +16.369 |
| Stegosaurus | random | +17.999 | +0.217 | +0.028 | +0.030 | -0.867 | +0.111 | -0.556 | +0.280 | +17.242 |
| Tyrannosaurus | random | +17.999 | +0.000 | +0.004 | +0.000 | -0.989 | +0.000 | -0.833 | +0.424 | +16.605 |
| Pachycephalosaurus | random | +17.999 | +0.181 | +0.009 | +0.000 | -0.958 | +0.056 | -0.778 | +0.370 | +16.879 |
| Sauropelta | random | +17.999 | +0.265 | +0.041 | +0.026 | -0.699 | +0.500 | -0.583 | +0.296 | +17.845 |
| Gallimimus | random | +17.999 | +0.207 | +0.025 | +0.004 | -0.842 | +0.417 | -0.583 | +0.347 | +17.574 |
| Velociraptor | noop | +18.000 | +0.000 | +0.000 | +0.000 | -0.696 | +0.000 | -0.500 | +0.364 | +17.168 |
| Stegosaurus | noop | +18.000 | +0.000 | +0.000 | +0.000 | -0.771 | +0.000 | -0.444 | +0.341 | +17.125 |
| Tyrannosaurus | noop | +17.999 | +0.000 | +0.000 | +0.000 | -0.823 | +0.000 | -0.667 | +0.428 | +16.937 |
| Pachycephalosaurus | noop | +17.999 | +0.000 | +0.000 | +0.000 | -0.818 | +0.000 | -0.611 | +0.395 | +16.964 |
| Sauropelta | noop | +17.999 | +0.000 | +0.000 | +0.000 | -0.889 | +0.000 | -0.750 | +0.361 | +16.721 |
| Gallimimus | noop | +18.000 | +0.000 | +0.000 | +0.000 | -0.837 | +0.000 | -0.667 | +0.341 | +16.837 |

Probes: attacking the nearest animal outside the group, while everyone else is a bot.

| species | as bot | brawling | kills / episode | deaths / episode | kill term | dealt term | food term |
|---|---|---|---|---|---|---|---|
| Tyrannosaurus | +20.71 | **+65.12** | 36.5 | 3.2 | +36.50 | +16.90 | 0 |
| Sauropelta | +19.07 | **+43.67** | 26.0 | 5.3 | +26.00 | +10.99 | 0 |
| Stegosaurus | +19.71 | **+39.87** | 20.1 | 3.9 | +20.11 | +9.62 | 0 |
| Pachycephalosaurus | +19.51 | +2.08 | 13.5 | 19.7 | +13.50 | +7.14 | 0 |
| Velociraptor | +19.10 | -6.12 | 2.8 | 15.5 | +2.83 | +1.63 | 0 |
| Gallimimus | +19.71 | -23.92 | 11.3 | 32.2 | +11.33 | +6.22 | 0 |

Everyone floating in deep water: return 17.03, survival 46%, no better than standing still.

### Findings

1. **Killing pays far more than surviving, for herbivores too (exploitable).**
   - Kill (+1) and damage dealt (+0.5 per victim max health) do not depend on diet, target or victim size. In-group hits are the only exception.
   - A stegosaur or sauropelta that tail-swings or bashes whatever is nearest doubles its return (+39.9, +43.7 against about +19.5). A brawling tyrannosaur triples it (+65.1, 36 kills an episode, mostly respawning raptors).
   - Victims respawn immediately next to their herd (`respawn_ticks = 1`; herbivores 15 to 60 m from the herd centre, predators 80 to 200 m out), so there is a steady supply.
   - The brawl probes logged 1053 kills by herbivores.
   - A predator's food term is small next to its kill term (tyrannosaur bot: food +0.36, kill +1.17), so eating barely matters to it.
   - Options: pay kills only to carnivores (or scale by the meat gained); scale kill and dealt by victim mass relative to the attacker; drop the dealt reward for herbivores, or keep it only when defending (the victim attacked first); add spawn protection or spawn away from recent kills.
2. **The alive term is about 92% of the return and carries almost no signal.**
   - It is +18.0 per slot-episode for bots, random and noop alike. With `respawn_ticks = 1`, a death costs one tick of it.
   - Its only effect is the value lost at a death terminal: about 0.001 / (1 − 0.995) = 0.2, on top of the −1 death penalty.
   - Harmless for PPO advantages, but it makes `episode_return` and `score` useless as progress metrics; use `perf`.
3. **Growth pays for time alive, not for eating.**
   - Bots earn +0.42 per slot-episode and noop +0.37. Growth accrues whenever food and water are above 0.25, and spawn stomachs start at 0.75 to 1.
   - Within a 600 s episode, growth's reward weight 2 never ties to feeding. Food and water (+1 per stomach) are the real feeding signal.
4. **Kill credit never expires (`last_attacker`).**
   - 15 of 30 kills in the bot and random runs went to an attacker whose last hit was over 30 s before the death. Most were animals dying of thirst or starvation long after a bite.
   - The attacker gets +1 kill for a death it did not cause.
   - Fix idea: clear `last_attacker` (or skip the kill credit) when the last hit is older than about 10 s.
5. **Sauropelta's spikes pay it for being bitten.**
   - When a carnivore bites a sauropelta, the thorn damage counts as damage dealt by the sauropelta (`b->ev_dealt` in `damage()`).
   - When the biter dies of it, the sauropelta also gets the kill: 11 kills per episode against brawling raptors.
   - This is arguably intended (armour as a weapon), but a policy may learn to seek out bites.
6. **Not exploitable as tested.**
   - Idling in deep water: attacks need the attacker out of the water, but nobody can drink while swimming, so floaters die of thirst like standing animals (46% survival, return 17.03).
   - Farming group members: in-group hits are skipped in `resolve_attack`.
   - Cannibalism outside a group is paid (63 of 1600 brawl kills were the killer's own species), which is a case of finding 1.
7. **Discount horizon against survival time scales.** `gamma = 0.995` is a horizon of about 200 ticks (6.7 s), while thirst kills in 450 to 600 s and hunger in 675 s or more.
   - The direct signals are the per-stomach food and water rewards.
   - Before an animal is parched, its value function cannot see a death from thirst coming.
   - Worth knowing when reading `perf` curves.

## Bugs found and fixed

Each fix is small and listed here so the merge can be checked:

| File:line (after the fix) | Bug | Fix |
|---|---|---|
| `sim.c:506-513` (`move_agent`) | Agents walked up to 0.4 m into sloped prop faces (boulder flanks). The plane solver pushed along the tilted normal, then the vertical part of the push was discarded, so only part of the horizontal push remained. Found by the real-prop test: 4 of 15 boulders were entered by 0.23 to 0.41 m. | Solve against each plane's horizontal part: the normal is flattened and the offset scaled by `1/|n_h|`. Planes within about 11° of horizontal (tops and undersides) get no push. 15 of 15 now hold. |
| `sim.c:878-880` (`cret_bot_input`) | A bot that is thirsty and cannot find water (for example far out at sea) re-ran `find_water`, a scan of about 4000 points, every tick. A 32-bot soak went from 0.3 to 12 to 15 ms per tick of bot time after tick 13000, which would overrun a 30 Hz server's 33 ms budget with a full server. | Search once a second per bot (`(s->tick + i) % CRET_TICK_HZ == 0`). The worst 1000-tick window is now about 0.9 ms per tick. |
| `net.c` (`client_welcome`; was `cret_net_join`) | The welcome check accepted `num_agents` up to 255 and any finite `world_cell`. A bad or hostile server could send `num_agents = 200, slot = 150`. `cret_sim_init` clamps the agents to 64, and the client then indexes `agents[150]` (out of bounds). It also accepted negative cell sizes. Found by the fuzzer. | Also require `num_agents <= CRET_MAX_AGENTS` and `0.25 <= world_cell <= 64`. The protocol-8 codec's welcome checks these plus the slot, tick rate and snapshot divisor. |
| `net.c` (`netp_server_input`, `get_ent`, self decode), `sim.c` (`move_agent`) | Out-of-range actions (below). | Inputs with an action past `CRET_ACT_NEST`, an ability slot past `CRET_AB_SLOTS` or the spare bit set are refused; snapshots range-check `act` against `CRET_ACT_NEST`; `move_agent` treats an out-of-range action as none. |

`make asan` (a 10k-tick bot soak with invariants, a 5k-step binding rollout, and a 200k-packet codec fuzz under `-fsanitize=address,undefined`) found nothing else in the sim, terra, world or net code.
Box3D is linked uninstrumented.
The only leak it reported was in the test itself.

## Open issues

- **Out-of-range actions**: fixed at integration (table above). With abilities the action list ends at `CRET_ACT_NEST` (6), so act 7 is the only out-of-range 3-bit value; ability slots use 2 bits (0 to 3, all valid).
- **Thirsty bots at sea.** On the tiny test world, up to 28 of 32 bots were thirsty at once, and some swam for long stretches. `find_water` needs dry, walkable ground 6 m before the water, which fails from open sea, and wander goals out there are all unwalkable. The throttle above fixes only the cost, not the behaviour.
- **Policy contract in checkpoints.** Existing `checkpoints/cretaceous/` runs are from contracts 1 and 2, with 160 and 176 inputs plus actions, and cannot be evaluated against contract 4 (243 inputs). The eval refuses them with both sizes in the message.
- **The bench runs on a shared, oversubscribed box.** Its baseline is noisy, and slow cases are not failed while the load average is above 1.5× the cores. Rewrite the baseline on a quiet machine.
- **The bot-only soak with `CRET_TEST_N=2048`** (the training world) is not part of `make test`, because the bake takes about 45 s. Run `CRET_TEST_N=2048 build/cretaceous/test/test_sim --soak 10000` by hand when the world changes. The baselines above already run on it.
