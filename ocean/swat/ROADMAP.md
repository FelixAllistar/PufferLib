# SWAT: Gold Element development roadmap

The target is a tactical game in the space of SWAT 4 and Ready or Not, built
around destructible environments and RL-controlled characters. Ocean is the
training interface to the game. Human play, NPC control, policy evaluation,
weapon rules, and physical cover must continue to use the same simulation.

Shenaniguns is parked as a separate project. SWAT starts from its preserved
low-level Box3D character ancestry and owns its controller and game systems
from here. New work belongs under `ocean/swat` and `config/swat.ini` unless it
is deliberately a general engine improvement.

## Active goal: Briar Court vertical slice (2026-10-08)

Deliver one complete, replayable motel scenario and level. The player chooses
the plan and tools; there is no required sequence of encounter triggers. Keep
the normal branch, config and native `./swat` path. Coordinate compact art
handoffs on Slack and integrate them here. Do not use Python for local work.

The slice is complete when these checks pass in the actual Windows player:

- Deploy with a useful squad, understand the task with minimal HUD text, execute
  a plan, account for occupants/evidence, extract and inspect a truthful debrief.
  Injuries have persistent consequences instead of abruptly ending tactical play.
- At least three viable approaches (front doors, exterior charge entry and an
  inter-room breach) work for humans, squad navigation, bullets, sound and replicas.
  Material rules are legible; breach boundaries stop looking like removed tiles.
- The motel and immediate approaches read as a finished place: reception,
  distinct rooms, readable numbers, background/perimeter, consistent materials,
  lighting, occupied-room variation and no conspicuous placeholder geometry.
- Officer and suspect behavior remains behind physical perception/controller
  boundaries; squad orders accomplish actual entry/security tasks. A reproducible
  baseline and held-out evaluation precede any claim of learned improvement.
- Weapon handling/reloads, hit feedback, destruction and spatial sound form a
  coherent playable loop. Integrate properly licensed production sounds rather
  than treating procedural placeholders as finished audio.
- The same scenario can be completed through different approaches, replayed,
  saved/resumed and joined late in co-op. Validate geometry/authority agreement,
  bounded performance during indoor shooting/breaches and asset fallbacks.

Current audit: human tactical scenarios now deploy three squad bots and require
occupant security, evidence, civilian evacuation and surviving-officer regrouping.
Tactical harm persists into the debrief instead of ending play. A 2.5 m staging
zone accommodates the formation; the real motel controller test walks an officer
through a door, escorts a civilian out of the furnished room, enters the cleared
aisle and regroups all three bots. Local visible-body avoidance only uses supported,
collision-checked space; narrow aisles still require their occupant to move.
Delivered numbers 102–104, desk folders and reception sign are integrated and
validated in the native view, including support removal and location teardown.
Stack/Clear are still generic movement orders, not useful room-entry procedures.
Bedside switches and material-aware masonry work, but light does not affect
perception and openings remain rectangular. Production audio, room/approach art
and trained tactical policies are unfinished.

Work order: close the scenario completion/squad gaps and integrate delivered art;
prove alternative routes with the actual controller; improve fracture silhouettes;
then address perception/behavior, level composition, sound and remaining polish.
Reorder using concrete play-test failures. Record each completed increment and
its evidence here; a successful subsystem test is not a finished vertical slice.

First slice increment: Linux encounter, motel, tactical, simulation, mission,
protocol, UDP, 3D navigation and save/replay checks pass. Native Windows encounter
and motel controller checks pass; the full Windows tool build, complete environment
graphics regression and ordinary `./swat play --mission motel --capture ...` pass.
Native proof and compact editable sources are under the guest-folder, room-numbers
and reception art handoffs. Shared the review on Slack (`F0C7N974YQ5`). The artist
is preparing an open-ended east perimeter fence and Room 103 variation; these are
not integrated yet. No claim of learned tactical behavior or a finished slice.

Alternative-route regression: with all doors wedged, the normal squad controller
remains blocked until destruction opens an exterior reception wall or guest-room
party wall, then physically crosses each opening. Linux and native Windows pass
(166 ticks exterior, 34 ticks inter-room). This tests traversal of the shared
breach result; charge placement/detonation has separate tactical/network tests.
Next gameplay priority is genuine Stack/Clear semantics and separated squad goals;
current single-point orders can still pile teammates together.

## Landed: playable foundation

- Motel: seven supported detail props in each guest room; three inter-room
  walls use physical drywall/stud assemblies, with replicated charge openings
  and navigation samples that find narrow passages beside furniture.
- Independent build/config, fixed-step game, standalone player and policy viewer.
- Tactical movement, physical lean, crouch/stand clearance, ADS, stamina and jump.
- Two weapons with chamber/ammunition/reload/equip/selector/recoil/spread state.
- Shared collision queries, cover damage, modular wall removal and penetration.
- Hinged door, basic armed guard and civilian, extraction/failure/reset.
- Versioned observation/action documentation, native FP32 training smoke,
  deterministic reset tests, mission solvability tests and sanitizers.
- Raygui main/pause/settings menus, focus-aware capture and pause, persisted
  mouse/display preferences, and a native Windows player/WSL launcher.
- Four-player ENet co-op with listen hosting, a headless dedicated authority,
  versioned snapshots, shared destruction, late join and leader restart.
- Material/doorway acoustic paths, delayed NPC hearing and procedural stereo
  audio, sharing destruction state and independent of player volume. Query-local
  acoustic spatial searches and gain bounds remove the indoor wall-hit full-scan
  bottleneck while matching exhaustive hearing/route results exactly.
- Cedar House: three rooms, two suspects, three hostages, outdoor staging,
  windows/doors, cutaway planning and three authored overwatch posts.
- Thin independent wall faces, cavities, timber studs, plates and headers;
  distinct material transmission and surface absorption tables.
- Recon/Control/Entry kit tradeoffs, collision-limited optiwand, melee/ram,
  less-lethal impact launcher, compliance and interruptible cuffing.
- Head/torso/arm/leg injury, plate protection, injury-driven movement/spread,
  secured-hostage extraction and protocol v8 replication of these states.
- Optional native Steam Audio HRTF, approximate room reflection/decay mixer,
  and 26 repeatable WAV/CSV comparison scenes.
- Two physical snipers: post/rifle selection, direct scope control, visible
  target designation, hold/execute, ammunition/cadence and friendly interlocks.
- Live A/B scope inset during officer movement, cursor controls without pausing,
  and animated floating takeover over the dimmed officer view.
- Live previous/next camera bindings and buttons, contextual F use/compliance,
  RMB cuff/pick/aim with held-action isolation, minimal floating text prompts
  and camera controls shown on held Tab.
- Locked house entries, reusable interruptible lockpicks, finite mounted
  breaching charges, owner-only remote detonation and shared blast/door state.
- Canonical first/third-person eye/hand/sight/muzzle geometry, high/low ready,
  buffered actions, staged interruptible reloads and optional retained magazines.
- Staging primary/sight profiles, stair/ramp/clearance test range, F3 physical
  debug overlay and portable exact solo input replay verification (protocol v8).
- Door peek/wedge/recovery, inspected trap disarming and finite occluded spray.
- Shared clearance navigation, initial sight/hearing-memory NPCs, vacant-slot
  squad bots, delayed/queued Gold/Red/Blue orders, escort/evidence/debrief state.
- Physical PepperBall/impact/CS/flash rounds, separate finite payload profiles,
  ten-probe/two-contact CEW with occluded tethers and an experimental restraint.
- Physical throwable camera, robot, audio communication ball and slow drone;
  owner-only takeover, live cycling/audio, recovery, batteries and vulnerability.
- Automatic under-door optiwand insertion, independent lens rotation and
  collision-limited corner/over-cover modes with explicit HUD feedback.
- Shared physical/acoustic materials, CCD flash/CS canisters, masks, finite
  stocks, taser, material impact/step sound and source-room decay through paths.
- Validated 3–5-room house grammar, CPU neural inference, a trained bootstrap
  model, seeded mission UI/CLI and exact generated-map replication.
- Explicit local house comparisons and a runnable preference reward/policy
  training pipeline. The shipped model has no human feedback yet; see
  [GENERATION.md](GENERATION.md).
- Initial live environment textures/doors and six bounded decorative tabletop
  props, tied to active damage pieces/supports without new collision or LOS.
- Initial linear-light shading, cached sun/nearest-room depth shadows and
  exposure overrides; transformed primitives and imported art share shading.
  Original body specular/gloss maps and bounded room ambient occlusion now
  separate surface finishes and adjacent structural planes.
  The coherent material family now has separate metric floor/frame/door bindings
  and signed OpenGL normal/roughness shading; authored fixtures and GI remain.
- Optional private rigid-carbine import with measured authority/render alignment,
  authored normal strength, revised R3 finish/contours and rounded sight ring and staged
  magazine visibility in both rigid and animated character rendering.
- Verified solo mission checkpoint/restore and journal continuation, with F5/F9
  input and `--resume`; co-op persistence remains pending.
- Explicit native/WSLg launcher routing, GPU fallback selection and a portable
  frame-cost benchmark covering simulation, audio, live cameras and recording.

Current art handoffs: the immutable F character GLB passes structural preflight
with all seven influences. An independent full-influence consumer now passes
377 numerical pose comparisons and renders the unretimed reload in an isolated
art lab; [CHARACTER_ART.md](CHARACTER_ART.md) records evidence and limitations.
Live carbine officers and first-person arms now use full-influence GPU skinning,
achieved stance/weapon fitting, original maps and authority-driven reload phases;
cameras/shadows reuse cached poses. First-person arms use camera-relative
authored grips, with a larger weapon projection and saved live-preview size and
position controls. Carbine ADS now has adjustable 80–220 mm eye relief (120 mm
default), a display-only aim preview and fading crosshair lines. The separate
72-joint F gear geometry is integrated with all seven original motion banks;
rigid anatomical elbow carriers are updated after arm IK, and source body maps
and authored pad/headset materials are retained. The engine now adds a measured
carbine support-thumb wrap in hip/ADS and world presentation, releasing it for
authored reload handling; the frozen C/C1 grip studies remain diagnostics.
Retreat now uses its own original one-second backward cycle, paced by actual
travel at 1.921261449 m per cycle, with continuous phase and the forward fallback
when the optional asset is absent. R3 retains the rounded outer rear sight and softens selected stalk/receiver
edges with restrained wear, without changing the opening or measured contacts.
First-person reloads now retain the authored working carry and frame visible
magazine handling; only forearms/gloves enter the camera pass. The support-thumb
correction is eased to avoid the oversized curved glove silhouette.
Remaining palm/finger contact and other rifle details need art iteration. Guns
have mechanical profiles and canonical pose targets. The local rigid carbine
uses authored normal strength, measured bindings and reload magazine visibility; see
[WEAPON_ART.md](WEAPON_ART.md). Other guns remain procedural and the supplied
sidearm needs physical-size calibration. The first coherent environment material
quality pass is integrated; [ENVIRONMENT_MATERIALS.md](ENVIRONMENT_MATERIALS.md)
records its scope. Briar Court batch 15 is now a playable authored motel with
146 instances, original mesh collision, five functional door leaves and network
replica reconstruction. Exterior masonry and bathroom partitions now use
material-aware destructible sections with clipped original art; masonry stops
gunfire but accepts a local charge breach. Exposed-core/chipped-plaster strips now track surviving edges. Irregular opening
silhouettes, rubble and building-scale structural support remain unfinished; see [ENVIRONMENT_ART.md](ENVIRONMENT_ART.md). The
checks now also cover Morrow Block batch 16: 151 modular placements plus the
authored street/sidewalk, five functional doors, original static triangle
collision and actual controller traversal. Original lateral, held crouch and
crouch-forward animation banks are integrated, with source-specific travel
phase and the original reload retained. Glass draws after opaque geometry and
current-mission module banks load on demand. The
motel desk now uses W2 oak, and supported bedside fixtures drive downward room
lighting without adding shadow slots. These foundation checks do not imply a finished
animation pipeline, production lighting or completed roadmap.

[ARCHITECTURE.md](ARCHITECTURE.md) records the portable technology choices,
authority/perception boundaries, current network limits and future fidelity tiers.
[AUDIO.md](AUDIO.md) separates the implemented foundation from production audio.
[EQUIPMENT.md](EQUIPMENT.md) compares tactical gear and records the next gadget choices.

The annex is a systems test level and the house is a small authored encounter.
The current game is not the finished tactical
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
   buffering, weapon-specific stance/recoil and broader less-lethal equipment.
4. Add a repeatable controller/weapon test range with saved input replays and
   debug overlays for bodies, eye, muzzle, impacts, damage and sensor visibility.

Acceptance: a human and an actor replay given identical inputs produce the
same state; pose, muzzle and damage geometry agree around tight cover; every
reload interruption conserves ammunition; frame rate does not affect rules.

## Tactical encounters and destruction

1. Play-test the generated houses and gather real comparisons. Expand room
   identity, cover, approach choices and the grammar with navigation checks;
   qualify learned preference improvements on new player judgments.
2. Extend locked/charged doors with peeking, wedges and trap inspection/removal;
   expand contextual prompts, equipment slots and tool use,
   evidence, surrender behavior, escort/rescue, objectives and an explicit
   rules-of-engagement state machine. Give civilians useful behavior.
3. Extend modular breakage into authored fracture/support rules, persistent
   openings, object/door debris, noise, and material-specific visibility.
   Benchmark collision rebuild and sensor cost before increasing complexity.
4. Extend the shared audio/perception foundation with production recordings,
   calibrated source/path acoustics, richer diffraction and delayed squad communications.
   Keep perception separate from global game truth.
5. Expand sniper behaviors with animated travel, stronger target identification,
   squad communications and more complex sight-line/crossfire cases.
   A piloted drone needs its own movement, collision, perception and exposure
   rules; the current overview is a planning camera.

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
key rebinding, replay/save/mission formats, network prediction/interpolation,
Internet connectivity and session services,
performance budgets, model/config manifests, crash handling, and release builds.
GPU simulation should be considered only with a tested fidelity target against
the authoritative Box3D game. These are planned systems, not shipped features.
