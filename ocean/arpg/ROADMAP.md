# Codex / Tibo's Takeover: the day before cyberpunk

The direction: a fictional tech-industry satire and casual army-command power
fantasy. A hoodie-wearing Tibo starts with a garage and helpful blue Codex robots;
his automation gradually becomes a world-conquering industrial machine. Show an
ordinary world becoming a dystopia, not an already-finished neon dystopia. This
premise is game fiction, not a claim about the real person's behavior.

The player/learned keeper makes meaningful exploration, expansion and combat
decisions. Bots handle recurring labor. Buildings anchor durable spatial choices.
Industry should fund spectacular combat without requiring constant repair chores.

## Visual identity

- The user's supplied Codex mascot sets the player-faction colors: saturated
  cyan-blue/azure bodies, black faceplates/joints, acid-lime faces and yellow
  utility accents. Preserve that relationship before adding surface detail.
- Tibo's public profile avatar anchors face/hair/black hoodie. Use genuine
  front, side and back views with stable animation scale and ground pivots.
- Begin with warm cafés/windows, public parks, concrete barriers, delivery vans,
  bus shelters and municipal cabinets. Expansion gradually brings server yards,
  cabling, fenced infrastructure and automation into these ordinary places.
- Rival factions are original peach/beige caution bureaucracy and charcoal/red
  velocity/security caricatures. Ordinary human employees replace fantasy
  monsters; their future tactical behavior still needs implementation.
- Blue units and lime/yellow interactions must stand out from muted environments
  at both action and strategic zoom. Lighting remains CPU-viewer-only.

The delivered art pass changes presentation, not generator/collision/save or
policy schemas. Streets and small scenic props are cosmetic. It does not yet
simulate civilian life, urban buildings, occupation, or industrial transformation.

## Progression target: crews become armies

These are design targets, not current unit limits or delivered unlocks:

| Stage | Approximate crew | New decision / earned capability |
| --- | ---: | --- |
| Garage experiment | 2–8 | First self-sufficient compute site and a useful scout/combat pair |
| Local operation | 12–24 | Specialized work teams, one new material, dependable cargo routes |
| District acquisition | 32–64 | Multiple staffed sites, squad objectives, distinct rival encounters |
| Industrial takeover | 100–200 | Demand-driven production, remote site simulation, mobile siege teams |
| Regional army | 300+ | Several armies with small shared policies and readable strategic command |

Earn the next stage by establishing operations and completing expeditions. Avoid
unlocking the entire arsenal after the first few passive resource increments.
Expansion should change choices, geography and composition. Upgrades also need
meaningful uses for old units rather than forcing replacement of an entire crew.

Before raising the current eight-slot limit: separate persistent unit storage
from the bounded training arena, replace slot-by-slot UI with squads/work areas,
add spatial neighbor queries and budget distant simulation. Validate 24/64/256
unit scenarios, save/reload, human overrides and moving-frame performance.

## Control and training contract

- No scripted player. Start with a human or RL keeper and scripted low-level bot
  execution, all going through one command/goal interface.
- Keep human-pinned orders authoritative; selection alone must not steal control.
- Later, batch small shared bot policies with role/goal inputs and per-unit state.
  Hundreds of bots do not require hundreds of independently stored networks.
- A commander supplies squad/site goals rather than one action head per physical
  unit. Local bot observations summarize nearby allies, rivals and work demand.
- Keep roles scripted while building the game. Later policies can be trained on
  these same goals, independently or together; all-at-once training is not a
  prerequisite for shipping reliable commands or a fun progression loop.
- Make schema changes explicit and test CPU/CUDA parity before claiming trained
  population-scale control. No trained policy is bundled with the art pass.

Next playable milestone: an excellent 15–20 minute first expedition with clear
orders, two independent work sites, one distinctive rival site and an earned
new capability. Controls/camera/readability precede hundreds of live units.

## Delivered in this slice

Camera regression fix; coordinate-stable streamed terrain; saved world edits and
outposts; independent pet commands and selection groups; eight companion slots;
renewable extraction; Burrower/Ember terrain work; mobile refining; bridges;
Starfire artillery; coarse distant production/movement; and CPU/CUDA tests.
The Reach pass adds numbered single-pet selection and WASD possession, independent
keeper/worker positions across scouting trips, a zoomable world atlas, six climate
regions, eight-frame creature strips, stable animation pivots, continuous click
steering, wider navigation clearance, chunk hashing and terrain-version-safe saves.
Lanternlight adds viewer-only lighting presets, projected sprite shadows, cached
light/minimap layers, animated ambient wildlife and light/heavy enemy skins,
wind/motes/waystones, and a selected-companion inspector. It does not add new
enemy behaviors, simulated animal ecology, or point-light occlusion.
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

- Extend the new chunk hash index to spatially indexed entity records and bounded caches.
- Stream several work sites concurrently; currently only the keeper OR driven pet has a live region.
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

## 6. Train the keeper, then introduce shared local bot policies

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
