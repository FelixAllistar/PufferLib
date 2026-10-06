# Walk Backward / Shared Ready N C1 Loop A

This private fixture preserves the authored backward gait and original N carry in a separate 70-bone cubic animation. It does not adopt the authored grip C/C1 diagnostics or an engine hand override. **C1 in the action name means loop continuity.** Existing cloth/strap contacts and measured material regressions remain qualified; this is not a material-clean, zero-regression, ADS or gameplay-speed-fit approval.

- GLB: `walk_backward_shared_ready_n_c1_loop_a.glb`, 5,847,176 bytes
- SHA256: `5769ec1b03a63e3d363ba360c66edd31784225e6ca49be7b2177980ba84170de`
- Exact animation: `Walk Backward / Shared Ready N C1 Loop A`
- Clip ID: `walk_backward_shared_ready_n_c1_loop_a_original_1s`
- Duration: exactly 1 second, saved frames 0–30 at 30 fps; original human take 60 Hz
- Editable SHA256: `91cc7919e4eaba63a8e6ddd658448bb7be0d2ac406738d22c9a0641d22e135e5`
- Source checkpoint: 19,285,530 bytes, SHA256 `a2916151e218b3b0b7ba002582f2ae37f039df122c77c74c996e93ee43452a03`

## Source intent, grounding and bounded repair

The native one-second gait travels approximately +Y 1.921261 m in Blender world, with its own oblique torso, gaze and bent-knee posture. Nominal handoff pace is 1.921261449 m/s; a fresh independent endpoint measurement gives approximately 1.921261410 m/s. These tiny float-evaluation differences are documented source measurements, not retiming. This GLB is in place: initial XY and linear cycle XY are removed while authored Hips Z remains. Z=0 in the removal vector means no vertical removal, not zero source vertical motion.

Native orientation proxies span hips yaw −48.53° to −27.01°, chest yaw −34.46° to −23.80°, head yaw −13.27° to −12.09°, and head-forward elevation −16.87° to −14.44°. Those are source-rest orientation proxies, not camera or optical axes. The natural raised Ready rifle is approximately **14.947–17.206° upward**. No extra facing correction or slower neutral retreat is substituted.

Before seam repair, source chest/gaze and foot/toe world rotations are preserved to the documented sampled tolerances. Grounding lifts ankles vertically by up to 16.541 mm; it changes knee positions and thigh/shin angles while retaining leg lengths and the source knee-plane convention. Between source keys, small interpolation residuals remain. Grounding is a deliberate lower-body change, not exact full-body pose preservation.

The final source has **700 curves / 168,700 keys**. Exactly **604 curves** change only within 0–0.125 s and 0.875–1 s. Source middle-75% curves, dense world matrices and checked center surfaces remain exact relative to the grounded carry baseline. `SOURCE_SAVED_CURVE_DIFF.json` contains all saved key/handle/interpolation edits. The independent source review separates original motion, grounding, then seam changes.

The bounded seam introduces ankle XY changes up to 8.001 mm left / 1.496 mm right and foot-surface departures up to 17.886 / 2.108 mm from the grounded baseline. Foot/toe orientations and torso/gaze also change within those windows as documented. These are not claimed to remain ankle-only vertical adjustments. Source 960 Hz sole minima are **3.685705 mm left / 3.371753 mm right**; no sampled boot is below 3 mm. The nominal 4 mm target is not exact everywhere, and dense sampling is not continuous floor proof.

## Actual binary continuity and parity

All 210 TRS channels use explicit CUBICSPLINE with 241 keys. Independent actual-byte evaluation finds equal joint and full-skin endpoint poses. Maximum analytic joint wrap residual is **0.00006622555 m/s** and **0.005045901°/s**; body surface residual is **0.00006759584 m/s**, with all seven influences included. Collapsed B has undefined angular velocity and is excluded only from angular extrema.

Practical finite wrap chords are different quantities: at 1/60 s the incoming/outgoing difference reaches approximately 0.4394 m/s for joints, 0.4557 m/s for body skin, and 36.04°/s angular. They include finite-time acceleration and converge toward the small analytic residual as epsilon shrinks. `EXPORTED_CUBIC_VALIDATION.json` and `finite_difference_convergence.png` preserve both measurements. Runtime float32 finite differentiation is not certified by the float64 replay.

Independent saved-scalar comparison covers 961 phases, including 721 middle samples. Maximum middle body error is approximately 0.357528 micrometres. Sampled middle derivative differences reach 0.0000952021 m/s and 0.01062353°/s. Exported polynomial coefficients are not described as algebraically identical to the source.

The separate full native/GLB/reimport check covers **1,243 poses**, including every source/export key, the complete 960 Hz grid, boundary and epsilon probes, plus 16 actual Blender-evaluated controls. Maximum native-to-GLB vertex error is **4.514796 micrometres**; explicit cubic-aware reimport is **4.666334 micrometres**. All rendered bind/weight vertices and oriented triangles are preserved. Source rest matrices, hierarchy, raw weights and geometry exactly match the verified original N70 template.

**Stock Blender 4.3.2 import discards cubic tangents.** Its separate maximum vertex error is **2.915087 mm**, so that stock round-trip is not qualified. The successful reimport check drives imported geometry and bind skeleton using the actual serialized cubic sampler. Matched native/cubic-reimport phase 0 and phase 0.95 images were inspected; stock images are diagnostics. Static images do not establish runtime playback or velocity continuity.

## Interpolation and skin contract

The source contains 36,240 BEZIER spans and 131,760 LINEAR spans. All scalar component grids match. Linear spans are represented with their secant derivatives; cubic endpoints use the stored incoming/outgoing slopes in seconds. Saved Bezier time handles differ slightly from exact thirds; 15 interior probes per cubic span found scalar Hermite approximation error up to 4.739e-9. Source quaternion key norm deviation is approximately 7.918e-8. The conversion normalizes keys and projects derivatives through normalization, with measured float32/rest-frame error reported separately.

Evaluate the component Hermite polynomial, multiply each span's tangent terms by its duration once, then normalize the rotation result, including key evaluation. Keep separate incoming/outgoing tangents. Do not normalize tangent vectors, substitute SLERP/LINEAR, independently flip quaternion signs or truncate weights. Unused first incoming/last outgoing tangents are zero. `conversion_report.json` and `source_representability.json` disclose every channel and conversion limit.

The unchanged body has up to seven influences, including JOINTS_1/WEIGHTS_1. There are 444 exported seam-split body vertices above four influences. Normalize combined sets. Eight-influence preflight passes and four-influence preflight deliberately rejects. The same six unreferenced rifle vertices are omitted as in the static template; every rendered triangle remains. Normal/UV/material arrays retain the original body texture-group compatibility in `material_compatibility.json`; no texture, tangent or new PBR semantics is fabricated.

## Garment, sleeve and grip qualification

Source mapped onto the N mesh already has crowded pants/strap contacts. Grounding and seam repair change local identities and extent. Across the source review's 109 phases, baseline/A cross-leg pair unions are 1,312/1,229; A adds 49 pair identities in existing pants/upper-thigh strap families plus three right-leg/torso pairs. The largest newly observed crossing chord is 20.704 mm at 0.066667 s. Some family totals decrease while others increase. Crossing chord and chord sum are intersection-segment measurements, not penetration depths; reduced totals do not imply zero regression.

Whole sleeve-root/vest crossings remain 52 left / 63 right. Existing folds, stretched connecting panel and thin/flattened elbow link remain. Arm-dominant/vest, rifle/nonhand and tested connector gates pass only their documented sample scope. Original N glove/rifle relationships remain; there is no new hand fit, extra inertial lag or grip-diagnostic adoption. Physical stock-center backing is about 2.191711–2.192493 mm, with 24/29 central patch samples within 5 mm; the whole pad is not claimed flush.

Prop_Magazine_B remains explicitly zero-scale, inherited from upright compatibility. The separate 48-vertex Hips-weighted spare sleeve stays visible in the source and oracle. Its same 28 crossing pairs against body component 64 remain, with no sampled thigh crossings or new seam pairs. This visible sleeve is separate from the collapsed magazine. Engine visibility or grip policies remain independently owned by the consumer.

The file `source_F72_compatibility_preview.mp4` is the separately labeled authoring compatibility preview: it uses existing F72 geometry with preview-only carrier reconstruction, not this GLB’s N70 geometry. Its four seconds show four original-speed repetitions of the one-second source. `SOURCE_F72_COMPATIBILITY_VIDEO.json` pins its provenance. The paired native/cubic-reimport PNGs instead show the actual N70 export geometry. The 72-bone F gear appears only in that compatibility video. It is not included in this GLB, and no F carrier channels or F geometry are silently imported. The source/export remains the original N70 contract. Prior source banks, gear revision and grip C/C1 diagnostics remain unchanged.

## Bindings, fit and events

Units are metres. Blender (x,y,z) becomes glTF (x,z,−y), with +Y up; engine reference axes are +X forward/+Y up/+Z right. `bindings.json` resolves every actual scene node, skin slot, parent and inverse bind. `measured_preview_fit.json` supplies an unbaked rifle-facing preview transform, identity root and measured source-native sole support maps. Original native vertex indices are not exported seam indices; actual primitive/vertex correspondences are included. Support centroids use unique native vertices.

Rifle and optical attachments use **Wnode × IBM[resolved slot] × B_bind_mesh**, or Wnode × J with J=IBM×B. Wnode is actual scene-node world before inverse bind. Resolve node/slot namespaces separately; apply external actor/preview placement once. If using an existing deformation Wnode×IBM, multiply B without applying IBM again. Physical stock/sight/muzzle/grip references and dense bridge replay are included; legacy helper origins are not physical calibrations.

The visor reference is the virtual midpoint of actual lens-surface vertices 23263/23516, each fully Head-weighted. Head is node 1/skin slot 5. This is a surface proxy, not anatomical eyes, gaze, IPD, eye relief, optical nodal point or camera origin. The portable optical check uses 16 explicit Blender-evaluated controls; its dense binding evidence is separately scoped.

There are **no authored gameplay, footstep, reload, magazine or transition events**. Source support-window diagnostics are kinematic measurements, not engine event timing. Time 1.0 is the matching cycle boundary, not an extra held frame. Loop wrap must preserve persistent identities; gameplay owns magazine_seated, ammo/chamber/inventory, replay and save/restore state. Source pace does not select runtime movement speed.

`SOURCE_INDEPENDENT_REVIEW.md`, copied exact pair/contact evidence and `SOURCE_DIAGNOSIS.md` retain the complete qualifications. Earlier source files may say export review was pending; this fixture's numerical checks are a separate completed step. `REPRODUCE.md` gives portable independent replay and full native/reimport commands. Private licensed-source constraints remain; public raw redistribution is not authorized.
