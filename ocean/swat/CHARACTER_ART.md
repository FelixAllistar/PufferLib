# Character consumer and preview

The independent C GLB consumer drives live carbine officers, squad members,
first-person arms, camera feeds and character shadows. Exact source sampling
and all supplied influences are preserved. The separate art lab remains useful
for inspecting the unchanged source; gameplay adds presentation pose fitting
and authoritative reload phase mapping outside that source.

## Live player

The tracked character bank is `ocean/swat/assets/characters/ready.glb`,
`walk.glb` and `textures/Ch15_*.png`, optionally extended by `walk_left.glb`,
`walk_right.glb`, `crouch_ready.glb`, `crouch_walk.glb` and `walk_backward.glb`. Ready is the verified six-second Shared
Ready N carry/reload export, SHA-256
`3a20375ec72f106925ef96718da0931e172f21908c6720cff4d792c173c0cc81`;
walk is the one-second cubic candidate identified below. Build scripts copy
this repository asset bank beside player binaries. Editable sources, measured
handoffs and original packages are tracked in `art_handoffs/`.
`./swat` uses it automatically. `SWAT_CHARACTER_ASSETS` selects another
install; `SWAT_CHARACTER_ART=0` selects procedural rendering.

`character_runtime.c` follows actual feet, heading, stance, achieved lean and
weapon pose. Source scale remains one metre per metre. Walk phase advances with
actual horizontal displacement divided by each bank's measured cycle travel,
without adding root travel to physics. Teleports, rewind/reset and actor changes
reset presentation travel. Legs turn toward actual travel and a two-bone solve
provides crouch/stance adaptation while preserving limb lengths and source foot
targets. An upper-body aim layer turns the chest over Spine, Spine1 and Spine2
(70% of weapon pitch, bounded to 35 degrees, plus a 20 degree bladed turn), then
aims the skull level and tipped 10 degrees toward the look direction across Neck
and Head. Rotations only, so abdomen length is preserved. The third-person rifle
keeps the achieved ballistic axes exactly, but its stock sits in the posed
chest's pocket (Ready entry stock point, chest frame from Spine2), not at the
authoritative shoulder point, which pivots about the eye and was 30-45 cm from
the mesh shoulder at steep pitch and in crouch. Hands solve to the Ready entry
grips in the rifle frame with fixed chest-relative elbow hints; the support hand
slides back up to 16 cm along the handguard rather than locking straight.
Reloads blend to the clip's own hand motion. `make officer-review` renders a
one-officer contact sheet and prints grip/reach metrics
(`tools/officer_review.c`; `REVIEW_CLOSE=1` upper-body framing, `REVIEW_WALK=1`
eight walk phases, `SWAT_AIM_TUNING=FILE` loads a parameter file). The stance
values in `swat_aim_tuning` were fitted by `make officer-fit`
(`tools/officer_fit.c`), a hill climber over a skinned-mesh score: butt-pad
gap, rifle penetration, cheek-to-comb gap, eye on the sight line, elbow tuck,
neck contortion, arm lock and stock seat (score 514 to 67). The seat term
holds the butt pad's top edge 1.5-4 cm below the deltoid top (the highest
right-upper-arm vertex within 8 cm of the shoulder joint, measured along the
chest's up axis), so the stock sits in the shoulder pocket instead of riding
on top of the shoulder. The rifle stock now blends 70%
toward the cheek-weld placement (sight line through the mesh's right eye).
Moving banks are stride-warped: foot travel along the walk direction is scaled
by 0.72 with the phase sped up to match, and standing walks raise the hips up to
16 cm within leg reach. Third-person tracers still start at the
authoritative muzzle. The measured Prop_Rifle inverse-bind bridge is applied exactly once.
Art landmarks never override the camera, physical muzzle or collider dimensions.

The bounded torso frame rebuilds its right axis after selecting forward/up,
preventing recoil/lean shear. Skeletal local translations blend direction while
retaining the current sampled length; non-skeletal prop translations retain
their original interpolation. A2,400-frame ordinary-input regression checks all
64 skeletal edges and all seven banks, with firing, lean, extreme pitch, crouch,
reload and ready changes. Final Linux/Windows maximum length error is0.82µm
against the unchanged10µm bound. Full software GL and bounded native source
skinning checks pass. This fixes deformation, not the remaining pose fit:
extreme high aiming can leave the support hand beyond reach, and crouched
reload transitions still exceed the physical head envelope. Source assets and
first-person sampling remain unchanged. Evidence and limits:
`tests/receipts/character-rigidity-2026-10-19.json`.

Standing locomotion now selects distinct original left/right banks according to
achieved local travel. Their pace is 1.92126191 / 1.92126155 m per one-second
cycle. The adapter uses each bank's measured travel vector after its fixed fit,
retaining authored chest/pelvis counter-rotation. Retreat now selects the original
one-second backward bank, SHA-256
`5769ec1b03a63e3d363ba360c66edd31784225e6ca49be7b2177980ba84170de`,
at a measured 1.921261449 m per cycle. Its fixed fit and oblique travel heading
are measured independently from its rifle-facing direction. Diagonals rotate
the nearest available bank toward achieved movement. Phase stays continuous across bank changes, stops without
actual displacement, and advances by 2.03907418 m per crouch-forward cycle.
Missing optional banks fall back to forward/Ready without disabling character art.

`tools/install_character_movement.py --backward EXTRACTED_FIXTURE` verifies and
installs the private backward original; rerunning the F gear installer adds its
unchanged motion to the current geometry. All 70 native joints match exactly at
765 keys, midpoints and independent reference phases. The consumer agrees with
151 independent Blender deformation poses and nine full-surface controls, with
maximum vertex error 0.000000940 m. Sampled cycle-end matrices are identical.
GPU checks cover actual travel phase, yaw/diagonal selection, stop, missing-bank
fallback, current thumb grip and anatomical elbow carriers. The source's raised
Ready posture and documented thigh/strap contacts remain; no authored ADS,
footstep or gameplay event track is invented.

The planted crouch contains a single STEP key at t=0 and no authored duration;
it is sampled and held at zero. Crouch-forward is the separate original one-second
cubic bank. Their phase-zero hips already sit 0.268843 m below Ready, so stance
adaptation subtracts that authored drop before solving the remaining controller
hip drop. Reloads always use the unchanged Ready bank and authority phase mapping.
Source garment/contact limitations remain, including the reported 18.392 mm
crouch-forward sleeve/thigh overlap. Rejected ADS study poses are excluded.

The separate deep-crouch grounding retarget remains rejected research. Actual
460-frame input checks exposed backward start/stop, lateral and diagonal floor
penetration after the four forward-heading cases passed. Restricting the knee
pole fixes the backward case but cannot fix every stance: one captured diagonal
hip/ankle pair admits no length-preserving knee solution above the floor (its
highest possible knee centre is 46.9 mm below it). Crouched reload also fails;
an earlier firing frame exposes a separate 85.6 micrometre arm-length difference.
Revise directional stance/endpoints before further adoption; keep the original
floor and bone bounds, source curves and gameplay authority. The rejected runs
are pinned by `tests/receipts/crouch-direction-diagnostics-2026-10-19.json`.

`tools/install_character_movement.py` accepts the four extracted private fixture
directories in left, right, crouch-ready, crouch-forward order. It checks all
four calibrated original GLB hashes before writing the tracked asset bank. The
measured fits are recorded in `character_movement_data.h`; no source mesh,
curve, duration, weight or binding is rewritten. The four new banks pass 748
independent numerical source samples, including every authored key and off-key
times; worst vertex difference is 0.000000827 m. GPU checks cover held zero-duration
crouch, lateral/stance selection, actual-displacement phase, missing-bank fallback,
abdomen length, physical rifle placement, reload commits and immutable authority.

The normal controlled body is omitted. First-person rendering selects forearm/glove
triangles from the original weights, excluding upper shoulders that otherwise
sweep through the camera during the authored working carry, along with the rifle and currently owned
magazine representations. Local unacknowledged look is a display correction
applied to those meshes only. Other live carbine officers use the full body;
other weapon/role and incapacitated/restrained states retain their existing
representations. Source garment/contact defects remain source art issues.

The optional F gear install is `assets/characters/upper_gear_f/`, built by
`tools/install_character_gear.py EXTRACTED_F_FIXTURE`. It verifies the original
geometry hash `5dd68cf3010b4ba24ec719fb949e1d8f62053c5761142c37da41079848f00835`
and all installed original motion hashes (six base banks plus optional backward), plus exact native joint default transforms
and hierarchy. It combines the new 72-joint geometry with unchanged original
animation accessors/channels in separate private runtime banks; original files,
weights, UVs and curves remain intact. All 70 original sampled joint matrices
match exactly across 3,114 authored-key/midpoint samples. Runtime reconstructs
the two new rigid elbow carriers from the anatomical bend plane after sampling
and after arm IK. The GPU contract checks unit frames, elbow/wrist alignment and
single-influence cap rigidity. SWAT_Wearer primitives retain the source
1001/1002 texture correspondence; the new headset and caps retain their authored
polymer/padding factors. `SWAT_CHARACTER_GEAR=0` selects the original geometry.
The supplied three-second articulation remains source review evidence, rather
than replacing gameplay locomotion or reloads. Mount gaps and remaining palm/
finger contact defects remain source limitations.

The frozen October 5 grip handoff is private diagnostic evidence, archive
SHA-256 `9a74320a3e2c2c8b6efb3216ebf90f4990bf73f38a5f54c5971215370cb94d8f`.
`ENGINE_THUMB_FRAME_HANDOFF.json` supplies the original thumb joint basis and
measured rifle frame; its C/C1 poses are not applied. The engine applies a modest correction to the three left thumb joints in
that measured frame. Rotation strength is now capped at 30% of the original
wrap target: the stronger correction made the thumb look oversized and bent
the glove web into a tall curved silhouette. This keeps the thumb alongside
the fore-end rather than stretching its skin upward. Wrist, right hand, other fingers, source weights, segment
lengths and rifle geometry remain unchanged. This correction is captured before
world arm IK, so first-person, world, feeds and shadows share the grip. It fades
out during the first 0.12 authored seconds of a reload, remains completely
released during magazine handling, and returns over the final 0.18 seconds.
The GPU contract checks every movement bank, rigid chain lengths, unchanged
unrelated joints, actual hip/ADS captures and exact source thumb matrices during
reload handling. This is a support-thumb iteration, not a replacement rifle or
a claim that all source glove contact defects are repaired.

First-person carbine aiming uses an adjustable eye-to-aperture distance, default
120 mm. This rigid presentation shift changes neither the physical muzzle nor
world rifle placement. GPU tests project the actual source aperture center and
post tip onto screen center at 80/120/220 mm through yaw, stance, lean, recoil
and +/-85-degree look limits. Hip view sliders do not displace full-ADS sights;
settings offer an aiming preview without changing simulation state.

Empty and tactical game reloads retain their original authoritative durations.
A piecewise presentation mapping aligns source release at 0.88 s with integer
`floor(D/4)` removal and source seating at 3.65 s with `floor(2D/3)` insertion,
then maps the remaining source through completion. Unseated INSERT restarts
skip removal. Cancellation/late snapshots reconstruct from current seated,
chamber and timer state; an unseated IDLE never redraws an installed magazine.
The source empty-mag fall and spare sleeve are hidden because authority retains
rounds/magazines and supplies no persistent drop/pouch identities. Animation
creates no inventory, ownership events or world drops.

Skeleton evaluation and IK happen once per actor per simulation tick. Cached
node matrices are reused by main, camera and shadow passes. Bind vertices and
all contiguous influence sets stay on the GPU; RGBA32F tables carry every joint
index/weight and the exact sampled palettes. The vertex shader computes the
weighted matrix and its inverse-transpose normal, with no four/eight-weight
reduction, animation resampling or weight renormalization. Shadow/unlit rendering
uses the same deformation. Headless simulation/server builds have no renderer
or graphics dependency.

Repeated draws compare the exact joint palette against its current uploaded
RGBA32F contents and reuse it byte-for-byte when unchanged. A different actor,
pose or first-person deformation still uploads every changed matrix; there is
no quantization or influence reduction. Native parity, eight settled stance/yaw
states and repeated full-image comparisons cover reuse alongside the existing
movement, ADS and reload checks.

Original 2K diffuse/emissive and specular-color maps are interpreted as sRGB. The paired source gloss map is linear and supplies perceptual
roughness `clamp(1 - gloss, 0.08, 1)`. Specular RGB supplies F0; diffuse energy
uses `1 - max(F0)`, following the
[Khronos specular/glossiness equations](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness).
This is runtime binding of the separate legacy maps, not support for importing
that glTF extension. The source maps are shared across all movement banks and
released once. Missing pairs use the existing rough dielectric fallback;
`SWAT_CHARACTER_FINISH=0` selects that fallback for comparisons. Normals use
signed UV derivatives on deformed geometry
and the preserved Blender material's unflipped +Y interpretation. That is not
proof of the original artist's convention; `SWAT_CHARACTER_NORMALS=0` or `-y`
allows the documented comparison without rewriting source maps. Original gun
maps are borrowed from the measured rigid carbine's unchanged 4K materials.

From the repository root:

```sh
./swat character --asset /path/to/private/character.glb
./swat character --asset /path/to/private/character.glb --time 1.4 --capture /tmp/pickup.png
./swat character --asset /path/to/private/character.glb --play --frames 370
./swat character --asset /path/to/private/walk.glb --play --loop --gpu --frames 370
```

The launcher selects native Windows in WSL and translates asset/capture paths.
`SWAT_NATIVE_WINDOWS=0` selects Linux and the existing hardware-driver routing.
Use Space to play/pause, arrows to scrub, Home to restart, left-drag to orbit,
and the wheel to zoom. Playback clamps at the original clip end by default.
`--loop` explicitly wraps a positive-duration clip for locomotion seam review;
scrubbing still clamps so the endpoint can be inspected. Preview wrapping
produces no inventory, visibility-policy or gameplay-event commits. `--clip` takes the exact name. Capture exits after three
frames; `--frames` bounds playback. These operations change no gameplay state.


First-person reloads use the fixed Ready entry rifle frame, preserving the
source rifle turn and hand/prop motion. Inverting each current sampled rifle
frame had cancelled its motion; applying the generic 28-degree busy tilt then
hid the entire animation below the weapon-view viewport. The camera adapter
now lets the authored working carry provide the tilt and smoothly frames the
measured seated-magazine centre at 62% across / 65% down, 0.60 m from the eye.
It uses the current projection so weapon-size/position settings retain visible
magazine handling. Ordinary hip/ADS framing and physical gun authority are
unchanged; world/camera poses are restored after each first-person draw.

The explicit GPU check now asserts visible geometry at seven reload stages,
and isolates the fresh magazine during carry/insertion to verify it remains
on screen at both slider extremes and while holding aim. Source thumb handling,
magazine removal/insertion ticks and cancellation/restart checks remain exact.
The current tests pass on both WSLg/D3D12 and native GTX 1060. All 21 headless
checks pass. A bounded CPU check finds no rifle penetration among 329 original
thumb skin vertices/triangle-centroid samples at the eased 30% correction, with
minimum sampled gap 0.25683 mm; it does not establish continuous clearance.

## Supported data

`character_asset.c` has no Raylib, physics, network, inventory or gameplay-event
dependency. It owns the input bytes and parses a self-contained GLB using a
pinned MIT cgltf header with private symbols, avoiding Raylib's cgltf ABI.

- Dense triangle positions/normals, optional UVs, rigid or skinned primitives.
- Up to 32 active influences, paired contiguous `JOINTS_n`/`WEIGHTS_n` sets;
  normalized float or unsigned integer weights are preserved without reduction.
  A requested lower limit rejects a mesh that exceeds it.
- Explicit `skin.joints` remapping and inverse binds, hierarchy ordering and
  scene-space deformation. Mesh-node transforms cancel for skinned primitives.
- Exact named clips, channel-specific STEP/LINEAR/CUBICSPLINE sampling,
  shortest-arc quaternion interpolation, Hermite tangents with key spacing,
  and reset from rest on every sample. Seeking produces no events.
- Inverse-transpose normalized normals; collapsed zero-scale props are hidden
  without inverting a singular matrix. Nonfinite poses fail.

The art lab preserves neutral fixture materials; `--gpu` selects the same
full-influence vertex path used in gameplay. The GLB parser itself rejects embedded textured character materials,
transparent/emissive/unlit or material-extension shading, morph targets, sparse/compressed accessors or
external dependencies. Unsupported textured character maps are rejected with a
specific error. All instantiated mesh nodes are consumed; selectable scenes
and skinning LOD are pending. Explicit node/skin/clip/geometry/input limits bound
the preview's allocations.

## Independent verification

Live integration passed 20 headless tests and real-GPU CPU/reference comparisons
with 7, 17 and 32 influences. Synthetic poses were pixel-identical; the private
Ready and walk comparisons differed in at most three of 262,144 pixels above
the comparison threshold. Reload cancellation/restart, stance and yaw fitting,
frustum culling and repeated-camera caching passed on Linux D3D12 and native
Windows. A 631-step gameplay journal was byte-identical with character art on
and off (42,961 bytes), including unchanged inventory and authority state.


The original, unchanged six-second F fixture has SHA-256
`2410005918b2b3e87c157229c12debb5743d228fefee1f29cd4bccb9e14ec45f`.
It contains 76 nodes, one 70-joint skin, six primitives, 48,755 vertices and
58,206 triangles. Maximum active influences are seven; 444 vertices exceed four.
The one clip is `Standing Empty / Coordinated Contact Cleanup F`.

At 377 poses (every authored key, off-key samples and transfer-boundary
neighborhoods), a separate NumPy/SciPy implementation compared every node
matrix, posed vertex and normal to the C output. Worst errors were below
9.33e-7 for matrix components, 8.68e-7 m for position components and 3.15e-6 for
normal components. A four-influence consumer rejected the fixture. This verifies
serialized GLB playback against independently calculated glTF transforms;
the native Blender deformation snapshots were not supplied to this test.
Screenshots were also inspected against the author's Ready reimport view.

Native Windows on the observed GTX 1060 3GB completed 370 frames; 298 changing
poses averaged 2.650 ms for CPU deformation plus vertex-buffer upload, with a
7.977 ms maximum while build work was also running. This is one preview
character, not a full-squad frame budget. These historical CPU lab costs motivated the live GPU consumer above. The preview's white character
comes from the fixture's neutral untextured materials. The nine original 2K body
maps have been received and hash-verified in private storage. The live renderer
uses the source conventions described above. Licensed fixture/capture
files are retained in `art_handoffs/`; runtime maps live in `assets/characters/textures/`.

### Forward walk cubic seam candidate

The separate one-second `Forward Walk / Shared Ready N C1 Seam Repair B`
candidate has GLB SHA-256
`82dd05c813a017b23ee205c2cb43a98edc56b062f4dd567741099d3e6a040d69`.
Its 210 TRS channels use CUBICSPLINE; geometry, all seven influences and the
collapsed secondary prop remain unchanged from the original walk. Sampling
uses the [glTF cubic interpolation rules](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#interpolation-cubic).

An independent oracle checked 257 poses, including every authored key,
off-key samples, repair boundaries at 0.125/0.875 seconds and end clamping.
Maximum component errors were 4.23e-7 for node matrices, 7.92e-7 m for positions
and 3.92e-7 for normals. A four-influence consumer rejected this candidate too.
The C consumer produced bit-identical endpoint matrices and posed vertices.
Independent analytic differentiation of serialized cubic curves, hierarchy
transforms and full-weight skinning measured a maximum endpoint vertex-velocity
residual of 6.214e-5 m/s. This is a small residual, not a claim of exact derivative
equality. Tiny finite-difference intervals amplify float32 position noise;
finite-distance chords also include acceleration and do not measure the
endpoint derivative directly.

Native Windows completed 370 frames and six wraps, with 369 changing samples
averaging 2.426 ms for CPU deformation plus upload (3.018 ms maximum). These
measurements apply to one neutral-material preview character. The source author
reports that stock Blender 4.3.2 reimport loses cubic tangents; our validation
uses the actual cubic consumer, not that reimport. The runtime now fits this
source to achieved gameplay pose and renders it with original maps; this does
not repair source garments or establish an anatomical eye landmark.

## Checks

`tests/test_character_render.c` is an explicit display check (not a headless
CTest): CPU/GPU image comparisons cover rest/off-key/endpoint/backward seeks,
full influence sets, nonuniform normal transforms and collapsed props. It also
checks yaw/crouch/high pitch, the measured stock transform, read-only authority,
camera reuse and real reload removal/insertion/cancellation/restart boundaries.
First-person rendering uses a separately cached copy of the original sampled
arm pose, aligned as a complete rig to the presentation rifle. Upright world-body
shoulder IK no longer enters the first-person pass; source grips and limb lengths
remain intact when the camera looks up/down. The shared bank's world pose is
restored after drawing. Look-limit checks compare screen masks through ±85°
pitch and quarter-turn yaw, verify the larger coverage, and ensure opposite
view-slider extremes produce identical fully aimed frames.
Public synthetic seven-, seventeen- and thirty-two-weight fixtures need no
licensed art. Live checks use the private Ready/walk install.

```sh
build/swat/portable/swat_test_character_render /path/to/synthetic.glb
build/swat/portable/swat_test_character_render ocean/swat/assets/characters/walk.glb ocean/swat/assets/characters
```


```sh
make -C ocean/swat character-test character-lab
# Optional independent matrix/deformation oracle (NumPy and SciPy required):
python ocean/swat/tests/test_character_asset.py --probe build/swat/character_probe
python ocean/swat/tests/test_character_asset.py --probe build/swat/character_probe --asset /path/to/private/character.glb
python ocean/swat/tests/test_character_asset.py --probe build/swat/character_probe --asset /path/to/private/walk.glb --event-time 0.125 --event-time 0.875
```

CMake always registers standard-library synthetic contract checks when a host
Python interpreter is available. It also registers the independent numerical
oracle if that interpreter has NumPy and SciPy. Headless configurations build
the importer/probe without Raylib. Public synthetic tests cover seven weights,
shuffled joints, nonidentity binds, nonuniform normal transforms, rigid nodes,
mixed interpolation, exact visibility boundaries, backward seeks and malformed
inputs. The synthetic contract also passes AddressSanitizer/UndefinedBehaviorSanitizer.

Actual-fixture checks derive the duration from serialized sampler times, rather
than assuming a six-second reload. Repeated `--event-time` values add clip-specific
event/repair boundary neighborhoods. No private source is embedded in the tests.

The Ready and walk repairs remain separate candidates and pass consumer checks
independently. [ANIMATION_CONTRACT.md](ANIMATION_CONTRACT.md) records the
remaining gameplay fit, authority and timing requirements.

## Civilian BC_CIV01 (stylized candidate, presentation only)

`assets/characters/civilian/` holds the BC_CIV01 runtime GLB (SHA256
`dc6211c4...`, matching the verified package in
`build/swat/review/civilian-bc-civ01-package-v1/`), its flat-colour maps,
`state_contract.json`, `material_bindings.json` and `mesh_colors.txt`
(each mesh's bound material resolved to linear colour/roughness; the maps are
flat, so this is equivalent). The package README is `PACKAGE_README.md`.

`swat_civilian_runtime_*` (in `character_runtime.c`) draws every live
civilian with one shared GPU view and a cached pose per actor. It maps the
existing civilian state to the authored loops (idle, walk, kneeling
surrender, restrained kneel/stand, escort walk; travelling loops advance with
ground covered) and plays the authored transition clips on state changes; a
transition in progress always finishes. Source front +Z maps to engine
heading zero +X. Collision and eye height are unchanged. A detainee picked up
for escort from a kneel stands in place for `SWAT_RISE_TICKS` (252 ticks, the
authored 4.2 s rise) before following (`gear.rise_ticks`, in the snapshot), so
the rise never slides. `SWAT_CIVILIAN_ART=0` restores the capsules. `make civilian-review`
renders a state timeline.

Suspects use the same BC_CIV01 body with a dark palette (charcoal hoodie,
black jeans and shoes; per-mesh albedo swapped at draw). While armed (not
surrendered, restrained or stunned) the chest turns toward the aim (60% of its
pitch over Spine/Chest), the weapon is drawn on the authoritative aim axes with
its stock in a chest pocket, and both hands are IK'd to the simulation pose's
grip points (12 cm along the barrel; support at 65% of it). Surrendering drops
the weapon from view and plays the civilian clips. `REVIEW_SUSPECT=1 make
civilian-review` renders aim level/up/down, walking, surrender and restraint.
Tracers and muzzle flash still start at the authoritative muzzle.

## Mixamo bodies (terrorists and civilians)

Fifteen Mixamo characters (raw FBX on `/mnt/d/puffer_media/mixamo/characters`)
are built into `assets/characters/mixamo/` by `tools/mixamo/build_assets.sh`.
That directory is git-ignored: Mixamo files may not be redistributed raw and
this repository is public. `index.txt` lists `name role`; suspects are Ch08
(grey hoodie) and Ch18 (tactical vest), the other thirteen are civilians.

- `export_bodies.py` (headless Blender 4.2, `~/tools/blender-4.2.3-linux-x64`)
  writes each FBX as a skinned rest-pose GLB.
- Armed suspects: `conform_body.py --rig officer` puts the body on the officer
  rig. The officer skeleton, clips and rifle props stay untouched; each vertex
  is carried from the body's bind to the officer bind by its skin weights, so
  every measured fit (aim layer, hand IK, banks, reload, clamber) applies.
  `swat_character_runtime_open_body` opens one such bank directory per body.
- Civilian states (civilians, and suspects once surrendered or restrained):
  `retarget_clips.py` keeps the body on its own skeleton and retargets the 12
  BC_CIV01 clips onto it: each bone is first rest-aligned onto its BC
  counterpart (A-pose vs T-pose), then takes BC's world rotation change from
  rest; hips take scaled BC hip travel. Arm and leg directions match BC
  exactly on every sampled frame.
- Hair and eyelash cards (any non-opaque source material) are alpha-tested:
  the texture list's fourth column flags them, the runtime marks the material
  with a reserved 254/255 albedo alpha, and the lighting shader discards
  texels below 0.45 alpha only for that flag (glass is unaffected).
- Textures are written once per image at 1024 px (`textures/`), listed per
  body primitive in `body_textures.txt` (officer rig) / `civ_textures.txt`.
- Bodies are chosen per actor by rank within its role plus a per-episode
  offset (`swat_body_variant`), so no body repeats in a scene; civilian
  bodies load on first use.

### Deaths

Every Mixamo body, and an Officer body retargeted from `ready.glb` (rifle,
magazine and sleeve props dropped), carries six Mixamo death clips:
`Death_Front`, `Death_Headshot`, `Death_Back`, `Death_Right`,
`Death_CrouchHeadshot` and `Death_Npc` (`export_clips.py` turns the raw
animation FBXs into skeleton-only GLBs; `retarget_clips.py --mixamo
NAME=clip.glb` retargets them like the BC clips). Presentation only: the sim's
death is unchanged.

- The clip starts at the tick the actor is first seen dead and holds its last
  frame. An actor already dead when first seen (late join, reset) shows the
  final pose. The root stays at the feet and facing at death.
- Choice: civilians `Death_Npc`; crouched `Death_CrouchHeadshot`; otherwise
  from the killing shot (nearest recent tracer end; its start against the
  body facing): front `Death_Front`/`Death_Headshot` (alternating per actor),
  back `Death_Back`, side `Death_Right`.
- Clear floor: the fall is swept as 12 cm spheres from the chest to the final
  head and from the hips and shoulders to the final feet and hands, against
  world geometry. A blocked fall turns by up to ±1.1 rad, then tries the
  other clips, so a body does not land through a desk, bed or wall.
- Dead officers draw on the Officer body (their banks have no death clips).

`make slice-capture` builds `slice_capture OUT_DIR [--shot N | --film]`: the
real motel mission rendered by the production view from scripted cameras
(`tools/slice_shots.h` stills, `tools/slice_film.h` a 30 fps film of the
real 60 Hz sim; encode with ffmpeg). `--list` prints actors, doors and the
plan-slice geometry for framing.

## Reload review

`make reload-review` builds `reload_review ASSET_DIR OUT.png`: one officer
reloads through the real simulation, standing (side and front), crouched,
walking and in first person, and the sheet shows the reload across its 2 s
(the 6 s authored clip plays at 3x). It prints stage, seated state, left
hand to magazine distances and magazine mesh visibility per frame.
`REVIEW_ROW=n`, `REVIEW_TILE`, `REVIEW_FRAMES` render one larger row.

The reload clip is authored standing still. While moving, the upper body keeps
the reload clip and the legs come from the locomotion bank at the travelled
stride phase (carried into the Ready frame, stride-warped as usual), so the
feet step instead of sliding.

## Shots in third person

`character_runtime.c` records each drawn officer's presented muzzle (the
authoritative muzzle carried by the posed rifle) and exposes it with
`swat_character_runtime_muzzle`; armed suspects use their presented gun pose.
`render.c` starts other shooters' tracers there instead of at the authoritative
eye/muzzle point (hit point unchanged) and draws the muzzle flash on the
third-person rifle. Each shot also kicks the drawn rifle 2.5 cm straight back
into the shoulder, recovering over 6 ticks (100 ms); the hands follow by IK.
All presentation only: authoritative recoil stays in the aim.

## Clamber (vault / climb)

Gameplay (`controller.c`): jump while moving forward into an obstacle whose top
is 0.40-1.25 m above the feet, within 0.75 m, starts a clamber instead of a
jump. If there is floor near the starting level and room beyond, the officer
vaults over it; otherwise, if there is room on top, climbs onto it; otherwise it
is an ordinary jump. The body crouches and follows a scripted four-point path
(rise, cross, land; 0.42 s + 0.25 s per metre for a vault). The path is swept
with a 55% body-width hull and the landing checked with the full hull before
the move starts. No firing, aiming, reloading or tools during it; stamina cost
0.15. The RL policy's existing jump action triggers it. `SwatMantle` is in the
actor snapshot (SWAT_NET_VERSION 48). Scripted squad bots clamber only when no
walking route exists: the nav grid stores two-cell clamber links (upper link
bits) over blocked edges, the route search tries them last, and the bot
approaches crouched and presses jump at the face.

Presentation (`character_runtime.c`): the support hand plants on the
obstacle's near top edge while rising and crossing, the chest leans 25 degrees
over it and the legs tuck while crossing, each ramped in and out by move
progress. `make clamber-review` renders a vault and a climb; `make clamber-test`
covers vault, climb, refusals (too tall, curb, no headroom), busy hands and
snapshot round trip.
