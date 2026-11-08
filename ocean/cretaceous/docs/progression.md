# Progression: abilities, quests, characters and nests

This layer is modelled on Path of Titans. It sits on top of the survival sim:
- every species has abilities on keys 1-3, with cooldowns, stamina costs and status effects;
- the server hands out quests at named places, which pay growth and marks;
- characters are saved by name, and death keeps the marks but not the growth;
- fed adults in a group build nests, where young members hatch and which predators raid.

Code:
- `abilities.[ch]`: the ability table, loadouts and unlocks, status effects.
- `progress.[ch]`: named places, quests and nests.
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

| Quest | Goal | Growth | Marks |
|---|---|---|---|
| Drink at <lake or river> | Drink there for 8 s | +5% | 20 |
| Graze at <meadow, grove or marsh> | Graze there for 20 s (group: 40 s) | +6% | 25 |
| Patrol | Visit 3 places in order | +8% | 40 |
| Hunt <species> | Kill 1 (group: 2) of a herbivore species living in the world | +10% | 50 |
| Drive off predators | Herbivores only: wound predators by a quarter of their health in total | +10% | 50 |

How quests work:
- Places are picked about 400 m away; patrol legs are about 350 m.
- Group quests pay 1.5x, to every member.
- Adults get 1.5x marks instead of growth.
- A quest expires after 20 minutes. A new one follows 10 s after a personal quest, 60 s after a group quest, and 20 s after an expiry.
- Bots in a group take on their leader's errand, so the herd or pack travels as one.

On the HUD:
- The quest tracker sits top left and shows marks, quest lines, distance and reward.
- The quest place has a diamond on the compass strip and an in-world marker with its name and distance.
- The minimap and the island map show quest diamonds (gold for personal, green for group), place dots (named on the big map), nests and marked predators.
- A note over the ability bar announces each completed quest.

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
unlocked = trex/rend_bite raptor/mend
loadout = fear_roar crush_bite rend_bite
```

- `unlocked` lists `species/ability` pairs. They map to bit `species*2 + choice` of `CretProgress.unlocked`.
- `loadout` lists the ability ids in slots 1-3.
- `position` is left out after a death.

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
- Marks, unlocks and quests done are kept.
- The next life hatches at `player_growth` (0.35), or as a juvenile (0.5) at a nest of the group.

## Nests

- A fed adult (growth at least 0.9, food at least 0.3) builds a nest by holding N for 5 s. It needs to be at least 60 m from any other nest, with no nest of its own yet. Building costs 0.2 food.
- A new nest has 2 eggs. Another egg comes every 90 s while a fed adult of the group stays within 40 m, up to 5.
- A nest is abandoned after 10 minutes with nobody tending it.
- A dead player of that species respawns at their own or their group's nest with eggs, as a juvenile (0.5) in the same group. It costs one egg.
  Bots hatch at any nest of their species whose group has room.
- Carnivores raid other groups' nests. Eating at a nest takes an egg every 1.5 s (8 kg of food each).
- Bot adults in a group start a nest about once in a few calm minutes.
- The HUD marks nests within 150 m and shows them on the map with their egg count. The renderer draws no nest or egg models yet.

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
| 213-214 | quest place, egocentric forward and right / 1000 m (clamped to ±2; 0 for hunt and defend quests) |
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
