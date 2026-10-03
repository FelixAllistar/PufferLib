# SWAT: Gold Element development roadmap

The target is a tactical game in the space of SWAT 4 and Ready or Not, built
around destructible environments and RL-controlled characters. Ocean is the
training interface to the game. Human play, NPC control, policy evaluation,
weapon rules, and physical cover must continue to use the same simulation.

Shenaniguns is parked as a separate project. SWAT starts from its preserved
low-level Box3D character ancestry and owns its controller and game systems
from here. New work belongs under `ocean/swat` and `config/swat.ini` unless it
is deliberately a general engine improvement.

## Landed: playable foundation

- Independent build/config, fixed-step game, standalone player and policy viewer.
- Tactical movement, physical lean, crouch/stand clearance, ADS, stamina and jump.
- Two weapons with chamber/ammunition/reload/equip/selector/recoil/spread state.
- Shared collision queries, cover damage, modular wall removal and penetration.
- Hinged door, basic armed guard and civilian, extraction/failure/reset.
- Versioned observation/action documentation, native FP32 training smoke,
  deterministic reset tests, mission solvability tests and sanitizers.
- Raygui main/pause/settings menus, focus-aware capture and pause, persisted
  mouse/display preferences, and a native Windows player/WSL launcher.

The current annex is a systems test level. It is not the finished tactical
controller, full weapon ecosystem, procedural campaign, or trained AI product.

## Next: finish the character and weapon interaction layer

1. Establish first-person and full-body pose definitions: shoulders, stance,
   collision envelopes, hand/weapon alignment, sight alignment, and view
   interpolation across fixed physics ticks. Expand movement tests for stairs,
   steep ramps, ledges, airborne lean and turning while pressed against cover.
2. Add high/low ready and explicit weapon obstruction feedback. Make sprint,
   ready, aim, fire, reload and interaction transitions visible in animation and
   audio. Preserve clearance checks throughout animation and camera motion.
3. Make weapon definitions/data extensible. Introduce staged reload/interrupt
   events and optional magazine inventory, sight/attachment profiles, action
   buffering, weapon-specific stance/recoil and nonlethal equipment.
4. Add a repeatable controller/weapon test range with saved input replays and
   debug overlays for bodies, eye, muzzle, impacts, damage and sensor visibility.

Acceptance: a human and an actor replay given identical inputs produce the
same state; pose, muzzle and damage geometry agree around tight cover; every
reload interruption conserves ammunition; frame rate does not affect rules.

## Tactical encounters and destruction

1. Replace the single annex with authored room connectors and seeded layouts.
   Validate spawn/goal reachability, doors, ceiling clearance and cover placement.
2. Add door states and equipment interactions, contextual prompts, tool use,
   evidence, surrender, restraint/rescue, mission objectives and an explicit
   rules-of-engagement state machine. Give civilians useful behavior.
3. Extend modular breakage into authored fracture/support rules, persistent
   openings, object/door debris, noise, and material-specific visibility.
   Benchmark collision rebuild and sensor cost before increasing complexity.
4. Add audio/perception events and delayed, occluded information shared through
   communications. Keep event visibility separate from global game truth.

Acceptance: destruction creates the same traversable/shootable openings for
every actor and the renderer; objective and civilian outcomes remain correct
when cover and routes change during an encounter.

## Train and evaluate the AI

1. Build a curriculum from locomotion/door use to weapon handling, target
   discrimination, cover, extraction and team coordination. Keep an unchanged
   evaluation suite separate from curriculum difficulty and shaped reward.
2. Expose role-specific actor policies through the existing per-actor input
   path. Suspects, civilians and officers need different objectives and legal
   actions, with recurrent perception, memory and communications. Any privileged
   training critic must not leak hidden state into deployed actors.
3. Train officer and suspect baselines, then cooperative squads and adversarial
   populations/self-play. Maintain scripted and frozen-checkpoint opponents so
   progress is measurable. Add enough scene variation to test generalization.
4. Track mission completion, civilian harm, ROE violations, time, ammunition,
   coordination and inference cost on held-out layouts/seeds. Inspect gameplay
   recordings and reward exploits before calling a policy an improvement.

Acceptance: trained behavior beats specified scripted baselines on held-out
missions, survives destruction/occlusion changes, respects the gameplay rules,
and runs inside the playable game through the same controller/weapon API.

## Production work after these slices

Animation/asset/audio pipeline, map tooling, squad commands, accessibility and
key rebinding, replay/save/mission formats, co-op networking and authority,
performance budgets, model/config manifests, crash handling, and release builds.
GPU simulation should be considered only with a tested fidelity target against
the authoritative Box3D game. These are planned systems, not shipped features.
