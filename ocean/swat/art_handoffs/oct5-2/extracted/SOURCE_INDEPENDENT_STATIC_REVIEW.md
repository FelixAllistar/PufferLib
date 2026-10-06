# Independent static crouch review

Reviewed selected frame 0: `Crouch Ready / Planted Shared N Low-Ready`. SHA256 `b316df0d4c4af329dffe5f5330c502da14d17c0d3af77eecd257262a435445d9`.

The saved candidate is an exact pose derivative of sealed compatibility N. All six evaluated meshes and all bone world matrices have zero difference from the pinned source; full-body crossing pair IDs and material diagnostics also match exactly. No corrective pose edit was made by this review.

## Measured posture

- Actual body extent: 1.355917 m; top Z: 1.359917 m. Standing extent: 1.732943 m. Ratio: 0.7824. No engine-height conversion or scale change.
- Optical goggle surface midpoint Z: 1.188157 m. This is a visible lens proxy, not an anatomical eye or camera origin.
- Left/right soles: +4.000026/+4.000143 mm. Both low sole patches retain 110 vertices; zero below-floor foot vertices.
- Left/right knee heights: 0.403129/0.358076 m; flexion 99.073943/103.284037 deg. This is a true two-foot crouch, with neither knee near the floor.
- Native knee material axial angles: left 13.961151 deg, right −9.748017 deg. Exact planted lower endpoint matrix discrepancy is only 5.07e−7.
- All 7 influences retained; independent normalized LBS differs from evaluated Blender body by at most 0.000365 mm. Native arm hinge and dorsal frames are preserved.

## Actual full-surface fit

Opposing leg/leg and arm/leg triangle crossings: 0. The leg/torso-dominant categories contain 72 left and 28 right pairs, all at existing pants/own upper-thigh component layers. There is no opposing-limb contact or newly introduced torso-gear family.

Whole-sleeve versus vest includes 50 left and 63 right true triangle crossings at inherited shoulder roots. These are mixed Spine2/Shoulder faces and match standing N counts/families. Arm/ForeArm-dominant vest crossings are 0. Therefore a narrow arm filter cannot justify a globally clean sleeve claim.

Same-leg knee brace/strap intersections and compressed cloth folds remain. The lower pants component has 93 normal-transport reversals, exactly the planted v 3 endpoint diagnostic. Main kneepad components 13314/13508 have 0; shin brace 18406 retains 1. These material limits are preserved, not corrected. Direct knee and rear lower-body pixels confirm the bent hardware and layered folds.

Rifle/body has 65 glove-region triangle pairs, no nonhand pair. Each magazine has 0 body crossings. The installed magazine has 234 rifle-slot crossings, an inherited assembly relationship. The hip spare sleeve has 28 belt-mount crossings, all between mounting block vertices 40–47 and hips-weighted belt component 64; the exact 28 pair IDs also occur in standing N. The spare magazine/sleeve cavity has 0 crossings. Both magazines and the sleeve remained visible for review.

Full true rear stock face:6772 samples, nearest 0.755138 mm, farthest 24.807142 mm, zero rear-face body crossings. Center axial gap 2.191921 mm. This is localized shoulder support with air elsewhere, not flush seating. Rifle elevation is-19.956483 deg.

Whole gloves preserve standing N rifle-relative surfaces within 0.000530 mm. The prior dense hand-depth result therefore carries over geometrically: left 0.538 mm/right 0.456 mm maximum confirmed depth, zero samples deeper than 1 mm. Dense hand parity rays were not rerun.

## Qualification

Usable as the one static planted low-ready compatibility source with the explicit native material limits above. Do not describe it as globally intersection-free, literal sole contact, ADS-ready, dynamically balanced, or an engine/runtime acceptance. No crouch-forward work was reviewed.

Evidence: `INDEPENDENT_STATIC_QUALIFICATION.json`, `selected_full_surfaces.json`, `selected_accessories_and_parity.json`, `sleeve_mount_classification.json`; eight direct final-pin PNG views in this folder. Source, standing and selected input hashes were unchanged.
