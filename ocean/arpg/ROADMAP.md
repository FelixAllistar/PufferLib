# Hearthwild: from frontier slice to a developed summoner factory game

The direction: a casual summoner power fantasy with persistent industry and
optional RTS precision. Pets do work that would otherwise require repeated
machine placement and belt management. Buildings make durable spatial decisions
meaningful. Combat should eventually let the player's industry fund spectacular
overkill, not demand constant defensive chores.

## Delivered in this slice

Camera regression fix; coordinate-stable streamed terrain; saved world edits and
outposts; independent pet commands and selection groups; eight companion slots;
renewable extraction; Burrower/Ember terrain work; mobile refining; bridges;
Starfire artillery; coarse distant production/movement; and CPU/CUDA tests.
These are working mechanics, not a claim that the following stages are complete.

## 1. Make the early loop consistently pleasant

- Playtest multiple seeds from a fresh save without developer-supplied currency.
- Measure first extractor, first camp clear, first Ember and first Starfire times.
- Measure worker idle time, unreachable orders, unnecessary returns and pet losses.
- Add a persistent order-status panel: traveling, working, waiting on stock,
  blocked route, recovering, or suspended outside the active region.
- Add queued orders, double-click class selection, minimap orders, a command
  cancel button, safe previews and repeated-placement ergonomics.
- Tune formation spacing, ranged/melee roles and target priorities before adding
  more health scaling. Build/pet costs, travel times and real sustainable seam
  output should be legible, not a hidden throughput puzzle.

Exit criterion: a new player can establish two independent work sites and clear
a camp without debugging targeting or pathfinding.

## 2. Make pets the interesting part of industry

- Introduce a small set of differentiated materials and recipes, not dozens of
  near-identical intermediary parts.
- Give specialist pets replaceable work modules and explicit work areas.
- Make automatic matching handle demand, available stock, safety and congestion;
  let the player pin exceptions rather than route every trip manually.
- Add visible carried loads and stockpiles only where they create useful choices.
- Use buildings for storage, crossings, protection and siege positions. Use pets
  for extraction, transport, refining, repair and construction assistance.
- Prototype outposts that recover/repair companions without making every base an
  upkeep or tower-defense obligation.

Exit criterion: expanding one production chain changes companion composition
and geography, not just the number of identical extractors.

## 3. Make the frontier support lasting consequences

- Replace linear world-record searches with spatial indexing and bounded caches.
- Stream active regions around multiple work sites, not only the keeper.
- Implement background excavation and a clear policy for distant combat before
  promising fully simulated remote colonies or offline catch-up.
- Add biome-specific resource/encounter rules, readable landmarks and scouting.
- Add save migrations, recoverable backups, world budgets and long-run soak tests.
- Keep terrain and point-of-interest keys independent of chunk load order.

Exit criterion: returning to a large established network is fast, deterministic
and faithful to its saved state; remote activity has no hidden simulation rules.

## 4. Develop destruction and water together

- Track terrain/material strength and excavation depth, with readable progress.
- Introduce an actual water-state model before claiming flooding or fluid volume
  conservation. Define what a dam, bridge, channel and explosion really change.
- Prototype tunnels/caves only once navigation, sight and layer selection are
  unambiguous. Current digging opens surface corridors; it is not a cave system.
- Persist scars, rubble and rebuilding. Destruction must update routes and work
  assignments without trapping units or silently erasing an outpost.

Exit criterion: the same destroyed bank produces the same navigable, saved water
state on CPU, after reload, and across region transitions.

## 5. Let industry buy ridiculous combat

- Add light enemy populations, varied camps and visible escalation rather than
  generic time-driven hordes around the player.
- Build an arsenal with distinct uses: piercing siege, terrain melting, area
  denial and expensive Starfire-scale strikes.
- Tie ammunition to the pet production chain; avoid mandatory chores between
  every satisfying shot. Make firing feedback, travel/impact timing, destruction
  and sound support the fantasy.
- Keep friendly-fire and retaliation choices explicit. A casual mode should not
  turn every spectacular shot into an hour of repair work.

Exit criterion: each late weapon creates a different tactical/geographic result
and feels powerful without making the underlying simulation unreliable.

## 6. Train and verify the optional brains

- Train new v3 policies; do not reuse incompatible older checkpoints.
- Add command-priority curricula and tests for changing automatic work tasks.
- Measure GPU-to-CPU transfer, navigation failures and production efficiency.
- Extend observations/actions only with an explicit schema change and regression
  tests. Eventually include work queues, world summaries and demand signals.
- Compare scripted assistance, learned task selection and human-pinned orders on
  the same saved scenarios. Optional autonomy should reduce input, not surprise
  the player by taking a worker away from a pinned job.

Exit criterion: learned assistance is observably helpful, honestly labelled and
always yields to a clear player instruction.
