# Character consumer and preview

The independent C GLB consumer and a read-only art lab are implemented. They
preserve the original animation timing and every supplied influence set. The
game player still draws procedural actors: anatomical/controller calibration,
reload phase mapping, material sources and optimized squad rendering are pending.
This is the next integration stage, not approval of the source animation's fit.

From the repository root:

```sh
./swat character --asset /path/to/private/character.glb
./swat character --asset /path/to/private/character.glb --time 1.4 --capture /tmp/pickup.png
./swat character --asset /path/to/private/character.glb --play --frames 370
./swat character --asset /path/to/private/walk.glb --play --loop --frames 370
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

The initial lab preserves neutral material factors and uses the existing
diffuse preview shader. It does not yet implement textured character materials,
transparent/emissive/unlit or material-extension shading, morph targets, sparse/compressed accessors or
external dependencies. Unsupported textured character maps are rejected with a
specific error. All instantiated mesh nodes are consumed; selectable scenes
and skinning LOD are pending. Explicit node/skin/clip/geometry/input limits bound
the preview's allocations.

## Independent verification

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
character, not a full-squad frame budget. CPU caching/LOD or full-influence GPU
skinning must precede default squad integration. The preview's white character
comes from the fixture's neutral untextured materials. The nine original 2K body
maps have now been received and hash-verified in private storage; their
diffuse/specular/glossiness conventions still need an explicit renderer adapter.
Normal-map green convention is not yet established. Licensed fixture/capture
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
uses the actual cubic consumer, not that reimport. Controller fit, garment
defects, textured character rendering and gameplay playback remain pending.

## Checks

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
