# Independent upper-gear F binary review

PASS for the scoped neutral static pose and three-second bounded articulation proof, against the independently verified final source checkpoint. This is a new 72-bone derivative; it is not an old animation-bank drop-in and is not ADS approval.

- GLB: swat_upper_gear_remake_f_v1.glb, 4,046,700 bytes
- SHA-256: 5dd68cf3010b4ba24ec719fb949e1d8f62053c5761142c37da41079848f00835
- Source checkpoint SHA-256: 1e586e869db06e5e6a0026b5c56860e5e0ed7f2d9bc1ae9bbee0eb8ca1fd3e35

## Independent checks

Read-only source captures and stock Blender 4.3.2 glTF imports were evaluated separately from the producer exporter. The Python validator decodes the actual GLB bytes, checks every accessor's bounds and finiteness, resolves the 72 skin slots by name, evaluates hierarchical quaternion SLERP/TRS transforms, and skins every exported vertex with every stored positive influence. No producer module is imported.

The check covers all 47,599 exported vertices in nine meshes and 14 material primitives, all oriented triangles, polygon material assignments, per-corner UVs and normals, all 72 joint parents and inverse binds. Bind-position/weight matching had zero ambiguous equivalent matches. Original 70 source rest/parent definitions are exact; the binary's inverse-bind identity residual is 2.67e-7. Gear_Elbow_L is node 25 / skin slot 31; Gear_Elbow_R is node 52 / skin slot 58. These node and skin indices must not be interchanged.

Source-to-binary maximum vertex error is 0.000429 mm in neutral and 0.000777 mm over 361 motion samples at 120 Hz, covering every one of the 181 authored knots and every half-key. Maximum carrier matrix residual is 7.54e-7. Binary-to-stock-Blender reimport maximum vertex error is 0.000989 mm over the same samples; normalized all-influence weights and oriented topology remain preserved. This demonstrates sampled interpolation parity, not continuous collision or production runtime acceptance.

All six PBR base colors, metallic factors, roughness factors, opacity and double-sided settings match the source. Polymer and padding roughness is 0.78. Source materials have no linked images; absent binary image payloads are correct. UV maximum component error is 2.99e-8. Normals are near-exact, not bit-identical: source-to-export maximum vector difference is 8.24e-5 (approximately 0.0048 degrees). This is separate from the already documented native-to-F custom-normal change of up to 0.03468 degrees.

## Animation contract

- Neutral Carry / Anatomical Gear F: 216 glTF TRS channels, one STEP key per channel at t=0, zero stored time span and no authored duration
- Upper Gear F / Three-second Carry Articulation: 216 TRS channels, exactly 181 LINEAR keys per channel at the original float32 60 Hz timestamps from 0 to 3 seconds
- Each carrier has translation, rotation and scale channels, preserving the source's 20 scalar carrier curves as six glTF vector channels
- Quaternion norm residual stays below 3.87e-8 and consecutive quaternion dots remain positive
- The original small endpoint difference is retained (maximum joint-matrix component delta 5.51e-6). No exact loop seam, derivative continuity or gameplay looping is claimed
- The stored source ADS action is absent from this binary

## Limits

No engine edits, engine runtime execution, old-bank registration, broad shoulder-support or anatomical-eye/camera calibration are covered. The rigid guards have integrated padding and measured stand-off; this does not establish uniform flush fastening, contact force or load support. Existing finite collision findings apply only to the source's documented sample scope. Captured and reviewed source binaries were never saved or modified.

## Evidence and replay

- SOURCE_CONTRACT_AUDIT.json and SOURCE_CONTRACT_REVIEW.md: original-to-F identity, full geometry/weight/material/UV changes and exporter hazards
- ACTUAL_BINARY_PARITY.json: independent actual-byte replay versus source
- STOCK_REIMPORT_PARITY.json: actual-byte replay versus stock Blender reimport
- MATERIAL_AND_SLOT_CONTRACT.json and ANIMATION_CHANNEL_CONTRACT.json: explicit material/skin-slot and timing/channel checks
- source_capture_compact.json/.npz and reimport_capture_compact.json/.npz: compact portable references with every joint pose, complete bind/skin/topology/UV data, and direct evaluated vertices only at 13 quarter-second controls. The dense references used for the full direct checks above remain outside the portable package.

Run from the directory containing the GLB and independent_validation folder:

```sh
python independent_validation/replay_f.py --glb swat_upper_gear_remake_f_v1.glb --source-capture independent_validation/source_capture_compact --output binary_replay.json
python independent_validation/check_reimport_f.py --glb swat_upper_gear_remake_f_v1.glb --capture independent_validation/reimport_capture_compact --output reimport_replay.json
```

To recapture directly with Blender, pass the source movement blend or delivered GLB to capture_f_blender.py, followed by an output prefix. Capturing the source requires the separately pinned source checkpoint. The script is read-only and does not save scenes. Python-only replay needs NumPy and the supplied compact captures. It reconstructs all-time algebraic skin from captured Blender joint matrices and independently checks direct evaluated controls at t=0, 0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75 and 3 seconds. It does not recreate omitted dense direct evaluated evidence; see COMPACT_REPLAY.md. Final metadata correspondence also passes every retained wearer triangle, original polygon/material group, UV corner, sole-index namespace, unique-vertex support centroid and unbaked heading/ground alignment.
