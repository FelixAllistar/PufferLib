# Reproducing the static pose checks

The complete independent replay uses the actual binary and included fresh native/reimport references. It needs only Python 3 and NumPy, with no authoring workspace or Blender installation:

```sh
python independent_validation/replay_fixture.py --package . --output independent_replayed.json
```

This also checks metadata, full skin weights, stock rays, optical/sole mappings and immutable static-array digests. To regenerate its references from separately pinned input files, see independent_validation/INDEPENDENT_BINARY_REVIEW.md.

Run from the extracted package root. These commands only read their inputs and write the named reports. Python 3 with NumPy is sufficient for the bridge and optical checks:

```sh
python tools/validate_bridge.py crouch_ready_planted_shared_n_static.glb bridge_reference_samples.json bridge_replayed.json
python tools/verify_optical_proxy.py crouch_ready_planted_shared_n_static.glb optical_proxy_bindings.json optical_replayed.json
python tools/check_animation_glb.py crouch_ready_planted_shared_n_static.glb --expected-animations 1 --max-influences 8
python tools/check_animation_glb.py crouch_ready_planted_shared_n_static.glb --expected-animations 1 --max-influences 4
```

The four-influence command must fail. It is the deliberate guard against losing native weights.

To regenerate the producer's actual Blender source and direct stock reimport capture, obtain the exact pinned editable from the separate source checkpoint, use Blender 4.3.2, and install NumPy/SciPy for the external validation Python:

```sh
blender -b --python tools/capture_parity.py -- SOURCE.blend crouch_ready_planted_shared_n_static.glb WORK_DIRECTORY
python tools/validate_fixture.py crouch_ready_planted_shared_n_static.glb WORK_DIRECTORY validation_repeated.json
```

This is a single-frame static capture at t=0; no duration or pose adapter is used. The packaged native_static_reference.npz and stock_reimport_reference.npz preserve the original captured evidence, with hashes in reference_capture_identity.json. Fresh source capture is a separate validation from replay against those references.

The export can be reconstructed from packaged source curves and the pinned ordinary forward GLB used only as the static geometry/bind template (SHA e44a396d19686b172184e241ed42e52291c96a3e27422770da899d18b5912435):

```sh
python tools/convert_static.py BASELINE_WALK.glb SOURCE_CURVES.json rebuilt_static.glb
```

Matched stills can be regenerated into a separate directory:

```sh
blender -b --threads 4 --python tools/render_matched.py -- source SOURCE.blend VIEW_DIRECTORY
blender -b --threads 4 --python tools/render_matched.py -- stock_reimport crouch_ready_planted_shared_n_static.glb VIEW_DIRECTORY
```

No command saves a changed source Blender file or edits engine code. Consumer must explicitly sample the one-key clip at t=0; its zero span must not be used as a divisor for phase or looping.
