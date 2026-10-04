# Animation asset contract and integration proposal

Target: `swat/gold-element`, `ocean/swat`. Verified baseline:
`8b0c74e5377d0afa48a91d524d7241024126f855` (2026-10-04).
This replaces the earlier `5.0/ocean/shenaniguns3d` integration assumption.
Facts below describe that published commit; items marked proposed require
coordination with ongoing engine work. This is a presentation contract, separate
from [the versioned RL contract](CONTRACT.md).

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

- [Architecture and authority](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/ARCHITECTURE.md)
- [Controller](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/controller.c), [body](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/body.c), [axes](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/swat_math.h)
- [Renderer](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/render.c), [build split](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/CMakeLists.txt)
- [Weapon state/timing](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/weapons.c), [authoritative muzzle and shots](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/sim.c)
- [Replica codec](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/protocol.c)
- [Environment builders](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/mission.c), [world and doors](https://github.com/FelixAllistar/PufferLib/blob/8b0c74e5377d0afa48a91d524d7241024126f855/ocean/swat/world.c)

Proposed integration seam: a presentation-only adapter called from
`swat_view_draw(SwatView*, const SwatSim*, ...)`. Keep dependencies out of
`swat_core` and the headless server. The existing C++ build support is needed
by Box3D; it is not evidence that ozz is implemented. Agree the adapter and active
files with the engine owner before runtime edits.

## Asset consumer requirements

The previously audited private full-body bank has 44 named clips, one skin with
74 joints, embedded textures, and up to seven nonzero influences per vertex.
Both character primitives use `JOINTS_0/WEIGHTS_0` and
`JOINTS_1/WEIGHTS_1`. Preserve all sets. A four-influence consumer must reject
the bank rather than drop weights and renormalize a subset. Weight reduction
would be a separately reviewed derivative. These facts identify a reference
profile, not a required format for every future character.

Raylib 5.5's stock skinning path has four influences per vertex. Its GLB
animation loader resamples at 17 ms and stores one interpolation mode per bone.
The reference bank has 15 clip/node combinations mixing motion and visibility
interpolation. Stock loading is not a verified consumer. Preserve each channel's
interpolation and timestamps in a separately tested consumer. These are source
findings, not a test of the project's actual Raylib binary.

Sources: [Raylib skinning](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c#L5218),
[animation sampling](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c#L5461-L5602).

Use exact clip names or stable IDs with an explicit name map; never index-only
mapping. Apply rest hierarchy, mesh-local basis, inverse binds, TRS channels,
all weights and normalized quaternions correctly. The rifle is rigidly skinned
to the right hand. Props use joints and baked transforms; no runtime Blender
constraints are needed. Optional `KHR_materials_specular` requires a separate
material-parity decision.

### Proposed ozz/Raylib consumer, not implemented

A small C++ presentation module with an opaque C wrapper is a candidate.
Preserve the source GLB; use a custom mesh path that reads every joint/weight set,
maps glTF skin indices through node IDs to the imported ozz skeleton, and applies
the matching inverse binds in the agreed mesh basis. Do not assume the imported
skeleton has exactly the skin's 74 joints or matching index/name order.

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

The reference GLB is Y-up at meter scale with an identity scene root.
Authoring-space Z-up matrices are not engine transforms. Measure asset forward,
sole origin, eye height, weapon sockets and model-to-capsule fit before freezing
the conversion. In particular, world-object yaw uses a different mapping:
`x' = cos(yaw)*x + sin(yaw)*z`,
`z' = -sin(yaw)*x + cos(yaw)*z`. Do not feed character heading into an
object transform without the required basis conversion.

Proposed ownership: the controller/physics alone move the world root and choose
actual stance. Keep visual hip/limb motion without double-applying root travel.
Crawl clips are authored in-place; verify the policy per clip. Locomotion
phase/rate must later be matched to measured cycle speed and actual travel;
the bank is not yet matched to the controller.

## First-person, full-body and muzzle boundary

The current normal player camera follows `swat_controller_eye/view`, including
achieved lean and recoil. ADS blends the selected FOV toward 45 degrees. The
controlled actor's world body is omitted; other actors use primitive full-body
drawing. Planning, sniper and inspection cameras have separate paths. Full-body
and FPS/viewmodel banks are distinct; confirm body visibility/head clipping and
viewmodel selection rather than treating either bank as interchangeable.

Authority computes muzzle clearance from the eye: forward offset 0.48 m primary
or 0.28 m sidearm, right offset `0.12*(1-ads)`, up offset -0.16 m, with a
0.035 m sphere sweep. The eye ray selects the aim target; accepted shots start
from the checked muzzle. The primitive displayed weapon instead uses its own
offset/length and lowering animation, so its visual muzzle is not the
authoritative muzzle.

Fit art and diagnostic socket overlays to these verified transforms first.
Changing authoritative muzzle, clearance or shot rules is a separate coordinated
gameplay decision. Body aim/fire, final ADS and gear fit are not certified by
the current animation bank.

## Reload timing and inventory mismatch

Verified game reloads are fixed-tick countdowns. GE carbine tactical/empty
durations are 120/156 ticks (2.0/2.6 s); P9 sidearm 90/120 ticks (1.5/2.0 s).
Equip durations are 24/18 ticks. Other weapon definitions have separate timings.
Reload requires a rising input edge; reload ammunition transfer commits only
on completion.
Switching weapons cancels the previous reload and clears its remaining/duration
fields. Reserve ammunition is pooled, without persistent individual magazines.

The authored full-body reloads are substantially longer: upright tactical
6.375 s, upright empty 5.625 s, prone tactical 8.15 s, prone empty 7.4 s.
A naive normalized-time map would greatly accelerate the motion. Agree a
timing/visual-prop policy before integration: retime/reauthor presentation to
existing rules, or separately review a gameplay timing change. This document
does not choose or change game timings, inventories, or cancellation semantics.

Playback uses seconds from clip start, not source frame numbers. Authoring rates
include 24 and 30 FPS; some tracks were exported at 60 Hz. Read timestamps.
Float32 endpoints may differ by sub-microseconds; endpoint-inclusive video frame
counts are not durations.

Magazine visibility uses binary uniform scale on `prop:Magazine*` joints with
STEP interpolation. Never blend ownership scale. Each physical magazine should
have exactly one visible representation at transfer. Authored tactical preview:
old installed, fresh outer spare, inner slot empty, then old stored in inner slot,
fresh installed and outer empty. Empty preview ends with old dropped, fresh
installed and both slots empty. These are preview states, not the game's pooled
inventory or a persistent magazine implementation.

Reference transfer times (seconds):

- Upright tactical: old grip 0.92, old stored 2.67, fresh grip 3.25,
  fresh seated 5.0916666667
- Upright empty: old drop 0.45, fresh grip 1.9416666667,
  fresh seated 3.7833333333
- Prone tactical: old grip 1.5, old stored 3.6, fresh grip 4.25,
  fresh seated 6.4
- Prone empty: old drop 1.0, fresh grip 2.4, fresh seated 4.55

The baked empty-mag fall is relative to the asset root, so translating the actor
moves it too. Proposed release: capture the world transform once, hide the
character instance, and transfer to a separately owned world prop. Gameplay
must choose whether it is cosmetic or persistent/collidable and control its
lifetime. Never spawn it every render frame or mistake preview motion for world
collision. Ready-pose defaults must not refill emptied visual carrier slots.

## Network and exact event ownership

Protocol v3 sends complete snapshots at 30 Hz from the 60 Hz authority. Actor
state includes pose/velocity/stance/lean, ADS/recoil, active weapon, ammunition,
reload remaining/duration, equip remaining and last-shot tick. Replicas restore
colliders without advancing rules. Round epoch, revision and input acknowledgement
protect against old state. Unacknowledged look affects the local displayed camera
only; movement/fire/damage remain snapshot-driven. Full interpolation and
prediction/reconciliation are still future work at this baseline.
The last-shot tick is a state stamp, not a guaranteed per-shot event stream.

Reload phase can be derived while a countdown is active, but snapshots can skip
a transfer or arrive after completion. There is no authored animation-notify
stream, persistent magazine ID, or reload-sequence ID in this interface.
Do not infer gameplay commits by noticing that a sampled pose crossed a marker.

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
schedule presentation unless authority explicitly owns their effects. Receiver
and bolt meshes are static in the bank; hand gestures are not functional hardware.
Future particles may share licensed assets/recipes and event sockets, while each
viewer creates its own instances and simulation keeps gameplay authority.

## Environment-art alignment

The house is currently built from procedural boxes, with no environment mesh
loader. Framed walls run along local Z; thickness is local X, height local Y.
Their placement uses the world-object yaw mapping above. Typical Cedar House
walls are 2.7 m high with 1.2 m by 2.1 m door openings; these are current authored
dimensions, not a universal modular-kit requirement.

A framed-wall door leaf is 0.044 m thick, leaves 0.04 m each side of its opening, starts
0.08 m above the floor, and pivots at its negative-local-Z edge. It swings from
0 to +pi/2 relative to `closed_yaw`, at up to 2 rad/s. Authority updates its
center/yaw and static-body transform; it is not a free physical hinge. The swept
obstruction check currently stops for actors only. Render from the authoritative
door pose rather than running a second hinge simulation.

Wall skins, timber frames and supports are separate objects with material,
part, health and active state. Board faces can break while studs remain physical.
The future environment kit should map visuals to those object identities and
states, preserve openings/hinge placement, and hide destroyed pieces. A combined
mesh that continues covering removed geometry would disagree with collision and
visibility. Coordinate any collision/door-builder changes separately.

## Public-asset and provenance boundary

No raw Mixamo character/animation, weapon-kit mesh, textures, Blender file, or
exported GLB belongs in this public PR. Use user-controlled private storage and
an ignored local asset path. Each asset needs source/product identification,
license evidence, an authorized use/distribution decision and an exact content
hash. The repository's code license does not establish art redistribution rights.
This PR contains no private authoring manifests, paths or asset payloads.

## Acceptance gates

1. Coordinate the presentation adapter and ownership with active engine work
2. Verify one idle and one empty reload in an isolated consumer first: all
   influences, joint remap, inverse binds, key/inter-key sampling, normal handling,
   exact visibility and pure seeking; then expand to all clips
3. Measure model-to-controller axes, feet, capsule, eye and muzzle alignment;
   test yaw/translation, lean, crouch clearance and FPS/full-body visibility
4. Agree reload timing, visual magazine identity, cancellation and network event
   policy before gameplay integration; prone requires its own simulation work
5. Test repeated tactical/empty cycles, interrupted transfers, zero/one spare,
   moving-root drops, restart, late snapshots/joins and replay without duplicates
6. Validate real-speed playback and locomotion phase at different render rates,
   then profile CPU skinning before choosing a GPU implementation

No runtime integration or ozz conversion is claimed. Native game/physics/network
tests were not run for this text/tool-only contribution. The private reference
bank passed this limited probe on 2026-10-03; this branch refresh reruns synthetic
tests, not the unavailable private asset. Passing preflight does not establish
deformation, materials, root motion, event authority or gameplay parity.

## Preflight usage

From the repository root, using Python's standard library only:

```sh
python -m unittest discover -s tests -p 'test_animation_glb_contract.py' -v
python scripts/check_animation_glb.py /path/to/local/character.glb --expected-animations 44
python scripts/check_animation_glb.py /path/to/local/character.glb --max-influences 4
```

The last command should reject the seven-influence reference profile.
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
