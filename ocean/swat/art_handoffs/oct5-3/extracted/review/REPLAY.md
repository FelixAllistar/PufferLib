# Portable independent replay

From the extracted package root, with Python 3 and NumPy available:

```sh
python review/replay_unit.py --glb crouch_forward_shared_n_c1_loop_a.glb --source-curves SOURCE_CURVES.json --source-analytic-fk SOURCE_ANALYTIC_FK.json --output-dir replay-results
python review/inspect_binary.py --package . --output replay-results/structure_validation.json
python review/inspect_curve_contract.py --package . --output replay-results/curve_contract_validation.json
```

These commands decode the actual GLB bytes and compare their normalized-quaternion, scale-aware FK and complete skin to the bundled exact native scalar curves. They do not import producer converter/evaluator code and do not require Blender, the original workspace or the engine checkout. The structure check includes the visible spare B and Hips sleeve, complete mappings, rifle bridge and Head optical surface proxy.

The optional `--source-blend` argument to `inspect_curve_contract.py` directly verifies a separately obtained native source byte pin. The optional `--static-reference` argument to `inspect_binary.py` checks exact static accessor identity to a separately obtained pinned ordinary-forward GLB.

Full native capture comparison also accepts explicit portable input paths:

```sh
python review/inspect_native_controls.py --package . --capture-dir /path/to/captured-native-data --output-dir replay-results
```

That additional input directory contains `source.npz`, `sample_times.npy` and `capture_report.json`; its source NPZ hash is recorded in `native_control_validation.json`. It is separate from the three self-contained core replay commands above. Do not treat absent captures as a successfully rerun direct Blender comparison.

Reproduction reports numerical fidelity only. Keep the native 18.392 mm sampled sleeve/thigh inward-distance limitation and visible spare state. Passing replay does not establish collision clearance, runtime support, ADS or camera fit.
