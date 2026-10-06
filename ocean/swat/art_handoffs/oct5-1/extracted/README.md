# Walk Right / Shared Ready N C1 Loop A

This private review fixture converts the sealed one-second WALK RIGHT source to explicit cubic glTF animation. It qualifies numerical export fidelity, loop continuity within measured residuals, and the retained Shared Ready N carry. **Visible inherited opposing upper-thigh strap/pants intersections remain, and the boundary repair adds five small cross-leg pair identities. The source is not interlimb-clear or zero material regression.**

- GLB: `walk_right_shared_ready_n_c1_loop_a.glb`, 5,730,956 bytes
- SHA256: `edcb227870a0483d06c99e89df491e24f6eb22c879e35833e597f3d6a196b899`
- Exact animation name: `Walk Right / Shared Ready N C1 Loop A`
- Clip ID: `walk_right_shared_ready_n_c1_loop_a_original_1s`
- Saved source: frames 0–30 at 30 fps, duration 1.0 s; original human source 60 Hz
- Source editable SHA256: `f85525ba445013f73884677bd8202d7ffff226b04fedee930250c90cbe402770`
- Source checkpoint SHA256: `93a8ec6e810c9d161c16cc90e5842952a07311a1dd5f9ab4e8a8ef669ef32180`

## Timing, direction and source qualification

The repair is confined to 0–0.125 s and 0.875–1 s. RIGHT changes **661 scalar curves**, not LEFT's 657. `SOURCE_SAVED_CURVE_DIFF.json` records the exact changes; `SOURCE_CURVES.json` contains all 700 curves / 159,854 keys. Original center keys are retained, with measured floating-point evaluation differences disclosed separately. Source geometry, binds, UVs, complete weights and original body/spine/gaze timing are retained.

The fresh FBX measurement records native displacement (−1.921261549, −0.000009783, −0.000000477) m and horizontal pace **1.92126154902 m/s**. The GLB is in-place; that displacement was removed before export. It is source provenance, not animated root motion or a selected gameplay speed. No runtime-speed retiming is applied.

RIGHT has its own body angles. Native chest-material forward starts (−0.424753, −0.905309, 0), pelvis-material forward (−0.754573, −0.656216, 0). Angles to source stride span chest 56.712–69.482° and pelvis 20.273–51.191°. Asymmetric posed shoulder lines are only a secondary heading diagnostic. `measured_preview_fit.json` keeps native travel separate from the unbaked rifle-facing preview alignment. That transform maps the travel reference to approximately (+0.522702, 0, +1.848791) m in engine axes; a pure-cardinal heading or mirrored LEFT pose is not imposed.

Measured rifle elevation is **-2.295297° to 4.633812°** over the sampled cycle. The source retains coupled shouldered carry and fitted grips. No ADS, eye/sight fit, blend-space behavior or achieved-gameplay-pose approval is implied.

Source review at 960 Hz finds left sole minimum 3.954407 mm and right minimum **2.931984 mm**; 17 of 961 right-foot samples fall below 3 mm. The earlier 4 mm clearance target is not maintained everywhere. There is no new ground solve or planted-foot lock. Our broader native capture agrees within approximately 0.01 micrometres, with a separately documented weight-based foot subset.

Native sleeve-root/vest folds, stretched connecting sleeve panels and thin/flattened elbow links remain. Bounded actual arm-versus-vest and rifle-versus-nonhand gates pass, but whole garment-root crossings persist. The lower material review finds up to 98 cross-leg tagged triangle pairs in a phase; baseline/candidate pair unions are 285/270. Candidate maximum inherited chord is 21.139238 mm. Five added cross-leg pairs reach 0.655282 mm and one added same-side hip/strap pair reaches 0.050905 mm. Chord length and summed chord length are not penetration depth. Decreases in some pair counts do not establish zero regression. `SOURCE_FINAL_STATUS.json`, `SOURCE_INDEPENDENT_REVIEW.md`, `source_opposing_thigh_straps.png` and `source_matched_thigh_contact.png` preserve this qualification and evidence. Those closeups are source-review images, not engine renders.

## Actual serialized cubic checks

All 210 TRS channels use CUBICSPLINE. Independent byte evaluation found exactly equal endpoint positions for all **48,755 exported vertices**, using every skin influence. Maximum endpoint joint-world matrix element difference across all 70 joints is **2.32813954e-10**; this is not an exactly-zero matrix claim.

Worst analytic joint velocity residual is **6.85526359e-05 m/s** at mixamorig:LeftToe_End and **0.00486510319°/s** at mixamorig:LeftLeg. Body surface residual is **6.92320433e-05 m/s**, rifle **5.7684388e-06 m/s**. These are measured float32-storage residuals, not exact-arithmetic C1. Collapsed Prop_Magazine_B has undefined angular velocity; it is excluded only from angular extrema and remains included in positional/skinning checks.

`EXPORTED_CUBIC_VALIDATION.json` includes analytic endpoints, full-influence skinning, practical 60/240/480/960 Hz finite chords and shrinking-epsilon convergence. Finite chords include boundary acceleration and differ from the mathematical endpoint derivative. `finite_difference_convergence.png` displays both quantities. Tiny-epsilon results use float64 evaluation of stored float32 data; runtime float32 differentiation is not certified.

Independent saved-curve comparison covers 961 phases, including 721 middle-75% phases. Middle body error is **0.373070 micrometres**. Sampled middle joint derivative differences reach **0.000104443858 m/s** and **0.0114245765°/s**. Exported interior polynomials are not algebraically identical to the source.

Separate all-vertex Blender checks cover **1,243 poses**: complete 960 Hz grid, every source/export key, boundary probes and finite-epsilon pairs, plus 16 actual evaluated-mesh controls. Maximum native-to-GLB error is **4.635513 micrometres**; explicit cubic-aware reimport is **4.775055 micrometres**. `NATIVE_STATIC_PRESERVATION.json` confirms exact native bind geometry, oriented triangles, raw weights, rest matrices and parents before reusing the static GLB template.

**Stock Blender 4.3.2 import discards cubic tangents and substitutes automatic handles.** Its measured all-vertex error is **2.990688 mm**, so that round-trip is not qualified. The separate cubic-aware check explicitly drives Blender-imported geometry and bind skeleton with the serialized Hermite sampler. `native_phase000/950.png` and `cubic_reimport_phase000/950.png` are matched neutral views; `stock_reimport_phase000/950.png` are importer diagnostics. Static views do not establish velocity. The included authoring comparison video is source playback, not GLB or engine playback.

## Conversion contract

The source has 39,660 BEZIER spans and 119,494 LINEAR spans, 661 mixed curves and 39 entirely LINEAR curves. Linear spans are encoded with their secant derivatives. Hips.scale has different component grids [61,107,107], so it uses their knot union. All other vector groups have shared component grids. `conversion_report.json` records every channel and timestamp change.

Literal lossless conversion is not claimed. Stored Bezier time handles differ from exact thirds by up to 6.35782879e-07 frame; 15 interior probes per cubic span found scalar Hermite approximation error up to 8.0062772e-09. Raw quaternion key norm deviation reaches 3.35616532e-05. The chosen conversion writes unit quaternion keys, projects raw derivatives through quaternion normalization, then applies the fixed rest-frame transform. Maximum serialized key norm residual is 4.04016374e-08. Numerical interior differences are measured above.

Tangents are derivatives per second. Evaluate each component Hermite polynomial with span duration applied once to the tangent terms, then normalize the rotation result, including exact key evaluation. Preserve separate incoming/outgoing tangents. Do not normalize tangent vectors, substitute slerp/LINEAR, or flip quaternion signs independently. Unused first in-tangent and last out-tangent are zero. The zero-scale spare must stay collapsed. Refer to `source_representability.json` and the [glTF interpolation specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).

## Skin, bindings, props and materials

All seven influences are retained, including JOINTS_1/WEIGHTS_1. The body has 444 exported seam-split vertices above four influences. Normalize combined sets, without truncation. All rendered bind/weight vertices and oriented triangles are preserved; the same six unused rifle vertices are omitted as in the static template. Eight-influence preflight passes; the four-influence guard deliberately rejects.

`bindings.json` maps all joints, skin slots, parents and inverse binds. Units are metres; Blender (x,y,z) becomes glTF (x,z,−y), with +Y up. Engine reference axes are +X forward/+Y up/+Z right. Root, basis and sole values are measured preview candidates, not controller calibration. Root remains identity. Sole indices explicitly identify the original 24,850-vertex Blender body; actual GLB primitive/vertex matches are included separately. Weight-based native foot subsets have 654/668 vertices, exported seam-split subsets 899/916; the source owner's component-defined foot subsets are different and labeled in its own report.

The physical rifle bridge is `model_from_rigid = W_node * IBM[resolved_slot] * B_bind_mesh`, equivalently `W_node * J_joint_local` with `J = IBM * B`. W_node means the actual global scene-node transform before inverse bind. Resolve each node through skin.joints; node and slot are different namespaces even when Prop_Rifle currently maps 67→67. Apply external actor/preview placement once. Both representations, dense native reference samples and measured stock/sight/muzzle/grip points are included. Legacy RifleSocket/Muzzle helpers are not calibrated physical sockets.

The goggle-surface proxy follows the same corrected attachment spaces: Head node1, skin slot5. It is the virtual midpoint of actual lens-surface vertices, not an anatomical eye, IPD, gaze, lens optics, optical nodal point or camera origin. Eye/camera calibration is unresolved. The portable replay checks 16 independently Blender-evaluated control poses; the dense binding report checks the full 1,243-pose capture separately.

Installed magazine A follows the rifle; unused Fresh magazine B stays collapsed throughout. No active game inventory owner is supplied for B. There are no authored footstep, fire, reload or ownership-transfer events. Loop wrap keeps weapon/magazine identities; gameplay owns magazine_seated, ammo/chamber/inventory, replay and save/restore state.

Body position/normal/UV/index arrays remain exactly compatible with the delivered nine-map 2K texture package. `material_compatibility.json` gives actual mesh/primitive/material assignments. No textures or tangents are fabricated here. The preserved source normal graph uses unchanged green through a tangent Normal Map, while the original texture-authoring convention remains unproven. Runtime tangent generation and appearance remain consumer work.

This fixture does not certify animated normals, a full Khronos validator run, engine/GPU integration, gameplay, ADS or artistic acceptance. `REPRODUCE.md` supplies portable independent replay and native/reimport commands; `manifest.json` pins every file. Prior source/LEFT/forward/F/Ready/rigid packages remain unchanged. This is private licensed-source material with no authorization for public redistribution.
