# Character consumer and preview

The independent C GLB consumer drives live carbine officers, squad members,
first-person arms, camera feeds and character shadows. Exact source sampling
and all supplied influences are preserved. The separate art lab remains useful
for inspecting the unchanged source; gameplay adds presentation pose fitting
and authoritative reload phase mapping outside that source.

## Live player

The optional private install is `build/swat/assets/characters/ready.glb`,
`walk.glb` and `textures/Ch15_*.png`, optionally extended by `walk_left.glb`,
`walk_right.glb`, `crouch_ready.glb` and `crouch_walk.glb`. Ready is the verified six-second Shared
Ready N carry/reload export, SHA-256
`3a20375ec72f106925ef96718da0931e172f21908c6720cff4d792c173c0cc81`;
walk is the one-second cubic candidate identified below. Build scripts copy
this ignored install beside player binaries; no licensed source is published.
`./swat` uses it automatically. `SWAT_CHARACTER_ASSETS` selects another private
install; `SWAT_CHARACTER_ART=0` selects procedural rendering.

`character_runtime.c` follows actual feet, heading, stance, achieved lean and
weapon pose. Source scale remains one metre per metre. Walk phase advances with
actual horizontal displacement divided by each bank's measured cycle travel,
without adding root travel to physics. Teleports, rewind/reset and actor changes
reset presentation travel. Legs turn toward actual travel and a two-bone solve
provides crouch/stance adaptation while preserving limb lengths and source foot
targets. Torso pitch is bounded and rotates around the sampled spine joint,
preserving abdomen length even when camera/stance targets differ; independent arm solves fit the achieved rifle
pose. The measured Prop_Rifle inverse-bind bridge is applied exactly once.
Art landmarks never override the camera, physical muzzle or collider dimensions.

Standing locomotion now selects distinct original left/right banks according to
achieved local travel. Their pace is 1.92126191 / 1.92126155 m per one-second
cycle. The adapter uses each bank's measured travel vector after its fixed fit,
retaining authored chest/pelvis counter-rotation. Diagonals and backward travel
rotate the nearest available bank toward achieved movement; no backward source
clip is claimed. Phase remains continuous across bank changes, stops without
actual displacement, and advances by 2.03907418 m per crouch-forward cycle.
Missing optional banks fall back to forward/Ready without disabling character art.

The planted crouch contains a single STEP key at t=0 and no authored duration;
it is sampled and held at zero. Crouch-forward is the separate original one-second
cubic bank. Their phase-zero hips already sit 0.268843 m below Ready, so stance
adaptation subtracts that authored drop before solving the remaining controller
hip drop. Reloads always use the unchanged Ready bank and authority phase mapping.
Source garment/contact limitations remain, including the reported 18.392 mm
crouch-forward sleeve/thigh overlap. Rejected ADS study poses are excluded.

`tools/install_character_movement.py` accepts the four extracted private fixture
directories in left, right, crouch-ready, crouch-forward order. It checks all
four calibrated original GLB hashes before writing the ignored install. The
measured fits are recorded in `character_movement_data.h`; no source mesh,
curve, duration, weight or binding is rewritten. The four new banks pass 748
independent numerical source samples, including every authored key and off-key
times; worst vertex difference is 0.000000827 m. GPU checks cover held zero-duration
crouch, lateral/stance selection, actual-displacement phase, missing-bank fallback,
abdomen length, physical rifle placement, reload commits and immutable authority.

The normal controlled body is omitted. First-person rendering selects arm
triangles from the original weights, along with the rifle and currently owned
magazine representations. Local unacknowledged look is a display correction
applied to those meshes only. Other live carbine officers use the full body;
other weapon/role and incapacitated/restrained states retain their existing
representations. Source garment/contact defects remain source art issues.

The optional F gear install is `assets/characters/upper_gear_f/`, built by
`tools/install_character_gear.py EXTRACTED_F_FIXTURE`. It verifies the original
geometry hash `5dd68cf3010b4ba24ec719fb949e1d8f62053c5761142c37da41079848f00835`
and all six original motion hashes, plus exact native joint default transforms
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
than replacing gameplay locomotion or reloads. Mount gaps and existing grips
remain source limitations.

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
files stay in ignored local storage and are not shipped in the public repository.

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
build/swat/portable/swat_test_character_render build/swat/assets/characters/walk.glb build/swat/assets/characters
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
