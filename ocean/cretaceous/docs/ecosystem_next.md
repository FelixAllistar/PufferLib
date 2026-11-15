# Ecosystem: what comes after the first build

Status: **proposals.** `ecosystem.md` is the first build (energy, plants,
fear, drives, groups, population). This file collects what would give the
world more depth afterwards. Each entry says what the player would see and
what it costs per tick, because the rule for all of it is the same: depth
must come from state that is cheap to keep, not from more thinking per
animal.

## The cost rule

Anything added must be one of:

- a **number per animal** that scales something already computed (a trait,
  a condition);
- a **short list per group** (at most 8 entries) read at decision time;
- a **coarse grid** over the island, updated a slice per tick;
- an **event** that happens rarely and touches a few animals.

Nothing may add a loop over all animals inside a loop over all animals, a
path search, or per-tick work for animals nobody is near.

## 1. Simulate far groups as one thing

The biggest lever, and it buys cost room for everything else. A group with
no person within about 400 m does not need 8 animals steering and looking
round. It becomes one record: position, heading, members with their energy
and health, what it is doing. It moves along its drive's target at group
speed, eats from the biomass grid, loses and gains energy by the same
equations, and meets other far groups by rule (a predator group that
reaches a herd rolls the hunt from the same `P_catch * P_kill` arithmetic
and both groups' records change). When a person comes within range the
members are placed around the record and the full simulation takes over.

- Player sees: a living island everywhere, herds that were somewhere else
  an hour ago, kills that happened without them.
- Cost: a handful of operations per far group per second. It makes the
  animal count on the island a choice (hundreds) rather than a limit.
- For training: learned animals always run in full; far groups are part of
  the world they meet.

## 2. Individuals: traits and inheritance

Three or four numbers per animal, set at hatching: boldness (scales flight
distance and how close to danger it feeds), aggression (scales defend and
hunt urgencies), sociability (scales staying with the group), and size
(a few percent either way). Young inherit the mean of the nest's parents
plus a small random step.

- Player sees: the bold stegosaur that does not run, the skittish herd;
  over many game days, populations that drift (hunted herds get warier).
- Cost: none (multipliers on urgencies already computed).

## 3. Places a group remembers

Each group keeps up to 8 remembered places with a kind and a freshness:
water that was safe, a rich feeding cell, a rest site, a nest, a place a
member died. Drives look in this list before searching, and a place decays
or is replaced.

- Player sees: home ranges, herds that return to the same water at dusk,
  ground that is avoided after a kill, routes you can learn and ambush.
- Cost: a list of 8 per group.
- With it, **trails**: a wear grid (64 m cells) that movement adds to and
  time removes; animals prefer worn cells when travelling. Visible later as
  trampled ground. Predators can read it.

## 4. Territory and display before fights

Predator groups mark the cells they use (the scent grid already exists).
Entering fresh marks of a stronger group raises risk; of a weaker one,
invites a challenge. A challenge starts as a display: both sides face off,
call, posture for a few seconds, and the side with the lower total of size,
health and numbers usually backs down. Fights happen when the sides are
close or one is desperate.

- Player sees: standoffs, roaring matches, being driven off a carcass by
  posture alone, having a territory of your own that the others respect.
- Cost: a comparison when two groups meet.

## 5. Wounds that last

A hit above a threshold leaves a wound with a place (leg, body, head) and a
severity that heals over a game day or more with rest and food, or worsens
when starving. Leg wounds slow, body wounds bleed and tire, head wounds
dull the senses. Herds slow for a wounded member for a while, then leave
it. Predators already prefer the weak.

- Player sees: the limping straggler, a predator that picked a bad fight
  and now starves, your own old injuries mattering.
- Cost: two numbers per animal.
- With it, **blood trails**: a wounded animal leaves marked points in the
  scent trail that the sniff view shows in red and that last longer. A hunt
  can be: wound it, follow it, finish it.

## 6. Reputation

Each group remembers up to 4 individuals that attacked it and how long ago.
A remembered attacker is treated as approaching even when it is not, and
its flight distance is doubled. Animals that walk past without attacking
are, over time, let closer (habituation).

- Player sees: the herd you hunted yesterday runs sooner from you than from
  another raptor; a herbivore player can graze near a herd that knows them.
- Cost: a list of 4 per group.

## 7. Seasons and weather that matter

One slow number, the season, over about 8 game days: it scales plant
regrowth, dries the smaller water bodies, and shifts the time-of-day
curves. Dry season concentrates everything at the remaining water, which is
where the predators wait. Storms (already drawn) suppress activity and
cover approach.

- Player sees: the lean time and the fat time, migrations, crowded
  waterholes.
- Cost: one number and a multiplier.

## 8. Different diets, so species do not all compete

Give each herbivore a feeding height and a preferred plant layer (the world
already has separate layers for ferns, cycads, horsetails and scrub): the
stegosaur and sauropelta feed low, the pachycephalosaur on scrub, the
gallimimus takes plants, critters, eggs and carrion. Then the biomass grid
has two or three layers rather than one.

- Player sees: mixed herds that share ground without starving each other,
  gallimimus raiding nests, each species in its kind of country.
- Cost: the grid is 2 to 3 times larger (still under 200 KB).

## 9. Signs the player can read

Hunting is the core of play, so the world should leave evidence: tracks
(the trail points already recorded, drawn as prints near the camera), dung
and feeding signs at grazed cells, drag marks to a nest, bones where a
carcass was, flattened plants where a herd rested. The sniff view ties them
together with age.

- Player sees: a story to follow instead of a distance on the HUD.
- Cost: drawing only, from data the simulation keeps anyway.

## 10. Calls that mean something

Calls become events with a kind: contact (where are you), alarm (with a
direction), food (a kill here), threat (display), young begging. Animals
react by kind and by who called. A player's call key picks the kind from
context, so calling a pack to a carcass or sounding an alarm works on
scripted and learned animals alike.

- Player sees: a soundscape that carries information; pack play that works
  through the same channel the animals use.
- Cost: an event per call.

## 11. Life history

Clutch size and survival of young depend on the parents' condition; old age
slows and then kills; a group that loses its adults breaks up and its young
try to join another. Combined with traits (2) this gives generations.

- Player sees: growing up as part of something, a pack that ages.
- Cost: none beyond the population rules.

## 12. Danger at the water

Drinking lowers the head and narrows what an animal senses; water edges
have cover on one side and open ground on the other. This already follows
from vigilance and fear, but it deserves tuning on purpose: the waterhole
is the stage where most hunts should begin.

## What to build first

1. Far groups as one record (1): it pays for the rest.
2. Wounds and blood trails (5) and signs (9): they feed hunting directly.
3. Remembered places and trails (3), then territory and display (4).
4. Traits (2) and reputation (6): cheap, and they make animals individuals.
5. Diets (8), seasons (7), calls (10), life history (11).

Each can land alone. Each needs a row in the probe (`tools/ecoprobe.c`):
what it should change, measured before and after.
