# SWAT: Gold Element development roadmap

The target is a tactical game in the space of SWAT 4 and Ready or Not, built
around destructible environments and RL-controlled characters. Ocean is the
training interface to the game. Human play, NPC control, policy evaluation,
weapon rules, and physical cover must continue to use the same simulation.

Shenaniguns is parked as a separate project. SWAT starts from its preserved
low-level Box3D character ancestry and owns its controller and game systems
from here. New work belongs under `ocean/swat` and `config/swat.ini` unless it
is deliberately a general engine improvement.

## Active goal: near-realistic fuller-game vertical slice (reset 2026-10-09)

Deliver a near-realistic vertical slice of the fuller tactical game, with a
finished Briar Court level as the main proving ground and the shared range,
building and generated scenarios supporting equipment/behavior validation.
Use Xbox 360-era visual quality as a practical baseline: believable proportions,
materials, lighting, composition, movement and sound with a compact asset budget.
The player chooses
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
- Representative lethal/less-lethal weapons and entry/support tools form a
  coherent loadout, with visible handling/reloads, useful commands and readable
  interaction feedback. Validate roles and material response in the shared range
  and building scenarios as well as the main level.
- Different occupant layouts and scenario seeds remain solvable without authored
  mission-trigger sequences. Officer/enemy policy training and held-out evaluation
  use the ordinary Puffer/config workflow; civilians may remain basic/scripted.
- Stable-load native playtests record frame-time distributions during indoor
  firing, breaches, squad movement and camera feeds. Concurrent-training timings
  alone do not establish the final performance budget or accept the slice.

Immediate work: background composition, further physically
mounted/destructible props, production impact/breach audio,
weapon/equipment completeness and squad/enemy perception/behavior. Iterate using
actual playtest failures and the acceptance checks above. Source/runtime/licenses
belong in the normal repository; complete backups remain on Drive. Avoid redundant
local versions. The previous motel-only tracker entry was usage-limited and its
replacement request was rejected because it remains unfinished; this roadmap is
the current working objective, not a claim that the earlier slice was completed.

The indoor-lighting follow-up now caches static sun geometry separately from
actor shadows. Empty or settled room shadow tiles retain their composited depth;
moving actors, room entry/exit and the final 0.22-second pose blend still refresh.
Partial fracture outlines now invalidate the sun and affected room immediately,
even when the object's center and bounding box stay unchanged. Native graphics
checks retain camera-independent coverage of every room, all six lamp directions,
clean actor departure, framebuffer restoration and immutable simulation state.
A matched 240-frame native motel indoor shooting run (30 shots, 1,327 objects,
nine actors) measured mean shadow time of 10.667 ms before and 2.827 ms after;
mean total time was 43.800 versus 20.048 ms, with p95 129.696 versus 31.383 ms.
Other stage timings also changed with machine load, so these separate runs are
diagnostics rather than a controlled attribution of the entire frame-time gain.
Evidence is in `build/swat/review/lighting-movement/{before,after,lighting}.log`.

World movement aiming now derives torso correction from the sampled chest and
shoulder frame rather than the independently lowered rifle in carry clips.
The authoritative rifle attachment, source-relative hand targets, two-bone arm
IK and source first-person poses remain unchanged. Native tests cover 336 samples
across seven banks, four phases, four headings and three pitches, retaining exact
stock attachment and rigid gear/limb checks. Head-center clearance from the
receiver centerline is at least 0.143 m in this skeletal regression; it is not a
whole-mesh collision guarantee or a completed cheek-weld/grip fit. Captured forward
and backward poses show the head beside the rifle. Existing ADS and interrupted
crouch/reverse/reload checks pass, and the normal Windows/Linux player and Puffer
builds are current. Native evidence and selected pose captures are in
`build/swat/review/lighting-movement/`.

Room 104 now has the original CC0 Poly Haven worn wooden nightstand beside its
bed: 470 triangles, original drawer/root transforms and 2K albedo/normal/ARM maps.
The full 50.46 x 50.87 x 61.55 cm geometry remains unscaled, with four measured
feet on the actual carpet, sparse leg clearance and exact missing-art collision
fallback. Damage/removal replicate through normal maps, snapshots and replay.
WOOD and the 24 mm health section are explicit game approximations; concealed
construction and pull material remain unknown. Linux/Windows checks include an
ordinary canonical-spawn room-entry/shooting save/restore. Native original/fallback
art matches 153,932 stable collision rays and verifies source material factors,
shared image lifetime, production lighting/shadows and authoritative removal.
Contact checks now preserve 31,989 triangles across 27 assets, including original
edge flags and 64,800 ray/exit comparisons per reconstructed world.

The separate realistic window R1 is installed in all four guest-room openings
and the lobby facade. Original GLBs/maps remain unscaled. Their 349 source
components are grouped into 137 material-specific damage objects, retaining
all 26,052 installed triangles in 194 bounded contact shapes. Twelve opaque
6.4 mm panes break independently; thin cloth blocks sight while bullets can
hit a concealed actor through it. Aluminum, rubber and cloth use explicit
editable game strengths. Failed frames/rod sockets shed dependent geometry
and collision together. Source adjacency guides grouping, but the proposed
518 geometric contact tolerances are not independently certified; original
opening owners are virtual support anchors, not measured wall fasteners.
Original source/CC0 receipts and reproducible Node grouping are retained.
Native source/fallback rendering matches 76,770 stable collision rays across
front/back/oblique views and independent pane/support damage. Source material
counts, shared texture lifetime, immutable authority and production lighting
are checked. The ordinary player, server, replay tool and Puffer use this recipe.

Mounted-prop foundation: the delivered galvanized junction box now has two
measured motel placements, exact hollow-shell collision and independent damage.
Its five original fastening anchors resolve to masonry fragments; support loss
removes art/collision together and propagates through attached children. Eight
bounded contact meshes preserve all 1,464 triangles by using centimetre mesh
construction and inverse shape scale. Steel strength scales by the authored
1.2 mm saddle section; values remain game approximations. Portable map/replay
version 17 carries support dependencies and rejects cyclic/unsupported states.
Linux and native Windows checks cover shell thickness/open mouths, impacts,
charge/support loss, late replicas and ordinary-input destroyed-prop save/restore.
Native graphics match 62,396 stable ray/raster samples across both placements and
the exact missing-art fallback; three model owners share their images and release
them safely. The hostile exterior-entry completion route passes all 9,827 ordinary
input ticks with two arrests, three evacuations, collected weapons, squad regroup,
exact replay, mid-run save and late replica, without contact-buffer overflows.
The delivered dirt/gravel zoning now blends all three material channels on twelve
soil tops, with exact linear color, original metre UVs and exclusion-safe R8 LOD0
sampling. Native production-shader checks match 39 independently calculated RGB
channel values exactly; 27,424 protected road/drive pixels remain unchanged in a
minified mask/control view, with 75,210 protected bilinear world probes. All four
added textures release/reload on location changes, source material maps and world
state stay unchanged, and the existing 740-ray/controller/squad ground checks
pass. Only the new original 1K dirt maps and mask are added; no duplicate gravel,
reference ground geometry or 2K runtime loads. The unchanged compact zoning
generator/config/control-point excerpt is now preserved locally: its hashes and
18 bilinear controls match the runtime mask, with no Python execution. Full source
backup remains on Drive. Background/ground repetition remains polish work.

Distant east-wall speckling was backing-core/face depth fighting. A core-only
polygon offset preserves authored surfaces, physical geometry and weapon/scene
projection. Native production-clip lit/unlit checks eliminate 340 mismatched
face pixels at the distant camera and reset GL state after every capture.
The two-shelf service trolley is now parked in the rear utility room, supported
by four measured contacts on original floor triangles. Twelve bounded contact
meshes preserve all 2,416 triangles and the open shelf bays. It has finite steel
strength, exact missing-art fallback and serialized removal; rubber/liner physical
response and hollow frame construction remain approximations. Native graphics
match 152,048 ray/raster samples on five views, source and fallback. Linux/Windows
contact checks preserve 25,983 triangles/edge flags over 23 assets with 55,200
matched rays each, including replicas. Old version-17 prefixes remain loadable.
The hostile exterior route still completes in 9,827 ordinary-input ticks with
exact replay, mid-run save, fresh replica and no contact-buffer overflow.
The original extinguisher is now mounted beside room 104 at measured scale,
with both brackets tied to the actual masonry sections. Thirteen bounded
contact meshes preserve all 2,412 triangles, including the dense curved bottle.
Box3D's new independently owned subset operation retains source edge flags and
material indices across contact boundaries, with a correct mesh hash; grid and
curved-surface checks pass on Linux/Windows. Source/fallback native graphics
match 154,496 stable ray/raster samples; source texture ownership/release checks
pass. Damage, support/charge loss, replicas, old trolley maps and malformed
recipes pass on both platforms. Contact checks now preserve 28,395 triangles
and edge flags across 24 assets with 57,600 matched rays per world/replica.
Full hostile completion/replay/save still passes in 9,827 ticks. The bottle
retains its original closed outer solid and a single steel physical response;
working discharge, pressure and separate thin-shell/rubber response remain
unimplemented. Normal Linux/Windows players are rebuilt.
The original reception noticeboard now mounts on the inside west wall at
measured scale, with both source anchors tied to physical masonry fragments.
Seven bounded meshes retain its 1,416 faces and source edge flags. Its wooden
frame uses finite material strength; ordinary shooting and support loss remove
art/collision together. Cork, paper and pins retain their authored appearance
with whole-assembly wood response. Linux/Windows mount, damage, legacy-prefix,
replica and unsupported-state checks pass. Native source/fallback graphics match
156,352 stable ray/raster samples, with original 1024px maps and safe texture
ownership. Contact checks now cover 25 assets, 29,811 triangles/edge flags and
60,000 matched rays per world/replica. Full hostile completion remains 9,827
ticks with exact replay and save/resume on both platforms. Normal players rebuilt.
The original spindle chair now sits behind the reception counter, at real scale
on four measured source feet touching floor 2. Nine bounded contact meshes retain
all 1,884 faces and original edge/material flags, with all three original wood
materials. Ordinary movement is blocked by the intact chair, clears the west
aisle and crosses its former footprint after actual rifle damage removes it.
Linux/Windows floor-contact, damage, legacy-prefix, replica and completion checks
pass. Native source/fallback graphics match 151,710 stable ray/raster samples;
texture sharing and final release pass. Contact checks cover 26 assets, 31,695
triangles and 62,400 matched rays per world/replica. Complete hostile gameplay
still passes in 9,827 ticks with exact replay and save/resume on both platforms.
Its single wood response uses the source's 42 mm minimum nominal leg diameter;
independent leg/spindle breakage remains future work. Normal players rebuilt.
The next composition handoff groups the unchanged refuse bin, tote, wheelbarrow
and one leaf patch; original hashes verified, world placement/contact/material
integration still pending. Its measured rigid resting pose and component material
tables have arrived; capped proxies and unknown substrates remain documented.
Next delivered art: service vacuum, umbrella stand,
bottle opener/cap catcher, paper-towel dispenser and first-aid cabinet. Continue
compact service composition with measured placement and actual route clearance.
All are original source candidates; integration follows physical material,
mounting, route clearance and shared authority checks.

Destruction audio now uses nine original CC0 rock/glass/metal foley clips, rather
than wood-breaking aliases for plaster and drywall. Brick, glass and steel have
three recorded break variants each; charges emit one material debris source per
aperture alongside the existing blast fallback. Production-bank Linux/Windows
checks load all 76 variants, exercise 15 break selections, and confirm finite
bounded directional output and expiry. Tactical tests cover the single charge
debris event. The hostile exterior route still completes in 9,827 ordinary-input
ticks with exact replay, mid-run save, late replica and no contact-buffer overflow.
Native indoor shooting under concurrent training measured 49 ms mean total,
with world drawing around 30 ms; this is a contended diagnostic, not acceptance
of the final performance budget. Full/culled graphics checks now also include
charged masonry and its exposed edges. Two early wall-core culling prototypes
passed image equivalence but failed to show a controlled timing gain, including
a synchronized GPU run; neither is shipped.
Wall backing cores now share a camera batch, starting at the first wall after
the canonical floors/furniture to preserve equal-depth seams. Authored faces,
exposed masonry edges and per-face shadow/contact filters retain their existing
draws. Untextured cores explicitly clear a preceding finish's normal/roughness
maps; this corrects stale surface lighting on visible backing seams.
Native graphics compare the batch with the corrected individual-core path:
42 pixel-identical lit/unlit pairs across seven cameras, intact/breached walls
and missing sources, with unchanged authority and clean offset state. Distant
depth checks still remove all 337 reproduced face mismatches in each lit/unlit
far view. A 960-frame alternating-block frozen indoor comparison measured median
scene-plus-geometry time of 20.734 ms individually and 19.820 ms batched; overall
timings remain noisy with large machine stalls. The separate 480-frame exterior
comparison also favors the batch, but its much slower absolute times reinforce
that these are machine-contended diagnostics. This is a modest submission
improvement, not acceptance of the final game performance budget. Renderer
batching and costs remain ongoing work.

Current audit: human tactical scenarios now deploy three squad bots and require
occupant security, evidence, civilian evacuation and surviving-officer regrouping.
Tactical harm persists into the debrief instead of ending play. A 2.5 m staging
zone accommodates the formation; the real motel controller test walks an officer
through a door, escorts a civilian out of the furnished room, enters the cleared
aisle and regroups all three bots. Local visible-body avoidance only uses supported,
collision-checked space; narrow aisles still require their occupant to move.
Delivered numbers 102–104, desk folders and reception sign are integrated and
validated in the native view, including support removal and location teardown.
Stack/Clear now use doorway geometry: separate exterior positions with closed
leaves blocked, then sequential far-to-near interior positions and inward facing.
Visible occupants can cause local sector/route replanning; no hidden occupant map
is added. Three-officer furnished-room entry, queued execution, wedge refusal,
four doorway rotations/both flanks and exact input replay pass on Linux/Windows.
Bedside switches now affect distant actor perception through the shared physical
light-source model described below. Masonry openings follow irregular physical polygons, with coarse
macro-fracture still visible. Production audio, room/approach art
and trained tactical policies are unfinished.

Work order: close the scenario completion/squad gaps and integrate delivered art;
prove alternative routes with the actual controller; improve fracture silhouettes;
then address perception/behavior, level composition, sound and remaining polish.
Reorder using concrete play-test failures. Record each completed increment and
its evidence here; a successful subsystem test is not a finished vertical slice.

Room sources now have one simulation-owned descriptor shared by rendering,
tactical/ordinary NPC detection and policy actor observations. It includes the
same bulb/fixture origin, lamp cone, power, switches and lost motel supports.
Opaque physical cover blocks punctual light; glass transmits the light query;
a clear vertical sky path restores daylight. A bounded exposure approximation
reduces distant actor detection continuously, down to half the daylight range.
Close targets still require a real sight ray and FOV, and remain detectable;
hearing retains its finite-speed, coarse-bearing path. Navigation excludes
distant dark-room occupants from its visible-body occupancy list. Unseen policy
samples disclose neither actor identity nor body depth. Observation/action shapes
and checkpoint dimensions are unchanged; interior perception semantics change.
This is approximate actor detection, not renderer pixel readback, photometric lux,
night vision, flashlight handling or a trained tactical-policy claim. The vertical
daylight probe does not yet model light arriving through every doorway/window.
Linux/Windows encounter, motel and tactical checks cover switches, cover, glass
light transmission, roof loss, close detection, hearing, FOV and late replicas.
Native motel lamp captures share the correct supported source and change 217,426
pixels on switch-off, with a measured 24 -> 12 m detection probe; lost wall
support extinguishes both models, and capture restores authority. Complete
hostile front/exterior/inter-room controller routes pass at 8,385/9,827/10,710
ticks, with exact replay, mid-save/resume, fresh replicas and no contact overflow.
The staff capsule route now goes around the chair's actual back rather than
through its occupied seat. Normal Linux/Windows players and the ordinary Puffer
trainer are rebuilt; no custom launcher/training wrapper or Python is introduced.
A fresh native headless 960-tick indoor firing diagnostic (1,179 objects, nine
actors, 107 shots) measured simulation median 2.046 ms and p95 4.130 ms. It
excludes rendering/audio and is not a controlled before/after speed comparison.

Clear-glass sight now has its own query boundary. Tactical and ordinary NPCs
can observe actors through `SWAT_GLASS`, while movement, hands, nonlethal tools
and actual bullet traversal retain physical pane collisions. Directed compliance
passes a clear pane; cuffs remain blocked. NPC fire requires the aimed optical
ray to reach the target before another person, including in the ordinary guard
path. Visible-body navigation awareness transmits glass without changing static
capsule clearance. Linux/Windows checks exercise a real bot shot breaking a pane
before damaging its target, a civilian crossing the firing line, blocked taser
and cuffs, opaque cover, reduced light visibility and intact/broken replicas.
All three hostile motel completion routes still pass, including every-tick
replay, mid-save/resume, fresh replicas, legal arrests and no contact overflow.
Normal Windows/Linux players and Puffer are rebuilt; the native player capture
is reviewed in `build/swat/review/glass-sight`.

Annex contract v3 retains 167 observations/14 action heads/39 logits, but keeps
nearest physical pane depth while exposing a visible actor's class/presence
behind it. Hidden people do not erase the pane or disclose identity/body depth.
Fresh annex training is required; shape-compatible old raw weights cannot be
identified automatically. Movement v2 is unchanged. This handles single-material
clear panes, not partial opacity or a material decomposition of mixed assets.
The pre-R1 motel recipe separates ten opaque dusty glass panes from
their five existing frame/curtain/sill aggregates. Each pane retains its original
44 triangles, independent damage and physical penetration, glass audio and local
C4 removal. Intact panes block sight; broken panes open the same physical/render
gap. Unknown frame substrates and intersecting curtain proxies retain their
existing aggregate behavior, so curtain-covered areas still block sight/movement.
The modeled 18/25 mm pane envelopes are not verified fabrication gauges; health
and material strengths remain game approximations. The frozen parts packet,
hash receipt and reproducible C importer are checked in. Artist follow-up requests
a separate glazing/curtain revision with explicit substrate/support evidence.

Bullet traversal now checks an object's occupied interval before advancing to
its exit, so overlapping glass/curtain/steel layers each consume energy. Occupied
objects are ignored only until their next exit; later disconnected portions of
the same mesh remain hittable. Eight impacts remain the bound. Nested/overlapping
glass and steel checks in both directions fail against the previous traversal
and pass with the new traversal.

The current motel recipe has 1,327 objects, including the room 104 nightstand
and 137 appended R1 window groups. The 1,190-object prefix retains the
nightstand and original independent panes. The previous 1,189-object prefix retains independent panes; earlier
canonical prefixes keep original aggregate glazing. Partial pane recipes and changed opacity/depth/support
are rejected. Wire layout remains version 17, but peers need matching builds for
the appended glass/aluminum/rubber/cloth materials and current map recipe. Movement v2 and annex
v3 remain unchanged. Panes use independently owned, bounded contact subsets and
cached material batches; original material maps/UVs and Room 101 v3/v4 art survive.
Missing art draws exact surviving mesh collision rather than aggregate boxes.
Linux and native Windows checks cover two-face shots/exit depths, overlapping
curtains, actor penetration, ordinary finite-inventory C4 placement/retreat,
encoded late replicas, reset and exact normal-input destroyed-pane save/replay.
Historical pre-R1 native graphics match 357,634 stable collision/raster samples over intact and
broken source/fallback windows; both Room 101 replacement versions retain/remove
the matching pane. Existing front/exterior/interroom completion, replay/save and
fresh-replica checks still pass on both platforms. The normal native player,
Linux player and Puffer are rebuilt; an actual launcher capture is reviewed.

Material hit/break cues now reconstruct directly from replicated sound events:
short glass glints, masonry dust/chips, wood splinters and metal flecks. Charges
use their existing colocated blast/break pair for a wider puff. Main view, sniper
and device feeds share this path. It selects at most 12 recent events/96 particles
and 1,404 vertices (468 triangles) per view, with one batch. Depth testing hides
cues behind cover; transparent cues do not write depth, and depth writes/culling
are restored before further geometry. Matching impact/break pairs produce one
cue. Actor-hit carpet placeholders are excluded. These are transient visual
cues, not persistent colliding rubble or additional gameplay cover.

Linux/native Windows checks cover every material, expiry/reset, wrapped/saturated
event logs, prioritization, real pane-shot events, encoded snapshot equivalence,
and unchanged authority/RNG. Native graphics check three material cues, opaque
cover, expiry, restored depth writes, texture cleanup and a real lobby-shot view.
Saturated-log sampling measured 11.8 microseconds on Linux and 12.0 on Windows;
the native maximum batch measured 0.123 ms mean CPU submission over 200 draws
on the GTX 1060 system. GPU completion is excluded: this does not establish a
whole-game frame-time budget. Proof is in build/swat/review/impact-effects.

The artist's separate window construction proposal is approved and retained
with a hash receipt: 6.4 mm glass, four lobby panes around the existing middle
rail, explicit frame substrates/pockets, thin supported curtains clear of glazing,
unchanged outer bounds and a 1.27 x 2.4 m lobby doorway. The delivered R1 meshes now replace runtime windows with grouped physical
parts. Geometric contact tolerances and measured wall fasteners remain future
qualification work; do not treat the assembly rules as structural certification.

Overwatch now uses that same sight query, removing its separate eight-pane
stepping cap. A physical nine-pane designation check fails against the former
implementation and passes on Linux/Windows with the shared query; an added opaque
steel sheet still blocks designation and no shot is issued by the test. Existing
post/rifle selection, glass-impact shooting, hostage interlock, steady aim,
ammunition preservation and replica checks still pass. Bullet penetration retains
its own physical energy/traversal limits. Normal players and Puffer are rebuilt
after this follow-up. Clear-glass visibility remains separate from the motel's
opaque authored panes.

Full front-door completion now has a C player-input regression starting from
the canonical motel spawn: five compliance/cuff interactions, two collected
weapons, three physical civilian escorts and all surviving officers regrouped.
It passes with hostile fire enabled, no deaths or unlawful force, exact per-tick
input replay, a mid-run save/resume and final late-replica state/debrief. The QA
driver knows the room plan and reacts to actual unobstructed rays; this is a
repeatable controller test, not a learned policy. It catches two real failures:
the 3 m occupancy horizon could make escorts oscillate around a crowded staging
area (route planning now includes visible people out to the existing 24 m
perception range), and dropped evidence could consume the initial cuff press
despite the displayed cuff action (body interaction now takes precedence).
Contact avoidance still checks local bodies every tick; walls still occlude
route occupancy. The later breach-route increment below extends this proof;
a multi-human co-op playthrough remains acceptance work.

Validation for this increment: Linux isolated/hostile completion, encounter,
motel, tactical and real-UDP regressions pass; native Windows hostile completion,
encounter/motel/tactical/UDP checks pass. Full Windows build, complete environment
graphics regression and ordinary `./swat` motel capture pass. The original-finish
guest desks now remove their seven raised scratch boxes on owners 48/72/96,
with byte-identical retained art and unchanged support/collision. Native review
was shared as Slack file `F0C8PNE4YGY`.

The shoulder/scrub kit now has five native placements, ten separate SOIL owners
and 65 STONE owners (1044–1118). Exact closed triangle meshes supply cover and
support; visual foliage adds no invisible collision or acoustic occlusion. Both
outer edges are in the 60 cm navigation grid. Linux and Windows pass 6,970 support
rays, ten live court/shoulder crossings, six repeated seams, real squad movement,
actual rifle-cover tests, replica reconstruction and reset/close. The native
graphics check passes independent rock removal and exact missing-art fallback.
The terrain is static; no crater or loose-rock simulation is claimed. Compact
editable source and the explicit CC0 notice accompany the runtime in the repo.

The expanded-level hostile completion, exact replay and save/resume also pass on
Linux and Windows. A busy-machine UDP regression exposed an acknowledged-input
loss: the idle timeout could replace an unconsumed queued command with neutral
input. The timeout now expires only repeated holds after the queue drains. A
deterministic stale-clock test fails before this fix and passes afterward; the
full Linux/Windows UDP door-tool and scenario suites pass.
The full environment graphics suite, full Windows tool build and final player
rebuild pass. Ordinary `./swat play --mission motel --capture ...` launches the
native GTX 1060 renderer and captures the deployed squad correctly. Terrain
proof was shared as Slack file `F0C7TS1RL3G`. Its furnished-room completion
exposed Box3D's 256-triangle contact-buffer warning, addressed in the next
increment below. Each perimeter component is below 240 triangles.

Appearance remains provisional: the first native perimeter has sparse repeated
shrubs and rounded rocks, and the level still needs a connected wider setting.
The Poly Haven scanned gravel/dirt/stone revision is now integrated after native
comparison, with verified hashes, a packed editable source and the matched
provenance/license packet (`F0C7QEJ23FX`). Collision and placement are unchanged;
the importer regenerates a byte-identical collision header. Native removal and
fallback checks pass. Gravel remains very bright, and the repeated vegetation
and rock silhouettes still need improvement. Duplicate source-map copies and
downloaded split archives were removed after retaining the verified packed source.

Full front-door, exterior-charge and inter-room-charge scenario runs now pass
on Linux and native Windows. All use ordinary inputs from the canonical spawn,
with finite charge inventory, placement/retreat/remote detonation and actual
wall-plane crossing. No forced surrender, teleports, door-state edits, wedge
injection or scripted completion are used. The exterior route also physically
escorts its first civilian back through the hole. The inter-room route cuffs a
suspect who stops in the aperture, leads him to the cleared sidewalk, uses
follow/hold to leave him there, closes the front leaf through the hole, and
crosses it. That run includes lawful squad return fire: one suspect is arrested,
one armed suspect dies and an officer survives wounded. Front/exterior retain
two arrests and no deaths. All routes recover two weapons, rescue three
civilians, regroup the surviving squad and reject unlawful force or protected
deaths. Every run also passes exact per-tick replay, mid-run save/resume and
fresh-replica state/debrief reconstruction. This is repeatable controller QA,
not an RL policy or a substitute for human playtesting.

The shared route planner now respects physical open door leaves, resamples
their completed swing locally, backs away while a leaf opens, skips reached
coincident doorway samples only after a body sweep, and chooses a reachable
free stopping point beside a visible person. Cuffed suspects share civilian
follow/hold controls; they remain arrests, never civilian rescues, and learned
locomotion does not override custody movement. Both roles pass physical
open-leaf detours from either side. Furnished-room squad entry, tactical tools,
training locomotion equivalence, real UDP and ordinary native-player capture
also pass.

Debugger traces identified the contact overflows in the bed frame and luggage
rack. `tools/partition_motel_contacts.cjs` groups whole connected components
into at most 240 triangles per shared contact mesh. Rendering and ballistic
exit-distance queries retain the original complete surface. Linux/Windows
checks preserve 22,103 triangles and edge flags over 21 active asset types,
50,400 matching ray hits and exit distances, replica rebuilding and teardown.
All full completion routes now emit zero overflow warnings. Several unrelated
indivisible dense source components remain unpartitioned; this is not a claim
that every possible collision in the level has been stress-tested. Protocol
and replay version 15 distinguish this physical/navigation recipe.

Next integration: the delivered connected-ground/road handoff has verified
runtime/source/review archive hashes and 54 closed support pieces. Native
support, traversal, navigation-domain and rendering checks remain to be done.
The delivered service trio and roadside delineator are queued behind it.
Broader context, material refinement, human/co-op testing and the other
acceptance gaps above remain active work.

First slice increment: Linux encounter, motel, tactical, simulation, mission,
protocol, UDP, 3D navigation and save/replay checks pass. Native Windows encounter
and motel controller checks pass; the full Windows tool build, complete environment
graphics regression and ordinary `./swat play --mission motel --capture ...` pass.
Native proof and compact editable sources are under the guest-folder, room-numbers
and reception art handoffs. Shared the review on Slack (`F0C7N974YQ5`). The artist
subsequently delivered the east perimeter fence and Room 103 variation, integrated
in the third increment below. No claim of learned tactical behavior or a finished slice.

Alternative-route regression: with all doors wedged, the normal squad controller
remains blocked until destruction opens an exterior reception wall or guest-room
party wall, then physically crosses each opening. Linux and native Windows pass
(166 ticks exterior, 34 ticks inter-room). This tests traversal of the shared
breach result; charge placement/detonation has separate tactical/network tests.
Second increment: Stack/Clear and separated Move goals now pass those checks.
Linux tactical, network, stairs/3D navigation, encounter, motel and save/replay
regressions pass. Full native Windows build and encounter/motel/replay tests pass.
Runtime-only doorway/sector planning is reconstructed by input replay; it adds no
wire fields or RL observation/action dimensions. Combat can still interrupt entry;
this does not establish full room-search, trained behavior or scenario completion.

Third increment: the delivered east fence uses shared original mesh geometry at
roots (12.5,-.08,6/4/2), yaw +90 degrees, plus terminal at Z=0. Both ends remain
open. Live character traversal, 2,400 bidirectional opening/wire rays, four solid
posts, bullet energy and replica reconstruction pass. Mesh penetration now uses
actual outward triangles instead of counting bounding-box air as solid material.
Room 103's original wallet/glasses attach to desk owner 72. The location material
loader now has enough slots for the original 12-material module (the prior eight
slots could overwrite an adjacent row). Full Windows build and Linux/Windows
motel, squad-entry and save/replay checks pass; Linux simulation/UDP checks pass.
Native graphics assertions and ordinary `./swat` capture pass. Desk/tray
clearance is 75.35/70.86 mm. Shared engine proof on Slack (`F0C7XUJML3E`); the
artist is refining the roadside sign while the engine handles asphalt and fence
collision/destruction. See the east-fence and Room103-personal
art handoffs for retained source, measured placement, proof and limitations.

Fourth increment: original fence components are grouped into 29 shared collision
meshes per bay, each at most 240 triangles. Actual-controller charge placement,
interruption, consumption, retreat, detonation and crossing pass: 18 groups are
removed, 11 survive, neighboring bays/posts remain, replica and reset agree.
Wire/rail bounds no longer act as solid steel acoustic slabs. Rendering filters
the original triangles into cached material batches; missing art draws surviving
collision triangles. Canonical count is 1,044 and network/replay version is 12.
This is an authored approximation of fastening failure, not loose-wire physics.
Other dense room props can still reach the Box3D mover warning.

CC0 Poly Haven Clean Asphalt is installed only on motel ground owner 0 at its
measured 2.1 m repeat. Original 16-bit PNGs are retained; GPU readback matches
independently decoded normalized samples, including the single upload row flip.
Normal/roughness remain linear; walking/grazing native captures were reviewed.
The roadside sign now uses original painted lettering (319 instead of 3,075
triangles), preserving bounds, owner and collision. Native full graphics checks,
motel/controller and replay/save checks and ordinary `./swat` capture pass.
Sources, licensing and compact proof live in the asphalt, roadside and east-fence
art handoffs. Review logs are in `build/swat/review/fence-destruction`.

Matched native lighting/caster/unlit/removal diagnostics attribute the apparent
detached eyeglass strokes to seven original desk scratch boxes, not the personal
props or shadows. Compact decisive proof and the diagnostic command are retained
in the Room 103 handoff; the artist has a bounded desk cleanup queued after the
surroundings layout. Fence/asphalt/sign proof was shared as Slack `F0C7PCK3485`.

Fifth increment: motel masonry/board sections now share deterministic oblique
fracture vertices, preserving architectural openings and the existing object
count. Convex collision, original-art clipping, exposed-core strips, penetration,
support attachment and map reconstruction use the same polygon boundaries.
16,182 intact-wall rays pass from both sides; 19,856 native Windows GPU samples
match physical hull coverage on both fragment faces. Existing human/squad breach
routes, material protection, late replicas and save/replay checks pass on Linux
and Windows. Linux real UDP checks, the full Windows graphics regression and
ordinary `./swat` capture pass. Network/replay version is 13; use fresh journals.
`layout_tool motel-walls` format 2 exports exact source polygons and angled strips,
byte-equivalent after JSON parsing on Linux/Windows. Native proof is retained in
the motel-masonry engine handoff. This is coarse deterministic fracture, not
arbitrary stress-driven fracture, small chips or dynamic rubble.

Next: complete scenario playthroughs through different approaches, perception
and level context. The artist is building two surroundings modules: reachable
soil/gravel shoulders east X24..30 / Z-12..12 and west X-30..-24 / Z-8..8, with
scrub confined to the outer halves. Engine integration will add separate physical
support/solid-bank owners and verify seams/routes/replicas; final numeric IDs are
not assigned yet. Existing ground owner 0, openings and fence-end routes remain.
The empty ground boundary/background is still conspicuous until delivery is integrated.


The connected road/ground foundation is now integrated: 54 closed supports
(owners 1119–1172), an 8 m road, 6 m driveway and graded joins extend the
physical envelope to X [-64,64], Z [-48,52]. Original court/perimeter ownership,
staging and extraction stay intact. Original render/collision triangles and
identity placement are checked by the hash-validating Node importer. Runtime,
packed editable source, source/QA records and scoped CC0 provenance are in the
repo. Asphalt currently shares CONCRETE authority; gravel uses SOIL. No terrain
cratering or loose-gravel simulation is claimed.

Navigation keeps exact prior indoor 60 cm arithmetic while covering the wider
level with 216 × 168 cells and four height layers. Grid and 32-bit BFS scratch
share a lazy heap allocation, eliminating the enlarged Windows stack hazard;
small worlds retain their prior grid size. Linux/Windows ground QA checks 740
support/seam rays, finite edges, physical player/squad crossings, replica
reconstruction, high layer indices beyond 65535, modified-map rejection and
reset/close. Native graphics validates all 54 owner bounds, original PBR maps,
56,250 silhouette samples, independent removal and exact collision fallback.
Actual native context was shared as Slack file F0C7RRC86ER.

The road setting is still sparse and bounded. Far scenery and near-realism
dressing remain acceptance work. The following exterior increment installs the
coordinated gravel/stone material and collision-free shrub silhouette revision;
chipped rock geometry requires matched new collision. The backed-up
hose hanger and other maintenance/roadside props are queued for placement.
Full scenario checks continue to accept lawful return fire on every hostile
approach, rather than requiring an arrest outcome for a particular entry route.
Protected deaths, restrained deaths, unlawful force and missing evidence remain
failures. Protocol/replay version is now 16.

After this ground expansion, front-door, exterior-charge and inter-room-charge
completion all pass on Linux and native Windows, with exact per-tick replay,
mid-run save/resume and fresh-replica debrief. These runs finish with two arrests,
three civilian evacuations, both weapons collected, squad regroup and no deaths
or unlawful force; lawful hostile outcomes remain permitted by the QA. Existing
encounter, generated two-storey movement and real-UDP regressions pass. The normal
Windows player rebuild and ordinary `./swat` motel capture also pass on the GTX
1060. Full-slice acceptance still needs human/co-op playtesting, finished setting
and audio, and useful held-out trained policies.

The native exterior now uses the v3 gravel/stone factors (0.65/0.78), lower
asymmetric shrubs, the delivered 40-triangle texture-wear road markings and
36 measured grass/seedhead placements. Original scan images, three collision
GLBs, placements and all connected-ground BIN bytes remain exact. Collision
importers regenerate unchanged headers. Soft grass adds no cover or obstacle;
every footprint support must survive, including both pieces beneath W_01.
Opaque paint belongs to the existing asphalt owner, keeps its 1 mm offset and
receives lighting without casting shadows. The 12 m road crossing stays clear.

Linux/Windows ground/controller/squad/replica tests and the new support checks
pass. Six native road/plant views are pixel-identical with full versus culled
geometry, including lighting/shadow passes; world authority stays unchanged.
Native ground graphics retain the 54 owner matches, PBR maps, 56,250 ray/raster
checks, independent removal and exact fallback. New-model texture owner/pixel
lifetime checks pass. Packed editable sources, runtime, scoped licenses and
native review are in the repo; full source backups remain on Drive. Native
proof was shared on Slack as F0C80EFCE7P. Finite background edges, repetition
and sparse vegetation remain visible. The junction box, maintenance props and
new suitcase/bib handoffs are verified art deliveries awaiting physical placement
and damage integration; this increment does not claim them installed.

Prior crouch/locomotion transition blending, exact nearest-navigation lookup,
conservative render culling and shared model texture storage remain in place.
Full-slice acceptance still needs human/co-op testing, finished composition,
production audio and useful held-out tactical policies.

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
- Initial linear-light shading, cached sun/all-room depth shadows and
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
