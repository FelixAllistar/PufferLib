# Crouch forward: Shared Ready N, loop A

This is one native-paced crouch-forward motion and loop-continuity candidate. It preserves the shared native geometry and is **not accessory-clear**: the stored spare sleeve enters the left outer-thigh hardware during swing. That significant fit limitation is retained visibly and measured below. It is not approved for gameplay, ADS, camera use or animation blending.

## Motion and source

The original crouch-forward FBX is 60 Hz, frames 1–61, exactly one second. Its root travels approximately (−0.0000000815, −2.039074183, −0.0000002384) m, giving a native horizontal pace of **2.039074183 m/s**. This is a brisk crouch stride. The recipe removes linear horizontal travel for the in-place action and applies the existing native-knee-plane sole-grounding policy, without retiming. It does not apply the game's speed, height or stance settings.

Before loop repair, all **170 human Hips, legs, spine and gaze curves** exactly match the verified grounded body source. The complete fitted N crouch arm/clavicle/hand/rifle packet follows the native Spine2 frame. The supporting grasp comes from the fitted crouch-compatible reference, retaining native hinge and dorsal material landmarks. The arms remain braced to that frame; no simulated inertial lag is claimed.

The current static crouch reference has a **visible stored spare magazine and sleeve**, so this action explicitly preserves that state. The spare follows the hips via Prop_Magazine_B; the sleeve retains its native Hips skin weights. The raw human FBX has no prop bones. The old derived locomotion library's collapsed secondary prop is not silently reused.

## Bounded loop repair

The body endpoints were already very close, but fresh FBX comparison confirms a source velocity kink. Saved-action analytic FK found maximum endpoint velocity gaps of **1.746610 m/s** at LeftLeg and **475.587698°/s** at LeftFoot. Existing grounding is not the cause.

The repair changes **683 of 700 scalar curves**, only within **0–0.125 s and 0.875–1.000 s**. It constructs common endpoint poses/tangents with normalized quaternion corrections and explicit Bézier handles. The fitted upper carry is recomputed from the corrected chest. Duration stays exactly one second. The entire central 75% retains exact scalar evaluations and bone matrices; sampled full-body skin also matches exactly there.

All saved endpoint scalar values, bone matrices and every evaluated body/prop vertex coincide exactly. Analytic residual velocity gaps are **0.0000524973 m/s** and **0.00713567°/s**, limited by the saved floating-point handles. The dense 960 Hz floor check stays positive: **3.336839 mm left / 3.891718 mm right**, with no samples below 3 mm. Compared with the native-timed baseline, maximum boundary foot-surface changes are **26.296938 mm left / 4.210062 mm right**; center changes are zero. These are measured sole clearances, not literal friction/contact simulation.

`curve_diff.json`, `SOURCE_SAVED_CURVE_DIFF.json` and `endpoint_target_derivatives.json` identify every altered curve, window, endpoint value/derivative and saved handle. `analytic_endpoint.json`, `ENDPOINT_POSE.json` and `boundary_velocity_series.json` give the physical checks. The normal-speed comparison repeats the complete cycle without retiming. Ordered rendered frames were inspected; live engine playback was not observed.

## Actual carry, anatomy and dimensions

The full-cycle 60 Hz finite-triangle check finds no Arm/ForeArm-dominant sleeve versus vest crossings, rifle versus non-hand body crossings, or installed-magazine/body crossings. The localized central stock patch remains backed at **2.191733–2.192016 mm**. This is localized shoulder support, not a flush pad fit or simulated force.

At 960 Hz, all actual glove vertices remain rifle-relative within **0.00103 mm left / 0.00086 mm right** of the fitted static reference, supporting transfer of its dense grip qualification. The supporting/firing wrist angles remain approximately **36.661° / 31.355°**. Native hinge/dorsal direction is retained; there is no forearm reversal. These checks do not erase the existing own-sleeve, brace and thin elbow-link folds.

At native scale, sampled body top ranges **1.306250–1.353069 m**, hips **0.643519–0.689466 m**, and visor surface midpoint **1.125935–1.172498 m**. Calibrated stock-center height is **0.961153–1.009475 m**; physical muzzle height is **0.692449–0.752625 m**. The rifle points **18.93–20.29° downward**. The visor proxy remains approximately **128–136 mm** from the sightline. These are actual surface/weapon measurements, not anatomical eye relief or camera calibration. This is low-ready, not ADS.

## Retained material and accessory limitations

The original shared body has native layered cloth/brace folds, stretched or flattened thin elbow links and shoulder-root sleeve intersections. Whole-sleeve/vest counts remain 50 left / 63 right through the sampled action. The scoped dominant-arm check above must not be interpreted as a globally intersection-free body.

The **stored sleeve collides with moving thigh hardware**, beyond its inherited belt mounting overlap. Closed-solid sampling at frame 3 (0.100 s) confirms sleeve points up to **18.392 mm inside the closed left outer-thigh component 17904**, with three parity directions agreeing. Body samples also enter the sleeve's solid walls by up to **4.000 mm**. The spare magazine has up to 12 exact pants crossings; those native shells are open, so no solid penetration depth is assigned to them. The same dynamic accessory issue is present before seam repair, including central-cycle phases, and cannot be removed by boundary smoothing alone. Direct worst-phase pixels and detailed measurements are included.

Opposing upper-hip garment contacts retain the existing component families 5118/11165 and 11165/11623. Sampled peak pair counts change from 22 to 28 near wrap, while maximum observed intersection chord decreases from 14.998 to 13.447 mm. These chords are not penetration depths; no new full-cycle component family is introduced. Pair identity/count changes remain disclosed rather than called zero regression.

No accessory is hidden and no mesh, topology, weights, bind, rig parent or bone-length change is made. Relocating the Hips-skinned sleeve only for this clip would break the pinned shared-mesh placement; a consistently applied separate accessory attachment offset would also need coordinated Ready/reload pickup changes. That integration choice remains separate from this source candidate.

## Reproduction and export boundary

The package contains the original FBX, immutable native geometry base, exact fitted crouch reference, grounded-body library and complete reconstruction recipes. Fresh offline body, carry and seam replays reproduce all 700 curves and 168,700 keys, including values/handles/interpolation. The final carry/seam worlds match at all verification phases. This is action-value parity, not byte-identical Blender serialization.

The neutral-material scene has no external images or texture dependencies. It retains all seven native skin influences where present. No GLB or engine parity is asserted here. A subsequent export must preserve cubic handles through the already checked path, resolve skin slots correctly, and use Wnode × inverse-bind × bind-mesh attachment frames. The calibrated visor is the midpoint of lens-surface vertices 23263/23516, not a camera origin. Legacy Muzzle/RifleSocket helper origins are not substituted for physical landmarks.
