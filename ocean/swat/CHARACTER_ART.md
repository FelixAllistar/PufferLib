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
```

The launcher selects native Windows in WSL and translates asset/capture paths.
`SWAT_NATIVE_WINDOWS=0` selects Linux and the existing hardware-driver routing.
Use Space to play/pause, arrows to scrub, Home to restart, left-drag to orbit,
and the wheel to zoom. Playback clamps at the original clip end; it never loops
magazine ownership. `--clip` takes the exact name. Capture exits after three
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
comes from the fixture's neutral untextured materials; original material
sources have been requested separately. Licensed fixture/capture files stay in
ignored local storage and are not shipped in the public repository.

## Checks

```sh
make -C ocean/swat character-test character-lab
# Optional independent matrix/deformation oracle (NumPy and SciPy required):
python ocean/swat/tests/test_character_asset.py --probe build/swat/character_probe
python ocean/swat/tests/test_character_asset.py --probe build/swat/character_probe --asset /path/to/private/character.glb
```

CMake always registers standard-library synthetic contract checks when a host
Python interpreter is available. It also registers the independent numerical
oracle if that interpreter has NumPy and SciPy. Headless configurations build
the importer/probe without Raylib. Public synthetic tests cover seven weights,
shuffled joints, nonidentity binds, nonuniform normal transforms, rigid nodes,
mixed interpolation, exact visibility boundaries, backward seeks and malformed
inputs. The synthetic contract also passes AddressSanitizer/UndefinedBehaviorSanitizer.

The new Ready repair will remain a separate candidate and must pass the same
consumer checks. [ANIMATION_CONTRACT.md](ANIMATION_CONTRACT.md) records the
remaining gameplay fit, authority and timing requirements.
