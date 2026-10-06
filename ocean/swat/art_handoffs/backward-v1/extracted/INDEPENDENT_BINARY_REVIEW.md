# Independent backward cubic fixture review

**PASS as a qualified numerical cubic fixture.** This validates GLB SHA-256 `5769ec1b03a63e3d363ba360c66edd31784225e6ca49be7b2177980ba84170de` against the sealed original N70 backward source. It is not material-clean, zero-regression, ADS, camera, runtime-speed-fit or gameplay approval.

The source archive and all 14 pinned workspace files match. The redundant action-only library was intentionally excluded from the source archive; its workspace hash was checked separately. A fresh read of the saved Blender action verifies every key, both handles, interpolation and extrapolation against the delivered source curves: 700 curves and 168,700 keys. The bounded repair changes 604 curves, and all 700 center channels match the grounded native-carry baseline exactly at 91 sampled center times. Fresh native geometry/rest signatures exactly match sealed N, including all weights, UVs, material assignments, hierarchy and original hands. No source or GLB file was edited.

## Actual bytes and derivatives

- All 70 joints and 210 TRS channels are explicit CUBICSPLINE over exactly one second
- All joint and skinned endpoint poses match exactly in the byte evaluator
- Maximum analytic wrap residual: 0.00006622555 m/s at joints, 0.005045901 degrees/s angular, and approximately 0.00006759584 m/s on skin
- Normalization derivatives are applied to quaternion polynomials before FK; zero-scale B is excluded only from undefined angular velocity
- All 16,800 quaternion intervals were checked using numerical squared-norm polynomial extrema; norm range is 0.9999958404–1.0000011141
- 961 scalar/FK replay phases include 721 center phases; maximum joint position error versus saved scalar evaluation is 0.000378209 mm

At a practical 1/60-second interval, incoming/outgoing finite chords differ by up to 0.439397 m/s at joints, 0.455702 m/s on body skin and 36.0376 degrees/s angular. These finite-time differences include acceleration and converge toward the much smaller analytic residual as epsilon decreases. This is not a claim of equal 60 Hz finite-step velocities or certified runtime float32 differentiation.

## Fresh full-surface and imported-skin checks

The independent capture evaluates every exported vertex at 151 native/cubic-aware/stock Blender phases, including endpoints, native half-frame samples and boundary epsilon probes. All 48,755 seam-split exported vertices, normalized influences, oriented source triangles and source UV seam associations were checked.

- Actual bytes versus fresh Blender source: maximum 0.001088 mm
- Explicit cubic-aware Blender reimport versus fresh source: maximum 0.001491 mm
- Stock Blender 4.3.2 import versus source: maximum 2.915081 mm; this importer discards the serialized cubic tangent fidelity and is not qualified
- Truncating to the four largest weights and renormalizing produces 1.025814 mm maximum error at these phases; the four-influence negative is rejected
- Every original influence is retained, including seven-influence vertices

The portable references retain all 151 independently evaluated native and imported bone/deformation poses, every bind vertex/triangle/weight, and nine complete actual evaluated surface controls. NumPy-only replay reconstructs every skin vertex at all 151 poses and verifies those reconstructions against the direct surface controls. It passes independently. The larger direct 151-phase surface capture was checked separately and is not bundled. This distinction keeps algebraic coverage and direct Blender control coverage explicit.

## Attachments, travel, floor and source qualifications

The static 39-accessor geometry/bind contract is unchanged. Resolved node and skin-slot mappings, all 210 channel rows, phase-zero transforms, corrected rifle bridge, optic vertex remap, preview support centroids, unbaked preview transform, source pace and empty events all pass actual-byte checks. The rifle bridge checks 5,346 native vertices over 1,243 reference phases; maximum native point error is 0.000599 mm. Lens-surface control error is below 0.000364 mm. These optical landmarks remain a visor-surface proxy, not anatomical eyes or a camera.

Fresh native FBX inspection finds a 60 Hz, one-second take with horizontal travel approximately +Y 1.92126141 m in Blender. Actual exported head-forward versus original travel dot stays negative, approximately −0.9770 to −0.9733. The in-place cubic retains backward intent and the raised Ready rifle elevation of 14.947141–17.206416 degrees. No source retiming or facing correction is inferred.

At 960 Hz, complete connected boot components have minimum clearance 3.685667 mm left and 3.371887 mm right. Both exceed 3 mm at sampled phases; the nominal 4 mm target is not exact, and finite sampling is not continuous clearance proof.

A separate nine-phase finite-triangle replay retains the source's lower thigh/pants/strap and sleeve/body intersections. Source and cubic counts and component families match at these phases: cross-leg pairs range 0–164; left leg/torso 32–78; right leg/torso 29–210; arm-dominant/vest 0; sleeve/body 28. Common-pair chord changes are at most 0.002143 mm. This does not replace the broader source regression history or establish collision freedom. Crossing chords are not penetration depth. The source's grounding and seam changes remain qualified as documented in its pinned review.

Prop_Magazine_B is explicitly zero-scale for the whole clip. The separate 48-vertex spare sleeve remains visible and fully Hips-weighted. The fixture uses original N70 geometry and hands; it adopts no grip C/C1 diagnostic, engine grip override, F72 carrier channels or F72 geometry. C1 in the action name denotes loop continuity. No authored events were introduced.

See `FINAL_REVIEW.json` for compact results and `REPLAY.md` for exact portable commands.
