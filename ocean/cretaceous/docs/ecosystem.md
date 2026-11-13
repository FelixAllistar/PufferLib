# Ecosystem and animal AI: design

Status: **being built on `cret/ecosystem`**; each section says what the code does where it differs from the design. This is the specification the scripted
animals are to be rebuilt from. It replaces the state switch in `sim.c`
(`cret_bot_input`, states `BOT_WANDER` .. `BOT_CARRY`) and the flat needs in
`step_needs`. Sections marked *contract* must not change what a policy sees
or does; everything else is free.

## Why

Today every species empties its stomach in the same 900 s, plants never run
out, a predator hunts whenever its food is under 0.75, and behaviour is a
hand-ordered list of special cases. The result plays as: nothing to fight,
or raptors mowing everything down; herbivores that are everywhere or
nowhere; carrying, nesting and resting that almost never show.

The goal is a small set of equations from ecology, each with one or two
parameters per species derived from body mass, from which the behaviour
falls out: where animals are, when they move, what a predator bothers to
chase, when it gives up, when a herd runs. The same quantities then define
the reward for the learned policies, so scripted and learned animals want
the same things.

Hard constraints:
- Deterministic: only `s->rng`, fixed iteration order, no wall clock.
- Cost: the whole animal AI at most **0.02 ms per animal per tick** on one
  core averaged (64 animals: 1.3 ms a tick is too much; decisions are
  staggered, see "Scheduling"). `tools/rlbench.c` must not get slower by
  more than 5% with bots off, 15% with bots on.
- *Contract*: `CretInput`, the action heads and `cret_sim_observe` do not
  change. New state lives in `CretAgent` fields the observation does not
  read, or in side arrays of `CretSim`.
- A person (the local player or a network peer) is treated exactly like any
  other animal of its species by every rule below.

## 1. Energy

Replace the fraction-of-stomach clock with energy in kilograms of food,
scaled by body mass `M` (kg, `cret_mass`).

- **Gut capacity** `G = 0.25 M` for carnivores (they gorge), `0.12 M` for
  herbivores. `a->food` stays the fraction `gut / G` (contract: the
  observation and the HUD read it).
- **Basal burn** (kg of food per second), Kleiber: `B = k_b M^0.75`, with
  `k_b` chosen so a resting adult raptor (80 kg) empties a full gut in
  `hunger_seconds` (900 s): `k_b = 0.25 * 80 / (900 * 80^0.75)`. Larger
  animals then last longer on a full gut in proportion to `M^0.25`: about
  2.8x for a 5 t stegosaur, 3.2x for a tyrannosaur. Herbivores use the same
  `k_b` on their smaller gut, so they must eat more of the time.
- **Movement** adds `k_m M^0.7 v` (cost of transport falls with size);
  `k_m` set so a raptor trotting burns 2x basal and sprinting about 4x.
  Fighting and swimming count as sprinting.
- **Water**: unchanged clock (`thirst_seconds`), times `M^0.1` relative to
  the raptor, plus 50% faster in the four hours around noon in the open
  (not under canopy: `cret_canopy_at` or the vegetation density maps).
- **Fat**: a second store `fat` in 0..1 of `0.15 M` kg. Food beyond a full
  gut is not eaten. While `food > 0.6` the gut drains 30% faster and the
  surplus goes to fat; while the gut is empty, fat burns instead and the
  animal is "hungry" but not yet starving. Health loss from starvation
  starts when both are empty. This gives large animals days of reserve and
  makes a kill worth defending.
- **Growth** needs surplus: growth advances at the current rate only while
  `food > 0.25` and `fat > 0.1`.
- **Digestion pause**: after a meal of more than 0.5 gut, a carnivore's
  drive to hunt is zero until `food < 0.5` (it rests, guards the carcass,
  or carries from it).

**As built** (`eco.h` holds the constants, `eco.c` the arithmetic,
`survive` in `sim.c` applies it every tick to every animal, people and
policies included):

- `k_b = 0.25 x 80 / (hunger_seconds x 80^0.75)`, so the clock still comes
  from the config (the tests' short clocks keep working). Full-gut times
  at basal: raptor 900 s, pachycephalosaur 665 s, stegosaur 1215 s,
  tyrannosaur 2846 s (`tests/test_eco.c`).
- `k_m = k_b 80^0.05 (2 - 1) / 4.6 m/s`; a sprint counts its speed 1.6x,
  so a sprinting raptor burns 3.99x basal. Fighting (an attack swinging)
  and swimming burn as a sprint at the species' sprint speed.
- Resting burns basal (the old 0.6x rest discount is gone: Kleiber's basal
  rate is the resting rate); rest still heals faster and, in section 5,
  clears fatigue.
- The fat transfer makes a full gut empty in `(0.4 / 1.3 + 0.6) x 900 =
  817 s` for a resting raptor, laying down 1.85 kg of fat on the way.
- Water: "times `M^0.1`" is read as lasting longer: the clock is
  `thirst_seconds x (M / 80)^0.1` (a stegosaur 1.51x, a hatchling raptor
  0.73x), with the old activity factors (sprint 2x, rest 0.6x). Midday heat
  (four hours around noon, not swimming) adds 50% where the canopy
  (`cret_eco_canopy`: conifers, broadleaves, 0.6 tree ferns, 0.4 palms) is
  under 0.35.
- Spawned animals start with fat 0.5. Starvation hurts only when gut and
  fat are both empty. A meal is the running total of gut eaten, fading
  with a 30 s time constant once eating stops; 0.5 sets `digest`, cleared
  when the gut falls under 0.5.
- The HUD draws fat as a thin line under the food bar; the food icon warns
  in amber (not red) while there is fat left. Clients get it in protocol
  15 (`docs/net.md`).
- The test critter hatchling (`tests/test_critters.c`) got a 530 s hunger
  clock: its 3.4 kg body now burns its gut in 0.45 of the clock.

## 2. Plants as a resource

A coarse biomass grid over the island, `B[x,z]` in 0..1 of the cell's
capacity `K[x,z]`, 32 m cells (a 2048² world at 4 m is 256² cells, 64 KB).

- `K` comes once from the world's vegetation layers (`cret_forage`'s
  inputs): ferns, cycads, horsetails, scrub.
- **Regrowth**, logistic, stepped for 1/16 of the cells each tick:
  `dB/dt = r B (1 - B)`, `r` such that a cell eaten down to 0.2 is back at
  0.8 in one game day (`day_seconds`). Floor `B >= 0.05` (seed bank).
- **Grazing** removes `intake * dt / (K_cell_kg)` where a cell's capacity in
  kg is `K * 400` (tune so a herd of 8 pachycephalosaurs exhausts a good
  cell to 0.3 in about four minutes and must move on).
- **Intake rate**, Holling type II: `intake = c_max * B' / (B' + B_half)`
  with `B' = K * B`, `c_max = 0.0010 M^0.75` kg/s (fills a 0.12 M gut in
  about 2 minutes of grazing on rich ground at 450 kg, longer when larger),
  `B_half = 0.25`.
- `cret_forage(w, x, z)` keeps returning the static richness (other code
  uses it); add `cret_graze_at(s, x, z)` returning `K * B` and use it for
  eating, the HUD's graze prompt, and the herbivore's choices.
- The renderer does not show depletion in this stage (later: ground cover
  density scaled by `B` near the camera).

**As built** (`eco.c`, section 2 of `eco.h`):

- Cells are 32 m. Only grazed cells are stored, in a 1024-slot table in
  `CretSim` (`plants`, 12 KB, keyed by cell); every other cell is full. A
  cell keeps its biomass and the tick it was written, and reading it
  applies the closed form of the logistic, `B(t) = 1 / (1 + (1/B0 - 1)
  e^(-r t))`, so nothing is stepped per tick (the design's "1/16 of the
  cells each tick" would have been 4096 cells a tick on the training
  island, over the 5% budget for the trainer). When the table is three
  quarters full it is rebuilt without the cells that have grown back past
  0.99 (and, if that is not enough, the most grown ones until it is half
  full).
- `r = ln 16 / day_seconds`: 0.2 to 0.8 in one game day. Floor 0.05.
- `K` is `cret_forage` at the mouth (the static richness), so no capacity
  grid is stored either; a cell's capacity in kg is `K x 320` (tuned, not
  400: eight pachycephalosaurs eating 60% of the time take a cell of
  richness 1 to 0.3 in 4.0 min, `tests/test_eco.c`).
- **Intake**: the design's `c_max = 0.0010 M^0.75` is only 1.2x the basal
  burn, so a herbivore could barely gain while grazing flat out, and its
  "two minutes to fill the gut" would need 0.0046 M^0.75. Both targets
  cannot hold; the time budget (graze 45-65% of daylight) is the one that
  matters, so `c_max = 4 x basal` (0.0033 M^0.75 at the default hunger
  clock; 0.325 kg/s for an adult pachycephalosaur), scaling with the
  config's clock like the burn. `B_half = 0.25` as designed.
- `cret_graze_at(s, x, z) = K x B` drives eating (`survive`), the HUD's
  graze prompt (`cret_food_state`), the graze quest's progress and the
  bots' choice of where to graze. The observation keeps reading
  `cret_forage` (contract). Clients get the biomass at their mouth in
  protocol 16 and write it into their own table, so the prompt matches.
- The old bots grazed in 6-14 s bouts, which the slower intake turned into
  starvation; until the drives (section 5) they keep grazing while hungry
  and the patch holds plants. With that the probe (3 days, 1 seed) has
  herbivores grazing 44-55% of daylight, eating 1.02-1.16 of what they
  burn, grazing where the biomass averages 0.58-0.88.

## 3. What each animal knows

Per animal, a small memory updated at its decision rate:

- **Seen**: other animals within its sight range (`60 m + 4 L`, halved at
  night for diurnal species, see 7), in a 220 degree cone, not hidden: an
  animal is hidden when the vegetation density on the sight line and its
  own height put it under cover (`h_target < 0.6 * cover_height` over at
  least a third of the line) and it moves slower than a walk.
- **Heard**: anything within `8 m * (v / 1 m/s)^2 * (M/80)^0.3`, capped at
  150 m; calls at their own range.
- **Smelled**: the existing scent system (`scent.c`): downwind lobe, trails.
- For each known animal: last position, velocity, time, and whether it has
  been seen looking at me (it is within its own cone and range of me).
- Memory decays over 20 s; a remembered predator keeps half its weight for
  60 s ("something was here").

## 4. Fear

A scalar risk at a point, used by everything that chooses where to be:

```
risk(p) = sum over known threats t:  D(t) * exp(-|p - p_t| / R_t) * A(t)
D(t)   = clamp(log2(M_t / M_me) + 1, 0, 3) for carnivores that can take me
         (their mass at least 0.15 of mine, or a pack of n: use n * M_t)
R_t    = 25 m + 6 * v_max,t           (how far it can reach in a few seconds)
A(t)   = 1 if it is hunting or approaching me, 0.4 if it has not noticed me,
         0.15 if it is feeding or resting
```

plus a slow **landscape of fear**: a 64 m grid per species group that adds
0.3 where a member was attacked or a predator's fresh scent lies, decaying
over one game day. Herbivores avoid high cells when choosing where to graze
and drink, which moves herds off kill sites and makes ambush spots matter.

Cover lowers risk for small animals (x0.5 in cover taller than them) and
raises it for large ones that cannot see out.

**Flight initiation distance**: an animal starts to flee when a threat with
`A = 1` is within `FID = (20 m + 5 L_t) * D(t) / (1 + 0.5 * cover) *
(1.3 - 0.6 * food)`: hungry animals let danger closer. Armoured and large
species (D small) stand instead (existing defences: stegosaur tail,
pachycephalosaur butt, sauropelta armour).

**Vigilance**: while feeding, an animal looks up every `T_v` seconds for 2
s; `T_v = 6 s * sqrt(n_group)` capped at 20 s (many eyes). Only while its
head is up does "seen" update for threats outside 15 m. This is what makes
stalking a lone animal possible and a herd hard.

**Alarm**: an animal that starts fleeing raises the risk known to group
members within 60 m (they get the threat's position), with a 0.3 s delay
per 10 m, so a herd peels away from the near side.

**As built** (sections 3 and 4 of `eco.h`, `eco.c`):

- The memory is 8 `CretKnown` slots per animal in `CretAgent` (never
  observed): herbivores keep predators, carnivores their prey and the
  carnivores of other species that could take them; never their own
  group. A newly sensed animal counts after the bot's reaction delay
  (0.2-0.75 s by mass, as before); one already known, or sensed while hurt,
  at once. A full memory replaces its stalest entry.
- "Hidden": the line of sight is sampled at a sixth, a half and five
  sixths of the way; the target is hidden when it moves slower than its
  walk and at any sample the cover (`cret_eco_cover_h`: ferns, cycads and
  horsetails 1.2 m, scrub 2 m, tree ferns 3 m at full density, at most
  4 m) stands taller than its height / 0.6. Resting animals see half as
  far (dozing). Night is before 0.21 and after 0.79 of the day.
- Smell uses `cret_scent_ranges` and `cret_scent_lobe` directly (the trails
  are only recorded around people, the trainer never pays for them).
- **D**: the design's `clamp(log2(M_t / M_me) + 1, 0, 3)` is 0 for any
  predator under half its prey's mass, so no herbivore would ever run from
  a raptor (80 kg against a 400 kg gallimimus is 0.2). As built,
  `D = clamp(log2(M_t / M_me) + 1.5, 0.3, 3)` for any carnivore (a pack:
  the members within 60 m summed) of at least 0.15 of its mass, else 0.
- **A**: only what the animal could see: 1 when the threat closes on it
  faster than 0.5 m/s (or a group member's alarm said so), 0.15 while it
  eats or rests, else 0.3 (the design's 0.4 made a predator standing still
  in view as frightening as half a charge).
- **FID** is capped at 80 m. Flight comes from `cret_eco_flight`: a
  noticed threat with A = 1 inside the FID. The cover term is the cover at
  the animal over its height, 0..1.
- **Alarm**: group members within 60 m get the threat into their memory as
  coming (A = 1) after 0.3 s per 10 m and decide at once; each runs only if
  it is inside its own FID, so a herd peels away from the near side.
- **Vigilance**: a grazing bot lifts its head (stops eating) for 2 s every
  `6 sqrt(n)` s, at most 20 s, n its group within 30 m, jittered 0.75-1.25x.
  With its head down it sees 15 m.
- **The landscape of fear** is a ring of 128 marks in `CretSim` (a mark
  0.3 where a herbivore is hit by a carnivore, 0.1 every 20 s where a
  moving predator passes), each reaching 64 m and fading linearly over a
  game day (`cret_eco_step`, every tick, the trainer included: a loop over
  the agents). One landscape for all herbivores, not one per species group.
- Decisions run at 3 Hz staggered by slot, 10 Hz with a known animal within
  60 m or while hurt, and at once for an alarm.
- Two quest tests now put the hatchling within reach of an unvisited place
  before the explore step, as the drink step already did for water: where
  its bot wanders while the test waits depends on the whole island.

## 5. Choosing what to do: drives

Every decision tick an animal scores each activity and does the best one.
`U = urgency(state) * chance(what it knows) * commit`, where `commit = 1.25`
for the activity it is already doing (hysteresis) and urgencies are in 0..1:

| Activity | Urgency | Chance |
|---|---|---|
| Flee | `clamp(risk_here / 1.0, 0, 1)^0.5`, forced to 1 inside FID | 1 |
| Defend (armoured, cornered, young near) | same as flee for standers; `0.8` when a threat is within 2 L of own young or nest | mass ratio in its favour |
| Drink | `(1 - water)^2`, forced to 1 below 0.15 | `exp(-d_water / 400 m)` |
| Graze (herbivore) | `1 - food` (plus `0.5 (1 - fat)` when food > 0.6) | `intake(B here or best cell within 150 m) / c_max * (1 - 0.6 * risk_there)` |
| Hunt (carnivore) | `(1 - food) ^ 1.5` once past the digestion pause; `0` above 0.75 unless fat < 0.3 | best prey's expected profit, below |
| Scavenge | `1 - food` | carcass known, `meat / G`, minus risk at it |
| Rest / sleep | `fatigue` (rises 1 per `0.6 day` awake, falls 3x faster asleep) + `0.5` in the species' rest hours (7) + `0.6` during a digestion pause | safe: `1 - risk_here` |
| Stay with group | `0.3 + 0.5 * (d_centroid / 40 m)` capped 1, for herd species | group exists |
| Follow parent (growth < 0.5) | `0.7` | parent or group adult known |
| Provision (carry food to young) | `0.7 * (food > 0.6)` | young of my group within 150 m of the nest are hungry (`food < 0.6`) and a liftable piece is at hand |
| Nest | as now (fed adult, group, no nest), urgency `0.3` in the morning | site: low risk, cover, 60 m from other nests |
| Guard nest / young | `0.4`, `0.9` if a threat is known within 80 m | has eggs or young |
| Explore / patrol | `0.15` | always |
| Quest (people's helpers only) | as now | |

Ties break by the table's order. A person's animal never runs this.

### Hunting, as arithmetic

For each prey `q` the predator knows about:

```
profit(q) = P_catch(q) * P_kill(q) * meal(q) - cost(q) - danger(q)
meal      = min(0.5 * M_q, G - gut) / G                    in gut fractions
cost      = (chase time * sprint burn + approach time * walk burn) / G
danger    = expected health lost, as a fraction, * 0.5     (injury is expensive)
```

- `P_catch`: closing a gap `d` with stamina: the chase lasts
  `t_c = d / (v_me - v_q)` if `v_me > v_q` over the sprint both can hold
  (`stamina / drain`); otherwise the prey escapes unless surprised. From an
  unnoticed start within `1.5 * FID_q`: `P_catch = 0.9`. Noticed from afar
  and faster: `clamp(stamina_time_me / t_c, 0, 1) * 0.7`. Slower: 0.05.
  Each pack member within 60 m adds 0.15, capped at 0.95; packs also flank
  (below). Juveniles, the injured (`health < 0.6`, limping, bleeding) and
  the lone are easier: `v_q` uses their actual speed, and a prey with no
  group member within 30 m drops its vigilance interval to the lone value.
- `P_kill`: from damage per second each way and health, as the existing
  `can_take` estimate but continuous: `1 / (1 + exp(-(ttk_them - ttk_me) /
  5 s))`.
- **Diet rule** (optimal foraging): rank known prey by
  `meal * P_catch * P_kill / (handling + chase time)`; hunt the best only if
  its `profit > 0.05` and above the profit of waiting (`0.02 * (1 - food)`
  per minute of expected search). A full predator ignores a herd walking
  past; a starving one takes risks. Critters (`critters.c`) enter the same
  ranking for animals under 300 kg.
- **Stalk**: approach from downwind (scent) and from cover, at a walk while
  the prey's head is down, frozen while it is up, until within the surprise
  distance; then sprint. Abandon when the gap stops closing for 3 s or
  stamina is under 0.15 ("give up" is a decision, not a timer).
- **Pack**: the finder calls; members take bearings 60 to 120 degrees apart
  around the prey's escape direction (away from the finder, toward its herd
  or cover) before the first one charges.
- After a kill: eat to `food >= 0.95`; then **guard** the carcass while
  `meat > 0.2 max` and nothing bigger threatens; **carry** a liftable part
  to the nest if there are young; else rest within 30 m of it and return to
  feed. This is where carrying becomes a common sight: also a predator
  whose kill is being contested by a bigger one takes what it can lift and
  leaves.

### Grazing, as arithmetic

- Graze the current cell while its `intake` is at least the average the
  animal has had over the last game hour (marginal value theorem), then
  walk to the best cell within 150 m by `intake * (1 - 0.6 risk) /
  (1 + d / 100 m)`.
- Herds decide by the member with the highest urgency (the hungriest or
  thirstiest leads; others' "stay with group" pulls them along), so a herd
  moves as one between feeding, water and rest.
- Spacing while feeding: 1.5 to 3 body lengths; juveniles in the middle
  (their "follow parent" target is the centroid when threatened).

**As built** (`bots.c`: `bot_choose` scores, the motor below it steers;
the hunt arithmetic is `cret_eco_hunt_eval` in `eco.c`):

- Activities are `bot_state` values (`eco.h`): flee, defend, seek water /
  drink, graze, hunt, feed (scavenge and the own kill), rest, group,
  carry (provision), nest, guard, raid (eggs), wander (explore and the
  quest places). Ties go to that order. The current activity scores x1.25;
  a hunt under way that still shows a profit x2.5, so a hunt ends by the
  chase going nowhere (the motor's give-up rule), not by a passing better
  idea.
- **Flee** from mere risk (nothing coming) is `sqrt(risk - 0.6)`, not
  `sqrt(risk)`: with the design's form, herbivores spent 20-35% of the day
  walking away from predators that were not hunting them. Anything coming
  inside the flight distance, the attacker it is alert to, or a roar is 1.
  The armoured (`can_defend`) stand instead when engaged.
- **FID**: the design's `(1.3 - 0.6 food)` makes a hungry animal run
  *sooner*, the opposite of its own words; as built `(0.7 + 0.6 food)`. D's
  floor is 0.5 (not 0.3): with 0.3 an adult gallimimus let a raptor to 8 m.
  "Coming" also needs the threat heading within about 45 degrees of it
  (closing speed over 0.7 of its speed), so a predator walking past is not
  a charge.
- **Drink**: water the animal does not remember counts as 200 m away
  (`exp(-200/400) = 0.61`); remembered water at its distance. A failed
  search (about 4000 samples) is not repeated for 30 s.
- **Graze**: chance is the Holling intake here, or at the best ground found
  in the last search, over `c_max`, times `1 - 0.6 risk`. The search
  scores 49 points (here and rings at 40, 90, 140 m) by `intake (1 - 0.6
  risk) / (1 + d / 100 m)`, less beyond 40 m from the herd's middle.
  Moving on: when the intake falls under 0.85 of its average over a game
  hour (the marginal value rule). At a shore, eating would drink: it turns
  to the plants.
- **Hunt**: chance is `profit / 0.3`. An unaware prey (it has not sensed the
  predator within 2 s) is valued from the charge distance, `1.4 x sprint
  speed` within 6-18 m, with the stalk's walk costed (at half the walk,
  1.5x basal); an aware one from where it stands. `danger` is weighted
  0.25 (not 0.5: P_kill already prices losing). Hatchlings (under 0.55)
  rank critters at 0.6. The pack joins silently (a call is heard by the
  prey at 200 m and cost every hunt its surprise); members within 150 m
  that are under 0.9 full help at urgency 0.5.
- **Stalk**: approach at a trot past 90 m, a walk to 60 m, then creep
  (forward 0.25, about 1.1 m/s for a raptor, heard at 10 m) while the prey's
  head is down or it faces away, and freeze while it looks this way;
  charge inside the charge distance, when it bolts within twice that, or
  when a packmate charges. Packs flank (about 60 degrees apart by rank).
  Give up when the gap has not closed by 0.5 m in 3 s or stamina is under
  0.15; the prey is left alone for 30 s.
- **Feed**: once on the kill it eats to 0.95 (urgency 1), then guards it
  (0.5, while it has over 0.2 of its meat and the risk is under 0.5).
- **Rest**: fatigue (1 per 0.6 game day awake, three times as fast down at
  rest) plus 0.6 while digesting, times `1 - risk`.
- **Stay with group** is for herbivores only (the design names herd
  species): packs, given it too, spent 60% of the time walking to each
  other. It starts 15 m plus the body length from the herd's middle.

## 6. Groups and young

- **Selfish herd** when a threat is known: steer toward the centroid of
  group members within 40 m; the group as a whole moves down the risk
  gradient. Stegosaurs and sauropeltas with young ring them instead.
- **Parents** (an adult in a group with a nest that hatched within the last
  two days, or with members under growth 0.5): provision (carry), guard,
  and lead young to water and food at the young's pace.
- **Young**: follow the nearest adult of their group within 15 m, flee to
  it when alarmed, play (short chases with siblings) when fed and safe.
- **Breeding** is paced by condition: an adult pair nests only when both
  have `fat > 0.6`, which ties births to food supply.

## 7. Time of day and weather

Per species an activity curve over the day, multiplying the urgencies of
hunt, graze and explore, and adding to rest when low:

| Species | Active |
|---|---|
| Velociraptor | dusk and dawn (crepuscular), moderate by day, low at night |
| Tyrannosaurus | dusk through the early night, low at midday |
| Stegosaurus, Sauropelta | day, midday pause in shade |
| Pachycephalosaurus, Gallimimus | morning and late afternoon |

- Midday heat: large animals seek canopy; drinking peaks in the afternoon.
- Night: diurnal species pick a rest site (low risk, cover, group together,
  one member awake in turn: vigilance interval x3 for the sentry).
- Rain (`cloud_cover` above the rain threshold): scent ranges shorten
  (already in `scent.c`), hearing halves, activity x0.7.

## 8. Population

The director (`population.c`) keeps its job of putting animals where people
are, but numbers follow the ecosystem:

- Target herbivore biomass is proportional to the island's summed `K * B`;
  target predator biomass is 8% of herbivore biomass (ectotherm-leaning
  dinosaurs: between the 1% of mammals and 20% of reptiles).
- A species under target respawns faster (as juveniles at nests when its
  group has one, else at the map edge away from people); over target, dead
  slots stay empty longer. Changes are slow (a game day) so the player sees
  consequences of over-hunting an area: prey thins, predators move.
- Floors for play: within 250 m of each person there is always at least one
  herd and, within 600 m, one predator group, reached by pulling the
  nearest group toward the person (as now), never by spawning in view.

## 9. Scheduling and cost

- Decisions (sections 3 to 5) run at 3 Hz per animal, staggered by slot;
  animals within 60 m of a threat or a person run at 10 Hz.
- Between decisions the animal steers toward its current target with the
  existing steering (`steer`, obstacle and water avoidance), every tick.
- Perception uses the existing nearest-neighbour pass where possible; the
  risk sum is over at most 8 known threats.
- The biomass and fear grids update a slice per tick.

## 10. What the learned animals get from this

No change to the contract in this stage. The quantities above give a reward
that matches what the scripted animals optimise, to be wired up when
training starts:

- energy balance: `+d(food) + 2 d(fat)`, small;
- survival: death `-1` scaled by expected remaining life;
- offspring: `+1` per young raised to growth 0.5 by the animal's group;
- no shaping for damage dealt, so killing without eating is not rewarded.

The scripted animals are then both the opponents in early training and the
standard to beat: a policy is better when it holds a higher energy balance
and raises more young than the script in the same world.

## 11. How to tell it works

`tools/ecoprobe.c` (headless, like `balance.c`) runs three parts:

- **world**: bots only, 48 animals (the solo and host population), `--days`
  game days per seed (10 x 2 seeds by default): time budget per species
  (from the activity in `bot_state` and the action), hunts started,
  abandoned and succeeded (a hunt: a bot chasing one herbivore; success:
  the prey died to the hunter or its group), kills and food eaten against
  burned, deaths by cause (`CretStats.death_cause`), population sampled
  every game hour, herd range (32 m cells visited per day), distance to
  water, carrying;
- **person**: one adult person of each species, driven by the bot AI, with
  the population director on, 3 x 20 min: time to the first sighting of
  prey (carnivores: a herbivore `cret_quest_prey_ok` allows) or a predator
  (herbivores: a carnivore of at least 0.15 of its mass) within 150 m,
  encounters (such an animal within 60 m, once per animal per 2 min) and
  predator blows per 10 min;
- **stalk**: an adult predator played like a person against a grazing young
  herbivore, alone or in its herd, 12 trials per case and style: a careful
  stalk (walk to 40 m, creep at 0.3 while its head is down, freeze while
  it looks up, charge from 12 m or when it bolts) against a blind charge
  from 60 m; the catch rate is a bite landing within 40 s.

```bash
make -C ocean/cretaceous ecoprobe && ./build/cretaceous/ecoprobe            # all three, about 4 min
./build/cretaceous/ecoprobe --only world --days 10 --seeds 2
```

`tools/rlbench.c --bots` measures the cost of the bots in the trainer's
loop (bot input, step and observe for every agent).

| Measure | Target | Before (the state-switch bots) |
|---|---|---|
| Time budget per species (graze, travel, rest, vigilance, flee, hunt, feed) | herbivores graze 45-65% of daylight; predators rest 55-75%, hunt 5-15% | herbivores graze 5-8% of daylight and travel 84-88% (wander legs); raptors rest 17%, hunt 2%; tyrannosaurs rest 17%, hunt 8% |
| Hunts started, abandoned, succeeded, per predator per day | success 15-35% (raptor packs higher on small prey, lower on adults); abandoned at least a third | raptor 1.1 a day, 16% succeed, 83% abandoned; tyrannosaur 5.9 a day, 37% succeed, 62% abandoned |
| Kills per predator per day against its energy need | intake within 20% of burn over three days (no starving packs, no surplus killing) | raptor 0.10 kills a day, eats 0.84 of what it burns; tyrannosaur 1.76 kills, 0.73 |
| Deaths by cause per species per day | predation the main cause for small herbivores; starvation rare (under 10%) | predation 75-96% of herbivore deaths (gallimimus almost never die: 0.01 a day); starvation 37% of raptor and 50% of tyrannosaur deaths |
| Population of each species over 10 days | no extinction; swings within x0.5 to x2 of the mean | fixed slots that respawn after 10 s: min/mean 0.67-1.0, max/mean 1.0; nothing follows food |
| Herd range: area visited per day, distance from water | herds cycle between feeding ground and water; cells recover | 55-98 cells of 32 m (0.06-0.10 km2) per animal per day, a mean 168-208 m from water; plants never run out |
| Carry events per day | at least 2 per predator group with young | 0.9 per predator group-day with young (12 pickups in 20 game days) |
| A person's experience: time to first sighting of prey / predator, encounters per 10 min | first sighting under 2 min; 2 to 6 encounters per 10 min | first sighting 0-4 s (the director places people by herds); encounters 2.3-4.7 (tyrannosaur 20.8); predator blows on a herbivore 0.2-3.2 per 10 min |
| Stalking dinosaur prey (catch rate, careful / blind charge) | a careful stalk usually works, a blind charge usually fails | raptor on a young gallimimus 8% / 0%, on a young pachycephalosaur 75% / 100%, in its herd 92% / 67%; tyrannosaur on a pachycephalosaur 0% / 0%, on a young stegosaur 50% / 75% |
| Cost | within section 9's budget | bots 0.56 us per animal-tick (48 animals) |

Each rule is unit-tested in isolation where it is arithmetic (energy,
intake, regrowth, FID, profit ranking) and the probe's targets become a
regression budget like `tests/camprobe_budget.txt`.

## 12. Order of work

1. Energy and fat (1), with the HUD and tests following; balance rerun.
2. Biomass grid and intake (2).
3. Perception, risk and vigilance (3, 4).
4. Drives replacing the state switch (5), species by species: grazers
   first, then predators, with the probe after each.
5. Groups, young, time of day (6, 7).
6. Population targets (8).
7. Tune against section 11; write up what was measured.

Each step leaves the game playable and the tests passing.
