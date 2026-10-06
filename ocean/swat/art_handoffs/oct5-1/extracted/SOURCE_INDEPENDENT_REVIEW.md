# Walk right: independent saved-action review

**Qualified for saved loop continuity and Shared Ready N carry, with explicit material limitations.** RIGHT is not interlimb-clear or zero-regression by triangle-pair identity. Visible inherited opposing upper-thigh garment contact remains, and the bounded repair adds five small cross-leg pairs plus one same-side hip/strap pair. This review does not authorize changing the source stance or path.

Candidate: `Walk Right / Shared Ready N C1 Loop A`, `../seam_a/walk_right_ready_n_c1.editable.blend`, SHA256 `f85525ba445013f73884677bd8202d7ffff226b04fedee930250c90cbe402770`. Frozen carry baseline: `../native_carry/ready_walk.editable.blend`, SHA256 `d3f1bf10decdcb52d7d227b3c3f03b8dde1a4f947f4268955f87a22be40936f4`. No pose, source, mesh, weight, or animation files were edited during this review.

## Source, timing and retention

The original `walk right.fbx.bin` SHA256 is `80f0debe85112dc5ae73fdd60d035cc5db3c343509a0e9541166264137a4fd82`. Native timing is 60 Hz, frames 1–61, one second. Actual Hips displacement is (-1.921261549, -0.000009783, -0.000000477) m. Double-precision horizontal pace is 1.921261549020877 m/s. Destination remains frames 0–30 at 30 fps, one second; no retime was introduced.

Native chest-material forward starts (-0.424753, -0.905309, 0); pelvis-material forward starts (-0.754573, -0.656216, 0). Their angles to the source stride span chest 56.712–69.482° and pelvis 20.273–51.191°. Source pelvis/chest directional intent is retained; the asymmetric shoulder-line normal is only a secondary diagnostic.

The carry baseline retains all 180 original body, leg, gaze and unused-prop curves exactly against `all16_actions.blend`. All six mesh objects retain exact geometry, topology, UVs and complete weights from sealed N. The body has 24,850 vertices, maximum seven nonzero skin influences, and both seven-influence vertices retained. The 70-bone bind hierarchy is identical. Prop_Magazine_B retains zero scale.

## Saved seam and bounded change

Repair windows are 0–0.125 s and 0.875–1 s. Every original interior key coordinate and interpolation mode is exact. At 721 center probes, scalar evaluation roundoff is at most 1.192093e-7; at 960 Hz, FK component roundoff is at most 3.576279e-7. Entire evaluated body meshes are identical at seven checked center phases. This is preservation within numeric evaluation precision, not universal bit-identical subframe evaluation.

| Independent measurement | Native carry baseline | Saved A |
|---|---:|---:|
| Worst analytic linear wrap gap | 1.228351271 m/s | 0.000068530 m/s |
| Worst analytic angular wrap gap | 457.664446°/s | 0.004866554°/s |
| Worst bone position C0 gap | 0.435506 mm | 0 mm |
| Worst bone rotation C0 gap | 0.050322° | 6.7e-9° |
| Actual rifle-surface C0 gap | 0.560861 mm | 0 mm |
| Actual left/right foot-surface C0 gap | 0.001247 / 0.004875 mm | 0 / 0 mm |

Saved scalar endpoint slope gap is at most 4.291600e-5/s; repair-window join slope gap is at most 4.291665e-5/s. Normalized-quaternion analytic FK reconstructs evaluated endpoint matrices within 4.434e-7. Float32 stored handles leave small residuals, so literal exact mathematical C1 is not claimed. Angular velocity of the collapsed unused magazine is undefined and excluded explicitly.

## Actual N carry, upper garments and neutral pixels

At 111 full-cycle and dense boundary phases, actual arm-dominant triangles versus vest, rifle versus nonhand body, magazines versus body, and connector self-crossings all remain zero. Stock-center gap is 2.191640–2.192191 mm; the same 24 central patch samples stay within 5 mm.

At 961 phases / 960 Hz, all 726 vertices of each connected glove retain the sealed N contact geometry in rifle coordinates within 0.003315 mm left and 0.002479 mm right. Complete rifle and installed-magazine surfaces remain within 0.000243 mm and 0.000237 mm. This is actual deformed surface verification.

Whole sleeve/shoulder-root versus vest crossings remain 50–52 left and 63 right per phase. Their pair unions are identical baseline to A: 54 left, 67 right. Four left and six right pair identities beyond static N are chest-dominant vest component 256 versus sleeve-root components 4248/3554, not arm-dominant/vest crossings. Largest such added-to-N chord is 4.942251 mm at an unchanged center phase. Neutral closeups retain the inherited root folds, stretched sleeve panel, and thin/flattened elbow link. These facts preclude an intersection-free-character claim.

## Floor and actual lower surfaces

At 960 Hz, left sole minimum is 3.954407 mm, right 2.931984 mm at frame 1.8125. No sampled sole lies below z=0. Right has 17 of 961 phases below 3 mm, so the nominal 4 mm clearance is not maintained throughout the repair window. Maximum actual foot-surface departures from baseline are 14.376192 mm left at frame 28.65625 and 2.910110 mm right at frame 28.4375.

Matched lower surface checks cover both repair windows at 240 Hz plus three center phases, 65 phases per clip. Unlike LEFT, RIGHT has inherited opposing upper-thigh garment intersections. Native carry and A both reach 98 cross-leg tagged triangle pairs per phase. Baseline/candidate cross-leg pair unions are 285/270, and maximum intersection chords are 22.156917/21.139238 mm. Fifteen same-pants-component pair identities also persist. These are real pants/strap crossings, not merely projected overlap.

The five newly observed cross-leg pairs are:

- Left pants component 5118 versus right upper-thigh strap 11623: `(12316,22378)`, `(12316,22379)`, `(12317,22378)`, `(12446,22206)`. Largest new chord 0.655282 mm at frame 2.25
- Left strap 11165 versus right strap 11623: `(21588,22374)`, chord 0.004702 mm at frame 2.875

A new same-side hip/pants 5118 versus left strap 11165 pair `(13874,21394)` has chord 0.050905 mm at frame 28.125. All six have zero chord at matched baseline phases. No new knee/boot or component family is found in these sampled windows. Fixed neutral and component-colored views show the existing inner upper-thigh strap/pants contact in both baseline and A; the repair does not eliminate it.

A triangle intersection may move between neighboring triangles: one existing cross-leg pair gains 9.435009 mm of chord while its neighbor loses 9.416276 mm at the same frame. Component-family evidence therefore accompanies pair identities. Greatest matched cross-leg family increase in summed segment length is 0.130743 mm; greatest decrease is 42.283300 mm. An inherited left hip/pants/brace family has a 7.522926 mm summed-length increase. Neither chord nor chord sum is penetration depth. Global decreases do not justify calling the result zero-regression.

## Evidence and scope

`FINAL_STATUS.json` separates continuity/carry/retention/floor results from the material limitations. `ARCHIVE_FILES.json` is the explicit compact checkpoint allowlist: reproducible scripts, source pins, analytic and contact reports, compact exact-pair classifications, and seven selected neutral/component PNGs or matched sheets. Repetitive raw dumps, logs and render trees stay outside the compact archive.

All protected input hashes remain unchanged. Finite samples and finite-triangle crossings are not continuous analytic or watertight penetration proofs. Scope is the saved RIGHT Blender action only: no ADS, runtime engine, resampled-export, LEFT rework, crouch, or other-direction claim.
