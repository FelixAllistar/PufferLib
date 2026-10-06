# Portable independent replay

Run from the extracted fixture root. Python 3 and NumPy suffice; these commands do not need Blender, producer modules, SciPy, or the original source archive. The review folder contains the independent reader, explicit normalized-quaternion differential/FK, compact capture references and checked reports.

```sh
python review/replay_unit.py --glb walk_backward_shared_ready_n_c1_loop_a.glb --source-curves SOURCE_CURVES.json --source-analytic-fk SOURCE_ANALYTIC_FK.json --output-dir replayed_independent
python review/inspect_binary.py --package . --output replayed_independent/structure.json
python review/inspect_curve_contract.py --package . --output replayed_independent/curve_contract.json
python review/replay_compact.py --package . --captures review/portable --output replayed_independent/native_reimport.json
python review/verify_metadata.py --package . --captures review/portable --output replayed_independent/metadata.json
```

The first command creates the output folder. All subsequent commands read the pinned actual GLB, source records, metadata and capture files. The original comparison GLB is not required for the normal replay; the independently verified 39-accessor identity is recorded in `structure_validation.json`. An optional `--static-reference` argument to `inspect_binary.py` accepts the separately pinned RIGHT static template for repeating that specific identity check.

`replay_unit.py` reports analytic endpoint residuals, actual skin velocities, finite-epsilon convergence and 961-phase source scalar/FK parity. It applies normalized quaternion differentiation and all stored weights. Practical 60 Hz chords remain distinct from analytic derivative continuity.

`replay_compact.py` verifies the capture file hashes, compares every skin vertex using all 151 native/reimported bone/deformation poses, and checks nine complete direct Blender evaluated surface controls. This is algebraic full-phase skin coverage anchored by direct full-surface controls. The separate fresh direct 151-phase vertex result is retained in `whole_vertex_validation.json`; its large raw surface caches are not bundled. The stock-import discrepancy remains visible.

`verify_metadata.py` independently checks empty events, one-second timing, the source travel vector and pace, actual B node/slot and zero scale, unique native sole-support remaps and centroids, identity root, unbaked preview alignment and rifle elevation. `inspect_binary.py` additionally checks bounds/types/strides, all influences, node/slot mappings, the rifle inverse-bind bridge and optic lens remap. `inspect_curve_contract.py` checks all saved window keys and every quaternion polynomial interval.

Source geometry, full serialized source keys and nine-phase garment reports were independently verified with Blender 4.3.2. Those source-authoring checks remain pinned evidence; they do not claim engine or gameplay acceptance. Regenerating source captures requires the separately supplied exact source checkpoint. See the fixture's main `REPRODUCE.md` for producer native/reimport regeneration commands.
