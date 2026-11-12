# Progression: abilities, quests, characters and nests

This layer is modelled on Path of Titans. It sits on top of the survival sim:
- every species has abilities on keys 1-3, with cooldowns, stamina costs and status effects;
- the server hands out quests at named places, which pay growth and marks;
- characters are saved by name, and death keeps the marks but not the growth;
- fed adults in a group build nests, where young members hatch and which predators raid.

Code:
- `abilities.[ch]`: the ability table, loadouts and unlocks, status effects.
- `progress.[ch]`: named places, quests and nests.
- `sim.c` (carrying section): carnivores carry food in the mouth.
- `render/nest.c`: nests, eggs, nests being built, and loose and carried food.
- `character.[ch]`: save files (player and server builds only).
- `sim.c`: hooks into combat, movement, observations and the bots.
- `hud.c`: the ability bar, status chips, quest tracker, markers, map and menu row.

## Abilities

Slots 1 and 2 hold the species' own abilities. Slot 3 holds one of two choices, which a player buys with marks in the Esc menu.
Bots and RL agents have every ability unlocked and use choice A in slot 3.

| Species | Key 1 | Key 2 | Key 3, choice A (marks) | Key 3, choice B (marks) |
|---|---|---|---|---|
| Velociraptor | Pounce | Sprint Burst | Hamstring Bite (120) | Mend (60) |
| Stegosaurus | Tail Slam | Defensive Stance | Spike Sweep (120) | Mend (60) |
| Tyrannosaurus | Fear Roar | Crushing Bite | Rending Bite (120) | Mend (60) |
| Pachycephalosaurus | Charge Headbutt | Herd Bellow | Sprint Burst (100) | Mend (60) |
| Sauropelta | Hunker | Shoulder Shove | Herd Bellow (100) | Mend (60) |
| Gallimimus | Dodge Leap | Alert Call | Sprint Burst (100) | Mend (60) |

Each ability below lists its cooldown (s), its stamina cost (on the same 0..1 pool sprinting uses), and what it does.
Strikes are the species' own attack swing with multipliers on damage, reach, bleed and knockback. They cannot be used while swimming, frightened or mid-leap.

Strikes:

| Ability | CD | Stamina | Effect |
|---|---|---|---|
| Hamstring Bite | 12 | 0.15 | 0.9x damage, 1.1x reach, 1.3x bleed. Slows the victim by 40% for 6 s. |
| Crushing Bite | 18 | 0.20 | 1.4x damage, 0.6x bleed, 1.2x knockback. Breaks a bone when the hit takes over 10% of the victim's max health. |
| Rending Bite | 14 | 0.15 | 0.8x damage, 3x bleed. |
| Tail Slam | 10 | 0.15 | 1.4x damage, 1.15x reach, 2.5x bleed, 1.3x knockback. 25% chance to break a bone. |
| Spike Sweep | 16 | 0.25 | 0.9x damage, 1.6x reach, 1.5x bleed, 1.8x knockback. |
| Charge Headbutt | 15 | 0.30 | Runs at 1.25x sprint speed until it hits. 1.3x damage, 1.4x knockback, 20% chance to break a bone. Stuns for 2 s, scaled by the mass ratio (0.4-1.5x). |
| Shoulder Shove | 10 | 0.20 | 1.2x damage, 1.5x bleed, 2.6x knockback (throws off a latched raptor). 15% chance to break a bone. Stuns for 0.6 s. |

Other abilities:

| Ability | Kind | CD | Stamina | Effect |
|---|---|---|---|---|
| Pounce | leap | 14 | 0.25 | Leaps forward at 11 m/s, rising 4.2 m/s. Landing on prey at least 0.4x the raptor's mass latches on (packmates excepted). |
| Dodge Leap | leap | 8 | 0.15 | Hops 8.5 m/s in the strafe direction (forward without one). Attacks miss for 0.6 s, and latched raptors are thrown off. |
| Fear Roar | area | 30 | 0.20 | Radius 35 m x (0.5 + 0.5 growth). Animals of other groups under 0.6x the roarer's mass flee for 6 s and deal 30% less damage. Latched raptors let go. |
| Alert Call | area | 25 | 0.10 | Marks carnivores within 90 m for 20 s. Marked animals take 15% more damage and show red to the caller's group on the HUD, nameplates and map. |
| Herd Bellow | area | 40 | 0.15 | Gives the caller's group within 45 m +15% armour for 10 s. |
| Sprint Burst | self | 20 | 0.10 | 4 s at 1.3x sprint speed, with no stamina drain. |
| Defensive Stance | self | 20 | 0.10 | 10 s at half speed, with +10% armour and +35% more on the flanks. |
| Hunker | self | 24 | 0.10 | 8 s at 15% speed, with +30% armour. Reflects 40% of the damage taken back onto the attacker. |
| Mend | self | 60 | 0.10 | Stops bleeding and heals 15% of max health over 10 s. Not while latched. |

Latching (Pounce):
- The raptor rides the victim's flank and bites every 0.6 s, for 0.16x damage plus bleed. Holding on costs 0.07 stamina/s, and the hold lasts at most 6 s.
- The victim slows by 20% per raptor on it (to at most 60%).
- The victim shakes the raptor off by turning hard, sprinting, jumping and attacking. Larger victims shake faster.
- A thrown raptor is stunned for 1.2 s. Jump lets go on purpose.

Status effects (`CRET_ST_*`): stun, fear, slow, latched, latching, defensive, marked, burst, evade and mend.
- A stunned animal cannot act.
- A frightened animal cannot strike, and deals 0.7x damage.
- Defensive armour stacks with species armour, capped at 85%.
- Death clears every effect and releases latches.

Bots use abilities (`bot_abilities` in `sim.c`):
- Raptors pounce on fleeing or flanked prey and hamstring.
- Tyrannosaurs roar at packs and use the crushing bite on big prey.
- Stegosaurs take a stance and slam.
- Pachycephalosaurs charge lone predators and bellow.
- Sauropeltas hunker under raptors and shove.
- Gallimimus dodge-leap when a predator closes and call alerts.
- All use Mend when bleeding badly and the fight is over.

## Quests and places

On first use, `cret_pois_build` names up to 32 places from the baked world data, working on 32 m blocks:

| Kind | Where | Count, spacing |
|---|---|---|
| Lake | The dry shore of the largest connected `water_kind` lakes | 8, 300 m |
| River | The highest river flow away from the coast | 6, 450 m |
| Meadow / grove / marsh | The densest fern, cycad or horsetail vegetation layer (meadows and groves with fewer trees) | 4 of each, 450 m |
| Peak | The highest ground with a walkable spot within 60 m | 2, 1800 m |
| Beach | Sand next to the sea | 2, 1500 m |

Names come from a hash of the place, for example "Mossy Pool", "Talon Ford", "Misty Fernfield", "Sun Glade" or "Crested Fen". No two are the same. World 1 has 17 places.

Quests are server-side (`cret_progress_step`): one personal quest per animal, plus one per group of two or more.
Every quest is one the animal can do: a kind with nothing feasible is struck off and another kind is rolled.

| Quest | Who | Goal | Where | Growth | Marks |
|---|---|---|---|---|---|
| Drink at <lake or river> | all | Drink there for 8 s | A lake or ford in the band, else the nearest water in it ("the marked water") | +5% | 20 |
| Graze at <meadow, grove or marsh> | herbivores | Graze there for 20 s (group: 40 s) | A forage place in the band, else thick plants in it | +6% | 25 |
| Patrol to <place> | juveniles and adults | Visit 3 places in order | Each leg within the band | +8% | 40 |
| Hunt a <species> | carnivores | Kill 1 (group: 2) | Prey the hunters can kill (below) | +10% | 50 |
| Drive off a predator | herbivores of 0.6 growth and up, not the gallimimus | Wound predators by a quarter of their health in total | - | +10% | 50 |
| Catch 3 small animals | carnivore people (small prey live around people) | Catch 3 critters anywhere | Leads to good critter ground in the band | +5% | 20 |
| Stay near another <species> | the young of herds and packs (and first steps) | 30 s within 25 m of a group member, else one of its species | The nearest such animal, within the band | +4% | 20 |
| Rest at <place> | all | Stand still or rest there for 20 s (group: 40 s) | A place in the band | +4% | 20 |
| Find <place> | all | Reach a named place not visited yet | A place in the band | +6% | 30 |
| Eat N kg from a carcass | carnivores with room in the stomach (food at most 0.6) | Eat 40% of a full stomach (0.5-40 kg) from carcasses | A carcass with twice that much meat, within the band's far end | +5% | 25 |
| Raid the <species> nest | carnivores of 0.5 growth and up | Eat an egg from another group's nest | That nest, within the band's far end | +8% | 40 |
| Guard your nest | animals with a nest with eggs | 60 s within 40 m of it | The nest | +6% | 30 |
| Build a nest | adults (0.9) without a nest | Build one (hold N for 5 s) | - | +10% | 60 |

Rules (`progress.c`):
- **Distances by growth.** Hatchlings (below 0.5) are sent 70-150 m, juveniles (below 0.9) 150-300 m, adults 250-500 m, measured to the place or spot (to the first leg of a patrol). Never further than half of what the animal's stomach and thirst last at a trot: a grazer eats on the way, so only thirst counts for it, and a drinking quest's own water does not count. Groups use the youngest member's band and the shortest reach. Patrols are for juveniles and adults (named places lie 300-450 m apart).
- **Survival first.** A thirsty animal (water below 0.45) is sent to the nearest water, however close, and rolls drinking eight times as often; a hungry one (food below 0.4) rolls grazing, catching and scavenging six to eight times as often.
- **Hunts.** The prey species must have a living animal that weighs at most 3 times the hunter (for a group quest, the members' combined mass); raptors leave sauropeltas past 0.6 growth alone (armour). Species are weighted by how many such animals live. A hatchling raptor (3.4 kg) has no such prey and catches small animals instead; a hatchling tyrannosaur (340 kg) hunts gallimimus and pachycephalosaurs; an adult raptor (80 kg) young ones.
- **Defend** only for stegosaurs, pachycephalosaurs and sauropeltas of 0.6 growth and up, while a predator lives.
- **Variety.** Weights per kind (ordinary quests): drink 2 (0.8 when water is above 0.7), graze 2.5, hunt 3, catch 3 for hatchlings (1 for other carnivores under 200 kg), patrol 1.2, rest 1, explore 1.5, defend 1, scavenge 1.2, raid 1, guard 1.5, nest 0.5, companion 1 (young herd and pack animals). The kind just done is rolled a seventh as often, so the same kind rarely comes twice in a row (`CretQuest.last`, kept while no quest is active and across a reset).
- **Expiry.** Walking there one and a half times at the animal's (or the slowest member's) walking speed, the time the quest takes, and two minutes more; at least four minutes. Hunts, defence, nests and group hunts last 15 minutes. A new quest follows 10 s after a personal quest (4 s after a first step), 60 s after a group quest, and 20 s after an expiry.
- Group quests (graze, patrol and rest for herds, hunt, patrol and rest for packs) pay 1.5x, to every member. Adults get 1.5x marks instead of growth.
- Bots in a group take on their leader's drink, graze, patrol or rest errand, so the herd or pack travels as one.
- Places count as visited within their radius (at most 60 m from the middle), once a second (`CretProgress.visited`).

### First steps

A new character's first quests teach the game in order, each a little further away (`chain_quest`). Bots skip them.

| Step | Quest | Where |
|---|---|---|
| 1 | Eat: graze for 12 s (herbivores), or catch 2 small animals (carnivores; else scavenge or hunt) | 40-100 m |
| 2 | Drink for 6 s | The nearest water 50-200 m off (else the nearest within 400 m) |
| 3 | Stay near another of its species (a group member first) for 20 s | One within reach, at most 600 m |
| 4 | Use ability 1 with another animal or small prey within 25 m | Here |
| 5 | Find a named place not visited yet | 100-400 m (up to 700 m past hatchling; a visited one when all are known) |
| 6 | Build a nest | Adults only: younger animals get ordinary quests until they are grown |

Each pays 15-60 marks and 4-10% growth, and the tracker labels it "FIRST STEPS n/6".
The steps done are stored per character (`first_steps` in the save, `CretProgress.chain`), so they carry over death and a reload and never repeat once done. A step that cannot be offered right now (no water or companion in reach) waits for the next offer and an ordinary quest comes meanwhile.

On the HUD:
- The quest tracker sits top left: marks, then each quest in plain words ("Drink at Mossy Pool", "Catch 3 small animals", "Stay near another Velociraptor"), with its progress, the distance and compass direction ("120 m NE", or "here"), and the reward.
- The quest target has a diamond on the compass strip and an in-world marker with its name (the place, or "Water", "Plants", "Small prey", "Carcass", "Nest", the species to stay near) and distance.
- The minimap and the island map show quest diamonds (gold for personal, green for group), place dots, nests and marked predators. Places the character has visited are solid and named in full on the big map; the others are faint until visited, so the map fills in.
- A note over the ability bar names each completed quest and its reward: "Done: Drink at Mossy Pool  +20 marks, +5% growth".

Measured with `tools/questprobe.c` (below).

### Quest probe

`tools/questprobe.c` plays a scripted player of each species at each stage (hatchling 0.35, juvenile 0.65, adult 1.0, put back to its stage when quests grow it) for 30 minutes after a minute of title screen, on the full island with the population director on, two sessions each. It drinks and eats when low, otherwise trots to the quest and does what it asks. It judges every quest when offered by its own rules: a hunt with no living prey of that species within 3x the hunter's mass, defend for an animal under 0.6 growth or a gallimimus, a place the trip to which (at a trot) outlasts the stomach or thirst or which cannot be walked to before the quest expires, catching with small prey off, and for the new kinds a companion of a species with none alive, a nest for an animal not grown, guarding without a nest with eggs, scavenging with no carcass that big.

```bash
make -C ocean/cretaceous questprobe && ./build/cretaceous/questprobe --sessions 2 --minutes 30   # --veteran: first steps done; -v lists every quest
```

Totals over the 18 species and stages, two seeds (3000 and 5000), 36 player-hours each. "Before" is the 5.0 branch with the same probe (its new kinds left out):

| | Offered / h | Done / h | Infeasible | Starvation deaths |
|---|---|---|---|---|
| Before | 20.5 | 15.9 | 20 | 5 |
| After, new characters | 30.8 | 25.0 | 0 | 4 |
| After, first steps done (seed 3000) | 27.0 | 20.8 | 0 | 2 |

By stage, quests done per hour (seed 3000, new characters; before in brackets): hatchlings 24-56 (28-63), juveniles 11-45 (2-8), adults 9-29 (3-6). Hatchlings already had short errands; juveniles and adults now finish three to five times as many quests. Before, juveniles and adults were sent a mean 300-760 m (up to 2.7 km) and spent 50-80% of the session travelling; now the mean is 130-240 m within each band and travel is 25-80% (the rest doing the quest or feeding). The infeasible quests before were places further than the stomach or thirst lasts (10), hunts with no prey within reach of the hunter's size (7) and defend for juvenile herbivores and gallimimus (3). The starvation deaths left are scripted predators that fail to catch prey; none died on the way to a quest place.
New hatchlings finish the five first steps they can do in 4-16 minutes (tyrannosaur 4, gallimimus 5, raptor and sauropelta 12, pachycephalosaur 13, stegosaur 16). Adults finish all six in 3-25 minutes where the scripted player manages the nest (raptor, tyrannosaur, pachycephalosaur); the scripted stegosaur, sauropelta and gallimimus adults often stop at the nest step within the 30 minutes (not investigated further).

The balance run (`make balance`, 2 seeds x 15 min, bots only) completes 27 quests instead of 10 (companion 8, graze 9, drink 3, raid 2, hunt 2, defend 2, explore 1); the tick cost is unchanged (0.030 ms against 0.031 ms).

## Characters

Characters are saved to `~/.config/cretaceous/characters/<name>.ini`, under `$XDG_CONFIG_HOME/cretaceous/characters` when that is set.
Names keep letters, digits, `-` and `_`, up to 23 characters. The file is written to a `.tmp` file first, then renamed.

```ini
# Cretaceous character (written by the game)
name = Testy
species = trex
growth = 0.3575
health = 1.000
food = 0.987
water = 0.981
position = 2453.3 3223.7
marks = 140
quests_done = 4
first_steps = 6
visited = 0 3 7
unlocked = trex/rend_bite raptor/mend
loadout = fear_roar crush_bite rend_bite
```

- `unlocked` lists `species/ability` pairs. They map to bit `species*2 + choice` of `CretProgress.unlocked`.
- `loadout` lists the ability ids in slots 1-3.
- `position` is left out after a death.
- `first_steps` counts the first-steps quests done (6: all). A save without it counts as done when `quests_done` is 6 or more.
- `visited` lists the indices of the named places seen (the world's place list, which is the same for every build of a world).

Choosing a character:
- On the title screen, the Character row cycles through the saved names. On "New", Enter types a name.
- The last character played is remembered in `settings.ini` (`character = ...`).
- From the command line: `play --character NAME`, or `join ADDRESS --character NAME`.
- Picking a different species for a saved character starts a new life: the respawn growth and no position. Marks and unlocks carry over.

When the game saves:
- Solo or host: on Play, every 60 s, at death, after a species change or a purchase, and on quit.
- Dedicated or listen server: a joining client sends its name (`MSG_PROFILE`), and the server loads that character from its own config directory, or creates it.
  It saves the character every 60 s and when the client leaves. The freed slot goes back to a bot.

Death:
- Marks, unlocks, quests done, first steps and places seen are kept.
- The next life hatches at `player_growth` (0.35), or as a juvenile (0.5) at a nest of the group.

## Nests

- A fed adult (growth at least 0.9, food at least 0.3) builds a nest by holding N for 5 s. It needs to be at least 60 m from any other nest, with no nest of its own yet. Building costs 0.2 food.
- A new nest has 2 eggs. Another egg comes every 90 s while a fed adult of the group stays within 40 m, up to 5.
- A nest is abandoned after 10 minutes with nobody tending it.
- A dead player of that species respawns at their own or their group's nest with eggs, as a juvenile (0.5) in the same group. It costs one egg.
  Bots hatch at any nest of their species whose group has room.
- Carnivores raid other groups' nests. Eating at a nest takes an egg every 1.5 s (8 kg of food each).
- Bot adults in a group start a nest about once in a few calm minutes.
- The HUD marks nests within 150 m and shows them on the map with their egg count. The marker floats well above the nest and fades close up, so it never hides it.
- The renderer draws each nest and its eggs (`render/nest.c`, README "Nests, eggs and carrying food"): a mound of soil, litter and sticks on the terrain, sized to the species, with as many eggs as the sim holds, elongated for theropods and rounder for ornithischians. A nest being built rises from the ground over the 5 s hold.

## Carrying food

Carnivores carry food in the mouth (`sim.c`, the carrying section), as in Path of Titans.
- A carry press (`CretInput.carry`, B for people) picks up what lies at the mouth, or puts down what it holds. It can pick up:
  - a carcass that weighs at most `CRET_CARRY_RATIO` (a quarter) of the carrier's mass; a body weighs twice its meat, so a half-eaten one is lighter;
  - the rest of a critter: a carnivore too full to swallow its whole catch leaves the rest lying (at least 100 g);
  - an egg from another group's nest (`CRET_EGG_KG`, 8 kg, the food an egg gives when eaten there; the nest loses it, as in a raid).
- Items are carcass slots (`CretCarcass.item`: body, critter or egg, and `carrier`). A new item takes a free slot or the oldest item nobody holds; a body never pushes out a carried item.
- Carrying slows the carrier by `CRET_CARRY_SLOW` (0.8) x the load ratio, so a full load costs 20% of its speed. The load is fixed when it is picked up (`CretAgent.carry_kg`), so client prediction applies exactly the same slowdown.
- A full mouth cannot bite (the primary attack unless it is a tail, the special bites and the pounce), eat or drink. Tail swings and slashes still work.
- The carrier drops the load on a blow of `CRET_CARRY_KNOCK` (8%) of its max health, on a blow that throws it (knockback over 3 m/s), when stunned and when it dies. Swapping species puts the load down.
- A carried item keeps rotting. Nobody eats from it while it is held (it is not in the nearest-carcass observation either). Once it is down, anyone eats from it, the carrier and the young at the nest included.
- Bots: a fed grown predator (food over 0.97 after feeding, or over 0.8 when calm) whose group has a member under growth 0.6 within 150 m of the group's nest (within 400 m of it) carries a kill it can lift home. It picks about half of the kills, by slot and carcass index, so the choice is deterministic and draws nothing from the rng. It puts the load down within the nest's radius plus 1 m and rests there. A bot that turns to anything else (a hunt, a flight) puts its load down. Grown animals that are not starving leave food lying at their own nest to the young.
- Stats (`CretStats`): `pickups`, `carried_home` (put down at the carrier's own nest) and `forced_drops`; `tools/balance.c` prints them.
- In `make balance` (4 seeds, 60 minutes) bots carry nothing: no young hatch at nests there, so no parent has a reason to. The rest of the report is identical to 5.0's; the tick cost is within noise (0.0206 against 0.0203 ms).

### Carrying and the policy contract

Carrying is not in the policy action head, as nest building is not; `CRET_CONTRACT_VERSION` stays 5 and the observations are unchanged (243).
- Adding a choice to the action head would change `CRET_ACTION_SIZES` and invalidate every trained policy, and the head is one categorical: carrying would compete with attacking and eating every tick.
- What carrying is for, feeding young at a nest, is a long-horizon group behaviour. The RL setup has no nests in the policy head and no young to feed: agents respawn without hatching.
- Policy agents still live with it. Bots in the same sim carry food, a carried carcass is not in the self block's nearest-carcass feature (keeping its meaning: the nearest carcass that can be eaten), and a policy's bite can make a carrier drop its load. A carried carcass used to be impossible, so this changes no existing observation.

## Hunting small prey: senses, stalking and scent

A hatchling raptor's first quest is to catch small animals, and a player could not: the HUD gave only a distance, the critters were hard to tell from the ground cover, and walking or sprinting at one worked no better than care.
This section is the design that replaced that; README "Small prey and the early game" has the controls.

### What the animal notices (`scent.c`)

`cret_critter_sensed` and `cret_prey_noticed` decide which critter the HUD may lead to:
- **seen**: in front (within 80 degrees of the heading), at 16 m when it moves, 7 m when it is still, 3 m when it hides; less in thick plants (`cret_forage`, down to 0.55) and half from dusk to dawn;
- **heard**: one moving close by, 6 m plus 1.5 m per m/s of its speed, all round;
- **smelled**: `cret_scent_strength` (below), at 0.35 of the sniffing range when not sniffing.

### Scent (`scent.h`)

- **Trails.** The sim records the last 8 places each critter slot and each agent passed (`CretSim.crit_trail`, `agent_trail`): a point when it has moved 1.5 m from the last one, or moved at all after 6 s. It is written at the end of `cret_sim_step` from positions the sim already has, every third tick per trail (staggered), only while a person is in the sim; a client records the same from its snapshots. No random numbers, and nothing the sim steps on reads it except the critters' noses (below).
- **Wind.** `cret_wind`: blowing toward 0.64 + 0.3 sin(0.009 t) rad on the sim clock, strength 0.75 + 0.9 x overcast. The plant wind in `render/veg.c` uses the same formula on the render clock, so the two agree to within the difference of the clocks (the angle swings 0.3 rad over about 12 minutes).
- **Smell.** A source (a critter where it is, a carcass, a trail point laid `age` s ago) has drifted downwind by 0.25 m/s x strength x age. A nose at distance d from where the smell has got to smells it when d is under range x lobe, with lobe = clamp(0.55 + 0.7 cos(angle off downwind), 0.1, 1.25): 1.25 straight downwind of the source, 0.55 across the wind, 0.1 upwind (prey that the wind blows away from you cannot be smelled). Strength is (1 - d / reach) x (1 - age / life), life 45 s (rain cuts it to 40%, and rain shortens every range by up to 45%).
- **Ranges** (sniffing, straight downwind, at adult size; growth scales them from 0.45 at hatching):

| Species | Critters | Carcasses | Other animals' trails |
|---|---|---|---|
| Velociraptor | 28 m | 140 m | 70 m |
| Tyrannosaurus | 24 m | 450 m | 160 m |
| herbivores | - | - | - |

  A carcass's range is scaled by its size (0.5 for the rest of a mouse, 1.5 for a big body). A hatchling raptor (growth 0.35) smells a mouse at 18 m straight downwind and 10 m across the wind.
- **What it shows.** Critters and carcasses with their position. Another dinosaur only through its trail: the freshest point at least 3 s old that reaches the nose, its age and the direction it went from there, never where the animal stands.
- **Cost.** Holding V creeps (at most 0.3 forward, 60% of the walk) and blocks sprinting; sniffing does not hide the animal. The query is a few hundred distance tests; it is not in the sim step.

### Stalking (`critters.c`)

Critters notice an animal by the largest of three senses (`cr_detect`):
- **hearing**: the detection range x (speed / 4.5 m/s)^2, so a sprint is heard at the full range (mouse 9 m), a walk at a sixth, a creep hardly at all;
- **sight**: 0.45 of the range (a third of that for an animal standing still) plus half its height, x the watch: each idle or wandering critter alternates head up (watching) and nose down (feeding), on a cycle from its pool slot and the sim tick (`cret_critter_watch`: mouse 1.2 s up, 2.8 s down; lizard 1.5 / 2.5; bird 0.8 / 1.6; Repenomamus 2 / 3; each slot +-20%). Feeding, it sees 0.3 of that; the head takes 0.3 s to come up, so a stalker that freezes when it sees the head lift is not seen. A hiding mouse sees 0.15 of it;
- **smell**: mammals 1.2 x the range, lizards 0.7, in the core of the predator's plume (1.25 straight downwind of it, 0.08 across or upwind; birds and fish none).

Once it bolts, a critter jinks 60-90 degrees when the chaser comes within 2.5 m (at most every 0.4 s), and a mouse or lizard still chased hard at the end of its second flee leg (the chaser within 0.6 of its detection range and faster than 2.5 m/s) dives into a burrow or under a stone and is gone (`CretCritterStats.escaped`). A hiding mouse now hears a run and bolts again.

A bite catches small prey 0.13 s into the swing (`resolve_snap`, `CRET_SNAP_TICKS`) within 1.25 x the bite's reach, unless an animal's body is nearer the jaws; the full bite on an animal still lands at the species' hit time. Before, the raptor's bite landed 0.5 s after the press, by which time a fleeing mouse had moved 2.3 m.

Bots (`cret_critter_bot`) and `tools/critprobe.c` stalk the same way: walk to 9 m, creep while the prey feeds, stand still while it looks up, chase one that bolts.

### Catching probe (`tools/catchprobe.c`)

`make -C ocean/cretaceous catchprobe && ./build/cretaceous/catchprobe` puts one critter on open ground and a hatchling raptor (growth 0.35) 20 m from it downwind, across the wind or upwind, and plays it like a person at 15 and at 60 fps (it decides once a frame, sees the world 0.2 s late, presses wait for the next tick): careful (walk to 9 m, then creep while the prey feeds and freeze while it watches), walk straight in, or sprint straight at it; any style chases prey that bolts. Catch rate within 40 s:

CATCH_TABLE

### Hunting and the policy contract

Scent is not in the observations or the action head; `CRET_CONTRACT_VERSION` and `CRET_OBS_SIZE` are unchanged, and the trainer has no people, so it records no trails and spawns no critters. To add it later:
- **Action**: sniffing would be a new flag rather than a choice in the last categorical head (it is held while moving, as sprint is): a sixth head `{2}` (`CRET_ACTION_SIZES` grows by one), decoded into a `CretInput.sniff` bit that `move_agent` turns into the creep and no-sprint. That changes the action layout and invalidates trained policies, so it needs a contract bump.
- **Observation**: a scent block appended after the current 243 floats: the wind (2: downwind direction egocentric, x strength / 1.65); the nearest smelled critter, carcass and animal trail (3 x 4: egocentric forward and right / range, strength, and for a trail its age / 45 s and its heading egocentric as sin and cos would replace the strength), with zeros when nothing is smelled and the range scaled by whether the agent sniffed last tick. 14 floats, computed by `cret_scent_query` with `sniffing` from the agent's last input.
- **Critters in training**: the trainer has none. Policies would first need critters in their sims (a person-free spawn rule, for example around each carnivore below growth 0.55), and their positions are already in no observation; the scent block would be the only way a policy finds them, which is the point.
- **Rewards**: food from a catch already counts through `ev_food`; nothing new is needed.

## Net protocol 13

Carrying needs three things on the wire (docs/net.md):
- the carry press, in the top bit of the input flags (it was always 0);
- the own carried slot and load in the own state, for prediction and the HUD;
- per carcass, its item kind, critter kind and carrier.

## Policy contract 4

`CRET_CONTRACT_VERSION` is 4. Observations grow from 197 to 243 floats. The new floats are appended; the first 197 are unchanged.

Self block, 22 floats from index 197:

| Index | Feature |
|---|---|
| 197 | stun / 2 s |
| 198 | fear / 6 s |
| 199 | slow fraction |
| 200 | raptors latched on / 3 |
| 201 | latching: hold time left / 6 s (at least 0.05 while latched; 0 when not) |
| 202 | defensive armour (stance or hunker) |
| 203 | marked |
| 204 | sprint burst / 4 s |
| 205 | evading |
| 206-208 | cooldown fraction for slots 1-3 (1 when the slot is empty) |
| 209-211 | slots 1-3 ready (cooldown, stamina, not stunned or latched) |
| 212 | has a quest (personal, else group) |
| 213-214 | quest target, egocentric forward and right / 1000 m (clamped to ±2): the place or spot, the next patrol place, or the animal a companion quest stays near; 0 for hunt, defend, ability and nest quests |
| 215 | quest progress / goal |
| 216 | has a nest |
| 217-218 | nest, egocentric / 300 m |

Then 4 floats per neighbour (6 x 4, indices 219-242, in the contract 3 neighbour order): stunned, afraid, latched (rider or ridden), marked.

Actions: the heads are `{5, 5, 3, 2, 7}`. The last head is none, attack, eat or drink, call, ability 1, ability 2, ability 3.
Nest building (`CRET_ACT_NEST`) is not in the policy head: bots and people only.

Binding (`cretaceous.h`, `[env]`):
- `reward_quest` (default 0.25) per quest completed. A group quest counts for each member.
- Two new logs: `quests` (per agent and episode) and `abilities` (casts per agent and episode).

## Net protocol 7

The protocol is kept small, and every addition is appended:
- Inputs carry the ability (1-3) in bits 5-6 of the flags byte. The action field already fits `CRET_ACT_NEST`.
- `MSG_PROFILE` (client name) and `MSG_UNLOCK` (slot-3 choice 0 or 1) are reliable client messages.
- Snapshots end with a block that has three parts:
  1. A status byte and `mark_by` for every animal.
  2. Per person (slot 0 and human slots): marks, unlocks, quests done, loadout and cooldowns, stun, defence, slow, latchers and nest progress, the personal quest, and the group quest.
  3. The active nests.

## Balance tooling

```bash
make -C ocean/cretaceous balance BOX3D=/path/to/box3d
./build/cretaceous/balance --seeds 2 --minutes 15      # from the repository root
CRET_BALANCE_TRACE=1 ./build/cretaceous/balance ...   # every agent's quest every 2 min
```

It prints:
- the places;
- the kill matrix, with deaths by environment, deaths per slot-hour and mean growth;
- ability uses and hits, and latch, stun, fear and mark counts;
- quests by kind and the marks earned;
- nest numbers;
- the tick cost, measured in thread CPU time.
