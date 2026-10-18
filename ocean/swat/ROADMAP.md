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

Autonomous continuation (Felix, 2026-10-17): keep developing the entire slice
against the acceptance checks below without waiting for Felix to playtest each
increment. Use ordinary-input automated scenarios, deterministic replay,
save/resume, late replicas, source/asset audits and automated visual review as
appropriate. Reproduce failures, fix them and validate the resulting behavior;
commit completed increments and proceed to the next gap. Manual feedback is
welcome when available, but is not a development gate. A passing increment does
not complete the slice. Heavy performance runs remain held under Felix's earlier
machine-use instruction; their final acceptance stays open rather than blocking
independent work. Do not ask Felix to repeat already-authorized routine checks.
The persistent full-slice goal was resumed on 2026-10-17 and is active. Keep
that complete objective active until every acceptance requirement is verified.

Acceptance work still open:

- Extend squad task completion across varied occupied layouts and further
  equipment roles, including broader room-search and moving threat coverage,
  beyond directed restraint/retrieval, door picks/wedges, recall and escort.
- Extend transparent-cover equipment qualification beyond the tested queued
  task and working-position cases, including initial command selection through
  panes and moving obstructions.
- Extend hostile-fire squad coordination beyond the inter-room sequence with
  staging holds during demolition. Following officers can cross the commander's
  firing line, and exploratory all-follow runs lost the commander or an officer.
  Broader moving coverage and autonomous protection around equipment remain open.
- Confirm native menu-click/focus timing alongside the accepted headless
  repeated/rejected command, render-only frame and UDP acknowledgement cases.
- Extend contact qualification to boundaries introduced by further authored
  layouts and props; the ordinary-player wheel-stop regression is covered below.
- Extend varied-occupant full completions beyond the three alternate quiet
  front-entry situations and hostile Mixed rooms inter-room route below,
  preserving ordinary inputs, replay, mid-run resume and fresh replicas.
- Finish the motel's composition, remaining props, production audio and visible
  equipment handling; integrate approved art and inspect actual rendered output.
- Establish reproducible officer/enemy policy baselines and held-out evaluation
  before claiming learned improvement, using the normal training workflow.
- Complete native visual/interaction review and stable-load frame-time acceptance
  when the machine-use constraint permits it. Automated evidence must state its
  scope; headless success alone does not accept visual quality or performance.
- Qualify vehicle cover separately. The delivered sedan has a closed exterior
  coachwork volume, with no hollow passenger interior or suspension model;
  current game material sections do not establish panel-stack ballistics.

Selectable motel occupancy (2026-10-18): four situations share the
accepted geometry and art. Standard preserves the original five starts;
Dispersed uses deeper floor bays, Mixed rooms changes armed/civilian assignments,
and Busy adds a fourth civilian in Room 101. A private hash of layout seed,
actor and situation chooses authored bays without consuming the encounter RNG.
Alternate starts require supported feet, full physical feet/capsule clearance
and separation. Planning, player/server CLI, native environment configuration,
maps, scenario requests and journals carry the selection in protocol/replay v39.

Ordinary-input iteration exposed route connections that cut a static corner,
invalid radial cuff workpoints inside furniture, and recalled workers blocking
room approaches or holding position when all three trailing samples failed.
Navigation checks the actual start connection, searches bounded physical
cuff approaches, reserves the local portal approach and tries supported rear
flanks. A separate escort snag was the original folding-chair seat being treated
as step-over cover: navigation now recognizes legacy furniture without changing
its object schema, material or triangles. Hand reach/duration, body collision,
perception, finite equipment and threat priority remain in force.

The hostile Mixed rooms inter-room route (episode 2026, layout 7) then reproduced
crowd reversals when both doorway flanks were occupied. Sixteen extra bounded
sidesteps clear the third worker. A recalled officer also held an inside flank
when the leader waited just outside: exterior-door clearing now chooses the
outside side and preserves a formation goal already clear outside. Three portal
fixtures and a crowded reception/escort fixture cover these cases. The full QA
plan stages the team through ordinary Move/Hold before a charge and queues its
late cuff handoff until the commander has physically left the single-person
aisle. Covering sectors remain held while weapons are outstanding; the trial
that lost an officer with moving cover is retained as a failure. The fixed seed
and route subsequently completed in 21,658 ticks with lawful response to both shooters,
three rescues, both weapons and exact replay/save/resume/fresh-replica checks.

An independent Windows exterior replay exposed a float-libm aim discrepancy at
tick 6,366 despite identical inputs and pre-shot snapshots. One aim component
differed by one ULP, moving the impact/tracer/sound point by two ULPs. Aim,
weapon transforms and spread now evaluate double sine/cosine and round once to
float. Seven fixed runtime reference vectors regress the old path. The same
seed 81 exterior mission now completes and replays all 9,497 ticks exactly in both
shipping tools. This is supported-build evidence; exact hashes remain required.

The final cohort passes 201 broad groups per platform, with all 117,052 state
samples across nine trace pairs exact between Linux and Windows. Focused checks
cover 84 placement starts, eight rear restraints, twelve rear escorts, three
portal situations and a crowded reception exit. Seven old-behavior controls
reproduce their original physical failures. Real UDP verifies leader selection,
late join, task resets and removal of the extra civilian.

Seven fresh ordinary-input missions complete in 25,811 / 23,037 / 28,275 /
21,658 / 28,019 / 20,529 / 9,497 ticks, with exact internal replay, checkpoint
continuation, fresh replicas, clean rules of engagement and no protected deaths.
Both shipping tools match all 156,826 mission frames per platform. Independently
recorded player journals and checkpoints are byte-identical. The source-coupled
receipt is `tests/receipts/occupied-motel-2026-10-18.json`; artifacts are under
`build/swat/review/occupied-motel`. This finite matrix is progress toward the
full slice. Further seeds, routes, moving threats, tool roles, art/audio/policy
and held native visual/focus/performance acceptance remain open.

Physical tool access through clear glass (2026-10-18): a queued Pick behind
an intact clear pane reproduced an officer stuck in Approaching with a planned
position and zero tool progress. Optical sight correctly reached the wooden
leaf, while the real hand ray hit glass. Working-position selection now uses
the physical ray. The separate optical perception query is unchanged.
The original probe and a current-rule negative control restoring optical-only
access both reproduce the false approach status.

The damaged-door matrix now has 60 cases: the existing 42 plus 18 clear-glass
cases across Pick/Wedge, three orientations and full/partial/low-gap cover.
Fully covered tasks publish Waiting for access, spend no stock and resume when
the pane is removed. Partial and low-gap cases complete with intact, undamaged
glass; low gaps require actual crouched work. Tool progress still requires the
ordinary physical hand hit. Fresh replicas and malformed-state rejection remain
part of each case. Before the version bump, all 11,996 existing opaque/cut state
samples remained exactly unchanged by this fix. Initial command targeting
through clear glass remains a separate qualification task above.

The v38 cohort passed 199 broad groups per platform, with all 17,439 focused
and 81,399 encounter state samples exact across Linux/Windows. Movement,
foundation/scenario, real UDP room/equipment/frontend and canonical motel
slow-join checks also pass. Both independently recorded player journals and
checkpoints are byte-identical. Four fresh ordinary-input missions complete in
25,741 / 25,730 / 20,457 / 9,513 ticks, with exact internal replay, mid-run
save/resume, fresh replicas and no protected casualties or unlawful force.
Shipping Linux/Windows tools match all 81,441 mission frames, all four native
saved prefixes and the 60-frame frontend recordings; prior v37 files reject.
The source-coupled receipt is
`tests/receipts/transparent-door-access-2026-10-18.json`.

The transparent-access increment used protocol/replay v38. Prior v37 source,
artifact and deployment evidence remains sealed under
`build/swat/review/damaged-door-tasks`; this candidate uses
`build/swat/review/transparent-door-access`. Native graphical/focus and
performance acceptance remain held. The full-slice goal remains active.

Damaged-door squad equipment (2026-10-18): a retained leaf with its centre
removed exposed a real task failure: the officer remained at the old centre
workpoint for 1,200 ticks while an ordinary human Pick succeeded on visible
material. Pick/Wedge now scan actual surviving convex sections for deterministic
workpoints and find supported, body/person-clear standing or crouched working
positions with physical sight and a navigable approach. The ordinary hand ray,
capability, finite stock and threat priority still control progress. Missing
workpoints invalidate after damage. Fully obstructed work waits and retries in
batches of at most sixteen material targets, retaining the scan cursor rather
than dropping later fragments. The squad menu replicates “Waiting for access.”
Cancellation, completion and human takeover clear that state.

Both platforms pass all 42 cases: Pick/Wedge across seven cut/cover/change/
cancellation conditions and three orientations. The lower-gap cases require
actual crouched work; fragmented searches advance beyond sixteen candidates.
Fresh encoded replicas preserve the access state and reject malformed state
without mutation. All 11,996 focused state hashes match after host text-line
ending normalization. The fixtures establish explicit geometry and cover events;
they are distinct from the ordinary-input complete motel journal below.
A first cross-platform mismatch was only two ULPs in the handling sound's ray-hit
position; actor state, private goals and aim matched. Named squad tool handling
now originates at the stable planned material point. Reach still uses the real
ray, and replay hashing still includes the full sound state. The first differing
snapshots, private diagnostics and earlier traces are retained.

The ordinary-input damaged-door mission removes the centre with two settled
shots from the real finite magazine, retains the leaf/hardware, queues a squad
wedge, saves/restores the queued task, uses surviving material, removes the
wedge normally and completes the full mission. Rapid firing initially produced
scattered recoil holes instead of the intended centre aperture; that failed
journal is retained and only the QA commander's firing cadence changed.

The complete route finishes in 25,741 ticks with no protected casualties or
unlawful force, exact every-tick internal replay, mid-run save/resume and fresh
encoded replicas. Shipping Linux and native Windows tools also match all 25,741
frames exactly, including the ordinary human wedge removal.

The final v37 cohort passes 181 broad groups on each platform, plus the
unchanged locomotion/foundation/scenario and player-contact checks. All 75,956
encounter samples and 1,751 player-contact samples match across platforms;
independently recorded player journals and checkpoints are byte-identical.
Fresh quiet, hostile inter-room and hostile exterior missions finish in
25,730 / 20,457 / 9,513 ticks. Together with the damaged-door route, shipping
Linux and Windows tools match all 81,441 recorded mission frames exactly.

Real UDP checks also replicate blocked status and actual resumption after cover
removal in an explicit geometry fixture. The separate canonical motel case
retains all authored collision meshes on its replicas and passes the unchanged
slow-join/takeover/tool-stock checks, including more than 6.5 seconds of injected
construction delay with a fixed gameplay tick. An earlier test wall invalidated
strict motel source binding and shortened that loading path; its failure and
diagnostics are retained. The correction separates the two fixtures, preserves
the production deadline and asserts actual mesh binding.

Protocol/replay v37 requires matching peers and fresh recordings/saves. The
status display and complete native graphics cohort are compiled; native visual,
focus and stable-load frame-time acceptance remain held. Evidence is retained
under `build/swat/review/damaged-door-tasks`; the source-coupled receipt is
`tests/receipts/damaged-door-tasks-2026-10-18.json`. The full-slice goal remains
active, with transparent-cover qualification and the broader gaps above open.

Player contact at wheel-stop lips (2026-10-18): direct ordinary player input
reproduces the retained launch without the discarded departure planner. A valid
grounded lip departure/re-entry reaches 67.964 m/s upward velocity. Ground
snapping was repeatedly pulling the full feet box into concrete after the
solver had separated it, because a shrunken support probe could find the floor
beyond the lip. Downward corrections now require clearance for the actual feet
hull and leaned capsule, including mesh rest offset. Velocity remains under
the existing controller/solver rules. Upward stair casts and source geometry
retain their behavior.

Both platforms pass 180 crossing/blocked-approach trials over all five original
stop meshes, both directions, three lanes, three gaits and both stances. All
starts check the entire footprint and settle on supported clear ground; eighteen
car-side starts move beyond the actual sedan rather than spawning inside it.
Four captured departure/re-entry cases finish at their destination on the court
floor. A default-spawn ordinary-input route completes twelve crossings/reversals
in 1,624 ticks, with checkpoint restore and fresh encoded replicas. All 1,751
focused state samples match across Linux/Windows; independently recorded journals
and checkpoints are byte-identical. Maximum observed feet Y is 0.149044 m and
absolute vertical speed 2.481589 m/s. Removing only the downward-snap guard
reproduces the 67.964 m/s launch.

The unchanged locomotion, foundation and scenario suites pass on both platforms,
including all five training stages/both roles, 1,000 sweep equivalence checks,
6,000 movement decisions, stairs/ramps, commanded upper-storey squad ascent,
entry replay and atomic mission save/continuation. The existing 139 broad
functional groups also pass on each platform with matching recorded states,
including real UDP room search, equipment, frontend and slow map construction.
The quiet mission's automated commander regroups in the court before entering
for hands-on work. A distant officer correctly covered a separate armed suspect;
an interior regroup had no valid trailing formation slot. Retained failed
journals explain the route change, which uses ordinary commands and movement.
Combat retains its paced moving coverage; an exploratory court regroup exposed
an officer to fatal fire during evacuation and is retained as failed evidence.
Late Windows playback also exposed a two-ULP officer pitch difference during
the wedge approach; positions, velocities, gear and sounds matched. Private
planning diagnostics isolate a one-ULP difference in the stored ray-hit target
height, with identical eye and distance calculations. Named door equipment
orders now anchor to the selected leaf centre. The ineffective math-only change
was removed. Different aimed contact heights still select a single capable
operator, retain the leaf identity and require actual tool reach/completion.
Failed journals, first-divergence snapshots and planning diagnostics are retained.
The shared-process squad network fixture now services the authority and other
peers while a replica constructs its world. The loading client still relies on
production socket service. A deliberate late join adds more than 6.5 seconds of
construction delay without advancing gameplay or changing shipping deadlines;
the original timeout and unchanged successful retry are retained as well.
Fresh v36 quiet, hostile inter-room and hostile exterior routes complete in
25,620 / 20,457 / 9,513 ticks. The quiet route arrests both suspects; the hostile
routes resolve two armed threats lawfully. Every route evacuates all three
civilians, recovers both weapons and regroups the squad without protected
casualties or unlawful force. All three pass exact whole-mission playback,
mid-run save/resume, fresh replicas and no contact-buffer overflow. The final
Linux and Windows shipping tools each replay all 55,590 mission frames exactly;
native queued room/wedge saves match 1,237 / 23,144 frames. Independent player
journals/checkpoints and frontend journals are byte-identical, with exact shipping
playback; both platforms reject v35 recordings. All 42 executables and four
unchanged deployed sedan/shelter GLBs have matching installed hashes. Native
graphics and heavy performance acceptance remain held.
Protocol/replay v36 requires matching peers and fresh recordings/saves. Evidence
is retained under `build/swat/review/player-contacts`; the source-coupled receipt
is `tests/receipts/player-contacts-2026-10-18.json`.

Roadside engine integration (2026-10-18): the original approved parked sedan
faces the guest rooms from (-6, -0.08, 6.65); the waiting shelter faces the road
from (-8, -0.08, 34.75). All 311 delivered solids and 20,992 source triangles
retain their original geometry, maps, UVs and provenance. Separate generated
descriptors assign 50 sedan and 28 shelter material/attachment owners. Actual
tire and baseplate contacts seat them on existing ground. Sparse physical
meshes preserve wheel arches and shelter openings; contact subsets stay within
240 triangles. Shared original model banks render through their first surviving
owner, with cached ownership filtering after damage. Normal runtime installation
includes both GLBs. No authoring/Python recipes are executed.

Protocol/replay v35 adds these physical objects while retaining old canonical
map prefixes and object IDs. Map construction now services the client's own
transport between geometry chunks. Optional external loading callbacks must not
access the partial replica; temporary world hooks are cleared before construction
returns. Current-state
publication advances transport revision without stepping gameplay. Forced
Linux/native construction delays exceeding 6.5 seconds pass with reliable ping acknowledgement,
tick-zero authority/replica and unchanged six-second shipping deadlines. Removing
only the production construction service reproduces failed reliable control
delivery after an 8.224-second load. QA services other hosts separately, so its
hook cannot hide missing production socket service.

Both platforms pass 139 broad headless groups with 63,938 matching state samples,
plus seven roadside cases and 6,382 actual furnished-room states. Ordinary
controller input reaches the shelter from the court in 602 ticks. Glass, timber,
steel fixings and the screen remove independently, retain encoded/fresh replicas
and reset correctly. Existing room/equipment/frontend real-UDP cases pass.
An isolated Linux Xvfb/llvmpipe display checks every original oriented triangle
exactly once within its physical owner, ten embedded textures, and sixteen
source/fallback raster cases against physical rays before and after damage.
Four additional production-lit views are inspected. Rendering leaves authority
unchanged and releases its textures. This is a one-thread CPU correctness check
with two isolated props and a reference plane, not full-scene native acceptance.
The final native quiet replay exposed a one-ULP blocked-handle sound height
difference; actor/object state matched. Feedback when trying a locked, wedged or
charged door now attaches to the leaf centre, like ordinary open/close cues.
Actual aimed reach and all blocking/tool rules remain unchanged. Ordinary held
interaction checks three blocked states at two aim heights, with one cue and no
state/tool consumption. The QA commander also waits for physical team regroup
before assigning room search, surveys the known facade while strafing along
court waypoints and turns toward coarse audible gunfire before requiring actual
visual threat confirmation. Retained failures show why these ordinary-input
plans changed; gameplay AI, navigation and weapon rules are unchanged.
Matching v35 peers and new recordings are required; native graphics/performance
and vehicle-cover qualification remain open. Evidence is retained under
`build/swat/review/roadside-integration`.

The v35 quiet front route finishes in 25,571 ticks with two arrests, three
evacuations, both weapons recovered and squad regroup. Hostile inter-room and
exterior routes finish in 20,819/9,512 ticks with two lawful suspect deaths each.
All three preserve replay/save/resume, fresh replicas, truthful debrief and no
contact-buffer overflow; no civilian/officer dies and no unlawful force occurs.
Their complete journals replay exactly in the final Linux and Windows shipping
tools. Native queued room/wedge prefixes match 1,301/22,907 frames, and frontend
journals remain byte-identical with exact 60-frame replays. Older v34 recordings
are rejected on both platforms. Receipt:
`tests/receipts/roadside-integration-2026-10-18.json`.

Physical squad room search (2026-10-18): Search room captures the room selected
through a visible surface or door, assigns one officer from the selected team
and retains that identity through queue/Execute. The officer physically visits
nine supported coverage regions and turns through eight views at each. Actual
views can reveal people to restrain and weapons to recover. Gunshots and visible
threats interrupt the task; Hold and human takeover cancel it. An obstructing
leaf can be closed through actual hand interaction. A seen cuffed occupant
blocking a furnished aisle can be escorted to a supported holding bay and
stopped before the search resumes. Planning verifies the actual resulting route;
it never moves bodies or ignores their physical occupancy. Unreachable coverage
stays unfinished, and the menu reports route searched without an all-clear claim.

Protocol/replay v34 replicates active/queued room IDs, progress, subtask and
blocked state, with scalar and atomic map-aware rejection. Private planning is
reconstructed by input replay. Clean 23-core/13-view-unit builds cover Linux and
Windows player/server/replay/performance tools and ten QA executables per
platform. Both platforms pass 137 broad headless groups, including thirteen
room cases with 45,663 matching state samples and 1,527 fresh-map/encoded-snapshot
roundtrips each. The actual furnished motel fixture matches another 6,077 states,
204 encoded snapshot applications and two fresh maps per platform. It starts
with compliant occupants; visual threat priority is covered separately. Actual
queued/active human takeover and completion also pass three semantic milestones
over real motel UDP on each platform. The existing equipment, frontend and
transport checks pass; frontend journals remain byte-identical at 60 frames.
No graphics window or GPU/performance tool is run. A contended same-process
cold join initially timed out before game inputs; final serialized functional
runs pass with shipping timeouts intact. Stable-load responsiveness and visual
acceptance remain open. Evidence is retained under `build/swat/review/room-search`.

The full quiet front route adds queued Room 101 search with journal
checkpoint/restore to restraint, weapon recovery, escort and queued wedge work.
It finishes in 25,155 ticks with two arrests, three evacuations, both weapons,
squad regroup and no casualties or unlawful force. Fresh hostile inter-room and
exterior routes finish in 20,644/9,637 ticks with two lawful suspect deaths each
and no civilian/officer deaths or unlawful force. All three journals replay
exactly in both shipping platforms, including every authoritative sound state.
A retained one-bit cuff-sound height divergence is fixed by attaching restraint
handling to the target's body rather than the aimed ray contact. A head/torso
aim regression checks this through ordinary held cuffs. The QA combat driver
now corrects actual reticle recoil through bounded mouse inputs; gameplay weapon,
damage and AI rules are unchanged. Matching v34 peers and new journals/saves are
required. Receipt: `tests/receipts/room-search-2026-10-18.json`.

Approved roadside art intake (2026-10-18): the sedan, shelter and UPDATE41
catalog are retrieved from Blenderbot's approved public handoffs. Their archive
hashes and all 849 indexed payloads verify. Fresh runtime inventory matches
13,124 sedan triangles / four opaque materials and 7,868 shelter triangles /
three opaque materials. The supplied export contact sheets were inspected;
source/provenance files remain unchanged and no authoring recipes were run.
These static candidates await engine placement, collision/navigation,
material/destruction binding, native rendering and performance acceptance.
UPDATE41 is a metadata/evidence index, not a complete runtime bundle; later
handoffs and engine receipts supersede its frozen statuses. The public thread
has an engine acknowledgement; no private searches were made. Receipt:
`tests/receipts/roadside-intake-2026-10-18.json`.

Squad door equipment (2026-10-17): Pick lock and Wedge door now select one
capable officer from the requested team, preserving the visible door and reported
point through the radio delay and queued Execute. Recon and Control carry picks;
Entry does not. Human and bot tools share this capability check. Wedges consume
the selected officer's stock. Supported work positions, ordinary navigation,
aimed hand rays and held tool durations perform the work. An unrelated door
cannot accept the assigned tool; Hold cancels, gunshot attention pauses, visible
threats take priority and human takeover cancels queued work. Other officers keep
their orders. Completion returns the operator to Hold.

Protocol/replay v33 now carries active/queued door references. Scalar decode and
map-aware application reject invalid/non-door targets before mutation. Matching
peers and new journals/saves are required. Clean 23-core/13-view-unit builds cover
both platforms, including the player, server, replay/performance tools and ten
QA executables per platform. No graphics window or performance tool is run.
Both platforms pass 122 broad headless groups. Thirteen equipment cases match all
3,944 state samples and 136 direct-map/encoded-snapshot roundtrips per platform.
One negative case directly exercises the tool API against another door; these
samples are not all independent simulation ticks. The existing frontend input
suite retains 78 matching states and byte-identical 60-frame journals.
Real motel UDP checks replicate queued Pick, physically unlock the authored leaf,
cancel queued work when a human joins, and give the remaining officer one wedge
placement. Three semantic milestones match across platforms. Native transport
uses the existing approved executable path and restores the ordinary network QA
tool under the same lock; five isolated wrapper cases cover that behavior.

The full quiet front route finishes in 25,515 ticks with two arrests, three
evacuations, both weapons, squad regroup and no casualties/unlawful force. It
adds a queued wedge with checkpoint/restore, physical placement and ordinary
human removal. The hostile inter-room/exterior routes retain their 20,268/9,637
tick results with two lawful suspect deaths each and no protected casualties.
All three pass full replay, mid-run resume and fresh replicas; native shipping
replay matches all 55,420 mission frames and the 23,337-frame queued-wedge prefix.
Motel doors start unlocked, so locked-door Pick remains covered by explicit
fixtures. An initial
mission plan also correctly paused work on sight of an unresolved armed suspect;
the accepted equipment plan accounts for occupants first. Room search, broader
moving coverage and final visual/performance acceptance remain open. Evidence:
`tests/receipts/squad-equipment-2026-10-17.json`. The full-slice goal remains
active and heavy performance testing remains held.

Frontend command delivery (2026-10-17): squad selections survive render-only
frames and failed input enqueue. They retain the selected team/queue choice and
are consumed after a local simulation tick or successful reliable submission.
The same frame's remaining ticks receive neutral order input. A neutral release
tick also rearms an immediately repeated order, preserving new directed targets.
Rejected targets do not become sticky requests when the view later crosses a
valid person/weapon. Newer unsent selections survive older handoffs; cancellation
clears requests on restore, episode/session changes and restart/scenario changes.
Nonleaders receive an explicit notice. A failed remote handoff stays in the
remote dispatch path instead of advancing the read-only replica locally.

Input collection now lives in `frontend_input.c`, shared by the player and
window-free tests with inert OS input. Eight cases pass on Linux/Windows and all
78 recorded local state hashes match. A real-house 60-tick journal retains the
two request pulses, neutral release and Execute; its bytes match across platforms
and every recorded state replays exactly. Real UDP checks verify delayed cumulative
acknowledgements and two distinct queued restraint targets; both platforms report
the same request/release sequences and recipient identities. Four isolated shell
cases verify temporary approved-path success/failure cleanup, original-tool
restoration and normal network-candidate installation. Native UDP checks use that
existing approved path, then restore its ordinary QA tool. CMake builds and runs
the headless targets without a Raylib library. Player and interactive-QA binaries
are rebuilt for both platforms; no window is opened by this acceptance pass.
Gameplay, protocol and saves remain v32. Native OS-event/visual acceptance stays
open; manual playtesting remains optional feedback. Evidence:
`tests/receipts/frontend-command-2026-10-17.json`. The full-slice goal remains
active, and heavy performance testing remains held.

Squad gunshot attention and combined hostile QA (2026-10-17): officers turn
toward propagated gunshots using only a coarse bearing. Attention expires after
90 ticks, including while stunned. Formation movement continues beneath the
bounded view turn; restraints and weapon retrieval pause and then resume.
Hearing never supplies a shooter identity or hidden position, and actual shots
still require visual confirmation and the ordinary aimed firing-line check.
Eight focused cases cover hearing versus footsteps, opaque cover, civilian
protection, lawful return fire after cover clears, formation movement, retrieval
resumption and stun expiry. All 1,950 state hashes match across Linux/Windows,
with 65 fresh map/encoded snapshot checks per platform. The clean v32 tools pass
108 headless groups per platform, including real UDP and twelve matching encoded
motel states. One earlier UDP synchronization failure is retained; these checks
do not qualify adverse-load or packet-loss behavior.

The level-aware QA commander now responds to observed gunfire and visible raised
weapons aimed at the team, keeps observed threats while visible and armed, and
aims at the actual exposed perception point. It walks at the existing formation
pace during hostile delegated-task runs, holds the team at extraction before the
inter-room charge, faces the charge before triggering, covers the opening and
recalls officers afterward. These are ordinary QA inputs, not game assistance
or NPC strength changes. Protected casualties now fail immediately with a closed
diagnostic journal. The hostile inter-room seed-2026 mission finishes in 20,268
ticks with three squad civilian restraints/evacuations, two squad weapon recoveries,
two lawful armed-suspect deaths and no protected deaths or unlawful force. The
quiet front seed-82 mission retains its 24,220-tick, two-arrest/no-death result.
Full replay, checkpoint, replica and installed-tool evidence is recorded in
`tests/receipts/squad-fire-2026-10-17.json`. Earlier failed approaches and the
firing-line obstruction trace remain in that receipt. Remaining coordination,
varied layouts, visual quality and performance acceptance stay open. No Python,
root trainer/CUDA rebuild, graphics or performance test was run.

Directed weapon retrieval and parking-stop navigation (2026-10-17): Secure
weapon names the visible dropped weapon and assigns one eligible officer from
the selected team. Queued orders retain that identity and reported position;
the officer follows real navigation, obtains hand reach and collects through
ordinary interaction. Other weapons and people retain their interaction rules.
Nearby evidence cannot stop the officer outside the assigned weapon's reach.
Completion/cancellation returns to Hold, and idle/recalled officers raise their
view after hands-on work. Ten focused cases cover queues, interruption, cover,
threat priority, target identity, team selection and the reach boundary. Their
3,880 encoded state hashes match on Linux/Windows, with fresh replicas every
30 ticks. Captured grounded/velocity fixtures reproduce the parking-stop snag.
The five stops now use a permanent obstacle part: original triangles and contact
shapes remain, while navigation excludes their tops and underlying footprint.
The decoder distinguishes these objects from attached breakable fixtures and
rejects malformed obstacle maps without mutation. Fresh complete-run replicas
now use encoded/decoded maps as well as snapshots.

Clean v31 Linux/Windows builds pass 100 headless groups per platform across
encounters, navigation, motel geometry, tactical rules, simulation, protocol and
real UDP. The separate focused suite also passes. The full quiet front route
with five squad restraints and both squad weapon recoveries finishes in 24,220
ticks with two arrests and no deaths. Ordinary hostile inter-room/exterior
routes finish in 12,599/11,707 ticks, each with one arrest and one lawful armed
suspect death. All three evacuate the civilians, recover both weapons, regroup
the officers and record no protected deaths or unlawful force. Every-tick
replay, mid-run save/resume and final wire replicas pass. Both clean platforms
replay all 48,526 frames exactly; Windows also verifies the 14,557-frame queued
weapon checkpoint. The combined hostile squad case remains explicitly open
above. Matched player/server/replay/performance tools and QA executables are
installed; performance tools were built only. Use fresh v31 recordings/saves.
Evidence: `tests/receipts/squad-search-2026-10-17.json`. No Python, root trainer
rebuild, graphics or performance test was run. Manual playtesting is optional
feedback, and the full-slice goal remains active.

Motel transport and seed acceptance (2026-10-17): the real UDP suite now adds
a motel listen host with three remote peers. Fourteen check groups pass on both
Linux and Windows, including queued directed-target handoff to a joining human,
independent remote movement, late join after separate lid damage and a wall cut,
physical aperture queries, received sound history and hearing-consumer gating,
epoch restart and disconnect. Twelve captured encoded simulation states match
across platforms, excluding sound history, which is tested separately. These
fixtures directly establish a surrendered occupant, damage, a cut and sound
events; they do not constitute a four-player ordinary-input mission completion.
The test helper allows sequential replica rebuilds to finish without changing
the game's connection timeout. Seed 82 completes the squad front route in 20,584
ticks with two arrests and no deaths; seed 2026 completes the hostile inter-room
route in 12,715 ticks with one arrest and one lawful armed-suspect death. Both
verify all objectives, no protected deaths/force violations, exact every-tick
replay, mid-run save/resume and final replicas. Windows replays all 33,299 frames
exactly. These seeds change resolve/RNG with fixed authored occupant positions;
varied layouts/populations and learned-policy evaluation remain open. Reproduce
with `net-test`, `net-motel-test` and `motel-seeds-test`; the sealed evidence is
`tests/receipts/motel-network-seeds-2026-10-17.json`. This increment changes QA
and documentation only; the installed v30 game remains unchanged. No graphics
or performance test was run, and manual playtesting remains optional feedback.

Dumpsters and complete approaches (2026-10-17): the approved BC_DUMP01 is now
placed beside the maintenance shed at (-4.9, -.08, -8.4), yaw pi. All eight skid
corners contact the real foundation. Its first candidate moved 0.6 m east to
keep the original service exit clear; all nine existing capsule routes pass.
The unchanged source supplies the thin steel body, two independently removable
closed lids, open fork sleeves and underbody space. Thirteen bounded collision
subsets preserve all 2,908 source triangles. Steel and molded-lid game responses,
support dependencies, reset and encoded late replicas agree across platforms.
Actual bullet queries pass through the fork opening and stop at the steel.
Twenty-seven encoded states match on Linux/Windows, including ordinary walking
through both service lanes and stopping at the container. Both platforms pass
85 headless check groups across the prop, prior props/windows, motel, encounter,
tactical, simulation and protocol suites. Fourteen small native correctness
captures check original materials, shared maps, art/fallback silhouettes,
lighting and lid removal. These use graybox yard context, not finished-level
visual acceptance; selected captures and the sealed receipt are retained in
`art_handoffs/motel-dumpster/ENGINE_INTEGRATION.md` and its adjacent files.

The complete squad-restraint front route finishes in 20,584 ticks with five
squad cuffs, two arrests and no deaths. Hostile-fire exterior/inter-room charge
routes finish in 11,697/12,715 ticks, each with one arrest and one armed suspect
lawfully killed. All three evacuate the civilians, recover both weapons, regroup
every officer and record no protected deaths or unlawful force. Linux verifies
every-tick replay, mid-run save/resume, fresh final replicas and no contact-buffer
overflow. Windows replays all 44,996 frames exactly. These remain fixed-seed 81
checks, not held-out policy evaluation or a performance result. The geometry-only
breach fixture now accounts for officer backoff while selecting the actual
authored wall; the ordinary-input charge runs retain real placement reach.
Matched v30 player/server/replay/performance builds are installed with the asset;
fresh recordings/saves are required. Performance tools were built only.

Squad task and escort pass (2026-10-17): Restrain now assigns one nearest
eligible officer to the aimed occupant and preserves that person's identity
through radio delay, queues, replication and saves. The officer obtains
compliance, approaches physically and holds the real cuff interaction. Glass
can transmit the order's sight ray but blocks hands; opaque cover prevents
selection and hidden movement cannot update the remembered position. Hold
interrupts the task, a different visible threat takes priority, and a completed
cuff holds position rather than starting an escort.

Fall In continues moving while tracking a visible threat, trails the commander's
heading on connected floor and holds its feet if formation support is blocked.
Recalled officers and escorted occupants use supported sidesteps at open doors
so their leader can pass. Staging destinations remain inside extraction, with
free reachable alternatives for an inward-facing commander or a crowd of
rescued people. When people block every grid exit, navigation can seed a nearby
supported cell from the actor's actual feet after fresh swept clearance checks.
All movement retains ordinary controller inputs and body collision.
Sixteen focused cases pass on Linux and native Windows; all 8,501 recorded
states match, including 291 fresh encoded replica roundtrips per platform.
Actual body sweeps and commander passage cover both doorway mirrors for recalled
officers and active escorts. Centre/edge/crowded extraction cases include all
three officers; a separate commander walk physically escapes the reproduced
staging crowd. These are authored tactical behaviors, not learned-policy results.
Complete ordinary-input front routes finish in 19,998 ticks with all five
restraints delegated to the squad (hostile fire disabled), and 11,611 ticks
with hostile fire enabled and player restraints. Each has two arrests, three
civilian evacuations, both weapons recovered, all officers regrouped and no
deaths or unlawful force. Linux verifies every-tick replay, mid-run save/resume,
fresh final replicas and no contact-buffer overflow. Both platforms also pass
the full encounter, tactical, simulation and protocol suites; Windows replays
all 31,609 frames of the two Linux journals exactly. Durable receipt:
`tests/receipts/squad-tasks-2026-10-17.json`. These fixed-seed
front routes do not reaccept charge approaches, varied seeds, visual quality or
performance. Evidence: `build/swat/review/squad-tasks`.
Protocol/replay v29 requires matching builds and fresh recordings/saves.
The approved dumpster source/intake is committed; level placement, collision
binding and native material/visual acceptance remain open. Blenderbot reports
the standalone shelter candidate backed up, with its sharing request pending.
Heavy performance testing remains held; manual playtesting is not a gate for
continuing the remaining slice work.

Entry interruption and interaction feedback (2026-10-17): one Execute pulse now
arms all queued recipients through their existing radio delays. Unexecuted
queued Clears no longer hold up an active entry queue, and human/dead recipients
cannot execute stale bot commands. Scripted officers recheck the actual aimed
sight ray before consuming ammunition, canceling direct or buffered shots after
same-tick compliance. Human input and its ROE consequences remain unchanged.
Four controlled fallen-leaf entries cover both occupant mirrors, actual compliance
commands, and Hold followed by a fresh Clear. Fire is disabled in those movement
fixtures; short hostile-fire cases independently exercise the firing guard.
Linux/Windows interruption traces match all 4,959 snapshot digests. Both platforms
also pass the full encounter, tactical, simulation and protocol checks.
A complete ordinary-input hostile-fire motel front route finishes in 11,749 ticks
on each platform, with two arrests, three evacuations, both weapons collected,
squad regrouping and no civilian/officer deaths or unlawful force. Every-tick
replay, mid-run save/resume and fresh replica outcomes pass; each platform replays
the other's complete recording exactly. Evidence: `build/swat/review/entry-followup`;
durable receipt: `tests/receipts/entry-interruptions-2026-10-17.json`.
Door HUD feedback uses a read-only actor sweep to identify an obstructed swing;
tool prompts explain reload/switch recovery. Visual review and performance runs
remain held while Felix uses the machine. Protocol/replay v28 requires matched
builds and fresh recordings/saves. This is another slice increment, not full
vertical-slice acceptance.

Room 101 jamb support (2026-10-17): destroyed masonry at each authored fixed
hinge mount now releases that attachment independently of leaf damage. Local
cuts, full backing removal and wall-charge apertures use the same support points
as the visible hardware. An intact opposite latch remains an independent
attachment. Support is refreshed on geometry changes rather than each tick.
Snapshot validation prepares all incoming frame geometry before checking the
combined leaf/frame failure bits, preserving atomic rejection and late joins.
Protocol/replay v27 requires matched builds and fresh recordings/saves.
Linux and Windows pass 25 functional checks each across the door-frame,
door-support, breach-geometry and protocol fixtures. All 794 new jamb-state
digests agree across platforms; each platform also replays the other's
1,314-tick ordinary-input door journal exactly. The new intact-leaf hanging
cases have at most 5.051 mm stepped anchor drift under contact, within the
existing soft-joint approximation; initial joint frames coincide without a
pose jump. Evidence: `build/swat/review/jamb-support` and the separate
`art_handoffs/motel-door-hinges/engine/JAMB_VALIDATION.json` receipt.
Open motel map reconstruction also uses the same deterministic rotation as the
physical door; its former libc-trig reconstruction could reject a 45-degree map.
This increment is limited to Room 101's defined fixed mounts; other doors retain
their leaf-side failures. Fine metal colliders remain future work. Performance
testing remains held while Felix uses the machine.

Occupied entry and sound pass (2026-10-16): officers receiving Clear/Move now
keep translating along their physical route while tracking a visible suspect,
instead of freezing the first officer and the entire entry queue. Movement
rebases into the aimed heading and retains policy-controlled locomotion. Loose
leaves are treated as physical obstacles rather than usable closed doors.
Steering and controller translation use the existing deterministic math to
remove Linux/Windows angle/velocity drift exposed by the new cases. Six
controlled opening/fallen-leaf/open-breach fixtures with mirrored occupants
complete separated sectors and match all 6,575 snapshot digests across platforms.
Those fixtures disable hostile fire and use stationary target placements; full
motel hostile-fire and actual charge routes are separate checks.

Two recorded opening, two closing and three mounted-breach variants bring the
production bank to 92 entries in its existing 96 slots. CC0 source URLs, archive
hashes and unchanged selected Ogg hashes are retained in the bank provenance.
Closing commands use a handle cue; frame contact emits once on achieved closure,
with no premature close sound from an obstructed leaf. Mounted charges have a
distinct replicated breach kind; flashbangs retain their own cue. Protocol/replay
v26 requires matched builds and fresh recordings/saves. Both platforms produce
byte-identical listening WAVs through the real bank/mixer. Room 102's mug and
Room 104's remote reuse existing small assets on their actual desk owners.

The seated Room 101 assembly is integrated: three local reliefs, a 6 mm casing
retreat, and separate leaf/jamb metal, pins and failure remnants. The complete
81-file handoff is retained. Independent checks verify all 80 payload hashes,
frozen local inputs, unchanged embedded maps, protected positions/UVs and all
three actual faceted pin/bore clearances. New metal shares the opening axis;
wood-support damage probes remain inside the slab beyond its reliefs.
The casing's render ownership reaches 20 mm beyond the coarse wall section
to preserve its authored inner edge, while following damage to its backing.
Native source/clipped casing areas agree to approximately 2e-8 square metres.
Both platforms pass hanging/falling silhouettes, seven cached/full shadow
comparisons and a pixel-identical late replica; native grazing checks have
zero missing solid-door pixels. Evidence is under `build/swat/review/hinge-seated`.
Hardware and splinters are visual attachments to the existing game slab/joints;
fine metal colliders remain future physics work. The 17 October increment above
adds logical failure from Room 101 jamb destruction. The inherited optional
threshold overlap is unchanged.

Performance tooling now optionally includes the solo journal's actual per-tick
digest, with digest/file-append cost separated from simulation. Fixture digest
profiling excludes file I/O; real recording includes the append. Measurement
UTC bounds allow matching GPU/load telemetry. Evidence and final validation
receipts for this increment live under `build/swat/review/occupied-slice`.
This increment does not complete the full vertical-slice goal.

The CPU profile exposed repeated acoustic routing for inaudible events,
full-world doorway scans, and unnecessary static-object rotation calculations.
Direct-distance range/arrival bounds now skip those queries without consuming
events; the cache keeps doorway indices in the original order. Exact exhaustive
hearing checks cover 720 paths, 288 next-float boundary cases and an approaching
listener. Existing motel/door journals reproduce every tick on both optimized
platforms, and all 6,575 controlled occupied-entry digests remain unchanged.

Native GTX 1060 3 GB measurements for the occupied-entry and sound pass were
taken before the hinge import. They use 600 measured ticks after 30 warm-up ticks,
67 finite-ammo shots, the real solo-journal digest and matched checksum cc0afddf.
Two headless baseline runs average 7.505/7.580 ms simulation time; optimized runs
average 6.121/5.506 ms, about 23% less across each pair's mean. These headless
runs exclude rendering and audio. Their p95 simulation times remain approximately
20–24 ms: damage spikes remain. Full-render HRTF runs average 29.910/39.825 ms
before and 23.265/46.948 ms after, including a 3,167 ms outlier. They establish
no repeatable FPS improvement. Rendering/submission and stalls still need work.
The four-case baseline and before/after logs retain UTC measurement bounds,
GPU telemetry and process snapshots. The desktop was idle, with other resident
apps still occupying GPU memory. These uncapped hidden-window runs advance one
simulation tick per rendered frame; digest-only profiling excludes file I/O.
Performance testing is now held at Felix's request while he uses the machine.
Do not collect further performance measurements without a new idle window.


Door pivot/source follow-up (2026-10-15): the three leaf-owned decorative
barrels remain the damage locations; last-hinge constraints use the logical
opening axis 18 mm toward the leaf edge. Damage probes and physics anchors are
now distinct, including snapshot validation. Ordinary opening, body/query
orientation, imported skins/backing, attachments and restored bodies share the
physics library's deterministic rotation calculation. The leaf centre and
swept obstruction bounds derive from that same rotation. Achieved-pose heading
uses the library's deterministic inverse trig instead of platform libc results.
Protocol/replay v25 requires fresh matched peers and recordings/saves.

The artist's source confirmation also establishes that the fixed hinge halves,
pins and failure remnants do not exist in the accepted model. Felix approved
and the engine sent a [focused hardware/remnant request](art_handoffs/motel-door-hinges/request.txt)
in the existing Slack thread, preserving the accepted slab/UV/maps and all three
source heights. That hardware was subsequently delivered and accepted in the
16 October seated assembly increment above.

Linux and native Windows checks cover the independent source anchors, 48
rotated/scaled body/query/restoration comparisons and nine last-hinge opening
cases. The ordinary finite-charge/eleven-shot door sequence preserves 1,314
exact replay ticks, in-flight save/resume and matching replay in both directions
between the two platforms. Live/late replicas, physical crossing, source viewer
attachment and malformed-state rejection pass. Twelve grazing views per platform
have zero missing solid-door pixels; hanging/fallen silhouettes agree with
125,090 physical ray samples per platform outside the existing 4 mm raster
boundary. Seven support-shadow and 56 general cached/full comparisons pass.
Both platforms also pass all four complete motel approaches with exact replay,
mid-run save/resume, fresh replicas and truthful debriefs, plus generated maps,
movement/navigation, tactical/device/sound, wall support, contact and UDP checks.
Evidence is under `build/swat/review/hinge-axis`. The normal players, servers
and replay/performance tools are rebuilt; no stable-load performance or full
vertical-slice acceptance is claimed by this increment.

Door attachment failure (2026-10-14): ordinary impacts can remove three
independent hinge attachments at the frozen motel source's hinge locations.
The retained leaf keeps its original owner/body and clipped surface maps.
Two hinges, or one hinge plus a captured latch, retain ordinary swept door
motion. A last hinge or latch becomes a spherical joint; losing that final
attachment frees the leaf to fall, collide with the actual scene and sleep.
The latch remains separate, so an off-latch charge never commands an opening.
Thresholds and a small tipping release are authored game behavior, rather than
a measured hardware strength or stress model.

Full physical orientation now drives the leaf, viewer, mounted devices,
ballistic/visibility queries, shadows and sound bounds. Doorway sound paths
retain the original opening height, and room reverberation accounts for leaf
tilt/displacement. Navigation evaluates
moving leaves per route instead of rebuilding floor cells every frame. Motion
storage is sparse, and each affected world adds one unshaped joint reference.
Falling panels exposed a contact-buffer limit in older dense motel meshes;
additional subsets now preserve every cooked runtime triangle and flag while
bounding each contact mesh to 240 triangles. Full query meshes remain intact.
Protocol v22 carries attachment failure, pose, velocities and sleep state,
validates them before mutation and preserves canonical map binding for late joins.
Matching players/servers and fresh recordings/saves are required.

Linux and native Windows checks cover lab/motel hinge failure, remaining latch
support, physical settling, carried devices, achieved-pose rays, live/late
replicas and atomic malformed-state rejection. An ordinary-input recording
approaches the lab door, mounts and remotely detonates a finite latch charge,
fires at the hinges, crosses the fallen leaf and preserves exact every-tick
replay plus an in-flight save/resume. Front/back hanging/fallen silhouettes
agree with 125,090 physical ray samples per platform outside a 4 mm raster
boundary; the independently measured viewer attachment follows the leaf.
Seven new cached/full shadow comparisons pass, meshes remain cached during
motion, and the settled late replica is pixel-identical. Existing 56 shadow
cases, material samples, contact coverage, live UDP, protocol, tactical,
movement and all four complete motel routes also pass on both platforms.
Logs, journals and captures are under `build/swat/review/door-support`.
This first support increment covers door leaves; connected wall-sheet support,
wall rubble, structural stress and full slice acceptance remain open. No new
performance conclusion is drawn under the concurrent machine workload.

Continuous damage and door follow-up (2026-10-13): the remaining six-impact
lifetime limit is replaced by thirty local damage patches. Later board/timber
hits grow nearby openings while retaining bounded state: 32 profiles including
two tool slots and at most 256 convex sections per owner. Adjacent retained
polygons join only when their convex union does not fill an opening. Protocol
v21 carries growing profiles, rejects restored cover atomically, and supports
the maximum 2,048-owner damage packet within its 2 MiB buffer.

Door charges now cut at the mount without commanding the leaf to open. Only a
cut reaching the hardware releases the latch; an off-latch blast leaves a
locked door locked. Explicit interaction or a ram opens the surviving leaf.
Closed leaves with broken latches can receive another charge. Door scorch is
evaluated per pixel in local leaf coordinates, eliminating broad diagonal
colour interpolation without adding surface subdivisions or texture maps.

Both platforms pass 360 impact attempts per door/board/frame sample, with
245/225/167 actual geometry changes, physical ray agreement and exact live/late
replicas. An additional Linux stress run covers 1,200 attempts per sample,
including region edges. Front/back art silhouettes agree with 238,416 physical
ray samples per platform, excluding a 4 mm raster boundary and cosmetic chips.
Unchanged painted faces preserve their UVs, replicas match pixel for pixel,
and moving damaged hinges reuse their meshes. Each platform passes 56 regional
shadow comparisons against full redraw, including growth at unchanged cut count.
All four mission routes retain exact replay, save/resume and fresh replicas;
live UDP, protocol bounds, tactical, house/ram and movement checks also pass.
Linux and Windows players/servers are rebuilt. Logs and native captures are
under `build/swat/review/continuous-damage`. No timing conclusion is drawn under
the concurrent machine workload. The structural prototype limitations below
still apply.

Interaction and shot-refresh pass (2026-10-13): charges now attach to the actual
aimed contact, preserve that opening height, follow moving leaves, and still
detonate after repeated bullet damage. Neighboring soft walls, timber, glazing
and props receive cover-tested secondary damage. Each neighboring wall shares
one damage location across its hidden divisions, avoiding a grid of small holes.
The lab's large masonry finish now has invisible local damage divisions.

Cut subtraction only partitions intersecting material. Eight-sided impact
profiles, triangle/polygon bounds checks and removal of unnecessary door soot
subdivisions cut the third decorated-door hit's polygon clipping work from
2,183,440 attempts to 9,820. Eight profiles allow six ordinary impacts while
reserving two tool cuts; snapshots use protocol v20. Bullet holes can refresh
only their affected shadow regions, using scissored writes and the original
projection. Nonlocal geometry changes and moving actor composition retain the
full refresh path. Forty-eight production shadow comparisons against full
redraw pass on each platform.

Six original CC0 timber, gypsum and clay fragments add sparse edge attachments
and settled chips. Source metre geometry and thickness are retained; sampled
vertex colours replace their texture maps. The 79,024-byte geometry set adds
no runtime textures, physics bodies or per-piece draw calls, with at most twelve
attached and twelve settled pieces. The cavity remains mostly empty. Both
platforms preserve 5,561 painted-face samples outside these small attachments,
pixel-identical replicas, unchanged authority and cached meshes during hinge
motion. The native gallery demonstrates the far-wall edge blast affecting the
middle wall and a freely placed charge after three door shots.

Full mission QA also found an escort snag on a low parking-stop rim. Navigation
can now take the next checked edge off a low support, with the existing body,
door and person sweeps. The movement course explicitly retains its original
low breach height. Evidence, source receipts, captures and timing CSVs are under
`build/swat/review/interaction-polish`; this remains the bounded material
prototype described below, with connected sheet support and loose structural
rubble still open.
All four ordinary-input mission routes pass on Linux and Windows with exact
replay, save/resume and fresh replicas. Both platforms also pass live UDP,
charge/geometry checks, the low-support escort regression and the movement
curriculum. Players and servers are rebuilt on both platforms.

Material destruction prototype (2026-10-12): `./swat play --mission breach-lab`
provides a retained hinged door, layered timber/board/insulation partition and
staggered masonry sample. Compact door charges and larger wall breachers have
separate finite stock. Latch failure leaves the original leaf and maps with a
local notch/scorch; ordinary board/timber impacts make small physical holes.
Wall openings preserve every outside section, including several sides of an
enclosed hole, on the existing physics body/owner. Original imported art stays
frozen; surviving painted faces retain continuous UVs and no obsolete trim.

Sparse affected-object data is bounded to four profiles and 128 convex sections;
small impacts reserve a profile for the wall tool. Intact objects allocate no
cut data, and tiny holes skip navigation resampling. Snapshot version 19 carries
monotonic profiles and latch state, prepares compound cuts before applying them,
and rejects malformed or growing updates. Fresh recordings/saves are required.
Destruction counters count removed owners rather than every changed fragment.
Route QA also exposed and fixed force evaluation against a post-impact surrender
state: a lawful first Impact hit is now distinguished from later force against
an already surrendered person.

Independent geometry checks cover enclosed-hole retention, repeated cuts,
material thickness, rotated rays/CCD, controller passage, damaged hinge motion,
live/late replicas and atomic rejection. Graphics checks preserve 5,778 painted
face samples, compare replicas pixel for pixel, and confirm hinge movement
creates no new door meshes. The actual motel shadow test passes 32 comparisons
against full redraw on each platform; native wall face batching passes 56
pixel-identical lit/unlit reference comparisons through breaches and corner edits.
Compact captures/logs are under
`build/swat/review/destruction-samples`. All four ordinary-input motel completion
routes pass on Linux and Windows, including physical wall entry, every-tick
replay, save/resume, fresh replicas, debrief and contact-buffer checks. Live UDP,
tactical, navigation, pane/prop/contact and force/payload checks pass on both.
Normal players/servers and float Puffer are rebuilt; this increment makes no
new frame-time claim under competing machine load.
This remains a bounded material prototype:
connected sheet support, hinge detachment, structural stress and loose structural
rubble are still open, as are the full slice acceptance checks below.

Shadow reuse increment (2026-10-12): local architecture changes now invalidate
individual conservative lamp faces within affected rooms. Sunlight reuses settled
actor depth, and actor presentation fingerprints are computed once per preparation
for all sun/room faces. Door/destruction invalidation stays immediate; actor refresh
and the 0.22-second blend settling window retain their existing cadence. Native and
Linux GPU lighting QA passes 91 full-redraw image comparisons across all six faces,
seams, movement, partial fractures/corner edits, removal, rotation and light relocation
(35 updated static faces versus 78 whole-room faces). Thirteen sun comparisons cover
settling/reuse, immediate doors/destruction, actor movement/removal/restoration, changed
sun and replay rewind; all sun comparisons have zero mismatches. Existing multiroom,
door, actor-face, lamp-coverage/contact/material/exposure/lifecycle checks pass.
The production shadow preparation is shared with the real-motel graphics check:
32 more full-redraw comparisons pass on both platforms through doors, C4, prop damage
and full skin/procedural caster movement/removal/restoration, with immutable authority.
Its native run updates 49 static faces versus 60 faces for complete changed rooms;
the 288-face unconditional reference is a correctness control, not an old runtime
performance baseline. Original meshes/materials and shadow map sizes remain intact.

Two sequential native 600-frame HRTF breach windows verify every skipped/warm/measured
input against the existing native scenario recordings, with 1,337 objects/nine actors.
Exterior tick 911 destroys eight owners and updates three static lamp faces instead
of six. Interroom tick 5394 destroys 25 owners and updates six faces instead of twelve.
Each window includes exactly one actual detonation input. Exterior total mean/p95/max
was 17.402/45.295/355.524 ms; interroom was 11.153/19.443/65.773 ms. Static-breach shadow
passes were 28.442/47.906 ms. Exterior world submission peaked at 236.604 ms and moving
shadows still have large spikes. This agent's build/test jobs had ended before timing,
but other activity, including possible League load, was uncontrolled; these figures
do not establish a speedup or a stable frame budget. Next engine work should target
moving caster submission and scene work with conservative bounds and image proof.
Normal native/Linux players and float Puffer are rebuilt. Compact evidence is under
`build/swat/review/shadow-reuse`; full performance/behavior acceptance stays open.

Latest prop increment (2026-10-12): the source-full 8,380-triangle hose hanger and
1,408-triangle capped tap are installed on the reception-side service wall.
Original triangles/attributes/images and metre frames pass independent preservation
and byte-identical regeneration checks. Four rubber/metal damage owners retain
open geometry and 46 bounded contacts with source flags; fixed wall dependencies,
damage/C4/removal, live/late replicas and old cabinet-prefix maps pass on both
platforms. All four full ordinary-input completion/replay/save routes also pass.
Native graphics passes 622,384 source/fallback ray/raster samples, original maps,
shared textures, lighting and removal. Generic motel/storefront PBR now honors
authored normal-map strength. Source gaps, disconnected hose and capped tap remain;
plumbing, deformable hose and certified mounts are outside this increment.
Source/limits/regeneration: `art_handoffs/motel-wall-service/ENGINE_IMPORT.md`.
The full slice acceptance checks below remain open.

Screwdriver increment (2026-10-12): the unchanged original 2,560-triangle MAIN
model now rests on the maintenance trolley's actual top-liner triangle, retaining
the artist's explicitly editorial 250 mm scale, normals/UVs, original 1K colour
and 512px packed scalar maps. No normal/AO or LOD is introduced. Native C physics
regeneration is byte-identical; independent image QA checks all 262,144 packed
roughness/metallic pixels against the source. Eleven contact subsets retain every
source triangle/edge flag. Linux/native contact, 595 silhouette hits/1,324 openings,
finite partial/removal damage, trolley dependency loss, live/late replicas, older
hose/tap maps and malformed-state checks pass. Expanded contact QA preserves
80,102 original triangles/flags and 84,000 ray/exit matches on both platforms,
repeated after encoded-map reconstruction. Native source/fallback rendering passes
157,428 stable comparisons, original bounds/PBR, lighting, removal and texture
lifetime; the trolley view was inspected. All four full ordinary-input scenario
routes pass on both platforms with every-tick replay, save/resume, replicas,
truthful debrief and no contact overflow. Normal Windows/Linux players/servers
and float Puffer are rebuilt. Source/editable/license data stays in repo, with
the original 18-tool archive retained once for future reuse. This is a static
mixed-assembly prop with explicit game health/support; handheld use, certified
balance/material strengths and separate handle/shaft destruction are not claimed.
Receipt: `art_handoffs/motel-screwdriver/ENGINE_IMPORT.md`; compact evidence:
`build/swat/review/screwdriver`. Full performance and behavior acceptance remain open.

Recorded breach profiling (2026-10-12): the normal diagnostic now accepts
`--replay FILE --seek TICKS`, replays ordinary inputs with every-tick digest
verification and measures actual player C4 openings. It rejects simulation
overrides and incomplete windows; read/digest overhead is measured separately
and excluded from simulation/total. Two sequential native HRTF runs with 1,336
objects/nine actors measured 600 frames after this agent's build/test jobs ended.
Exterior ticks 832–1431 include one detonation and eight destroyed owners; total
mean/p95/max was 18.376/32.844/82.773 ms. Interroom ticks 5312–5911 include one
detonation and 25 destroyed owners; total was 17.239/27.699/100.167 ms. Every
skipped/warm/measured tick matched the native completion recording. At detonation,
shadow time was 42.935/39.313 ms and refreshed one/two static lamp rooms plus sun.
The largest shadow passes (74.644/73.487 ms) preceded detonation with no static
rebuild; world submission also remains costly. This localizes further work to
moving shadows, scene submission and breach refresh rather than claiming a
stable frame budget. Other machine activity was uncontrolled; these are diagnostics,
not a paired speedup claim or full performance acceptance. Compact evidence is
under `build/swat/review/wall-service`.
Felix subsequently confirmed League of Legends was running during these timings.
Treat them as competing-load diagnostics, not a reliable performance baseline;
the deterministic breach/replay checks still pass independently of timing.

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

The indoor shadow trace identified dynamic room composition as the expensive
stage in the controlled service firing run: 900 room refreshes, no static
sun/room refreshes, and a 167.270 ms maximum shadow pass. Native input inspection
confirmed stationary NPC poses and real squad motion, approximately 14 mm per
tick in the inspected interval. The renderer now composites hinged leaves over
cached architecture and restores only lamp faces whose door/actor signatures
change, using the same conservative bounds as submission. Eight door rotation/
room-crossing/removal states and seven actor direction/removal states match full
redraws with zero samples outside a three-unit RGB-sum tolerance. The actor cases
refresh two faces per crossing and one on removal, while static depths remain
cached. Camera-independent rooms, partial-fracture invalidation, all six lamp
directions/seam, contact occlusion and GPU lifecycle checks still pass.
Exact unchanged RGBA32F joint palettes also reuse their current upload. Native
CPU/GPU parity, eight stance/yaw states with byte-identical repeated captures,
movement/blend/ADS/reload and immutable-authority checks pass. No matrices or
influence sets are reduced. Normal Linux and Windows players are rebuilt, and
the ordinary `./swat` launch/capture passes without another freshness rebuild.
Normal float Puffer is rebuilt with the same renderer changes for eval.
Two final native service runs reduce measured lamp composites from 5,400 to
3,750 faces for the same 900 dynamic rooms and 67 shots. Total mean/p95/max
times are 10.805/20.320/32.991 and 11.057/20.700/41.108 ms; shadow maxima are
25.303 and 24.644 ms. A guest-room run measures 16.412/27.279/45.671 ms total
and a 25.542 ms shadow maximum, with 3,150 faces across 750 dynamic room updates.
All runs use HRTF audio, 1,332 objects and nine actors. This batch destroys no
objects; active breach/destruction performance needs its own measurement.
This agent's build/test jobs had ended before timing; other machine activity
was not controlled, so these observations do not establish a guaranteed speedup
or full slice acceptance. Guest world submission still averages 8.222 ms, and
changing/alternating character palettes still upload frequently. Evidence and
the inspected service capture are in `build/swat/review/shadow-refresh`.

The original service cabinet is installed above the rear washer pair, retaining
the two source meshes, static open-door pose, metre dimensions and mixed PBR
with three 1K maps. Native C import regeneration is byte-identical. All 21,272
source triangles survive the full query and 97 source-flag contact subsets.
Four actual modeled rear-rim vertices resolve to canonical wall-skin supports;
source fasteners, material gauge and electrical behavior are not inferred.
One static metal-cover damage owner uses an explicit 1.2 mm game health section.
Linux/native rim rays, washer clearance, partial/removal damage, support/C4 loss,
replicas, malformed state and old bag-prefix checks pass. Expanded contact QA
preserves 67,754 triangles/flags on 30 tested assets and 72,000 ray/exit matches
per platform, repeated after map reconstruction. Native source/fallback graphics
passes 155,722 stable ray/raster comparisons with original bounds/maps, lighting,
removal and texture lifetime checks; the room render was inspected. Both
platforms' four ordinary-input completion routes pass with every-tick replay,
mid-run save/resume, fresh replicas, truthful debrief and no contact overflow.
Normal Windows/Linux players/servers and float Puffer are rebuilt. The existing
performance tool can select any room and initial yaw for targeted indoor
shooting. Compact evidence is under `build/swat/review/service-cabinet`; source,
runtime, licenses and reproducible import remain in the normal repo. Hose/tap
integration, cabinet hinge/electrical behavior, further character/material/
destruction polish and full slice performance/behavior acceptance remain open.
Two sequential 600-frame native HRTF firing runs after this agent's build/test
jobs ended each fired 67 shots with 1,332 objects and nine actors. Guest-room
total time was 30.487 ms mean / 52.692 ms p95 / 101.835 ms max; service-room total
was 23.320 / 33.676 / 278.587 ms. The service maximum included a 255.979 ms shadow
pass, while simulation topped at 11.411 ms. Texture maps add exactly three unique
1K images and 12 MiB of API-format full-mip data, totaling 368.92 MiB; driver
allocation is not measured. Other machine activity was not controlled, and this
is not a paired old/new comparison or slice performance acceptance. Large
indoor shadow spikes motivated the shadow-cache iteration recorded above.

The interrupted-pose audit reproduced a 0.79951 m rifle-stock displacement on
the first controlled crouch-transition frame: local skeletal blending moved
the prop after its achieved gameplay alignment. Active blends now restore the
rifle frame and carry magazine frames with it, then resolve arms from the
blended shoulders to the sampled grip/reload targets with existing length/reach
limits. Native checks pass stock/all-axis alignment and authored wrist targets
within actual reach deficit through reversals and 112 interrupted
locomotion/aim/ADS/low-ready frames, plus the existing 336 steady movement poses,
rigid carriers, reload boundaries, aperture/post alignment and immutable
authority. Both ordinary player builds are refreshed. Source clips and shared
simulation remain unchanged. Full helmet mesh clearance and perfect grip
contact during unreachable intermediate poses remain visual acceptance work;
this is a measured prop-frame correction, not completion of character polish.
Compact evidence is under `build/swat/review/transition-attachments`.

The maintenance-bag handoff is now installed beside the rear service trolley at
source metre scale. Original 4,482-triangle geometry, UVs, one glossy nonmetallic
material and three 1K maps survive. Nineteen bounded contact subsets preserve
source triangle edge flags; full-mesh bullet queries, minimum-height source
floor contact, finite soft-cover damage and support loss replicate together.
The appended owner leaves all existing IDs intact; seating-only canonical maps
retain their mesh reconstruction. Runtime strength uses the existing soft-cover
game approximation; unknown contents and torn-polymer dynamics remain outside
this increment. CC0 source/artist packet and reproducible C import are retained.
Linux/native physics and 156,054 native art/fallback ray/raster checks pass. All
four completion routes pass on both platforms with every-tick replay, saved
continuation, fresh replicas and no contact-buffer overflow. Normal players,
servers and Puffer are rebuilt. Three unique maps add 12 MiB of API-format mip
data; total shared texture data is 356.92 MiB, excluding driver overhead. The
600-frame native indoor-firing diagnostic measured 19.559 ms mean total and
38.141 ms p95 with a 74.827 ms maximum. It overlapped native scenario checks;
this does not establish stable-load performance acceptance. Evidence is under
`build/swat/review/maintenance-bag`. Reuse hose/tap integration,
squad visual clearance, further materials/destruction and stable-load playtests
remain part of the full slice goal.

The physical breach-edge increment chips surviving shared boundaries next to
material removed by the current charge. Deterministic cuts stay inside their
original convex fragment, cap adjoining-edge movement at 7 cm for brick and
12 cm for board (also capped at 20% of each adjacent edge), and proportionally
reduce the surviving health. Queries, Box3D hulls, source-face clipping and
authored exposed-edge strips use the resulting corners. Floors, doors, fence
rules, material penetration/resistance and remote wall geometry are unchanged.
Four-corner fragments and disappearing chunks remain approximations; this does
not complete fracture realism or introduce loose structural rubble.

The next rendering increment combines the motel's clipped opaque wall finishes
by assembly and original material. The camera requests 20 wall models instead
of 536 fragments; positions, normals, UVs, textures, structural cores, exposed
edges and shadow/contact geometry retain their existing values. Occupancy,
partial corner changes and source replacement invalidate the affected cache.
Twenty initial builds reuse 1.72 MiB of combined vertex attributes on CPU and
GPU each, with no extra textures. Native Windows checks pass 56 pixel-identical
reference comparisons, unchanged-view cache reuse, the 42 existing core pairs
and the live-replica breach graphics. Later paired indoor firing runs measured
9.10 versus 12.09 ms mean world-geometry time and 17.85 versus 20.23 ms total;
machine contention and large outliers limit these results. Stable-load indoor
shooting acceptance remains outstanding. This changes presentation only, not
physics, training, authority, wire versions or the ordinary player workflow.

Protocol/replay version 18 carries mutable fragment corners in snapshots. Full
validation and hull preparation precede mutation; nonfinite, nonconvex,
out-of-bounds and growing shapes fail atomically. Existing pre-damage clients,
late maps and reconstructed replay state agree. Older wire versions/journals
are retained but cannot load into this build; use fresh recordings and matching
peers. RL observation/action shapes and user config remain unchanged.

Linux/native Windows geometry checks pass for masonry and board at two yaws:
eight surviving masonry pieces or twelve board pieces shrink in the test
charges; removed-area rays and capsule casts, unchanged remote cover, identical
reconstructed charge geometry, live/late replicas, malformed/growing update
rejection and packet capacity are checked. The motel snapshot is 61,248 bytes.
Native graphics show 48,737 changed pixels after the charge and pixel-identical
replicated damage; 3,166 marker pixels prove exposed source edges still draw.
All four Linux completion plans pass ordinary inputs, every-tick replay,
mid-run save/resume and replica state. The 14,398-tick hostile inter-room route
also passes in native Windows. QA staging accepts a reachable position beside
an occupied grid cell while independently checking tool reach and actual wall
crossing; Room 104's capsule plan goes around its daybed and desk. No gameplay
success conditions or scenario triggers were added.

Normal Linux/Windows players, both servers and Puffer rebuilt; `./swat` native
capture passed. The 240-frame indoor HRTF automatic-fire diagnostic measured
22.950 ms mean / 38.923 ms p95 / 79.364 ms maximum total, with 13.457 ms mean
world geometry and 3.404 ms mean shadows. CPU checks/builds were running;
this is not a controlled speedup/regression comparison or final frame budget.
Geometry tests, native captures and logs live in
`build/swat/review/breach-geometry`. Detailed rubble/fracture and remaining
rendering spikes still need work.

The breach-feedback increment replaces per-removed-piece flying cube submissions
with the existing single event batch: charge pairs produce up to 24 dust/chip
particles for 1.15 seconds, masonry/wood/metal chips tumble as small solid shapes,
and glass keeps thin shards. Material tone variation and ballistic chip fall
remain cosmetic. The hard limits are still 12 selected events, 96 particles and
1,404 vertices per view; sampling never changes authority, RNG or collision.
The production bank now has 88 entries in 96 slots. Nine unchanged Kenney CC0
ceramic/thin-metal/soft contacts add 67,619 bytes. Exact material entries precede
documented foley families and wildcard clips; all 16 impact/break materials have
three recorded choices. A same-contact break replaces redundant player hit
playback, while shared hearing/events remain intact. Blast transients and these
foley families are not measured explosive or ballistic source recordings.

Linux/native Windows sampler and production-bank checks pass, including finite
directional PCM, cleanup, complete material coverage, expiry, event/snapshot
equivalence and immutable authority. Native graphics cover opaque occlusion,
depth-state restoration and an ordinary held-C4 mount/retreat/detonate scenario.
Maximum-batch CPU submission measured 0.121 ms; GPU completion is excluded.
Normal Linux/Windows players and Puffer rebuilt. Native indoor automatic firing
with HRTF and the 88-entry bank (240 frames, 30 shots, 1,330 objects, nine actors)
measured 18.279 ms mean total, 28.732 ms p95, 35.326 ms maximum, and 0.329 ms mean
audio. This short local diagnostic is not a controlled speedup or final slice
acceptance. Logs, compact before/burst/after captures and one listening proof are
in `build/swat/review/breach-feedback`. Production blast/door/voice sources and
further physical fracture detail remain work for subsequent increments.

The seating increment adds source-scale CC0 furniture to Rooms 103 and 104:
Modern Arm Chair 01 with separate wood/cushion owners, and the original mixed
Vintage Day Bed primitive. Source geometry, materials, UVs and hierarchy remain
intact; separate 1K runtime maps reduce the shared nine-texture mip data from
144 to 36 MiB in the native API format. Original 2K GLBs, compact provenance,
independent C/Node checks and two selected native room captures are retained in
the repo. Driver VRAM allocation is not measured. Low fixture seats/bases are
now blocked in navigation without changing stair or player controller rules;
fixture removal restores access. Coarse damage sections are game approximations,
and the daybed still has one mixed-primitive material response.

Seating/support/snapshot checks, exact collision contacts, native original/1K
ray/raster matches and shared texture lifetime checks pass. Full front-door
isolation/combat, exterior-charge and inter-room-charge runs pass on Linux and
Windows, including every-tick replay, saved continuation and fresh replicas.
The repeatable QA plan uses ordinary escort/hold tools to clear arrested suspects
before recovering weapons from reachable floor positions; no production scenario
sequence or success trigger is added. Normal Windows/Linux players, server and
Puffer are current, and an ordinary native `./swat` motel capture passes.
After completion checks finished, the 240-frame native indoor-fire diagnostic
(30 shots, 1,330 objects, nine actors) measured 18.660 ms mean total, 28.444 ms
p95 and 2.570 ms mean shadows. One 106.040 ms frame remains; this is a local
diagnostic, not final performance acceptance or a controlled texture-speedup
claim. Concurrent-test and later runs are retained separately in
`build/swat/review/seating/indoor-fire{,-idle}.log`.

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
matched rays each, including replicas. Legacy map prefixes remain supported
when encoded with the current wire version; older wire-version journals do not.
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
are rejected. That increment used wire version 17 (the current version is 18
as documented above); peers need matching builds for
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
- Connected material in the middle timber lab wall and three motel partitions
  follows the surviving path to fixed timber anchors. Disconnected sheets/stud
  sections become bounded physical debris with original finishes, achieved
  poses, dynamic clearance, sound/shadows and live/late/save/replay replication
  in protocol v23. Corner contact and insulation do not carry wall support.
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
silhouettes and physical rubble have bounded prototypes; building-scale structural
support remains unfinished; see [ENVIRONMENT_ART.md](ENVIRONMENT_ART.md). The
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
