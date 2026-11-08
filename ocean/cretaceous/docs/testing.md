# Testing and evaluation

Everything here runs on the CPU, from `ocean/cretaceous`:

```bash
make test     # fast suite: determinism, binding contract, physics, net codec, respawn protocol, eval smoke, tick bench
make asan     # 10k-tick bot soak, binding run and codec fuzz under AddressSanitizer + UBSan
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
- **Attack buffer.** A press up to `CRET_ATTACK_BUFFER` ticks (0.2 s) before the cooldown ends starts the attack when it can; one pressed 12 ticks early is dropped.
- **Soak.** 3000 ticks of 32 bots. `test_sim --soak N` runs longer; `CRET_SOAK_TRACE=1` prints bot and sim ms per tick and the number of swimming and thirsty animals every 1000 ticks.

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

### `tests/test_net_codec.c`: net codec (protocol 10)

`net.c`, `netclient.c` and `netsim.c` are compiled unchanged against an in-memory ENet (`tests/fake_enet/enet/enet.h`). Events are queued by the test, and sent packets are captured as bytes.
If the net code starts using another ENet call, add a stub there (sockets are stubs that fail: the link conditioner is not used here).

- **Round trips.**
  - Welcome: slot and world parameters.
  - 60 snapshots over a 3000-tick bot run, with forced deaths, carcasses, rallies, long flinches, statuses, progress and a nest. The client's input (with its acks) reaches the server before each snapshot, so baselines move and deltas are used.
  - Every entity a snapshot carries (interest management sends far ones less often) must match within its quantisation: 1/256 m positions, 12-bit heading, 1/16 m/s speed, 1/255 for q8 fields, 1/8 m rally points, 1/62 for the aim, exact flags, statuses and marks. The own state (with the v9 secondary cooldown, queued attack and aim) must match exactly (floats) or to the ms (status timers, cooldowns); the progress block and nests exactly.
  - 2000 random inputs over every action head, the secondary attack and the aim (including -1 and 1): within 1/127; flags, action and ability exact, in order. An aim byte of -128, a bit past the 40 and the top flag bit are refused, and non-finite axes pack to 0.
  - Hits (v10): the block keeps the client's own hits first, then near ones in the window, at most 24, dropping old, far and self hits, and round-trips exactly; every out-of-range field is refused. In the bot run a hit on the client is logged once although the next snapshot repeats it, and every hit the client logs is in the server's log.
  - Respawn requests: settled after the cooldown with a reply, a second one held until the second is up, an unknown species refused at once. Profile (character name) and slot-3 unlock reach the server.
  - Disconnects, a full server turning a peer away, and another protocol version refused.
- **Atomic snapshots.** Every truncation of a new snapshot must be refused and leave the client's state unchanged.
- **Fuzz** (`--fuzz N`, 100k in `make test`, 200k in `make asan`, 2M in `make fuzz`):
  - mutations: truncation, bit flips, byte writes, trailing junk, random bodies with a real message type, and swaps;
  - targets: the client's snapshot and spawn-reply decode (each snapshot made new to it), the server's input, request, profile, unlock and junk handling, and the join handshake;
  - afterwards, the client's agents, carcasses, nests, progress and hit log must be valid and finite, server inputs bounded on every action head, names terminated, and an accepted welcome must give a slot below `CRET_MAX_AGENTS` and a positive cell size.

`tools/netfuzz.c` is the codec's own fuzzer, which `make asan` also runs (300k packets): round trips over a lossy, duplicating, reordering channel at 64 and 128 agents (including the v8 blocks, the v9 aims, secondary cooldowns and queued attacks, and the v10 hits, each of which must reach the client), then mutated snapshots, inputs, welcomes, requests and replies into the decoders. docs/net.md has the details.

### `tests/test_cache.c`: derived-data caches

The startup caches (`render/cache.c`; no GL, no world): an item stored in one part or several loads back byte for byte; another key (a changed input or version), a flipped payload or header byte, a truncated or extended file, a missing file and `CRET_CACHE=0` are all misses, and a store after a miss hits again, so a deleted or corrupt cache rebuilds itself; no temporary files are left. Content keys change with a file's bytes, its appearance and the order of files, and the threaded hash matches. The reader refuses short reads and `cret_parallel_for` runs every index once. `make test` and `make asan` run it.

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
