# Ready WALK LEFT: one native-timed, seam-repaired candidate

This is one lateral walk using the sealed Shared Ready N coupled carry and its original human-motion source. It is a separate authoring candidate; Ready N, the source banks, the delivered forward walk and its cubic derivative remain unchanged.

## Source and direction

The original `walk left.fbx.bin` is SHA-256 `c66c409acea597462d088a26a2a388127aa8c03238b49c02117264f5504616ba`. A fresh import confirms 60 Hz, frames 1–61, and a one-second duration. Hips travel approximately +1.921261907 m along X, with negligible Y drift, giving a source pace of **1.92126190665 m/s**. Root travel is removed in the grounded in-place action; this pace is metadata for later phase/speed mapping, not a gameplay retime.

The source has an oblique pelvis and chest relationship. At its first pose, the native chest-material forward vector is approximately (−0.270,−0.963,0), while pelvis forward is (−0.744,−0.668,0). This native counter-rotation is retained. The motion is not a generic forward stance rotated into a new direction.

The initial fitted-carry baseline copies all 180 hips, legs, spine, gaze and unused-prop curves exactly from `Rebuilt Upright Native Frame / walk left`. N's complete clavicle/arm/hand/rifle packet follows that native chest. Native geometry, UVs, all influences (up to seven), 70-bone rest/parents and the collapsed secondary magazine channel remain unchanged. Rifle/visor binding conventions are left to the existing calibrated export contract; this clip does not establish ADS alignment.

## Why a seam correction is necessary

The original fitted-carry loop has small endpoint pose differences but a substantial velocity mismatch: approximately **1.29181 m/s** at the muzzle and **760.243 degrees/s** at the left toes under analytic saved-channel forward kinematics. The evaluated maximum bone endpoint mismatch is approximately 0.4744 mm and 0.03371 degrees; actual rifle skin differs by 0.5708 mm.

The selected action is **Walk Left / Shared Ready N C1 Loop A**, frames 0–30 at 30 fps, still exactly one second. Corrections occupy **0–0.125 s and 0.875–1 s**. The central 75% retains original key values and interpolation; additional window-edge keys introduce at most 1.1921e−7 scalar evaluation roundoff. Original source timing and root pace are unchanged.

657 of 700 scalar curves have boundary edits. Local translation, normalized quaternion and scale corrections establish common endpoint poses and tangents, while the complete N carry is recomputed relative to the repaired chest. Explicit Bezier handles retain the endpoint derivatives. `SOURCE_SAVED_CURVE_DIFF.json` contains every affected curve, original and new boundary keys/handles, time intervals, and actual endpoint slopes. Numerical corrections to coupled prop and otherwise nearly constant channels are included rather than hidden.

The saved evaluated endpoint poses and actual feet/rifle surfaces match. Independent analytic residuals are at most **0.00006857 m/s** and **0.005232 degrees/s**, bounded by stored float precision. This source-level result depends on its cubic tangents; an unverified LINEAR resample cannot be assumed to preserve it.

## Ground, contact and material limits

At 960 Hz, sole clearance remains positive: approximately **3.96044 mm left / 2.87575 mm right**. Right sole clearance falls below 3 mm at 23 sampled phases. This is disclosed against the original 4 mm grounding target; no extra ground solve or foot-locking claim is added. Maximum foot-skin path change within the repair windows is approximately **13.9124 mm left / 3.31559 mm right**.

Across the full-cycle actual-surface audit, functional upper/forearm-to-vest, rifle/non-hand-body, magazine/body and connector self-crossings remain zero. Central rear-pad shoulder backing stays approximately **2.19158–2.19207 mm**, with the N central support footprint retained. This is localized shoulder support on curved gear, not full flush pad seating.

The fitted opposing grasps, native hinge directions and real cuff/back-of-hand relationships remain coherent. Dense actual glove/rifle transport and exact skin data are in the independent reports. The upper garment/vest pair union stays the same as the fitted-carry baseline.

The actual left and right leg surfaces do not intersect in the sampled seam windows. Eight newly encountered pair identities occur at the existing left hip/upper-thigh mixed garment junction; their maximum new crossing chord is approximately **1.430 mm**. Existing hip garment overlap reaches a 36.300 mm chord versus 36.216 mm in the baseline. A chord is an intersection-segment length, not penetration depth. These must be evaluated as native layered pants/waist behavior, separate from interlimb penetration. Focused neutral and component-colored views classify these as existing continuous pants, upper-thigh strap and small strap/buckle families. They show tiny seam deformation without a new thigh, knee or boot collision.

Native connecting-sleeve stretch, flattened mechanical elbow linkage, localized armor/cloth folds and hip garment layering remain visible limitations. Arms are braced relative to the chest; independent secondary arm inertia has not been added. Dense sampled tests do not prove continuous watertight clearance.

## Recovery and export scope

A portable two-stage rebuild first reconstructs the N carry from the bundled static N scene and exact original action subset, then applies the bounded seam repair. The initial baseline reproduces all 700 curves / 158,520 keys exactly. The final action reproduces all **700 curves / 159,716 keys**, including handles/interpolation, with zero world-matrix difference at nine checked phases. This is animation-value parity, not byte-identical Blender serialization.

The matched preview repeats the one-second motion at normal speed, showing front/side and feet/support-arm views. Ordered frames and numerical data were inspected; live engine or gameplay behavior is not claimed. The checked cubic GLB path must establish its own endpoint derivative and full-vertex parity, preserving the corrected inverse-bind and calibrated optical conventions. No WALK RIGHT, running, crouch, reload or other direction is promoted by this left-clip report.
