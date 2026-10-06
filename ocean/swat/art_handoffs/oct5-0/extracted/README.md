# Walk Left / Shared Ready N C1 Loop A

This private review fixture contains the exact one-second WALK LEFT source, converted to explicit cubic glTF animation with measured numerical tolerances. It is a separate artifact from the forward walk; RIGHT is not included or inferred by symmetry.

- GLB: `walk_left_shared_ready_n_c1_loop_a.glb`, 5,730,956 bytes
- SHA256: `4fe97ec1997bb4799440216ddc3a74d858154c0d8e51bf863e2d86082b1a1996`
- Exact animation name: `Walk Left / Shared Ready N C1 Loop A`
- Clip ID: `walk_left_shared_ready_n_c1_loop_a_original_1s`
- Saved source: frames 0–30 at 30 fps, duration 1.0 s; original human source 60 Hz
- Source editable SHA256: `6737a18666f08d89ea097ed304e17ed47095ffb28f87f5324d32b34e416bd173`

## Timing, direction and art scope

The authoring repair is confined to 0–0.125 s and 0.875–1 s. The native-carry baseline preserves the original body/spine/gaze timing. The final source changes 657 curves in those boundary windows, with central key values retained and measured floating-point evaluation differences disclosed. `SOURCE_SAVED_CURVE_DIFF.json` records every changed key/handle/interpolation; `SOURCE_CURVES.json` contains all 700 curves / 159,716 keys used for conversion.

The fresh FBX measurement in `NATIVE_SOURCE_MEASUREMENT.json` records native displacement (+1.921261907, +0.000009646, +0.000000298) m and horizontal pace **1.92126190665 m/s**. The GLB is in-place; this original displacement was removed before export. It is source provenance, not an animated root displacement or a chosen gameplay speed.

The original chest and pelvis counter-rotate relative to travel. The posed shoulder line is a biased heading diagnostic. `measured_preview_fit.json` therefore distinguishes native travel from its unbaked, rifle-facing preview alignment. That preview transform maps the original travel reference to approximately (−0.21670, 0, −1.90900) m in engine axes. No extra yaw correction, timing change or pure-cardinal movement assumption has been applied.

Measured rifle elevation over this clip is **−4.208° to +1.325°**. The source keeps its coupled shouldered carry and fitted grip. It is not an ADS or achieved-gameplay-pose approval.

The native source review finds left sole minimum 3.960442 mm and right minimum **2.875747 mm** at 960 Hz. The right sole falls below the earlier 3 mm level at 23 sampled phases; the 4 mm authoring target is not maintained everywhere. This is not a new ground solve or foot-lock guarantee. Native stretched sleeve/connecting panels, flattened elbow link and garment-root folds remain. Eight new sampled triangle-pair identities are confined to inherited left hip/pants/strap material families; the bounded source review found no new interleg triangle crossings. Chord length is not penetration depth. Read `SOURCE_QUALITY.md` and `SOURCE_INDEPENDENT_REVIEW.md` for scope and limits.

## Actual serialized cubic checks

All **210 TRS channels use CUBICSPLINE**. Independent byte evaluation found exactly equal endpoint matrices for all 70 joints and exactly equal positions for all 48,755 exported vertices under float64 evaluation of the stored float32 data.

The worst analytic joint velocity residual is **0.0000686182 m/s** at LeftToeBase and **0.00523484°/s** at LeftLeg. Full-body surface residual is **0.0000702193 m/s**; rifle surface residual is **0.00000832532 m/s**, using every influence. These are small measured residuals, not literal exact-arithmetic C1 continuity. The zero-scale Prop_Magazine_B has undefined angular velocity and is excluded only from angular extrema.

`EXPORTED_CUBIC_VALIDATION.json` includes analytic endpoints, all-influence skinning, practical 60/240/480/960 Hz finite chords and shrinking-epsilon convergence. Finite chords include boundary acceleration and must not be mistaken for the mathematical one-sided derivative. `finite_difference_convergence.png` visualizes these separate quantities. Tiny-epsilon results use float64 evaluation; runtime float32 differentiation is not certified.

Against independently evaluated saved scalar curves, the middle-75% body error is **0.3872 micrometres** over 721 middle samples within the 961-phase full comparison. Sampled middle joint derivative differences reach **0.000108108 m/s** and **0.0191842°/s**. This is measured curve preservation, not algebraically identical polynomial coefficients.

The separate all-vertex Blender check covers **1,243 poses**: complete 960 Hz grid, every source/export key, boundary and finite-epsilon probes, plus 16 actual evaluated-mesh controls. Maximum native-to-GLB error is **4.675 micrometres**; explicit cubic-aware reimport is **4.576 micrometres**. `NATIVE_STATIC_PRESERVATION.json` confirms exact native bind geometry, topology, raw weights, rest matrices and parents before reusing the existing static GLB template.

**Stock Blender 4.3.2 import discards cubic tangents and substitutes automatic handles.** Its measured body error is **3.031 mm**. That round-trip is not qualified. The separate cubic-aware check explicitly drives the imported geometry and bind skeleton using the serialized Hermite sampler. `native_phase000/950.png` and `cubic_reimport_phase000/950.png` are matched neutral views; `stock_reimport_phase000/950.png` are diagnostics. Static views do not prove velocity. The included source comparison video is not GLB or engine playback.

## Conversion contract

There are 39,420 native BEZIER spans and 119,596 LINEAR spans. The same sampler can preserve both by expressing the linear spans with their secant derivatives. The three differing scalar component grids, Hips.scale, Spine.scale and Spine1.scale, use their respective knot unions. Every channel change and timestamp quantization is recorded in `conversion_report.json`.

Literal lossless conversion is not claimed: saved Bezier time handles differ from exact thirds by up to 6.358e-7 frame. At 15 probes per cubic span, endpoint-slope Hermite differed by up to 3.363e-8 scalar units. Raw source quaternion key norms differ from unity by up to 3.3054e-5. The selected conversion writes unit quaternion keys, projects derivatives through normalization, and applies the fixed rest-frame transform. Maximum serialized key norm residual is 4.485e-8. These choices preserve physical endpoint derivatives while introducing the measured interior differences above.

Tangents are derivatives **per second**, not per frame. Evaluate the component Hermite polynomial with each span's duration multiplying its tangent terms, then normalize the rotation result. Preserve independent incoming/outgoing tangents. Do not normalize tangent vectors, substitute slerp/LINEAR, or introduce independent sign flips. The unused first in-tangent and last out-tangent are zero. See `source_representability.json`, `conversion_report.json`, and the [glTF interpolation specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).

## Skin, bindings, props and materials

The full body retains all seven influences, including JOINTS_1/WEIGHTS_1. There are 444 seam-split exported body vertices above four influences. Normalize across all combined sets; truncation is invalid. All rendered bind/weight vertices and oriented triangles are retained; six unused rifle vertices are omitted as in the static template. Eight-influence preflight passes; four-influence preflight deliberately rejects.

`bindings.json` maps all joints, skin slots, parents and inverse binds, plus the root and prop bindings. Body mesh references are in the preview and optical metadata. Units are metres; Blender (x,y,z) becomes glTF (x,z,−y), with +Y up. The engine reference basis is +X forward / +Y up / +Z right. Root/basis/sole data are measured preview candidates, not controller calibration. Sole indices explicitly identify their native Blender vertex namespace; resolved GLB primitive/vertex matches are supplied separately. Native foot subsets contain 654/668 vertices; exported seam-split subsets contain 899/916.

The calibrated rifle bridge is `model_from_rigid = W_node * IBM[resolved_slot] * B_bind_mesh`, equivalently `W_node * J_joint_local` with `J_joint_local = IBM * B`. W_node is the actual scene-node world matrix before inverse bind; apply external model placement once. Resolve a node through skin.joints rather than assuming node and slot indices coincide. Both representations, physical stock/sight/muzzle/grip points and dense bridge validation are included. Legacy RifleSocket/Muzzle helpers remain uncalibrated markers.

The goggle-surface proxy uses the same corrected spaces. Head is node 1, skin slot 5. It is the virtual midpoint of actual lens-surface vertices, not an anatomical eye, IPD, gaze, optical nodal point or camera origin. Eye/camera alignment remains unresolved.

Installed magazine A follows the rifle. Exported Fresh magazine B remains collapsed by its original zero-scale channel. No active game inventory owner is supplied for B. There are no authored footstep, reload, fire or ownership-transfer events. Cycle wrap preserves identities; gameplay owns magazine_seated, ammo/chamber/inventory, replay and save/restore state.

Body position/normal/UV/index arrays remain exactly compatible with the previously delivered nine-map 2K texture package; `material_compatibility.json` gives actual current assignments. No images or tangents are fabricated here. This fixture does not claim animated-normal certification, a full Khronos validator run, engine/GPU integration or gameplay/ADS approval.

`REPRODUCE.md` provides portable independent replay and full native/reimport commands. All files are pinned by `manifest.json`. Source and previous F/Ready/forward/rigid archives remain unchanged. This is private licensed-source material; public redistribution is not authorized.
