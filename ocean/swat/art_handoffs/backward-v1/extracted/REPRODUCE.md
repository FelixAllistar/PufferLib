# Reproducing the cubic fixture checks

Run from the extracted package directory. Commands read the GLB and write only explicitly named reports/work directories. Python with NumPy is sufficient for the portable bridge and optical checks:

```
python tools/validate_bridge.py walk_backward_shared_ready_n_c1_loop_a.glb bridge_reference_samples.json bridge_replayed.json
python tools/verify_optical_proxy.py walk_backward_shared_ready_n_c1_loop_a.glb optical_proxy_bindings.json optical_replayed.json
python tools/check_animation_glb.py walk_backward_shared_ready_n_c1_loop_a.glb --expected-animations 1 --max-influences 8
python tools/check_animation_glb.py walk_backward_shared_ready_n_c1_loop_a.glb --expected-animations 1 --max-influences 4
```

The last command must fail because the body requires seven influences. This preflight is structural; actual cubic math, seam and deformation validation are separate.

The independent endpoint/finite-epsilon/full-influence replay uses the delivered GLB plus saved scalar curves and source analytic FK:

```
python tools/replay_cubic_validation.py --glb walk_backward_shared_ready_n_c1_loop_a.glb --source-curves SOURCE_CURVES.json --source-analytic-fk SOURCE_ANALYTIC_FK.json --output-dir replayed_cubic_checks
```

This replays serialized cubic geometry/velocity checks; it does not run Blender or claim engine acceptance. See the script's help/output for its exact scope.

For the full native/reimport check, obtain the pinned editable source from the separately delivered source checkpoint, install NumPy/SciPy for the external validation Python, and use Blender4.3.2:

```
blender -b --python tools/capture_parity.py -- SOURCE.blend walk_backward_shared_ready_n_c1_loop_a.glb WORK_DIRECTORY
python tools/validate_fixture.py walk_backward_shared_ready_n_c1_loop_a.glb WORK_DIRECTORY validation_repeated.json
```

The capture records stock import first, then explicitly drives the imported bind skeleton with the actual cubic sampler. Stock Blender drops the source tangents; its failure is retained in the report rather than hidden.

To reconstruct the GLB conversion itself, use the existing pinned RIGHT GLB used only as static geometry/bind template with SHA edcb227870a0483d06c99e89df491e24f6eb22c879e35833e597f3d6a196b899 as the static geometry/bind template:

```
python tools/convert_cubic.py BASELINE_WALK.glb SOURCE_CURVES.json rebuilt_walk_backward.glb unit
```

That command copies static geometry/binds and derives all animation accessors from the saved scalar data. It does not invoke Blender's mixed-interpolation exporter. The separate source checkpoint and SOURCE_PROVENANCE.json identify exact sources. The raw-policy probe is optional diagnostic work; this backward package delivers the unit policy.

Matched views can be regenerated into a separate directory:

```
blender -b --threads 4 --python tools/render_matched.py -- source SOURCE.blend VIEW_DIRECTORY
blender -b --threads 4 --python tools/render_matched.py -- cubic_reimport walk_backward_shared_ready_n_c1_loop_a.glb VIEW_DIRECTORY
blender -b --threads 4 --python tools/render_matched.py -- stock_reimport walk_backward_shared_ready_n_c1_loop_a.glb VIEW_DIRECTORY
```

No provided command edits the pinned source Blender file, previous GLBs or engine files.

Additional portable structure/attachment and saved-key/quaternion-norm checks use only the extracted files and NumPy:

```
python review/inspect_binary.py --package . --output structure_replayed.json
python review/inspect_curve_contract.py --package . --output curve_contract_replayed.json
```

The native-control audit report additionally used the separately recreated Blender parity captures. Those producer cache NPZs are not embedded; recreate them from the pinned editable with capture_parity.py above. Separately, review/portable contains the independent compact references: all151 captured native/reimport matrix poses plus nine complete direct surface controls. See review/REPLAY.md for its self-contained replay and the explicit distinction between algebraic skin comparison and direct controls. The source garment collision reports are preserved authoring review evidence, not re-run collision tests or runtime claims.
