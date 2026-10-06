# Animation asset contract and integration proposal

Target: `swat/gold-element`, `ocean/swat`. Verified baseline:
`811bc1ed9ed0f33767589696907227bd374d45c0` (2026-10-04, 12:16 UTC inspection).
This replaces the earlier `5.0/ocean/shenaniguns3d` integration assumption.
Facts below describe that published commit; items marked proposed require
coordination with ongoing engine work. This is a presentation contract, separate
from [the versioned RL contract](CONTRACT.md). Reported local save/restore work
is not in this verified snapshot and is not covered here.

## Scope and verified implementation

This contribution adds only this document, an asset-agnostic read-only GLB probe,
and synthetic tests. It changes no gameplay, controller, renderer, dependencies,
weapon tuning, wire format, or assets.

The game uses C, Raylib 5.5, the pinned Box3D fork, and ENet 1.3.18.
`SwatSim` is authoritative at 60 Hz with four physics substeps. Humans, remote
players, scripted NPCs and future policies share its rules. The dedicated server
has no Raylib/window/GPU dependency. The current player draws primitive actors
and a primitive first-person weapon; no skeletal loader or ozz adapter is wired
into its build or renderer.

Pinned sources:

- [Architecture and authority](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/ARCHITECTURE.md)
- [Controller](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/controller.c), [body](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/body.c), [axes](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/swat_math.h)
- [Renderer](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/render.c), [build split](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/CMakeLists.txt)
- [Canonical achieved pose](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/pose.c), [pose API](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/pose.h)
- [Weapon state/timing](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/weapons.c), [authoritative muzzle and shots](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/sim.c)
- [Replica codec](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/protocol.c)
- [Environment builders](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/mission.c), [world and doors](https://github.com/FelixAllistar/PufferLib/blob/811bc1ed9ed0f33767589696907227bd374d45c0/ocean/swat/world.c)

Proposed integration seam: a presentation-only adapter called from
`swat_view_draw(SwatView*, const SwatSim*, ...)`. Keep dependencies out of
`swat_core` and the headless server. The existing C++ build support is needed
by Box3D; it is not evidence that ozz is implemented. Agree the adapter and active
files with the engine owner before runtime edits. The engine owner is implementing
the character adapter; character authoring supplies exports and validation. The
environment adapter has separate ownership. This document does not implement or
reassign any of those adapters.

## Asset consumer requirements

The first integration candidate is the self-contained Standing Empty F authoring
checkpoint: a 6-second, 24 FPS study with editable geometry and baked actions.
It is an import/skinning candidate, not an approved final animation bank or a
runtime-ready GLB. Its checkpoint records remaining stock/grip/contact limitations
and pending timing adaptation. The inspected package contains no exported GLB.
Exact source regeneration does not establish export or engine parity.

A separate, earlier private full-body bank had 44 named clips, one skin with
74 joints, embedded textures, and up to seven nonzero influences per vertex.
Both character primitives use `JOINTS_0/WEIGHTS_0` and
`JOINTS_1/WEIGHTS_1`. Preserve all sets. A four-influence consumer must reject
the bank rather than drop weights and renormalize a subset. Weight reduction
would be a separately reviewed derivative. These historical figures are not measurements of a new F export. Recompute
its clip/joint counts, maximum influences, hierarchy and material requirements;
do not impose 44 clips or 74 joints on the candidate.

Raylib 5.5's stock skinning path has four influences per vertex. Its GLB
animation loader resamples at 17 ms and stores one interpolation mode per bone.
The historical bank had 15 clip/node combinations mixing motion and visibility
interpolation. Stock loading is not a verified consumer. Preserve each channel's
interpolation and timestamps in a separately tested consumer. These are source
findings, not a test of the project's actual Raylib binary.

Sources: [Raylib skinning](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c#L5218),
[animation sampling](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c#L5461-L5602).

Use exact clip names or stable IDs with an explicit name map; never index-only
mapping. Apply rest hierarchy, mesh-local basis, inverse binds, TRS channels,
all weights and normalized quaternions correctly. Record the new export's rigid
rifle attachment and every prop attachment explicitly; bake authoring constraints
into the delivered transforms. The historical bank used optional
`KHR_materials_specular`; decide material support from the actual new export.

### Proposed ozz/Raylib consumer, not implemented

A small C++ presentation module with an opaque C wrapper is a candidate.
Preserve the source GLB; use a custom mesh path that reads every joint/weight set,
maps glTF skin indices through node IDs to the imported ozz skeleton, and applies
the matching inverse binds in the agreed mesh basis. Do not assume the imported
skeleton has the same count as the skin or matching index/name order.

Start with CPU `SkinningJob` and dynamic Raylib position/normal buffers. Its
interface supports arbitrary influence counts and uses N indices with N-1
explicit weights, reconstructing the final weight. Adapt that layout explicitly,
handle normal transforms, and normalize output normals. Hidden zero-scale props
must skip singular normal-matrix inversion and drawing. Consider an eight-weight
GPU path only after parity and profiling justify it.

The official glTF converter handles LINEAR and STEP transform channels but does
not import custom user-property tracks. STEP is represented using adjacent
float-time keys, so exact binary ownership/visibility must remain independent.
Keep exact event/ownership data in a
separate sidecar, independent of compressed pose sampling. Pin an ozz revision,
conversion settings and error budget before building; no ozz conversion,
deformation or runtime parity has been tested by this PR.

Sources: [SkinningJob](https://github.com/guillaumeblanc/ozz-animation/blob/master/include/ozz/geometry/runtime/skinning_job.h),
[glTF converter](https://github.com/guillaumeblanc/ozz-animation/blob/master/src/animation/offline/gltf/gltf2ozz.cc).

## Coordinates, collision and root motion

Verified world units are meters with +Y up. Character yaw is radians; yaw zero
faces +X. Horizontal forward is `(cos(yaw), 0, sin(yaw))`; right is
`(-sin(yaw), 0, cos(yaw))`. Place the render root using
`swat_body_feet_position`, not the rigidbody center.
The replicated actor `position` is that body center; derive feet after applying
the replicated actual stance rather than treating the network position as soles.

Nominal standing/crouching heights are 1.8288/1.016 m. The feet box has horizontal
half-width 0.2032 m; the upper capsule radius is about 0.2873 m. The misleadingly
named `bodyRadius = 0.4064` is not the actual capsule radius. Crouch is immediate;
standing requires clearance, including the achieved leaned capsule. Lean is
swept against geometry, up to 0.42 m; `upperOffset` is world-space. The eye is
based on actual stance height minus 0.2032 m, lowers immediately, and eases upward.
Do not move the camera or shrink colliders to hide art-fitting errors.

Base movement targets are slow 1.2, aim-walk 1.7, walk 2.8, sprint 4.6 and crouch
1.25 m/s, multiplied by mobility. Use actual velocity, grounded, crouched,
sprinting and achieved lean for presentation, rather than requested inputs.
Current input/controller state has no prone mode: prone/crawl clips remain
asset-preview-only until gameplay and protocol support are separately agreed.

Require the new GLB export to document Y-up meter scale and its scene-root
transform; the historical bank used an identity scene root.
Authoring-space Z-up matrices are not engine transforms. Measure asset forward,
sole origin, eye height, weapon sockets and model-to-capsule fit before freezing
the conversion. In particular, world-object yaw uses a different mapping:
`x' = cos(yaw)*x + sin(yaw)*z`,
`z' = -sin(yaw)*x + cos(yaw)*z`. Do not feed character heading into an
object transform without the required basis conversion.

Proposed ownership: the controller/physics alone move the world root and choose
actual stance. Keep visual hip/limb motion without double-applying root travel.
Historical crawl clips were authored in-place; verify the policy per export. Locomotion
phase/rate must later be matched to measured cycle speed and actual travel;
the bank is not yet matched to the controller.

## First-person, full-body and muzzle boundary

The current normal player camera follows `swat_controller_eye/view`, including
achieved lean and recoil. ADS blends toward 30 degrees for an optic and 45
degrees otherwise. The
controlled actor's world body is omitted; other actors use primitive full-body
drawing. Planning, sniper and inspection cameras have separate paths. Full-body
and FPS/viewmodel banks are distinct; confirm body visibility/head clipping and
viewmodel selection rather than treating either bank as interchangeable.

`swat_pose(controller, arsenal)` now supplies canonical world-space eye,
shoulder, left/right hand, sight and muzzle positions, plus view basis,
weapon pitch and reload fraction. Both primitive rendering and authority use
this helper. It derives geometry from achieved controller state and the active
weapon definition, including ready blend, ADS, recoil and weapon busy state.
Carbine barrel/sight-height are 0.56/0.055 m; sidearm 0.36/0.03 m. Use the helper
rather than duplicating these offsets or retaining the earlier fixed-muzzle rule.

Authority sphere-sweeps from the eye to this muzzle with radius 0.035 m. The eye
ray selects a target and accepted shots originate at the checked muzzle.
At ADS the sight lies on the aim ray when the weapon is in its normal ready
pose; ready/busy pitch intentionally changes the assembly. High/low ready and
sprinting affect presentation. The sim suppresses new fire input while
`abs(ready_blend) > 0.12`; existing buffered requests are still handled by the
weapon state machine. Shot permission remains authority-owned, and visual
completion must not grant it.

The current hand positions are procedural targets, not an authored contact
solution. Fit the export to the achieved pose through the engine-owned adapter;
keep ownership of the render root, weapon and each socket explicit so both an
animation and procedural pose do not transform the same object twice.
For first-person drawing, the renderer may evaluate the pose with its local
unacknowledged look offset. That displayed pose must not replace authority.

Pose evaluation precedes weapon stepping and recoil in the current shot path.
For accepted-shot effects use the shot's recorded origin/state where available;
a later pose sample may include changed recoil, ready or reload state. Body
fitting, IK constraints, final ADS and gear fit still need consumer validation.
Changing authoritative geometry or shot rules remains a separate engine decision.

## Reload timing and inventory mismatch

Verified game reloads are fixed-tick countdowns. GE carbine tactical/empty
durations are 120/156 ticks (2.0/2.6 s); P9 sidearm 90/120 ticks (1.5/2.0 s).
Equip durations are 24/18 ticks. Other weapon definitions have separate timings.
Reload input has a rising edge with a 12-tick request buffer; the engine owns
acceptance and timing. For duration D, the accepted seated reload starts in
`REMOVE`. The state machine uses integer thresholds:

- At elapsed `floor(D/4)`, REMOVE transfers the old magazine rounds to retained
  magazine storage or pooled reserve, sets magazine rounds to zero and seated
  false, then changes stage to `INSERT`
- At elapsed `floor(2*D/3)`, INSERT supplies the next magazine, sets seated true,
  then changes stage to `CHAMBER`
- At completion, an empty chamber takes one round if available; timers clear
  and stage returns to `IDLE`

Stage names describe the active phase before its commit. For the carbine the
three commit ticks are 30/80/120 tactical and 39/104/156 empty, relative to the
accepted start; empty commits occur at 0.65, 1.733333... and 2.6 seconds.
Do not round source animation frames into these game ticks independently.

Explicit cancellation or weapon switching clears countdown/stage but preserves
already committed magazine, reserve and chamber state. Thus `IDLE` may mean a
magazine is still absent, or seated without a chambered round. A subsequent
reload starts in INSERT if unseated; it is a new countdown, not a resumed source
clip fraction. Always inspect `magazine_seated` and `chambered`, not stage alone.
The chambered round may survive removal and can fire afterward while unseated.
Animation must not undo these facts. Edges during an active reload are ignored;
holding the input does not repeat it. Cancellation runs before buffer processing,
so a fresh reload edge or an eligible pending buffer may start a new reload in
the same tick as cancellation. A timer reset alone is not an action identity.

Optional retained-magazine mode now stores up to four round counts. Removal
appends the old count when space exists, otherwise returns its rounds to reserve;
insertion selects the fullest stored count and compacts the array. This is not
a persistent magazine-object or pouch-ID system. Pooled reserve remains a
supported mode. Do not assign lasting visual identity from an array index, infer
pouch capacity from the authored two-slot preview, or invent a gameplay drop.

Standing F is a 6-second study versus the 2.6-second carbine empty reload.
A uniform speedup would be about 2.31x and would not by itself align its hand and
prop transfers to the three game commits. Export exact source markers first,
then coordinate a phase-aware presentation timing/contact pass against the
existing authority. The historical 44-clip bank had different upright reload
lengths (6.375 s tactical, 5.625 s empty); do not reuse those as F's durations.
This PR does not change game timing, inventories or cancellation semantics.

Playback uses seconds from clip start, not source frame numbers. The historical
bank mixed 24/30 FPS authoring and some 60 Hz exports; F is a 24 FPS study.
Read the new export's timestamps rather than assuming any of those rates.
Float32 endpoints may differ by sub-microseconds; endpoint-inclusive video frame
counts are not durations.

For GLB visibility using `prop:Magazine*` scale channels, require binary uniform
scale with STEP interpolation. Never blend ownership scale. The export sidecar
must identify installed, hand-held, carrier and released representations and
source transfer times for this specific clip. Do not transplant historical
bank marker times into F. Render visibility must reconcile to the authoritative
seated/inventory/chamber state, including interrupted and restarted reloads.
Ready-pose defaults must not refill emptied visual carrier slots.

The old bank's baked empty-mag fall followed the asset root. The new export must
state and test its drop coordinate space explicitly. Proposed cosmetic release:
capture a world transform once, hide the character representation, and let an
independently owned world visual persist while the actor moves. A persistent or
collidable item needs an explicit gameplay contract. Existing authority retains
rounds; a visible dropped prop must not create ammo or an invented inventory
object. Never spawn it every render frame or replay a late snapshot as a new drop.

## Network and exact event ownership

Protocol v7 sends complete snapshots at 30 Hz from the 60 Hz authority. Actor
state includes pose/velocity/stance/lean, ADS/recoil, active weapon, ammunition,
reload remaining/duration/stage, magazine-seated and retained-magazine fields,
ready state/blend, sight/profile, equip remaining and last-shot tick. Pose is
reconstructed from that state; its six world-space targets are not serialized
as an independent animation skeleton. Replicas restore
colliders without advancing rules. Round epoch, revision and input acknowledgement
protect against old state. Unacknowledged look affects local camera/weapon
presentation; movement/fire/damage remain snapshot-driven. Full interpolation and
prediction/reconciliation are still future work at this baseline.
The last-shot tick is a state stamp, not a guaranteed per-shot event stream.

Reload stage is explicit and phase can be derived while the countdown is active,
but snapshots can skip
a transfer or arrive after completion. There is no authored animation-notify
stream, persistent magazine ID, or reload-sequence ID in this interface.
Do not infer gameplay commits by noticing that a sampled pose crossed a marker.
Recent sound events have IDs, but generic reload sounds cover several stage,
start and end changes; they are not a magazine-ownership event schema.

Proposed sidecar: asset hash, stable clip ID, exact source/rational marker times,
marker identity, prop/socket identity and ownership transition. A runtime adapter
also needs an agreed actor/round/action identity and time mapping. Use consistent
crossing intervals (for example `previous < event <= current`), explicit restart
initialization, deduplication and cancellation/late-join policy. Seeking a pose
must be pure; it must not grant ammo, replay effects, or duplicate dropped props.
Any new authoritative state or wire fields need the engine owner's versioning
and round-trip tests.

Gameplay remains authoritative for ammo/chamber, shot permission, reload
accept/commit/cancel and world-object persistence. Renderer event markers only
schedule presentation unless authority explicitly owns their effects. Inspect
receiver and bolt articulation in the new export; the historical bank kept them
static, and hand gestures alone do not establish functional hardware.
Future particles may share licensed assets/recipes and event sockets, while each
viewer creates its own instances and simulation keeps gameplay authority.

## Initial private export package

Deliver a standalone import/skinning fixture before any gameplay-timed bank.
The first F fixture keeps its original 6-second authoring timing and source
geometry. Add a separately named stationary Ready clip only if it is explicitly
derived and checked. Keep the authoring checkpoint immutable. The package should
contain the following, with schema/version recorded in its manifest:

- `character.glb`: self-contained GLB 2 with dense embedded accessors; mesh,
  skeleton, inverse binds, normals, UVs and used materials/textures. Bake
  constraints into TRS channels, preserve every nonzero skin influence and each
  channel's interpolation, and include exact transfer-boundary samples. Retain
  explicit prop nodes/attachments. Do not silently decimate weights, rename
  joints, combine physical magazine representations or apply root-motion removal
- `manifest.json`: fixture status and limitations, source and export SHA-256,
  Blender/exporter versions and reproducible export command/settings, file hashes,
  actual clip/skin/joint/primitive counts, maximum active influences, extensions,
  scale/axes/root transform, and stable clip IDs mapped to exported names.
  For each clip record exact start/end/duration, source FPS, loop policy and
  root-motion policy. An authoring frame rate is not a runtime sampling rate
- `bindings.json`: exported node IDs and names, parent hierarchy, skin-joint
  order, inverse-bind convention, model-to-feet transform and semantic binding
  candidates for eye, shoulder, both hands, sight, muzzle, rifle, installed/held/
  spare/released magazines and carrier slots. Socket offsets must declare local
  parent space and rotation convention. Record uncalibrated bindings as such;
  exported markers are candidate art sockets, not overrides of `swat_pose`
- `events.json`: exact source times, preferably rational ticks with a declared
  rate, for ownership and visibility changes; prop IDs, before/after owners,
  coordinate spaces and restart state. Distinguish guided extraction from free
  release, and record release transform/velocity when available. Include an
  explicitly pending mapping to game REMOVE/INSERT/CHAMBER commits. No invented
  mapping or uniform speedup is part of this initial fixture
- `validation.json` and a short README: the probe's complete output and scope,
  full-validator result if run, reimport comparison at keys/inter-keys and
  immediately around each transfer, rest/bind-pose checks, visibility counts,
  finite/normalized weights, root/feet bounds and material/texture availability.
  Include a normal-speed reference and known remaining contact/quality issues;
  distinguish source regeneration, export parity and engine playback results

Measure this export rather than copying the old bank's 44/74/seven-influence
statistics. The current preflight does not validate joint remaps, inverse binds,
semantic sockets, event sidecars, arbitrary animation output accessors or every
glTF reference/component constraint; those need separate export/consumer checks.
Reimport must preserve visibility jumps and all weights, and must not rely on
authoring constraints or files outside the delivered package.

In the engine, first compare an idle/Ready pose and the unretimed F reload against
the native authoring reference. Then test all game commit boundaries with phase
mapping, cancellation and inventory reconstruction. Skinning success alone does
not approve F's animation quality or its 2.6-second gameplay adaptation. Public
repository documentation may describe this package; source geometry, textures,
authoring manifests and private delivery links remain outside this PR.

## Environment-art alignment

The house is currently built from procedural boxes, with no environment mesh
loader. Framed walls run along local Z; thickness is local X, height local Y.
Their placement uses the world-object yaw mapping above. Typical Cedar House
walls are 2.7 m high with 1.2 m by 2.1 m door openings; these are current authored
dimensions, not a universal modular-kit requirement.

A framed-wall door leaf is 0.044 m thick, leaves 0.04 m each side of its opening,
starts 0.08 m above the wall origin, and pivots at its negative-local-Z edge.
Its open target is +pi/2 relative to `closed_yaw`, or 12 degrees when peeking;
a wedge forces the target closed. Motion remains at up to 2 rad/s. Authority updates its
center/yaw and static-body transform; it is not a free physical hinge. The swept
obstruction check currently stops for actors only. Render from the authoritative
door pose rather than running a second hinge simulation.

Wall skins, timber frames and supports are separate objects with material,
part, health and active state. Board faces can break while studs remain physical.
The future environment kit should map visuals to those object identities and
states, preserve openings/hinge placement, and hide destroyed pieces. A combined
mesh that continues covering removed geometry would disagree with collision and
visibility. World-object orientation also carries pitch about local Z, composed
as Y-yaw then Z-pitch. Door replicas include lock, breach, wedge, peek and trap
state. Let the environment adapter consume those fields; character animation
only follows agreed interaction targets. Coordinate any collision/door-builder
changes separately.

## Public-asset and provenance boundary

No raw Mixamo character/animation, weapon-kit mesh, textures, Blender file, or
exported GLB belongs in this public PR. Use user-controlled private storage and
an ignored local asset path. Each asset needs source/product identification,
license evidence, an authorized use/distribution decision and an exact content
hash. The repository's code license does not establish art redistribution rights.
This PR contains no private authoring manifests, paths or asset payloads.

## Acceptance gates

1. Deliver and validate the private candidate package below with the engine owner
2. Verify one idle and one empty reload in an isolated consumer first: all
   influences, joint remap, inverse binds, key/inter-key sampling, normal handling,
   exact visibility and pure seeking; then expand to all clips
3. Measure model-to-controller axes, feet, capsule, eye and muzzle alignment;
   test yaw/translation, lean, crouch clearance and FPS/full-body visibility
4. Align authored REMOVE/INSERT/CHAMBER presentation to committed authority,
   including retained/pooled modes and cancellation; prone needs simulation work
5. Test repeated tactical/empty cycles, interrupted transfers, zero/one spare,
   moving-root drops, restart, late snapshots/joins and replay without duplicates;
   test immediately before/at/after each integer commit, including unseated IDLE
6. Validate real-speed playback and locomotion phase at different render rates,
   then profile CPU skinning before choosing a GPU implementation

No export, runtime integration or ozz conversion is claimed. Native game/physics/network
tests were not run for this text/tool-only contribution. The historical private
bank passed this limited probe on 2026-10-03. This refresh reruns synthetic tests;
Standing F has not yet been exported or run through the probe here. Passing it
does not establish
deformation, materials, root motion, event authority or gameplay parity.

## Preflight usage

From the repository root, using Python's standard library only:

```sh
python -m unittest discover -s tests -p 'test_animation_glb_contract.py' -v
python scripts/check_animation_glb.py /path/to/local/character.glb --expected-animations 2
python scripts/check_animation_glb.py /path/to/local/character.glb --max-influences 4
```

The two-clip count is an example for one idle plus one reload, not an assertion
about the pending F export. Use the delivered manifest count and actual consumer
influence limit. A four-influence profile rejects a measured seven-influence
asset; report the new export's result rather than copying the historical audit.
The probe reads without modifying or uploading input. It checks dense embedded
buffer bounds for skin attributes, animation inputs and magazine-scale outputs;
weight sets/sums, nonzero influence counts, animation names/timestamps, and
magazine STEP/binary visibility. Mixed interpolation is grouped by target node
index with names for display. Other animation outputs/transforms are not
validated by this deliberately limited probe.

Exit 0 means those checks pass; 1 means a contract mismatch; 2 means
malformed/unsupported input or I/O error. Supply the actual consumer influence
limit explicitly. Sparse accessors and external buffers are unsupported; run a
full glTF validator separately. Tests construct synthetic bytes and need no
Blender, game dependencies, asset license or downloaded fixtures.
