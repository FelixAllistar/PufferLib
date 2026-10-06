# Replay the upper-gear F fixture

From the extracted package, Python 3 and NumPy are sufficient for the independent compact replay:

```sh
python independent_validation/replay_f.py --glb swat_upper_gear_remake_f_v1.glb --source-capture independent_validation/source_capture_compact --output binary_replay.json
python independent_validation/check_reimport_f.py --glb swat_upper_gear_remake_f_v1.glb --capture independent_validation/reimport_capture_compact --output reimport_replay.json
python independent_validation/check_metadata_f.py --package . --source-capture independent_validation/source_capture_compact --output metadata_replay.json
python tools/check_animation_glb.py swat_upper_gear_remake_f_v1.glb --expected-animations 2 --max-influences 8
python tools/check_animation_glb.py swat_upper_gear_remake_f_v1.glb --expected-animations 2 --max-influences 4
```

The four-influence command must fail. Compact replay uses all 361 captured joint poses and full bind/weight geometry plus direct evaluated controls at 0, 0.25, …, 3 seconds. Its report labels algebraic source-skin reconstruction separately from those controls and from the already-recorded dense evaluated-vertex audit.

For fresh independent Blender captures, obtain the separately pinned F source checkpoint and use the provided `independent_validation/capture_f_blender.py`; its CLI accepts the source movement `.blend` or the delivered GLB and an output prefix. These captures can be large. The script does not save either opened scene. The independent review gives exact reference hashes and supported invocation.

Producer scripts are in `tools/`. Geometry export reads the pinned static source and selects only the rig and nine character meshes. Native capture records the exact static action and movement key/half-key samples. `build_animations.py` bakes evaluated worlds against resolved exported inverse binds and parent worlds, explicitly handling the two carriers' inherit-scale NONE. It requires NumPy/SciPy and the documented `work/` captures; it does not overwrite source Blender files. No command edits engine code or activates an old animation bank.

For a separate producer rebuild, create a new working directory, copy the scripts beside its `work` and `package` directories, and substitute the exact static and movement source paths from the pinned checkpoint:

```sh
mkdir -p rebuild/work rebuild/package
cp tools/*.py rebuild/
blender -b --python rebuild/export_bind_geometry.py -- STATIC_SOURCE.blend rebuild/work/gear_f_bind.glb
blender -b --python rebuild/capture_native.py -- STATIC_SOURCE.blend 'Neutral Carry / Anatomical Gear F' rebuild/work/native_static static
blender -b --python rebuild/capture_native.py -- MOVEMENT_SOURCE.blend 'Upper Gear F / Three-second Carry Articulation' rebuild/work/native_movement movement
python rebuild/build_animations.py
```

Use Blender 4.3.2 and the pinned sources. The output is `rebuild/package/swat_upper_gear_remake_f_v1.glb`. Re-run numerical parity before accepting a rebuilt binary; platform/exporter byte identity is not promised.
