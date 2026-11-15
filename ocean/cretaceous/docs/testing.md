# Testing and evaluation

Everything here runs on the CPU, from `ocean/cretaceous`:

```bash
make test     # fast suite: determinism, ecosystem arithmetic, critters, quests, binding contract, physics, net codec, respawn protocol, joining during a slow load, foot IK, animation check on the test dinosaurs, eval smoke, CPU policy path, camera probe, combat probe, ecosystem probe, tick bench
make asan     # 10k-tick bot soak, critters, binding run, codec fuzz, foot IK, the camera probe and the combat probe under AddressSanitizer + UBSan
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
  - The hash covers each agent's raw bytes except the Box3D ids, so new `CretAgent` fields are covered automatically (the wounds among them). It also covers the carcasses, the sim rng, the weather, the prop stream and the blood marks (which hunting bots follow, cret/wounds).
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
- **Kill credit.** A pachycephalosaur bitten by a raptor and killed 1 s later is the raptor's kill (event, paid, `stats.kills`, predation); one killed 10 s + 1 tick after the bite with the wound closed credits nobody and counts as an environmental death; one killed then while the wound still bleeds (at least `CRET_WOUND_OPEN`, 0.2% of max health a second) is the raptor's kill.
- **Reward worth** (`cret_reward_worth`): a tyrannosaur's damage and kill on a raptor pay 80/8000 of the raw amount and on a stegosaur 5000/8000; a raptor on a gallimimus is paid in full. A stegosaur's unprovoked swing at a raptor or at a sauropelta pays nothing, a swing at a raptor that bit a herd mate 1 s before pays in full and one 11 s after pays nothing, and its kill is counted but not paid. A raptor biting a sauropelta is paid in full, the spikes pay the sauropelta nothing, and a raptor dying on them is the sauropelta's kill in the statistics only.
- **Knockdowns** (`test_knockdown_*`, sim.h `CRET_KD_*`):
  - an adult stegosaur's tail swing knocks an adult raptor down: counted in `stats.knockdowns`, logged as `CRET_HIT_DOWN`, down 0.63 s (`CRET_KD_MIN` to `CRET_KD_MAX` by the knockback), and it reads as stunned in its own observation and in the stegosaur's neighbour record; lying, every input (move, sprint, turn, jump, attack, ability) is ignored and the knockback slide stops; it gets up over exactly `CRET_KD_GETUP_TICKS` (0.4 s), turning but not moving or attacking; then it runs, a second blow within `CRET_KD_IMMUNE` (4 s) does not knock it down and one after does;
  - who knocks down whom: a stegosaur's sweep a gallimimus, a sauropelta's tail swipe a raptor, a pachycephalosaur's headbutt a raptor and a gallimimus, a tyrannosaur's bite a stegosaur at growth 0.8 and a grown sauropelta; not a grown stegosaur, not a pachycephalosaur its own size, not a raptor its own size, not a gallimimus's kick. A raptor biting a stegosaur's flank for 20 s (at least 10 bites landed) never knocks it down, and neither its bite nor its slash knocks down any animal heavier than itself at any growth; Each of those blows pays exactly its damage at `cret_reward_worth` and no kill: a knockdown pays nothing of its own.
  - two stegosaurs taking turns, a tail blow at a raptor every 0.5 s for 20 s: 4 knockdowns, down 20% of the time (the test allows half; the bound from the timings is 26%).
  - The determinism run prints its knockdowns (2 in 5000 ticks of 32 agents) and stays in lockstep.
- **Wounds** (`test_wound_*`, `test_blood_trail`, sim.h `CRET_WOUND_*`):
  - place: a blow of 20% of the victim's health after armour leaves severity 0.5 on the place it came at: a raptor on a raptor in front the head, behind the legs, beside the body; a raptor on a stegosaur (a third of its height) the legs from in front; a tyrannosaur on a raptor (four times its height) the body from in front. A blow of 5% leaves none; three blows of 20% on one place add up to 1. Each is counted in `stats.wounds`;
  - healing over 10 s at severity 0.5: fed and resting, fed and awake, hungry, starving and parched each change it by exactly their rate (1, 0.5, 0.15, -0.4 and -0.4 a game day); a scratch heals away to no wound;
  - effects at severity 1: a raptor trots at 0.73 of its speed (3.31 against 4.56 m/s; 0.7 with the slope) and turns at 0.70 on the spot, and client prediction (`cret_sim_move`) ends on the sim's step; a body wound costs 0.06% of max health a second while too hungry to heal (nothing unhurt) and stamina comes back at 0.60; a head wound makes the trail, critter and sight ranges 0.60 of the unhurt ones;
  - blood: with nobody about, a gallimimus with a body wound walking 17.6 m leaves 5 marks (one every 3 m), one with a scratch and the raptor leave none and keep no trail, a raptor 30 m off smells the marks, and a mark is still more than 0.4 fresh after 90 s (twice a trail's scent) and gone after `CRET_BLOOD_LIFE`;
  - herds: a pachycephalosaur herd keeps to a walk for a member with a fresh leg wound of 0.6 (`cret_herd_waits`), not after `CRET_WOUND_HERD_DAY`, and not for one that does not limp;
  - the determinism run prints its wounds (5 in 5000 ticks of 32 agents) and the blood marks held at the end (105), and fails without a wound.
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

`make test` runs the probe's quick set on the test world (24 animals, 2 game days, persons for 5 minutes, 4 stalking trials per case; about 7 s) and fails if any of its 146 results leaves `tests/ecoprobe_budget.txt` (each result of the stored run +-50%, at least +-2). It guards against regressions; the design's targets and the full run are in `docs/ecosystem.md`, section 11. `--write FILE` rewrites the budget after a deliberate change. Run it from the repository root, as `make test` does (`./build/cretaceous/test/ecoprobe --quick --quiet --write ocean/cretaceous/tests/ecoprobe_budget.txt`): from `ocean/cretaceous` the probe finds no props (no "prop colliders" line), runs a different trajectory and writes a budget `make test` then fails. That happened once: from 2026-11-13 to 11-14 the stored budget was a run without props, and `make test` failed 19 of 146 lines on 5.0.

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
  - `OBS_SIZE == CRET_OBS_SIZE == self + patch + neighbours + contract 4 self + contract 4 neighbours + contract 6 self` (contract 5 kept the observations; contract 6 adds the own leg, body and head wound, `CRET_SELF_OBS6`, with their rows in the obs-stats table);
  - `NUM_ATNS == CRET_ACTION_HEADS == len(CRET_ACTION_SIZES)`;
  - the config states the same action heads (`{5, 5, 3, 2, 8, 5}`, contracts 5 and 6) and obs count (`246 obs floats`), the README states `246 floats`, and both name the contract version (6).
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

### `tests/test_net_codec.c`: net codec (protocol 20)

`net.c`, `netclient.c` and `netsim.c` are compiled unchanged against an in-memory ENet (`tests/fake_enet/enet/enet.h`). Events are queued by the test, and sent packets are captured as bytes.
If the net code starts using another ENet call, add a stub there (sockets are stubs that fail: the link conditioner is not used here).

- **Round trips.**
  - Welcome: slot and world parameters.
  - 60 snapshots over a 3000-tick bot run, with forced deaths, carcasses, rallies, long flinches, statuses, progress and a nest. The client's input (with its acks) reaches the server before each snapshot, so baselines move and deltas are used.
  - Every entity a snapshot carries (interest management sends far ones less often) must match within its quantisation: 1/256 m positions, 12-bit heading, 1/16 m/s speed, 1/255 for q8 fields, 1/8 m rally points, 1/62 for the aim, exact flags, statuses and marks. The own state (with the v9 secondary cooldown, queued attack and aim) must match exactly (floats) or to the ms (status timers, cooldowns); the progress block and nests exactly.
  - 2000 random inputs over every action head, the secondary attack, the aim (including -1 and 1) and the carry press (v13): within 1/127; flags, action, ability and carry exact, in order. An aim byte of -128 and a bit past the 40 are refused; the top flag bit unpacks as the carry press; non-finite axes pack to 0.
  - Views (v18): 300 inputs, two thirds of them presses (attacks, secondary attacks, abilities), some without a view: every press's view arrives to the 1/16 tick, every other input carries none. The rewind the server takes from a press is clamped: a view 40 ticks in the future rewinds nothing, one 3.5 ticks back rewinds 4.5 ticks (to the state the tick makes), one 500 ticks back is held to `CRET_REWIND_MAX` (6 ticks) behind the snapshot the client acknowledged, an input without a press keeps the rewind, and a press without a view rewinds nothing.
  - Carrying (v13): during the status rounds the client's animal carries an egg and a fish's rest lies on the ground; the carcass entities' item kind, critter kind and carrier and the own carried slot and load round-trip exactly.
  - Knockdowns (v19): during the status rounds one animal lies (40 ticks left of a 50-tick fall, to its left), another gets up (9 ticks left), and the client's own animal is by turns lying, getting up and just up (immune for 77 ticks). Remote lying and get-up ticks, fall length and side, and the own timers round-trip exactly; the fuzz checks every decoded agent's timers are in range.
  - Wounds (v20): during the status rounds one animal has a leg wound on the limp line and a body wound just under the drip line, another a scratch and a leg wound of 1, and the client's own animal changing wounds. Remote severities arrive in twentieths rounded down (a scratch as one twentieth) and read the same on either side of `CRET_WOUND_LIMP` and `CRET_WOUND_DRIP`; the own ones arrive exactly; the fuzz checks every decoded severity is within 0..1. The lag-compensation run clears the gallimimus's wounds each tick, as it already cleared its bleeding and broken bones: a leg wound from the raptor's bites slowed it and the run judged too few swings.
  - Hits (v10): the block keeps the client's own hits first, then near ones in the window, at most 24, dropping old, far and self hits, and round-trips exactly; every out-of-range field is refused. In the bot run a hit on the client is logged once although the next snapshot repeats it, and every hit the client logs is in the server's log.
  - Respawn requests: settled after the cooldown with a reply, a second one held until the second is up, an unknown species refused at once. Profile (character name) and slot-3 unlock reach the server.
  - Disconnects, a full server turning a peer away, and another protocol version refused.
- **Critters** (v12): a server with people steps bots for 3 s, then 40 snapshots reach the client while critters move and one is eaten. Every critter the client holds is the server's at that slot, kind and state exact, position within 1/32 m, heading within 3°; the set is the 24 nearest within 80 m; eaten ones vanish. A spot quest (catch, at 1234.5, 777.25, radius 40) arrives with its spot and the client finds its target. These snapshots join the fuzz seeds, and the fuzz checks every client critter and spot quest is valid.
- **Lag compensation** (v18, `test_lag_comp`): a deterministic loopback at 150 ms round trip. The fake transport delays every datagram 75 ms each way on a virtual clock (`cret_netsim_set_clock`), the server and the client tick at 30 Hz with the client 0.4 ticks out of phase. The client's grown raptor holds its post, head straight, and bites once a pass at a grown gallimimus sprinting back and forth at 9 m/s, its body 1.3 m beside the strike point, timed so the jaws close on its hind end as the client draws it (every third pass 2 m late). At the client's own hit moment each swing is judged in its drawn world (`cret_swing_reaches`), with a 15 cm margin either way, and the server's hit log says whether it landed. With the view sent: 11 swings, the 7 clearly seen to land all land, none of the 4 clearly seen to miss lands, rewinding 8.1 ticks (270 ms). Without it (the control) none of the 7 lands: the gallimimus is 2.4 m further on at the server's present. The bite's lunge reaches for the gallimimus where the client drew it on both ends (cret/predict-casts), so where it carried the raptor by each blow, as the client first predicted it, is within 4.9 mm of the server's at all 11 blows (`LC_LUNGE_TOL`, 2 cm); without the view (the server lunging at the present) they part by 200 mm. The test fails if fewer than 3 clear hits or no clear miss were judged, if any clear judgement disagrees, if the control lands every clear hit, if the predicted lunge is off by 2 cm or more, or if the control's lunge agrees too.
- **Predicted casts** (`test_cast_predict`, cret/predict-casts): the same 150 ms loopback. The client's grown raptor (pounce) and, in a second run, its grown gallimimus (dodge leap, to the right with the strafe key) stand 4 s on the cleared lane, walk 1 s, press slot 1 once and walk on, with the only other animal 60 m off. The leap starts at the press input in the client's first prediction and on the server, the path the client showed (each input as first predicted) matches the server's through the same inputs within 1 cm for the 60 inputs (2 s) from the press, no correction is smoothed in, and the leap covers more than 4 m (measured: 0.00 mm, 14.9 m for the pounce with the walk, 9.5 m for the dodge). On the code before casts were predicted the leap started 5 inputs late and 1.6 m (pounce) and 1.3 m (dodge) were smoothed in, which the test fails.
- **Atomic snapshots.** Every truncation of a new snapshot must be refused and leave the client's state unchanged.
- **Fuzz** (`--fuzz N`, 100k in `make test`, 200k in `make asan`, 2M in `make fuzz`):
  - mutations: truncation, bit flips, byte writes, trailing junk, random bodies with a real message type, and swaps;
  - targets: the client's snapshot and spawn-reply decode (each snapshot made new to it), the server's input, request, profile, unlock and junk handling, and the join handshake;
  - afterwards, the client's agents, carcasses, nests, progress and hit log must be valid and finite, server inputs bounded on every action head, names terminated, and an accepted welcome must give a slot below `CRET_MAX_AGENTS` and a positive cell size.

`tools/netfuzz.c` is the codec's own fuzzer, which `make asan` also runs (300k packets): round trips over a lossy, duplicating, reordering channel at 64 and 128 agents (including the v8 blocks, the v9 aims, secondary cooldowns and queued attacks, the v10 hits, the v12 critter block and spot quests, and the v18 views of presses, each of which must reach the client or the server), then mutated snapshots, inputs, welcomes, requests, loading reports (v20) and replies into the decoders; every rewind an accepted (mutated) press sets must stay within its limits. The fuzzed client's own predicted ticks press bites and ability slots with sane, garbage or no views, so predicted casts and the predicted lunge (`netp_client_predict`) run on fuzzed states, and the animal must stay finite. Extending it turned up a signed overflow in `scent.c` (`trail_update`, ticks from a fuzzed snapshot), which `--seed 5` already hit before; ticks are now compared modulo 2^32. docs/net.md has the details.

### `tests/test_cache.c`: derived-data caches

The startup caches (`render/cache.c`; no GL, no world): an item stored in one part or several loads back byte for byte; another key (a changed input or version), a flipped payload or header byte, a truncated or extended file, a missing file and `CRET_CACHE=0` are all misses, and a store after a miss hits again, so a deleted or corrupt cache rebuilds itself; no temporary files are left. Content keys change with a file's bytes, its appearance and the order of files, and the threaded hash matches. The reader refuses short reads and `cret_parallel_for` runs every index once. Texture payloads (`render/texbuild.c`): BC3 blocks decode back near their texels (flat blocks to 565 rounding with exact alpha, two-colour gradients and alpha ramps within the format's quantisation, noisy blocks within a looser bound), mip chains have the right levels and sizes and average in linear light for sRGB, every block of every level and layer is written, float formats pass through as one level, unknown formats are refused, and a threaded build matches a single-threaded one byte for byte. `make test` and `make asan` run it.

### `tests/test_policy.c`: the CPU policy path (`policy_cpu.c`)

Trained policies on the CPU (docs/encoder.md, "CPU inference"):
- the stored fixtures `tests/policy_fixture_net0.bin` and `_net1.bin` (written by `tools/policy_reference.py`, 41 KB each): the weights are regenerated from the fixture's seed, written as a checkpoint and loaded, both with the sizes named and found from the file; the encoder output, the logits + value and the greedy and sampled actions of 8 agents over 4 steps (two reset mid-sequence) must match the float64 NumPy reference (max abs error 7.2e-7; bar 2e-4 + 1e-4 · |reference|);
- the linear path against PufferNet (`src/puffercpu.c`) on the same checkpoint (1.2e-6);
- refusals naming both counts: another hidden size, the other encoder, a contract 3 file, a truncated or missing file;
- `--policy` parsing (WHO, sizes, a drive letter or a colon inside the path, a bad WHO or encoder);
- 1 and 4 threads give bit-identical outputs at hidden 256 with 64 agents;
- two sims with 8 of 12 slots on the fixture policy for 240 ticks, one on 1 thread and one on 4: the same game tick for tick, and the animals move.

`test_policy FIXTURE...` checks other fixtures (`tools/policy_reference.py --hidden 1024 --batch 16 --out F` for the full size); `test_policy --bench [--runs N]` prints the timing table in docs/encoder.md.

### `tests/test_net_respawn.c`: respawn protocol over loopback ENet

A server and a client in one process over the real ENet on 127.0.0.1 (from 5.0's `tests/test_net.c`): species requests, held requests within the cooldown, the in-combat refusal and its lift, a dead animal respawning right after a hit, validation of bad and short requests and of a claimed position, profile and unlock, statuses, cooldowns and nests reaching the client, and leaving clearing the slot's pick. `make net-test` runs it alone.
It also checks the predicted slot-3 switch (`cret_net_client_choose`): the client's loadout changes on the press, stays through the progress blocks that follow and matches the server's, and a choice the server refuses is undone within a second.

### `tests/test_net_join.c`: joining while the world loads (v20)

A real server ticking at 30 Hz with bots on its own thread, two clients playing in a child process, and a third client joining from the main thread with a slow world load (`CRET_TEST_SLOW_LOAD=SECONDS`, a test-only hook in `world.c`: every world load waits that long first and reports progress as a bake does). The server's ENet timeouts are cut to 2 s so that a 3 s load stands for a 45 s first bake. It checks that a client that does not service its connection during the load is dropped (the old join), that one that does (`cret_net_client_load_begin`) joins, gets no snapshots while it loads, has its animal held and then plays (snapshots, no holds), that the other two clients get at least 13 snapshots in every whole second of the load (15 are sent; measured 15), and that a client still loading after `CRET_NET_LOAD_LIMIT` (5 s here) is dropped with the load reason. About 16 s of wall time, under 1 s of CPU. `test_net_join --long 45` runs the first two parts with ENet's own timeouts and a 45 s load (about 95 s). docs/net.md, Join and loading, has the results.

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
- turns the camera toward the target at most 10 rad/s, easing in over about 80 ms, with a wandering aim error of about 2.5 degrees; the attack aims where the camera looks and the body turns toward the camera while a move key is held, as in `player_aim` and `player_input` (`cretaceous.c`), and, standing still, toward a blow aimed past the neck arc (squaring up, `move_agent` in `sim.c`);
- decides once a frame at 30 or 60 fps, latches presses to the next 30 Hz tick as the player does, and runs with no input latency and with 80 ms;
- clicks when the target, as seen, is within reach plus 0.3 m of the jaws at the current aim, at most every 0.25 s and not while its own blow is visibly winding up; it does not watch the cooldown;
- plays two styles: **press** keeps W held into the target, **stand** lets go of W once the target is in reach and bites standing. Defending herbivores under **press** walk only to turn toward the attacker (a stegosaur puts its tail to it and looks back); under **stand** they never step: the camera stays on the attacker and they click whenever it is in reach or alongside (behind the hips, for a stegosaur), trusting the animal to square up. A fleeing gallimimus waits until the raptor commits (18 m, or coming at more than a walk), then sprints away.

Pairings (players first, bots after; adults unless a growth is given): raptor chasing a gallimimus, raptor against a pachycephalosaur, a pack of four raptors against a growth 0.6 stegosaur, the tyrannosaur against each species (gallimimus and pachycephalosaur as chases), each herbivore's defence (stegosaur against a tyrannosaur and against three raptors, pachycephalosaur against a raptor, sauropelta against a tyrannosaur, a growth 0.45 gallimimus fleeing a raptor), same-size duels (raptor, pachycephalosaur, tyrannosaur), and two defences against **flankers** (`pachy<fraptor`: a raptor, `stego<fraptor3`: three raptors on a growth 0.6 stegosaur) that circle for the defender's rear quarter (its shoulder, for a tail) and bite from there, as a quick player does: on 5.0 the bots choose their fights and a lone raptor leaves an adult pachycephalosaur be, so `pachy<raptor` throws nothing. Each runs 6 trials (start direction and aim noise) for each of 8 settings (2 styles x 30/60 fps x 0/80 ms) on open, flat ground of the test world with trees and props walk-through (`CRET_NO_PROP_COLLIDERS`), at most 60 s (90 s for the pack).

Per pairing it prints: attacks thrown and landed; swings knocked out before the blow (stunned); why each miss missed: **passed** (the target alongside or behind: the attacker ran past it or it circled), **arc** (another aim would have reached it; degrees off), **moved** (in reach when the swing started, and the blow would have landed where it stood then; how far it moved), **self** (in reach at the start, but the attacker's own movement, usually a throw, took the jaws away; metres short), **short** (out of reach at the start and at the hit; metres short); clicks dropped (they reached the game while it could not attack and nothing swung or stayed queued); press to start and press to hit, from the click the swing answered; the wait from the first click since the last swing; time closing and in reach; time alongside and passes per minute; median time to kill; won, lost or drawn; and for chases whether the prey was caught (the first blow) or escaped (15 m further off than at the start), when, and how far the chaser ran.

```bash
make -C ocean/cretaceous combatprobe        # from the repository root, like every run below
./build/cretaceous/combatprobe              # the full table: 18 pairings x 8 settings x 6 trials, about 4 s
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

What still felt wrong by the numbers, and what cret/combat-feel2 did (each item: origin/5.0 at d4f2afe with this branch's probe → this branch; the list's original numbers predate the knockdown port and the ecosystem bots, so 5.0's own are given first):
- **The raptor never settles a chase with an adult gallimimus.** On 5.0, 12 caught, 0 escaped, 36 open of 48; 24 of the 36 open ones ended with both animals afloat a metre apart, because the fleeing bot ran straight away from the raptor into the lake by the probe's spot, and nothing can start an attack while swimming. The rest hovered at 15 to 30 m: the bot eased to below a trot once the raptor was 25 m back, the raptor trotted up again, and it sprinted off again. Both were bot bugs (`bots.c`, `bot_flee`): the flee heading is now the one nearest to straight away with walkable ground 10 and 20 m along it (so along a shore, and back out when afloat), and it trots on until 45 m and a body length clear. Now 32 caught (in 28 s, 185 m run), 0 escaped, 16 open (four start directions where both end winded 12 to 20 m apart). That is what the hunt arithmetic predicts (`cret_eco_hunt_eval`): the gallimimus is faster but burns stamina twice as fast. No species stat changed. The same fix lets every animal outrun a tyrannosaur on land instead of dying in water a tyrannosaur can wade (tyrannosaur on a raptor 18 kills → 0, on a pachycephalosaur 20 caught → 0 of 48, on a gallimimus 4 → 0); its sprint (6.4 m/s) is the slowest, so it hunts by ambush.
- **A tyrannosaur on a stegosaur that walks away.** On 5.0, 93% landed (100% pressing W, 86% standing): standing, it brakes through the 0.85 s wind-up while the stegosaur walks on at 2.1 to 2.5 m/s, and the 0.9 m lunge (half accel x `hit_time` squared) fell 0.2 to 0.5 m short. The lunge now follows a target moving away by as far as it moves in the wind-up at up to the attacker's own trot, still within a quarter body length (`lunge_step`): 2.7 m for a tyrannosaur, unchanged caps of 1.1 m for a pachycephalosaur and 0.75 m for a raptor. Now 99% (standing 99%, short misses 12 → 0; time to kill 18.2 s either way). A cap at the walk instead gave 81% standing (the stegosaur walks faster than a tyrannosaur); the trot cap does not let defenders chase raptors down (bots, below).
- **Defenders get circled.** The body turned toward the camera only while a move key was held. Now an animal standing still that presses an attack, has one queued or is winding one up squares up (`move_agent`): a bite, headbutt or bash turns the body at its turn rate while the aim is at the end of the neck arc (`CRET_SQUARE_AIM` 0.98), a tail blow brings the tail square onto its aim. Bots, policies and people get the same rule. "Passed" misses: pachycephalosaur against a flanking raptor (`pachy<fraptor`) 193 of 303 → 5 of 210 (landed 34% → 95%, wins 24 → 48 of 48), stegosaur against a tyrannosaur 20 → 4 (74% → 84%); against three flanking raptors on a young stegosaur 33 → 51 (75% → 74% landed; the raptors bite sooner now, below, and alone that took it to 76 passed and 64%). Tried: turning the body to centre the head for every aim (the head turning back by as much). For a policy holding one aim bin open-loop, the body kept turning through the wind-up (`test_aimed_attacks` failed), so bites, headbutts and bashes wait for the end of the arc. For the tail, waiting for the end of the arc was worse than not turning (130 passed against the flankers), since the camera's tail aim fades in behind the hips and seldom gets there.
- **Press to hit is 0.5 to 0.95 s.** The raptor's bite now lands 0.35 s into the swing (was 0.5) and the pachycephalosaur's headbutt 0.55 s (was 0.7); the tyrannosaur (0.85 s), stegosaur (0.55), sauropelta (0.4) and gallimimus (0.3) keep theirs, so the quick animals snap and the tyrannosaur stays heavy. Cooldowns and swing lengths are unchanged (the same damage per second). Press to hit: raptor 583 → 455 ms, pachycephalosaur 767 → 636 ms, all settings 665-753 → 573-674 ms. Shorter wind-ups also land more: raptor pack on a stegosaur 84% → 91% (swings knocked out 111 → 30), raptor duel 41% → 97%. The lunge from a stand shrinks with them (raptor 0.75 → 0.55 m). The clips stay in sync by themselves (`render/dino.c`, the attack-clip block of the animation update): the wind-up plays at `CLIP_HIT / hit_time`, at most 2.5x, starting partway into the clip when that is not enough (the raptor's 1.0 s clip wind-up at 2.5x from 0.125 s; the pachycephalosaur's 1.3 s at 2.36x), so the clip's hit frame lands at `hit_time`. The sim resolves the blow on tick floor(`hit_time` x 30): 10 for the raptor and 16 for the pachycephalosaur, half a tick (17 ms) before the clip's frame, as the tyrannosaur's (25 vs 25.5) and the stegosaur's (16 vs 16.5) always have. New: after the blow the recovery plays at the rate that ends the clip with the swing (`attack_time`) instead of the wind-up's rate. The owner should watch: `play scenario` fights with raptors and pachycephalosaurs, the bite and the butt landing as the damage numbers and the hit flash appear, and the start of the raptor's bite (its first eighth is skipped); `capture --shot 'dino_feel_*'` (`dino_feel_bite` stages the bite at frame lroundf(`hit_time` x 30), now 11 for the raptor (was 15), and shoots 4 frames later: the jaws should be closing on the stegosaur's flank; `dino_feel_tail` and `dino_feel_hurt` are unchanged).
- **Fleeing duellists.** Raptor duels on 5.0: 41% landed, 39 "moved" misses (1.6 m) and 145 short (0.1 m): the losing bot fled into the water and around it. With the flight on dry ground and the quicker bite: 97%, no moved misses, 2 short; the player wins all 48 (was 38, 10 drawn) in 25 s (32 s).

#### Before and after (cret/combat-feel2)

Before is origin/5.0 (d4f2afe) with this branch's probe; after is this branch. All settings together; misses in brackets are metres short or moved.

| pairing | thrown | landed | passed | moved (m) | short (m) | press>hit | TTK (median) | won/lost/draw | chase: caught/escaped/open, s, m |
|---|---|---|---|---|---|---|---|---|---|
| raptor>galli | 155 → 39 | 8% → 82% | 0 → 0 | 0 (0.0) → 0 (0.0) | 6 (0.3) → 6 (0.1) | 570 ms → 437 ms | -1.0 s → -1.0 s | 0/0/0 → 0/0/0 | 12/0/36, 26.8 s, 141 m → 32/0/16, 28.1 s, 185 m |
| raptor>pachy | 303 → 302 | 94% → 96% | 0 → 0 | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 583 ms → 455 ms | -1.0 s → -1.0 s | 0/48/0 → 0/48/0 |  |
| raptor4>stego | 1125 → 1004 | 84% → 91% | 0 → 0 | 12 (0.8) → 2 (0.5) | 8 (0.3) → 8 (0.2) | 587 ms → 474 ms | 11.5 s → 10.3 s | 48/0/0 → 48/0/0 |  |
| trex>raptor | 20 → 0 | 90% → 0% | 0 → 0 | 0 (0.0) → 0 (0.0) | 2 (0.2) → 0 (0.0) | 894 ms → 0 ms | 32.9 s → -1.0 s | 18/0/30 → 0/0/48 |  |
| trex>stego | 189 → 184 | 93% → 99% | 0 → 0 | 1 (1.5) → 0 (0.0) | 12 (0.2) → 0 (0.0) | 933 ms → 930 ms | 18.2 s → 18.2 s | 48/0/0 → 48/0/0 |  |
| trex>pachy | 36 → 0 | 56% → 0% | 0 → 0 | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 897 ms → 0 ms | -1.0 s → -1.0 s | 0/0/0 → 0/0/0 | 20/28/0, 22.4 s, 85 m → 0/48/0, 26.2 s, 104 m |
| trex>sauro | 288 → 284 | 89% → 90% | 0 → 0 | 5 (1.7) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 930 ms → 928 ms | 17.0 s → 17.0 s | 48/0/0 → 48/0/0 |  |
| trex>galli | 18 → 0 | 22% → 0% | 3 → 0 | 1 (4.9) → 0 (0.0) | 6 (1.2) → 0 (0.0) | 983 ms → 0 ms | -1.0 s → -1.0 s | 0/0/0 → 0/0/0 | 4/44/0, 9.3 s, 38 m → 0/48/0, 5.6 s, 27 m |
| stego<trex | 108 → 110 | 74% → 84% | 20 → 4 | 0 (0.0) → 0 (0.0) | 8 (0.9) → 8 (4.6) | 652 ms → 651 ms | -1.0 s → -1.0 s | 0/24/24 → 0/28/20 |  |
| stego<raptor3 | 12 → 12 | 33% → 33% | 0 → 0 | 0 (0.0) → 0 (0.0) | 8 (3.3) → 8 (2.8) | 608 ms → 608 ms | -1.0 s → -1.0 s | 0/0/48 → 0/0/48 |  |
| pachy<raptor | 0 → 0 | 0% → 0% | 0 → 0 | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 0 ms → 0 ms | -1.0 s → -1.0 s | 0/0/48 → 0/0/48 |  |
| sauro<trex | 106 → 106 | 92% → 92% | 0 → 0 | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 490 ms → 490 ms | -1.0 s → -1.0 s | 0/24/24 → 0/24/24 |  |
| galli0.45<raptor | 0 → 0 | 0% → 0% | 0 → 0 | 0 (0.0) → 0 (0.0) | 0 (0.0) → 0 (0.0) | 0 ms → 0 ms | -1.0 s → -1.0 s | 0/0/0 → 0/0/0 | 8/40/0, 10.1 s, 10 m → 8/40/0, 10.1 s, 10 m |
| raptor=raptor | 454 → 224 | 41% → 97% | 2 → 0 | 39 (1.6) → 0 (0.0) | 145 (0.1) → 2 (0.1) | 586 ms → 459 ms | 32.0 s → 25.3 s | 38/0/10 → 48/0/0 |  |
| pachy=pachy | 574 → 544 | 92% → 98% | 0 → 0 | 19 (1.8) → 5 (1.5) | 25 (0.1) → 5 (0.1) | 767 ms → 635 ms | 40.5 s → 40.3 s | 40/0/8 → 48/0/0 |  |
| trex=trex | 326 → 285 | 80% → 95% | 1 → 0 | 5 (4.2) → 1 (5.2) | 43 (0.6) → 7 (0.2) | 937 ms → 935 ms | 39.0 s → 31.2 s | 34/0/14 → 36/0/12 |  |
| pachy<fraptor | 303 → 210 | 34% → 95% | 193 → 5 | 0 (0.0) → 0 (0.0) | 0 (0.0) → 5 (1.3) | 771 ms → 635 ms | 13.8 s → 12.2 s | 24/24/0 → 48/0/0 |  |
| stego<fraptor3 | 342 → 436 | 75% → 74% | 33 → 51 | 0 (0.0) → 1 (2.2) | 5 (1.8) → 1 (0.1) | 634 ms → 647 ms | 46.8 s → 48.8 s | 8/13/27 → 3/23/22 |  |

By style (landed, press>hit, median time to kill), before → after:

| pairing | press | stand |
|---|---|---|
| raptor>galli | 42%, 567 ms, -1.0 s → 84%, 440 ms, -1.0 s | 3%, 575 ms, -1.0 s → 80%, 434 ms, -1.0 s |
| raptor>pachy | 89%, 566 ms, -1.0 s → 93%, 404 ms, -1.0 s | 100%, 596 ms, -1.0 s → 100%, 499 ms, -1.0 s |
| raptor4>stego | 82%, 580 ms, 11.7 s → 91%, 433 ms, 10.9 s | 87%, 595 ms, 10.3 s → 92%, 519 ms, 9.4 s |
| trex>raptor | 100%, 900 ms, 33.0 s → 0%, 0 ms, -1.0 s | 80%, 888 ms, 31.0 s → 0%, 0 ms, -1.0 s |
| trex>stego | 100%, 905 ms, 11.9 s → 100%, 905 ms, 11.9 s | 86%, 965 ms, 33.8 s → 99%, 956 ms, 32.8 s |
| trex>pachy | 71%, 894 ms, -1.0 s → 0%, 0 ms, -1.0 s | 45%, 900 ms, -1.0 s → 0%, 0 ms, -1.0 s |
| trex>sauro | 92%, 903 ms, 17.1 s → 92%, 903 ms, 17.1 s | 86%, 954 ms, 16.8 s → 89%, 951 ms, 16.8 s |
| trex>galli | 30%, 944 ms, -1.0 s → 0%, 0 ms, -1.0 s | 12%, 1100 ms, -1.0 s → 0%, 0 ms, -1.0 s |
| stego<trex | 100%, 652 ms, -1.0 s → 100%, 652 ms, -1.0 s | 0%, 0 ms, -1.0 s → 40%, 647 ms, -1.0 s |
| stego<raptor3 | 0%, 0 ms, -1.0 s → 0%, 0 ms, -1.0 s | 33%, 608 ms, -1.0 s → 33%, 608 ms, -1.0 s |
| pachy<raptor | 0%, 0 ms, -1.0 s → 0%, 0 ms, -1.0 s | 0%, 0 ms, -1.0 s → 0%, 0 ms, -1.0 s |
| sauro<trex | 92%, 490 ms, -1.0 s → 92%, 490 ms, -1.0 s | 92%, 490 ms, -1.0 s → 92%, 490 ms, -1.0 s |
| galli0.45<raptor | 0%, 0 ms, -1.0 s → 0%, 0 ms, -1.0 s | 0%, 0 ms, -1.0 s → 0%, 0 ms, -1.0 s |
| raptor=raptor | 78%, 577 ms, 28.1 s → 100%, 494 ms, 23.9 s | 26%, 595 ms, 40.8 s → 95%, 425 ms, 28.1 s |
| pachy=pachy | 99%, 752 ms, 37.9 s → 100%, 608 ms, 39.3 s | 85%, 778 ms, 43.9 s → 96%, 662 ms, 42.8 s |
| trex=trex | 86%, 952 ms, 35.5 s → 96%, 956 ms, 31.8 s | 75%, 917 ms, 40.2 s → 95%, 910 ms, 31.1 s |
| pachy<fraptor | 100%, 771 ms, 13.8 s → 100%, 634 ms, 12.2 s | 0%, 0 ms, -1.0 s → 91%, 636 ms, 13.5 s |
| stego<fraptor3 | 75%, 634 ms, 46.8 s → 76%, 640 ms, 48.8 s | 0%, 0 ms, -1.0 s → 69%, 664 ms, -1.0 s |

By setting (all pairings), before → after:

| style | fps | latency | landed | dropped clicks | press>hit |
|---|---|---|---|---|---|
| press | 30 | 0 ms | 88% → 93% | 509 of 3337 → 543 of 2721 | 665 ms → 575 ms |
| press | 30 | 80 ms | 83% → 91% | 553 of 3118 → 538 of 2714 | 753 ms → 652 ms |
| press | 60 | 0 ms | 88% → 94% | 535 of 3363 → 479 of 2695 | 666 ms → 573 ms |
| press | 60 | 80 ms | 84% → 91% | 494 of 3261 → 514 of 2751 | 702 ms → 626 ms |
| stand | 30 | 0 ms | 56% → 92% | 377 of 3494 → 326 of 2101 | 702 ms → 634 ms |
| stand | 30 | 80 ms | 66% → 91% | 465 of 2784 → 330 of 2079 | 739 ms → 674 ms |
| stand | 60 | 0 ms | 58% → 89% | 401 of 3335 → 333 of 2236 | 698 ms → 616 ms |
| stand | 60 | 80 ms | 68% → 90% | 372 of 2962 → 316 of 2126 | 724 ms → 635 ms |

`make test`'s quick budget (`tests/combatprobe_budget.txt`, not rewritten here): `trex>raptor` (83.3%) and `trex>pachy` (100%) now fail, since both throw no attacks: the prey reach dry ground and outrun the tyrannosaur (above), and `--write` leaves pairings without attacks out. Every other line passes and moves up: `raptor>pachy` 95 → 97%, 583 → 451 ms; `raptor4>stego` 83 → 90%, 586 → 471 ms; `trex>stego` 89 → 100%; `raptor=raptor` 46 → 100%, 583 → 462 ms; `pachy=pachy` 91 → 97%, 767 → 635 ms; `trex=trex` 78 → 96%; `trex>sauro` and `stego<trex` unchanged. New lines `--write` would add: `raptor>galli` 80% 457 ms, `stego<raptor3` 33% 608 ms, `pachy<fraptor` 96% 634 ms, `stego<fraptor3` 75% 646 ms.

Bots against bots (`balance --seeds 48 --minutes 60`, 16 agents, without the prop assets), kills by row against column, two sets of seeds (1-48 / `--first-seed 1000`) before → after. The two sets of 5.0 differ by up to 30% in a cell, which is the run-to-run spread:

| killer \ victim | raptor | stego | trex | pachy | sauro | galli |
|---|---|---|---|---|---|---|
| raptor | 3/2 → 1/3 | 0/0 → 0/0 | 0/0 → 0/0 | 60/75 → 58/51 | 0/0 → 0/0 | 57/50 → 61/73 |
| stego | 20/25 → 30/34 | 0/0 → 0/0 | 2/3 → 3/0 | 2/0 → 0/3 | 0/0 → 0/0 | 0/0 → 0/0 |
| trex | 2/9 → 6/4 | 113/115 → 113/105 | 0/0 → 0/0 | 1/10 → 1/3 | 52/46 → 36/61 | 1/1 → 2/4 |
| pachy | 21/23 → 19/18 | 0/0 → 0/0 | 0/0 → 0/0 | 0/0 → 0/0 | 0/0 → 0/0 | 0/0 → 0/0 |
| sauro | 9/10 → 8/11 | 0/0 → 0/1 | 1/3 → 2/1 | 0/0 → 0/0 | 0/0 → 0/0 | 0/0 → 0/0 |
| galli | 0/0 → 0/0 | 0/0 → 0/0 | 0/0 → 0/0 | 0/0 → 0/0 | 0/0 → 0/0 | 0/0 → 0/0 |
| env | 165/122 → 148/123 | 7/5 → 14/2 | 9/5 → 11/5 | 44/15 → 19/11 | 4/5 → 4/2 | 13/14 → 25/4 |
| deaths | 220/191 → 212/193 | 120/120 → 127/108 | 12/11 → 16/6 | 107/100 → 78/68 | 56/51 → 40/63 | 71/65 → 88/81 |

Deaths per species and who beats whom stay within the spread, with two exceptions that follow from the flight on dry ground: gallimimus deaths rise 71/65 → 88/81 (raptors catch them on land: 57/50 → 61/73), and pachycephalosaur deaths fall 107/100 → 78/68, nearly all of it "env" (44/15 → 19/11: fewer run into deep water). Defenders do not get the raptors down more: pachycephalosaur on raptor 21/23 → 19/18, stegosaur on raptor 20/25 → 30/34, raptor deaths 220/191 → 212/193.

The ecosystem (`ecoprobe --only world --days 2 --seeds 3`, the island with 48 animals and the director, without the prop assets), deaths per animal-day by cause, seeds 1-3 / 4-6 (`--seed0 3`) before → after:

| species | deaths/day | predation | starved | thirst |
|---|---|---|---|---|
| raptor | 0.81/0.22 → 0.31/0.19 | 0.79/0.21 → 0.22/0.18 | 0.00/0.00 → 0.00/0.00 | 0.03/0.01 → 0.09/0.01 |
| stego | 0.26/0.22 → 0.19/0.27 | 0.26/0.20 → 0.19/0.19 | 0.00/0.00 → 0.00/0.00 | 0.00/0.00 → 0.00/0.09 |
| trex | 0.06/0.20 → 0.14/0.00 | 0.06/0.20 → 0.14/0.00 | 0.00/0.00 → 0.00/0.00 | 0.00/0.00 → 0.00/0.00 |
| pachy | 0.15/0.12 → 0.29/0.20 | 0.07/0.07 → 0.19/0.19 | 0.00/0.05 → 0.00/0.00 | 0.08/0.00 → 0.10/0.00 |
| sauro | 0.20/0.14 → 0.03/0.08 | 0.17/0.03 → 0.03/0.08 | 0.00/0.00 → 0.00/0.00 | 0.03/0.11 → 0.00/0.00 |
| galli | 0.06/0.06 → 0.11/0.08 | 0.06/0.06 → 0.08/0.08 | 0.00/0.00 → 0.00/0.00 | 0.00/0.00 → 0.03/0.00 |

| predator | hunts/day | success % | kills/day |
|---|---|---|---|
| raptor | 1.43/0.59 → 1.39/1.19 | 4/24 → 25/34 | 0.06/0.08 → 0.14/0.18 |
| trex | 5.54/5.52 → 5.17/3.56 | 24/9 → 13/22 | 1.15/0.87 → 0.82/0.78 |

The two sets of seeds of 5.0 differ as much as before and after do (raptor deaths 0.81 vs 0.22), so only the raptors' hunting moves for sure: success 4%/24% → 25%/34%, kills 0.06/0.08 → 0.14/0.18 a day, as prey no longer escape into water. The pachycephalosaur's predation (0.07/0.07 → 0.19/0.19) is the number to watch with more days and seeds; the balance runs show no rise in raptor kills of pachycephalosaurs. Populations hold (the director keeps them): mean numbers alive are within 0.1 of 5.0 for every species but the tyrannosaur, 2.9/2.5 → 2.5/3.0. `ecoprobe --quick` against `tests/ecoprobe_budget.txt` fails 21 of 146 lines without the props both before and after, five different ones each way (before only: `budget.galli.rest`, `deaths.raptor.starved_share`, `deaths.sauro.predation_share`, `person.pachy.encounters`, `person.raptor.blows`; after only: `budget.sauro.travel`, `budget.trex.flee`, `hunt.raptor.per_day`, `range.pachy.cells`, `range.sauro.cells`), so it needs the owner's run with the real props.

### `tests/test_dino_ik.c`: foot IK on synthetic ground

The foot IK lives in `render/dino_ik.c` (no GL; `render/dino.c` includes it), so this test runs it on the test dinosaurs in `tests/fixtures` (below) over a stub `cret_ground_at` and no world. `make test` runs it with its table sent to `/dev/null`; run `./build/cretaceous/test/test_dino_ik` from the repository root to see the table. `make asan` runs it under ASan and UBSan.

Grounds: flat; slopes of 15, 30 and 45 degrees up the body (the body pitched by 0.4 of the slope), 15 down, 30 and 45 across (rolled by 0.15), 30 diagonal; a 15 cm step under the front feet; a 25 cm rock top under one foot, and a 70 cm one, taller than half the hip height, which no foot should stand on. Three passes on each: the idle pose held until the smoothed pelvis settles (2 s at 60 fps), every walk frame held the same way, and the walk played for two cycles. A juvenile (growth scale 0.5) also stands and walks on 30 degrees.

Checks: a foot whose target is in reach ends at its animated height over the ground under it (within 5 mm, and not moved sideways); a foot is out of reach when its leg is straight or the IK's own limits on how far a foot moves against the body apply (-0.3 to 0.75 hip heights). The knee keeps the leg's animated bend plane: the plane may tilt only as far as moving the foot out of it forces (asin of the plane normal against the new hip-ankle line), with no twist about the leg. Settled, the pelvis drops no further than the highest foot can fold (0.55 hip heights, within the pelvis limits). Planted feet turn toward the ground normal by the IK's amount (the angle, at most 0.4 rad, by how planted the foot is). With the weight at 0 the legs are untouched and only the pelvis drop eases out. Nothing is NaN, also with the ground 2 m and 50 m above or below the feet.

Results (364 checks): reachable feet end 0.00 mm off everywhere. Feet out of reach, counted over the settled walk poses (a leg in a frame): none on flat ground, 15 degrees up or down, 30 across, the step and the rocks; for the biped 2 of 38 on 30 degrees up and 4 on 45 up or 30 diagonal; for the quadruped 39 of 124 on 30 up, 62 on 45 up, 55 on 45 across and 33 on 30 diagonal. Knees at most 0.01 degrees beyond what the foot's move forces, and feet tilt within 0.06 degrees of the IK's amount. Walking, the smoothed pelvis (12/s) lags up to 0.11 hip heights below the fold limit, so the highest foot folds that much further for a moment (reported, not failed).

Two IK faults the test found, both fixed in `dino_foot_ik`:
- **Knees twisted about the leg on cross slopes.** The knee's side was the thigh projected onto the new hip-ankle line. On a folded leg that turns a small sideways move of the foot into a large twist: the biped on 30 and 45 degrees across twisted 4 and 7 degrees standing and 11 and 35 walking, the quadruped 4 and 7 standing and 115 and 128 walking (its knees flipped to the other side as the feet passed under the body). Now the knee keeps the animated bend plane's axis (its normal) and sits in the plane through the new hip-ankle line closest to it, on the animated side; a straight animated leg, which has no plane, falls back to the old rule. On flat ground and straight up or down a slope (the foot stays in its plane) the result is the same as before.
- **A foot in its swing stepped onto tall props.** The prop probe started half a hip height above the foot, so a lifted foot reached tops it would never stand on: walking past the 70 cm rock, the quadruped's front foot 25 cm up landed on its top and the pelvis rose 11 cm in four frames. The probe now starts half a hip height above the animation's ground under the foot, so planted feet see exactly what they saw before and swinging feet see the same.

### `tools/animcheck.c`: the dinosaur clips, headless

```bash
make -C ocean/cretaceous animcheck
./build/cretaceous/animcheck                                   # every species' model in ocean/cretaceous/assets/dinos
./build/cretaceous/animcheck --check ocean/cretaceous/assets/dinos/galli.dino   # exit 1 on a broken limit
./build/cretaceous/animcheck --species raptor FILE.dino        # a file not named after its species
./build/cretaceous/animcheck --limit slide=15 --check          # loosen a limit (no argument lists them)
```

It reads `.dino` files without GL (`tools/dino_file.h`, the converter's layout with the game loader's checks) and prints, per file, the species, joints, length against the sim's, biped or quadruped and the IK legs as the game finds them, and the feet it measures with: joints named toe, foot or metatars, as `tools/dino_convert.py` finds them, or the IK ankles when none is named so (the sauropelta). Then one row per clip:

| Column | What |
|---|---|
| speed, feet | The stored speed the game plays the clip at, and the speed measured from the planted feet (the median). |
| slide | Mean slip of the planted foot against the ground, which runs back at the stored speed: % of it for gait clips, cm/s for clips that stand. The planted foot is the lowest named foot each frame (the converter's rule), counted only while it and the frames either side are within 2% of the height of the lowest foot (lift-off, touch-down and a trot's flight are not stance). |
| below, hover | The lowest skinned LOD0 vertex: under the ground plane (y = 0, the idle pose's), and over it while a foot is planted. |
| seam | Looping clips: the largest local rotation jump (degrees) and joint displacement (cm) from the last frame to the first. The game loops over frames 0 to n-1, so the two must match. |
| drift | The largest change of a bone's length (joint to parent) against the rest pose, %: translation or scale keys in the wrong units, or a retarget that stretches. |
| step, flips | The largest local rotation change between two frames (a hemisphere jump pops the joint), and quaternion sign flips (harmless: the blend aligns signs; for the record). |

Then one row per sim gait of the species (walk, trot, sprint, adult): the clips `animate` blends there and their weights, the standing pose's share (a model with one gait clip and no idle shortens its stride below the clip's speed by blending toward a stand), the playback rate (the rate it wanted, when the game clamps it to 0.55-2.4, or 0.4-4.5 with one gait), and the planted-foot slip and the share of the cycle with no foot down, measured on the blended pose itself (a port of `pose_add`, `pose_stand` and `stance_frame`) played at that rate over ground running back at the sim's speed.

Limits (`--check` fails on any): gait-clip slide 10%, idle slide 5 cm/s, feet against stored speed 10% apart, below the ground 2% of the height, seam 3 degrees or 1% of the height, drift 5%, step 30 degrees in a frame, slip at a sim gait 10%. Each failure is printed with what it means on screen.

On the test dinosaurs (`make test` requires the clean ones to pass and the sliding ones to fail):

| File | Clip: slide, feet / stored m/s | Slip at the sim's walk / trot / sprint | Result |
|---|---|---|---|
| `fixture_biped` (raptor) | walk 0.8%, 2.20 / 2.20; trot 2.8%, 4.60 / 4.60 | 0.7% / 3.1% / 3.1% | passes |
| `fixture_quad` (sauropelta) | walk 2.6%, 1.30 / 1.30 | 2.6% / 2.6% / 2.6% | passes |
| `fixture_biped_slide` | walk 34.8%, 1.98 / 2.13 | 34.8% at each | fails: slide, gait slip |
| `fixture_quad_slide` | walk 38.9%, 1.02 / 1.47 | 37.8% / 39.0% / 39.0% | fails: slide, feet, gait slip |

All four have seams of 0.04 degrees, nothing below the ground and steps of at most 21.9 degrees.

Two scratch models (not committed) checked the converter and the single-gait case:
- A walk with root motion at 2.2 m/s was stored at 4.40 m/s: `dino_convert.py` added the root's drift to the feet's speed, which already is the ground speed once the drift is removed, so the game played such clips at half rate and their feet slid at half the ground speed. Fixed (the feet's speed is used; the drift only when no foot joint is found): stored at 2.20 m/s. Models converted before keep their old speeds until converted again.
- A gallimimus-sized model whose only clip is a 9 m/s run (no idle, as the real gallimimus) slips 73.5% at the sim's walk and 13.7% at its trot (4.6% at the sprint). `animate` ramps the walk-to-stand weight against the run clip's own speed, so at 2.2 m/s only 12% of the run reaches the pose while the playback rate assumes 30%. Letting the rate count that weight brings the walk to 43%, still far off: blending a run toward a stand does not make a walk. Not changed; see the predictions.

#### The real models (run on the owner's machine)

There are no dinosaur models in the cloud sandbox. With the models in `assets/dinos`, from the repository root: `make -C ocean/cretaceous animcheck && ./build/cretaceous/animcheck`. What the README, `tools/fetch_dinos.sh` and the converter predict:
- **Gallimimus** (one run clip; idle, eat, sleep and the rest retargeted from the raptor): fails the slip at the sim's walk, and probably at the trot, as the scratch model above. The fix is a walk clip: add `walk,trot` to the gallimimus `retarget` list in `tools/fetch_dinos.sh` (as the tyrannosaur's eat and sleep are copied from the raptor) and run animcheck again. The retargeted clips may also show drift (bone lengths copied from the raptor) and `below` while lying.
- **Sauropelta** (one walk clip, generic bone names; feet by the IK ankles, the same joints as its `--feet` regex): passes the gait slip unless its walk is faster than 2.9 m/s (the sim's walk is 1.3). Look at its seam (the single "ArmatureAction" is looped because it is named walk) and the rate at the sprint, up to 2.4 times the clip's speed.
- **Raptor and stegosaur**: their speeds are set by hand (`--speed walk=1.8,trot=3.6,run=6.0`; stego `walk=2.0,run=4.2`), not measured. If `feet` disagrees with them the clips slide; if the clips carry root motion, the hand speeds may have been set against the doubled measurement. Convert again without `--speed` (the fixed converter) and compare.
- **Tyrannosaur**: walk and run measured from its metatarsus joints; its eat, drink, call, sleep, hurt and death are the raptor's, retargeted: check those rows for drift, `below` and seams.
- **Pachycephalosaur**: the header line says whether its feet are found by name; if they fall back to the IK ankles, the converter measured no foot speed and stored the root's drift.
- **Every species**: death, sleep and knocked-out clips may fail `below` (a flank under the ground plane while lying); `flips` are harmless.

To look at a failure: `./build/cretaceous/play play gallery`, Tab to the dinosaurs row, C / V to the clip, Z for slow motion, K for the skeleton. For the IK on real ground: `./build/cretaceous/play capture build/cretaceous/shots --shot 'dino_*'` (`dino_stego_walk`, `dino_raptor_run`, `dino_herd` and `dino_chase` cross slopes; the knees should no longer twist when an animal walks across a hillside), and `./build/cretaceous/play play scenario knockdown` for the roll (the IK fades out while the animal lies).

### Test dinosaurs: `tests/fixtures`, `tools/blender/make_fixture.py`

```bash
blender -b --factory-startup --disable-autoexec --python-exit-code 1 --python ocean/cretaceous/tools/blender/make_fixture.py
```

Builds, exports as glTF and converts with `tools/dino_convert.py` (`--tex 256`): a raptor-sized biped (3.0 m, the raptor's leg names, 180 triangles) and a sauropelta-sized quadruped (5.2 m, the stegosaur's leg names, 216 triangles), a box per bone. Clips are in place, posed by an exact two-bone solve per frame, at the sim's speeds after the converter's scaling: `fixture_biped` has idle, walk (2.2 m/s) and trot (4.6 m/s), `fixture_quad` idle and walk (1.3 m/s); `fixture_biped_slide` and `fixture_quad_slide` have only a walk whose planted feet slip (their speed swings 60% either side of the gait speed over the stance and they skid 4 cm sideways). The four files are 197.8 kB and rebuild bit for bit (Blender 4.2.3). The glTF files and textures go to `build/cretaceous/fixtures`.

### `tools/fightprobe.c`: staged bot fights (balance)

Not part of `make test`. Per pairing (a raptor, three raptors or a tyrannosaur against each herbivore, a tyrannosaur against two stegosaurs) it builds a scenario in memory, runs N fights with the game's bots on both sides (or `--hostile` attackers that press home and never break off) and prints each side's share of wins, the standard error of the attackers' share, the mean fight length and the knockdowns each side gave per fight. `--first SEED` runs another set of seeds, for the run-to-run spread. Unlike `combatprobe` nobody plays: it measures balance, not feel. It was `tools/combatprobe.c` on the old cret/knockdown branch, renamed because 5.0 has its own combatprobe. Results: Knockdown balance, below.
### `tools/lookcheck.py`: look regressions against approved stills

```
python3 -I ocean/cretaceous/tools/lookcheck.py run --ref REFDIR --out NEWDIR --bin BUILD/cretaceous   # render, then compare
python3 -I ocean/cretaceous/tools/lookcheck.py approve NEWDIR REFDIR                                   # accept the new look
```

Renders 20 fixed review stills at 640x360: the built-in shots (third person, herd, tyrannosaur, ridge, river, coast, canopy, aerial, sunset, dusk, morning, night, rain, ground, river mouth, steep creek) and four fixed cameras (the redwood grove, a forest sky gap, the gorge from its floor and from above). It then compares each with the approved reference and prints, worst first: the share of changed pixels (a channel moved more than 6/255), the SSIM of the luma, the shift in mean brightness, warmth (red minus blue) and saturation, and which third of the frame changed. A sheet (`NEWDIR/lookcheck_sheet.jpg`) shows reference, new and an amplified difference for each changed shot. Run from the repository root; it takes about 30 seconds on WSL's d3d12 driver. Needs numpy and Pillow.

Captures are deterministic: two renders of one commit on one machine and driver are bit for bit the same, so every changed pixel comes from the change under test. References therefore belong to one machine and driver; keep them outside the repository. Use it after a render or shader change, to see what else moved besides the shot you were looking at: approve when every change is intended.

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
| `hunt` | Probe: `brawl`, but only at animals worth at least a quarter of a kill to it (`cret_reward_worth`) |
| `swim` | Probe: walks into deep water and floats |
| `net:FILE[:HIDDEN:LAYERS]` | Trained weights through PufferNet (`src/puffercpu.c`, the same forward pass as `./puffer eval`). Linear encoder only. See below. |
| `cpu:FILE[:NET:HIDDEN:LAYERS]` | Trained weights through `policy_cpu.c`, either encoder (`cretaceous_net` 0 or 1); the sizes are found from the file unless named. About 18x faster than `net:` (docs/encoder.md, "CPU inference"). `cpu-argmax:FILE` takes the most likely action instead of sampling |

Output is Markdown:
- **Per species:** policy, slots, mean life in seconds, the share of slot-episodes with no death (the trainer's `perf`), and deaths, kills, food and water (stomachs), growth at the end of each life, damage dealt and taken (fractions of max health) and return, all per slot-episode.
- **Kill matrix:** the victim against the credited killer (`cret_killer`: the last attacker, if it hit within 10 s of the death), with death causes:
  - attack: a hit within 3 s;
  - starved, thirst, bleeding;
  - fall or other.
- **Reward terms:** with `--audit` or a custom run.

The run is deterministic for a given seed. `make test` runs `cret_eval --smoke`: one short episode with every policy type, including a `net:` slot (carnivores) and a `cpu:` slot (sauropeltas) on all-zero weights of the current contract's size.

### Plugging in a trained policy

`net:FILE` loads a checkpoint written by the trainer (`checkpoints/cretaceous/<run>/<step>.bin`). The size comes from `[policy] hidden_size` and `num_layers`, or from `net:FILE:HIDDEN:LAYERS`.
- The file must hold exactly the weights for `CRET_OBS_SIZE` inputs and the action heads, or the eval stops and names both counts. Checkpoints from an older contract are refused: the ones in `checkpoints/cretaceous/` from 2026-11-07 hold 160 or 176 inputs plus actions, not contract 3's 197 + 19.
- Actions are sampled, as in the trainer.
- Every agent goes through one batched net, with recurrent state per slot, reset at each death and episode start. Only the slots assigned to the net use its actions.
- `cpu:FILE` runs the same checkpoint through `policy_cpu.c`, which also loads structured-encoder checkpoints (`cretaceous_net = 1`); it steps only its own slots and draws actions from its own stream. A policy spec is cut at 95 characters, so keep checkpoint paths short.

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

`make eval EVAL_ARGS="--audit --episodes 6"` runs the three baselines and four probes, and prints every reward term per species.
The probes are:
- bots with herbivores set to `brawl`;
- bots with carnivores set to `brawl`;
- everyone set to `swim`;
- bots with carnivores set to `hunt`: `brawl`, but only at animals worth at least a quarter of a kill to it (`cret_reward_worth`), fresh spawns included. It is the strongest farming policy the probes have.

Weights are from `config/cretaceous.ini`: alive 0.001 per tick, food 1, water 1, damage dealt 0.5, damage taken 1, kill 1, death 1, growth 2, quest 0.25.
Dealt and kill are scaled by what the victim is worth (see Reward design below).

The numbers below are from the cloud sandbox on 2026-11-14, without the prop assets (no Blender to convert them), so they differ from the Baselines tables above.
Before and after are both from that sandbox: 8 seeds (`--seed 1` to `8`) of 6 episodes each, with the seed-to-seed spread (one standard deviation over the 8 seeds) after the ±.

### Reward design

What each term pays for now, and why:
- **Alive** (0.001 a tick): a constant 18 per slot-episode. It carries almost no signal (finding 2); read `perf`, not the return.
- **Food and water** (1 per stomach gained): the real feeding signal. A carnivore's meal pays here, not in the kill term, so a kill it does not eat pays little.
- **Damage taken** (−1 per own max health) and **death** (−1): the cost of fighting and of dying.
- **Growth** (2 per unit): pays for time alive and fed (finding 3).
- **Quests** (0.25 each).
- **Damage dealt** (0.5 per victim max health) and **kills** (1 each) are paid by `cret_reward_worth` (`sim.c`), as `ev_dealt_paid` and `ev_kills_paid` beside the raw `ev_dealt` and `ev_kills`:
  - A carnivore earns the victim's mass over its own, at most 1, for both the damage and the kill. The kill is the food a hunter is after, so a tyrannosaur killing an 80 kg raptor earns 0.01 of a kill, a sauropelta 0.19 and a stegosaur 0.63; a raptor earns the full weight on anything its size or bigger.
  - A herbivore never earns a kill reward. Its damage pays at full weight only while it defends: the victim is a carnivore that struck it, or a member of its group, in the last 10 s (`CRET_DEFEND_TICKS`). A tail swing at a passing herbivore, or at a carnivore that did nothing, pays nothing.
  - A sauropelta's spikes, and the reflected part of a hunkered bite (abilities.c), pay the defender nothing, even when the biter dies of them. A policy should not learn to seek out bites.
  - Group members are never hit (`resolve_attack`), as before.
  - A kill is credited only if the last hit landed within 10 s of the death (`CRET_KILL_CREDIT_TICKS`, `cret_killer`). After that the death credits nobody: no kill event, no `stats.kills`, no quest progress, and its cause is not predation.
- The binding's logs (`kills`, `damage_dealt`) still count every hit and kill, paid or not. `tests/reward_terms.h` mirrors `puf_step`, and `test_binding` checks every tick that the terms sum to the reward.
- No spawn protection was needed: the brawl and hunt probes show that farming fresh spawns does not pay (below).

### Results after the fix

Probes: everyone else is a bot. Returns per slot-episode, mean ± spread over 8 seeds:

| species | as bot | brawling, before | brawling, after | hunting, after | kills / episode (after) | deaths / episode (after) | kill term (before → after) | dealt term (before → after) |
|---|---|---|---|---|---|---|---|---|
| Velociraptor | +19.50 ± 0.30 | -6.86 ± 3.65 | **-7.05 ± 4.84** | +5.38 ± 2.37 | 3.5 | 16.3 | +3.85 → +2.93 | +2.02 → +1.80 |
| Stegosaurus | +19.01 ± 0.22 | +31.34 ± 2.85 | **+13.59 ± 0.60** | | 11.8 | 2.2 | +11.89 → +0.00 | +5.58 → +0.01 |
| Tyrannosaurus | +18.92 ± 0.19 | +64.35 ± 11.94 | **+16.59 ± 0.48** | +11.24 ± 1.59 | 30.6 | 1.8 | +33.85 → +1.25 | +16.30 → +0.69 |
| Pachycephalosaurus | +19.66 ± 0.28 | +10.85 ± 3.82 | **-3.92 ± 2.89** | | 8.6 | 11.9 | +9.20 → +0.00 | +4.93 → +0.02 |
| Sauropelta | +19.41 ± 0.26 | +32.15 ± 3.55 | **+12.53 ± 0.63** | | 15.4 | 2.7 | +13.55 → +0.00 | +6.07 → +0.01 |
| Gallimimus | +19.83 ± 0.30 | -16.58 ± 8.12 | **-24.83 ± 6.21** | | 4.6 | 23.3 | +4.97 → +0.00 | +2.57 → +0.10 |

- No probe beats the bots for any species, by at least 2.3 (tyrannosaur brawling: 16.59 ± 0.48 against 18.92 ± 0.19). Before, stegosaur, tyrannosaur and sauropelta brawlers beat the bots by 65% to 230%.
- The tyrannosaur brawler still kills 30.6 animals an episode, mostly respawning raptors, but they pay 1.25 in all. Hunting only prey worth a quarter kill or more pays even less (+11.24): the stegosaurs and sauropeltas fight back.
- Bots defending against brawling carnivores still earn dealt reward: stegosaurs +2.34 and sauropeltas +2.42 per slot-episode.

Reward terms per slot-episode after the fix (bot, random, noop), with the total before:

| species | policy | alive | food | water | dealt | taken | kill | death | growth | quest | total | before |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Velociraptor | bot | +18.000 | +0.476 | +0.586 | +0.073 | -0.316 | +0.138 | -0.142 | +0.420 | +0.263 | +19.50 ± 0.30 | +19.33 ± 0.37 |
| Stegosaurus | bot | +18.000 | +0.582 | +0.187 | +0.012 | -0.152 | +0.000 | -0.132 | +0.463 | +0.045 | +19.01 ± 0.22 | +19.19 ± 0.37 |
| Tyrannosaurus | bot | +18.000 | +0.150 | +0.146 | +0.137 | -0.230 | +0.230 | +0.000 | +0.417 | +0.068 | +18.92 ± 0.19 | +19.52 ± 0.38 |
| Pachycephalosaurus | bot | +18.000 | +1.073 | +0.474 | +0.050 | -0.211 | +0.000 | -0.167 | +0.381 | +0.064 | +19.66 ± 0.28 | +19.68 ± 0.35 |
| Sauropelta | bot | +18.000 | +0.716 | +0.320 | +0.007 | -0.071 | +0.000 | -0.031 | +0.428 | +0.044 | +19.41 ± 0.26 | +19.46 ± 0.18 |
| Gallimimus | bot | +18.000 | +1.153 | +0.493 | +0.001 | -0.156 | +0.000 | -0.146 | +0.421 | +0.062 | +19.83 ± 0.30 | +19.82 ± 0.33 |
| Velociraptor | random | +17.999 | +0.000 | +0.005 | +0.013 | -1.091 | +0.012 | -1.071 | +0.378 | +0.013 | +16.26 ± 0.10 | +16.32 ± 0.10 |
| Stegosaurus | random | +18.000 | +0.034 | +0.002 | +0.019 | -0.201 | +0.000 | -0.063 | +0.295 | +0.023 | +18.11 ± 0.14 | +19.10 ± 0.30 |
| Tyrannosaurus | random | +18.000 | +0.006 | +0.010 | +0.023 | -0.064 | +0.037 | -0.021 | +0.373 | +0.000 | +18.36 ± 0.15 | +18.78 ± 0.42 |
| Pachycephalosaurus | random | +17.999 | +0.032 | +0.001 | +0.012 | -1.055 | +0.000 | -0.944 | +0.336 | +0.023 | +16.40 ± 0.12 | +16.66 ± 0.23 |
| Sauropelta | random | +18.000 | +0.056 | +0.001 | +0.006 | -0.358 | +0.000 | -0.136 | +0.293 | +0.005 | +17.87 ± 0.15 | +18.20 ± 0.15 |
| Gallimimus | random | +17.999 | +0.027 | +0.002 | +0.001 | -1.307 | +0.000 | -1.198 | +0.363 | +0.024 | +15.91 ± 0.24 | +16.14 ± 0.28 |
| Velociraptor | noop | +17.999 | +0.000 | +0.000 | +0.000 | -0.879 | +0.000 | -0.708 | +0.356 | +0.019 | +16.79 ± 0.13 | +16.79 ± 0.13 |
| Stegosaurus | noop | +18.000 | +0.000 | +0.000 | +0.000 | -0.003 | +0.000 | +0.000 | +0.352 | +0.009 | +18.36 ± 0.06 | +18.36 ± 0.06 |
| Tyrannosaurus | noop | +18.000 | +0.000 | +0.000 | +0.000 | +0.000 | +0.000 | +0.000 | +0.403 | +0.000 | +18.40 ± 0.06 | +18.40 ± 0.06 |
| Pachycephalosaurus | noop | +18.000 | +0.000 | +0.000 | +0.000 | -0.352 | +0.000 | -0.167 | +0.298 | +0.012 | +17.79 ± 0.25 | +17.79 ± 0.25 |
| Sauropelta | noop | +18.000 | +0.000 | +0.000 | +0.000 | -0.111 | +0.000 | -0.021 | +0.340 | +0.016 | +18.22 ± 0.14 | +18.22 ± 0.14 |
| Gallimimus | noop | +18.000 | +0.000 | +0.000 | +0.000 | -0.407 | +0.000 | -0.188 | +0.290 | +0.011 | +17.71 ± 0.23 | +17.71 ± 0.23 |

- Return without the kill and dealt terms is within the seed spread of before for every species, for bot and random alike (largest change: bot raptor +0.17 against a spread of 0.31). Honest play earns what it did; only the combat terms moved.
- All bots: 19.46 ± 0.14 before, 19.43 ± 0.15 after. The bot tyrannosaur loses 0.60 (kill 0.62 → 0.23, dealt 0.33 → 0.14): its kills are mostly sauropeltas, worth 0.19 to it. Random stegosaurs lose 0.99, the kills and damage of their random tail swings.
- Swimming earns less than standing still for every species (all: 17.41 ± 0.15 against 17.67 ± 0.08; the closest, tyrannosaur, 18.37 ± 0.09 against 18.40 ± 0.06). Swim and noop runs are unchanged by the fix, since nobody fights in them.

### Findings

1. **Killing paid far more than surviving, for herbivores too.** *Fixed.*
   - Was: kill (+1) and damage dealt (+0.5 per victim max health) did not depend on diet, target or victim size. A stegosaur or sauropelta that tail-swung whatever was nearest earned +31 to +32 against +19.5 as a bot; a brawling tyrannosaur +64 (34 kills an episode, mostly respawning raptors).
   - Now: kills and damage pay by `cret_reward_worth` (Reward design). The brawlers earn +13.6 (stegosaur), +12.5 (sauropelta) and +16.6 (tyrannosaur), all under the bots.
   - Respawns are not a farm: the tyrannosaur brawler still kills 30.6 animals an episode but is paid 1.25 for them, and the hunt probe (only prey worth a quarter kill or more) earns +11.2. No spawn protection was added.
2. **The alive term is about 92% of the return and carries almost no signal.** Unchanged.
   - It is +18.0 per slot-episode for bots, random and noop alike. With `respawn_ticks = 1`, a death costs one tick of it.
   - Its only effect is the value lost at a death terminal: about 0.001 / (1 − 0.995) = 0.2, on top of the −1 death penalty.
   - Harmless for PPO advantages, but it makes `episode_return` and `score` useless as progress metrics; use `perf`.
3. **Growth pays for time alive, not for eating.** Unchanged.
   - Bots earn +0.42 per slot-episode and noop +0.34. Growth accrues whenever food and water are above 0.25, and spawn stomachs start at 0.75 to 1.
   - Within a 600 s episode, growth's reward weight 2 never ties to feeding. Food and water (+1 per stomach) are the real feeding signal.
4. **Kill credit never expired (`last_attacker`).** *Fixed.*
   - Was: 15 of 30 kills in the bot and random runs went to an attacker whose last hit was over 30 s before the death, most of them animals dying of thirst or starvation.
   - Now: `CretAgent.last_hit_tick` records the last hit next to `last_attacker` (bites, spikes, reflected bites, crushing props), and `kill()` credits `cret_killer()`: the last attacker within 10 s, or later while the wound is still open (bleed at least `CRET_WOUND_OPEN`, 0.2% of max health a second: the line below which an animal starts to heal). `tests/test_sim.c` `test_kill_credit` checks inside the window, past it with the wound closed, and past it still bleeding.
   - Effect: in the bot and random runs, 14 to 31 kills per seed are credited and 10 to 18 deaths with an attacker on record now credit nobody. Most of those are animals that bled out 10 to 60 s after the last bite (in a 4-seed bot sample, 12 of 60 deaths with an attacker were bleed-outs in that range, 3 were thirst or starvation over 60 s later). With the window alone they were no longer predation, and `ecoprobe --quick` read 0% predation for raptors, stegosaurs and pachycephalosaurs; on merge the credit was extended to open wounds, so a bleed-out stays its biter's kill and only deaths after the wound has closed credit nobody.
5. **Sauropelta's spikes paid it for being bitten.** *Fixed.*
   - Was: thorn damage counted as damage dealt by the sauropelta, and a biter that died of it was the sauropelta's kill (11 kills an episode against brawling raptors).
   - Now: the spikes and a hunkered animal's reflected bites are still counted (`ev_dealt`, `stats.kills`) but never paid. `test_reward_worth` checks it, including a raptor dying on the spikes.
6. **Not exploitable as tested.**
   - Idling in deep water: attacks need the attacker out of the water, but nobody can drink while swimming, so floaters die of thirst like standing animals (return 17.41 against 17.67 standing still).
   - Farming group members: in-group hits are skipped in `resolve_attack`.
   - Cannibalism outside a group still pays a carnivore its full mass ratio (a raptor killing a raptor earns a full kill). The raptor brawler, which does this most, earns −7.
7. **Discount horizon against survival time scales.** `gamma = 0.995` is a horizon of about 200 ticks (6.7 s), while thirst kills in 450 to 600 s and hunger in 675 s or more.
   - The direct signals are the per-stomach food and water rewards.
   - Before an animal is parched, its value function cannot see a death from thirst coming.
   - Worth knowing when reading `perf` curves.

## Knockdown balance (`tools/fightprobe.c`, `tools/balance.c`, `tools/combatprobe.c`)

Measured on 5.0 at 943efa430 (before) and on cret/knockdown2 (after), with the same tools; the only difference between the two builds is the knockdown change.

`fightprobe` stages bot-against-bot fights on open ground (README, Headless tools): 400 fights per pairing with seeds 1-400 and 400 more with seeds 1001-1400, the attackers 25-35 m off at a random bearing, at most 90 s. The two seed sets of one build differ by at most 3.5 points and usually by under 1 (the run-to-run spread); a single share's standard error is at most 2.5 points.
```
make -C ocean/cretaceous fightprobe
./build/cretaceous/fightprobe --trials 400 [--first 1001] [--hostile]
```

Game AI (the bots decide whether to hunt, fight or run). Attackers' share of fights won, seeds 1-400 / 1001-1400:

| Pairing | 5.0 | knockdowns | knockdowns per fight (att / def) | fight length s, 5.0 / knockdowns |
|---|---|---|---|---|
| 3 raptors v pachycephalosaur | 53.8 / 53.3 | 35.0 / 38.5 | 0 / 3.4 | 59 / 73 |
| 3 raptors v gallimimus | 62.8 / 64.0 | 62.8 / 64.0 | 0 / 0 | 47 / 47 |
| every other pairing | 0-0.5 | the same | under 0.01 | |

With the ecosystem AI on 5.0 a lone raptor or a tyrannosaur rarely starts a fight within 90 s (the game AI weighs the hunt first), so most pairings are draws either way.

Hostile attackers (`--hostile`: they press home on the first defender and never break off). Attackers' share won (defenders' share in brackets where it is not 0):

| Pairing | 5.0 | knockdowns | knockdowns per fight (att / def) |
|---|---|---|---|
| raptor v pachycephalosaur, stegosaur or sauropelta | 0 (100) | 0 (100) | 0 / 1.0-1.3 |
| 3 raptors v pachycephalosaur | 85.2 / 85.8 (0.8 / 1.2) | 1.2 / 1.5 | 0 / 10.9 |
| 3 raptors v stegosaur | 0 (100) | 0 (100) | 0 / 3.5 |
| 3 raptors v sauropelta | 0 (100) | 0 (100) | 0 / 2.9 |
| tyrannosaur v stegosaur, young stegosaur, sauropelta | 100 | 100 | 0, 1.0, 2.9 / 0 |
| tyrannosaur v 2 stegosaurs | 0.5 / 1.0 | 0.5 / 1.0 | 0 / 0 |

Three raptors against a pachycephalosaur is the one pairing that moves by more than the spread: 17 points with the game AI (fights 14 s longer) and 84 points with hostile raptors, whose fights now all run out the 90 s. Why (a traced fight, seed 1): the pachycephalosaur flees at nearly the raptors' speed and, when they catch up, swings its secondary attack (a tail swipe) behind it. A tail swipe is a blunt blow and the pachycephalosaur outweighs a raptor more than 1.1 times, so every swipe that lands floors the raptor it hits. The raptor loses its swing, lies 0.5-1 s and gets up over 0.4 s while the pachycephalosaur opens 3-4 m, and the hostile driver does not sprint inside 12 m, so the pack closes again at under 1 m/s. On 5.0 the same swipes only shove (the knockback is a shove since a3281ad), the pack stays at about 4.5 m and keeps biting. The raptors swing 3-6 times in 90 s instead of 13 in 40 s, and the pachycephalosaur heals faster than they hurt it.

What part of the knockdown does it (variants built only for this measurement, not on the branch; 400 fights per set, seeds 1-400 / 1001-1400):

| Variant | game AI | hostile | knockdowns per fight (game AI / hostile) |
|---|---|---|---|
| 5.0 (no knockdowns) | 53.8 / 53.3 | 85.2 / 85.8 | 0 / 0 |
| as built (0.5-1.0 s lying, 0.4 s get-up, 4 s immune) | 35.0 / 38.5 | 1.2 / 1.5 | 3.4 / 10.9 |
| half the times (0.25-0.5 s, 0.2 s) | 42.5 / 41.5 | 28.0 / 30.0 | 3.2 / 14.4 |
| one-tick knockdowns (the swing is lost, nothing else) | 50.5 / 49.0 | 62.5 / 63.0 | 3.1 / 10.6 |
| 8 s immune instead of 4 | 47.5 / 48.8 | 5.5 / 6.8 | 1.9 / 8.5 |
| secondary attacks (tail swipe, slash, kick) never knock down | 53.8 / 53.3 | 85.0 / 85.2 | 0.04 / 0.05 |

So the whole effect comes from the pachycephalosaur's tail swipe at its pursuers; its headbutt almost never floors a raptor in these fights. Even a one-tick knockdown costs the hostile pack 23 points, because the lost swing and the gap it opens matter more than the time lying. Nothing else was retuned: the timings are the branch's (README, Knockdowns). On merge, a side swipe (`swipe` in `damage()`: the tail of an animal whose main attack is its head) was given its own mass rule, `CRET_KD_MASS_SWIPE` 10: the sauropelta (19 times a raptor) and the tyrannosaur still throw raptors with it, the pachycephalosaur (5.6 times) no longer does, and its headbutt and charge still do. fightprobe, 200 fights, seeds 1-200: three raptors against a pachycephalosaur win 53.0% with the game AI (5.0 53.8) and 82.0% hostile (5.0 85.2; se 2.7), 0.04 knockdowns by the pachycephalosaur a fight.

Open world (`balance --seeds 12 --minutes 30 --first-seed 1|101|201|301`, 16 bots, 48 seeds of 30 game minutes per build). Kills, each seed set, then the sum:

| Kills | 5.0 | knockdowns |
|---|---|---|
| raptor kills pachycephalosaur | 7/7/8/13 = 35 | 9/11/8/11 = 39 |
| raptor kills gallimimus | 9/10/12/3 = 34 | 8/10/13/3 = 34 |
| stegosaur kills raptor | 14/4/3/3 = 24 | 3/7/3/0 = 13 |
| tyrannosaur kills stegosaur | 17/9/13/12 = 51 | 16/11/20/13 = 60 |
| tyrannosaur kills sauropelta | 4/7/5/2 = 18 | 5/6/4/4 = 19 |
| pachycephalosaur kills raptor | 4/7/3/2 = 16 | 4/9/6/4 = 23 |
| sauropelta kills raptor | 4/2/6/0 = 12 | 3/1/3/1 = 8 |
| raptor deaths without an attacker | 11/10/12/18 = 51 | 13/9/17/34 = 73 |
| raptor deaths | 33/23/24/25 = 105 | 23/28/29/39 = 119 |
| pachycephalosaur deaths | 11/7/15/18 = 51 | 11/11/16/13 = 51 |
| gallimimus deaths | 13/10/13/7 = 43 | 8/12/13/3 = 36 |

Every per-set count with knockdowns lies within or next to the range of the four 5.0 sets; counts are small and the sets vary by a factor of 2-4, so nothing here is a measured change. The raptor deaths without an attacker come mostly from one set (34 in seeds 301-312).

Knockdowns in those 24 hours of game time: pachycephalosaur on raptor 53/71/52/71 = 247, tyrannosaur on sauropelta 79, stegosaur on raptor 43, tyrannosaur on stegosaur 38, sauropelta on raptor 28, stegosaur on a young tyrannosaur 9, a handful of others; raptors never knock anything down.

`combatprobe` (how combat feels to a person, above), the full table before and after. Unchanged except where knockdowns happen:

| Pairing | landed 5.0 | landed knockdowns | swings knocked out | clicks dropped | time to kill |
|---|---|---|---|---|---|
| raptor>pachy | 96% | 94% | 12 -> 16 | 159 of 1414 -> 491 of 1411 | |
| raptor4>stego | 92% | 84% | 0 -> 111 | 253 of 3893 -> 1452 of 5012 | 8.8 -> 11.5 s |
| trex>sauro | 90% | 89% | 22 -> 26 | 188 of 1715 -> 178 of 1849 | 15.2 -> 17.0 s |
| sauro<trex | 100% | 92% | 0 -> 8 | 36 of 588 -> 138 of 542 | |

The dropped clicks are the scripted player clicking while it lies or gets up. `make test`'s quick run fails its budget on raptor4>stego (82.8% of attacks land, budget 91.1%): the stegosaur's tail floors the raptors mid-swing. The budget was left as it is (the owner re-baselines it).

`rlbench --hash --threads 1 --ticks 2000`: `a85daa57804e5b37` on 5.0, `df36129045a92da3` with knockdowns. Knockdowns change the sim (bot fights take other courses), so the fingerprint differs; it is the same on every run of either build.

## Wounds (cret/wounds)

What changed: `wounds.c` (new: `wound_place`, `wound_hit`, `wound_step`), `sim.c` (`damage` calls `wound_hit`; `survive` heals and bleeds through `wound_step`; `move_agent` reads the leg's speed and turn and the body's stamina factor; `cret_sim_observe` appends the wounds), `scent.c` (`blood_kind`, `blood_drip`, `cret_scent_record` keeps the trails of bleeding animals with nobody about, `cret_blood_strength`, the head factor in `cret_scent_ranges` and `cret_critter_sensed`), `eco.c` (`eco_top_speed`, `cret_eco_sight_m`, hearing in `cret_eco_perceive`), `bots.c` (`prey_ok`, `blood_lead`, `blood_prey`, `best_prey`, the blood-trail leg of `bot_hunt`, `cret_herd_waits` in the explore motor). README, Wounds, has the rules.

How the threshold was chosen: from the species table, the share of the victim's health one adult blow takes (README, Wounds). 8% falls between a raptor's bite on a grown pachycephalosaur (7%) with a pack's nibbling of the big animals (1-3%) below it, and a raptor's slash on a raptor (9%), a gallimimus's kick (10%), and the bites, tail blows and butts that decide fights (16-50%) above it. `combatprobe` lands those at 82-100% (raptor on gallimimus 82%, raptor duel 97-98%, every tyrannosaur bite 90-100%), so a fight that matters leaves a wound.

Baseline (the unmodified 5.0 checkout, `ocean/cretaceous/tools/fetch_props.sh` with Blender 4.2 from `scripts/install_blender.sh`, so with the real props): `make test` fails `camprobe` (304 violation frames in 184 runs: 42 line-of-sight frames against a budget of 12) and 10 of the 146 `ecoprobe --quick` lines (`budget.raptor.flee`, `budget.stego.defend`, `budget.trex.hunt`, `budget.sauro.defend`, `budget.galli.flee`, `hunt.raptor.success`, `deaths.raptor.starved_share`, `deaths.pachy.predation_share`, `deaths.sauro.predation_share`, `cost.bot_us_per_animal`). Everything else passes. After: `camprobe` the same 304 frames; `ecoprobe --quick` 23 lines out. My change moves 14 lines out (`budget.stego.rest`, `budget.pachy.graze`, `budget.pachy.vigil`, `budget.pachy.rest`, `budget.pachy.flee`, `budget.sauro.travel`, `budget.galli.graze`, `budget.galli.vigil`, `budget.galli.travel`, `budget.galli.rest`, `hunt.raptor.abandoned`, `hunt.trex.success`, `hunt.trex.abandoned`, `deaths.galli.starved_share`) and one back in (`budget.raptor.flee`). The quick probe is 24 animals for 2 days: one herd's path decides a line, and a wound changes paths from the first blow on. On the 12-seed island runs below the gallimimus flee 5.2% → 9.8% of the day (more chases of the limping), pachycephalosaurs graze 39% → 45% and travel 26% → 19%, stegosaurs graze 42% → 37% and defend 2.5% → 4.0%, tyrannosaurs hunt 7.8% → 10%; the rest within a point. The budget needs the owner's re-baseline with these props; `tests/*_budget.txt` are unchanged.

The island (`ecoprobe --only world --days 6`, 48 animals with the director, the real props), three seed sets of the same code (`--seeds 3`, `--seeds 3 --seed0 3`, `--seeds 6 --seed0 6`), before → after. Deaths per animal-day, all causes, then predation, starvation and thirst pooled over the 12 seeds:

| species | seeds 1-3 | seeds 4-6 | seeds 7-12 | pooled | predation | starved | thirst |
|---|---|---|---|---|---|---|---|
| raptor | 0.22 → 0.16 | 0.34 → 0.38 | 0.52 → 0.55 | 0.40 → 0.41 | 0.21 → 0.24 | 0.01 → 0.01 | 0.18 → 0.14 |
| stegosaur | 0.16 → 0.17 | 0.15 → 0.27 | 0.16 → 0.17 | 0.16 → 0.19 | 0.14 → 0.17 | 0.01 → 0.00 | 0.02 → 0.01 |
| tyrannosaur | 0.08 → 0.02 | 0.07 → 0.04 | 0.07 → 0.12 | 0.07 → 0.07 | 0.04 → 0.01 | 0.01 → 0.00 | 0.02 → 0.06 |
| pachycephalosaur | 0.13 → 0.10 | 0.14 → 0.11 | 0.18 → 0.11 | 0.16 → 0.11 | 0.12 → 0.07 | 0.02 → 0.01 | 0.02 → 0.02 |
| sauropelta | 0.10 → 0.13 | 0.17 → 0.13 | 0.09 → 0.20 | 0.11 → 0.17 | 0.10 → 0.13 | 0.00 → 0.01 | 0.01 → 0.02 |
| gallimimus | 0.16 → 0.38 | 0.24 → 0.17 | 0.13 → 0.17 | 0.17 → 0.22 | 0.14 → 0.19 | 0.01 → 0.01 | 0.01 → 0.01 |

Who kills whom, all 12 seeds (row: killer), before → after:

| killer | raptor | stegosaur | tyrannosaur | pachycephalosaur | sauropelta | gallimimus |
|---|---|---|---|---|---|---|
| raptor | 16 → 18 | 1 → 0 | 0 → 0 | 56 → 44 | 0 → 0 | 55 → 76 |
| stegosaur | 106 → 126 | 7 → 6 | 3 → 3 | 3 → 3 | 0 → 0 | 2 → 0 |
| tyrannosaur | 9 → 18 | 86 → 115 | 2 → 0 | 26 → 4 | 44 → 55 | 4 → 4 |
| pachycephalosaur | 42 → 28 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 |
| sauropelta | 22 → 40 | 0 → 0 | 2 → 0 | 1 → 0 | 0 → 0 | 0 → 0 |
| gallimimus | 1 → 0 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 |

Hunts (bots chasing one herbivore) per predator per day and the share that ended with the prey dead, per seed set, before → after:

| predator | hunts/day 1-3 | 4-6 | 7-12 | success 1-3 | 4-6 | 7-12 |
|---|---|---|---|---|---|---|
| raptor | 1.29 → 1.98 | 1.20 → 0.66 | 0.99 → 1.11 | 19% → 17% | 16% → 20% | 19% → 14% |
| tyrannosaur | 3.12 → 4.01 | 3.43 → 3.31 | 3.27 → 3.50 | 29% → 22% | 37% → 25% | 31% → 27% |

Wounds per animal-day (blows that left or deepened one) by place, and bot hunts begun on a blood trail (and of them, ended with the prey dead), after:

| species | leg | body | head | all (1-3 / 4-6 / 7-12) | blood hunts (1-3 / 4-6 / 7-12) | killed |
|---|---|---|---|---|---|---|
| raptor | 0.10 | 1.10 | 0.17 | 1.36 (1.20 / 1.50 / 1.38) | 35 / 5 / 37 | 3 |
| stegosaur | 0.58 | 0.14 | 0.04 | 0.76 (0.85 / 0.93 / 0.63) | 0 / 0 / 0 | 0 |
| tyrannosaur | 0.01 | 0.00 | 0.00 | 0.01 (0.02 / 0.00 / 0.00) | 6 / 3 / 5 | 0 |
| pachycephalosaur | 0.14 | 0.12 | 0.06 | 0.32 (0.47 / 0.41 / 0.19) | 0 / 0 / 0 | 0 |
| sauropelta | 0.00 | 0.76 | 0.00 | 0.76 (0.74 / 0.48 / 0.90) | 0 / 0 / 0 | 0 |
| gallimimus | 0.97 | 0.07 | 0.02 | 1.05 (1.98 / 0.58 / 0.83) | 0 / 0 / 0 | 0 |


Balance (`balance --seeds 48`, 16 bots, 30 game minutes, the real props), kills by row against column, before → after; the documented run-to-run spread is 20 to 30% a cell:

| killer | raptor | stegosaur | tyrannosaur | pachycephalosaur | sauropelta | gallimimus |
|---|---|---|---|---|---|---|
| raptor | 1 → 2 | 0 → 0 | 0 → 0 | 33 → 33 | 0 → 0 | 36 → 34 |
| stegosaur | 17 → 26 | 0 → 0 | 2 → 0 | 0 → 0 | 0 → 0 | 0 → 1 |
| tyrannosaur | 1 → 2 | 60 → 66 | 0 → 0 | 3 → 1 | 17 → 26 | 1 → 0 |
| pachycephalosaur | 15 → 15 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 | 1 → 0 |
| sauropelta | 16 → 13 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 |
| no attacker | 56 → 58 | 7 → 6 | 4 → 2 | 8 → 18 | 6 → 6 | 18 → 15 |
| deaths | 106 → 116 | 67 → 72 | 6 → 4 | 44 → 52 | 23 → 32 | 56 → 50 |

The ten more pachycephalosaur deaths with no attacker are thirst and starvation of unwounded animals seeking water (a debug build printed all 18: one had a body wound of 0.31, none was bleeding); the herds took other paths. Wounds in the balance run (victim: leg / body / head): raptor 34 / 360 / 105, stegosaur 202 / 47 / 21, pachycephalosaur 99 / 19 / 16, sauropelta 0 / 154 / 0, gallimimus 181 / 6 / 2, tyrannosaur none; 16 bot hunts begun on a blood trail (10 raptor, 6 tyrannosaur), 2 ended with the prey dead. Tick: sim 0.0171 → 0.0171-0.0173 ms, bots 0.0092 → 0.0094-0.0095 ms (two runs after).

`combatprobe` (full, 6 trials per pairing and setting), time to kill before → after where it moved: tyrannosaur on stegosaur 18.2 → 12.3 s (its stand style 32.8 → 12.5 s: a stegosaur standing its ground takes leg wounds and turns slower), raptor duel 25.3 → 22.5 s, tyrannosaur duel 31.2 → 25.1 s (wins 36 → 40 of 48), `stego<fraptor3` 48.8 → 42.2 s; blows landed within 1 to 5 points everywhere (sauropelta against a tyrannosaur 92% → 87%). `combatprobe --quick` stays within `tests/combatprobe_budget.txt`.

Cost: the island's bots 0.0430 / 0.0539 / 0.0413 → 0.0500 / 0.0585 / 0.0520 ms a tick (the three seed sets); `bench`, three runs each: step_256 0.681-0.720 → 0.714-0.729 ms, bots_128 0.363-0.370 → 0.364-0.404 ms, the rest within 0.003 ms. The blood search is a distance check per mark before the full smell test, and only for hungry predators at decision time.

What it does not do yet: a wound's slow bleed that kills long after the blow credits nobody (the reward's kill credit is unchanged); herbivores never notice blood (they have no hunting nose); critters do not wound; falls break bones but leave no wound.

## Bugs found and fixed

Each fix is small and listed here so the merge can be checked:

| File:line (after the fix) | Bug | Fix |
|---|---|---|
| `tests/test_sim.c` (`settle`) | `settle` stepped the sim with an input array of 2, and `test_reward_worth` (4 animals) and the wound tests (3) read past it: AddressSanitizer (gcc's; clang's runtime is not installed in the cloud sandbox) stopped `test_sim` in `cret_sim_step`. | An input for every slot (`CRET_MAX_AGENTS`). `test_sim` and `test_net_codec --fuzz 50000` are clean under gcc's AddressSanitizer and UBSan (cret/wounds). |
| `sim.c:506-513` (`move_agent`) | Agents walked up to 0.4 m into sloped prop faces (boulder flanks). The plane solver pushed along the tilted normal, then the vertical part of the push was discarded, so only part of the horizontal push remained. Found by the real-prop test: 4 of 15 boulders were entered by 0.23 to 0.41 m. | Solve against each plane's horizontal part: the normal is flattened and the offset scaled by `1/|n_h|`. Planes within about 11° of horizontal (tops and undersides) get no push. 15 of 15 now hold. |
| `sim.c:878-880` (`cret_bot_input`) | A bot that is thirsty and cannot find water (for example far out at sea) re-ran `find_water`, a scan of about 4000 points, every tick. A 32-bot soak went from 0.3 to 12 to 15 ms per tick of bot time after tick 13000, which would overrun a 30 Hz server's 33 ms budget with a full server. | Search once a second per bot (`(s->tick + i) % CRET_TICK_HZ == 0`). The worst 1000-tick window is now about 0.9 ms per tick. |
| `net.c` (`client_welcome`; was `cret_net_join`) | The welcome check accepted `num_agents` up to 255 and any finite `world_cell`. A bad or hostile server could send `num_agents = 200, slot = 150`. `cret_sim_init` clamps the agents to 64, and the client then indexes `agents[150]` (out of bounds). It also accepted negative cell sizes. Found by the fuzzer. | Also require `num_agents <= CRET_MAX_AGENTS` and `0.25 <= world_cell <= 64`. The protocol-8 codec's welcome checks these plus the slot, tick rate and snapshot divisor. |
| `net.c` (`netp_server_input`, `get_ent`, self decode), `sim.c` (`move_agent`) | Out-of-range actions (below). | Inputs with an action past `CRET_ACT_NEST`, an ability slot past `CRET_AB_SLOTS` or the spare bit set are refused; snapshots range-check `act` against `CRET_ACT_NEST`; `move_agent` treats an out-of-range action as none. |
| `tools/dino_convert.py` (clip loop) | Clips with root motion were stored at twice their ground speed (the root's drift plus the feet's speed), so the game played them at half rate. Found by a scratch fixture through `tools/animcheck.c`. | The feet's speed is used; the drift only when no foot joint is found. |
| `render/dino_ik.c` (`dino_foot_ik`, the knee) | Knees twisted about the leg on cross slopes, up to 128 degrees (flipping sides) for a walking quadruped. Found by `tests/test_dino_ik.c`. | The knee keeps the animated bend plane's axis. |
| `render/dino_ik.c` (`dino_foot_ik`, the prop probe) | A foot in its swing stepped onto props taller than half the hip height (the probe started above the lifted foot). Found by `tests/test_dino_ik.c`. | The probe starts above the animation's ground under the foot. |

`make asan` (a 10k-tick bot soak with invariants, a 5k-step binding rollout, and a 200k-packet codec fuzz under `-fsanitize=address,undefined`) found nothing else in the sim, terra, world or net code.
Box3D is linked uninstrumented.
The only leak it reported was in the test itself.

## Open issues

- **Out-of-range actions**: fixed at integration (table above). With abilities the action list ends at `CRET_ACT_NEST` (6), so act 7 is the only out-of-range 3-bit value; ability slots use 2 bits (0 to 3, all valid).
- **Thirsty bots at sea.** *Fixed (bots.c).* A swimming bot always has water ahead, so `BOT_SEEK_WATER` switched it to `BOT_DRINK`, and drinking never starts while swimming: bots at sea floated in the drink state until they died of thirst. `find_water` also needs dry, walkable ground 6 m before the water, which fails from open sea.
  - Now a swimming bot that needs water heads for the nearest walkable ground (`find_shore`: rings out to 400 m, rechecked every 2 s), then drinks at the edge or searches from dry ground. `BOT_DRINK` and the drink quest give up while swimming. `nearest_carcass` also skips carcasses lying in water deep enough to swim in, where nobody can eat: carnivores circled them.
  - The soak's 32 bots on the test world, 8 seeds (11 to 18) of 60000 ticks, before → after: swim time 1.59M → 1.12M bot-ticks (−30%), swimming while thirsty (water under 0.35) 560k → 245k, ticks spent "drinking" afloat 430k → 11, carnivores afloat at a carcass 338k (with only the shore fix) → 94k, thirst deaths 83 → 60.
  - Seed 11 alone (`CRET_SOAK_TRACE=1 test_sim --soak 60000`) is one chaotic trajectory: thirsty at once 7.8 mean, 16 max → 7.1 and 16; swimming 5.9 mean → 6.6 (393k swim ticks against 353k, mostly fleeing and grazing, not seeking water).
  - On the island (`ecoprobe --only world --days 4 --seeds 4`): deaths 273 → 211, raptor starvation 0.28 → 0.07 per animal-day, time budgets within a few points.
- **`ecoprobe --quick` budget re-baselined on merge.** The cloud branch could only run the probe without props (138 of 146). On merge it was re-baselined with the real props from the repository root, after a 3-seed, 6-day island run (`ecoprobe --only world`) stayed healthy: deaths a day raptor 0.25 to 0.22, stegosaur 0.18 to 0.23, tyrannosaur 0.10 to 0.02, pachycephalosaur 0.13 to 0.12, sauropelta 0.14 to 0.19, gallimimus 0.15 to 0.21 (single seeds of these swing as much).
- **Policy contract in checkpoints.** Existing `checkpoints/cretaceous/` runs are from contracts 1 and 2, with 160 and 176 inputs plus actions, and cannot be evaluated against contract 4 (243 inputs). The eval refuses them with both sizes in the message.
- **The bench runs on a shared, oversubscribed box.** Its baseline is noisy, and slow cases are not failed while the load average is above 1.5× the cores. Rewrite the baseline on a quiet machine.
- **The bot-only soak with `CRET_TEST_N=2048`** (the training world) is not part of `make test`, because the bake takes about 45 s. Run `CRET_TEST_N=2048 build/cretaceous/test/test_sim --soak 10000` by hand when the world changes. The baselines above already run on it.
