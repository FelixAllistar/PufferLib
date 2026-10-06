# Crouch Ready: one measured planted low-ready pose

The selected pose is exactly the sealed Shared Ready N planted-crouch compatibility reference, now isolated under the action **Crouch Ready / Planted Shared N Low-Ready**. All 700 single-key curves, bone worlds and evaluated geometry are unchanged. This is a static source/review package, not a new idle cycle, transition, ADS pose or engine-height conversion.

## Measured stance and height

Measurements use actual evaluated native geometry at scale 1, with Blender world Z up:

| Measurement | Value |
| --- | ---: |
| Full-body vertical extent | 1.355917 m |
| Highest body vertex, world Z | 1.359917 m |
| Hips bone origin, world Z | 0.670974 m |
| Goggle-surface midpoint, world Z | 1.188157 m |
| Lowest soles, left / right | 4.000026 / 4.000143 mm above z=0 |
| Knee origins, left / right | 0.403129 / 0.358076 m above z=0 |
| Knee flexion, left / right | 99.074 / 103.284 degrees |
| Body extent of standing N reference | 1.732943 m |
| Goggle-surface drop from standing N | 0.396343 m |

Both feet retain broad heel/toe support patches, with 110 low-sole vertices per foot within 8 mm of the sole minimum. Both knees remain raised. This is a true two-foot crouch; no knee-down source or uniform body scaling is used. Knee-to-ankle planes align with each boot's forward plane, while the upper-leg material hinge retains the native crouch reference (dot with native −X: 0.999527 left / 0.999095 right). Native limb lengths and bind frames are retained.

The 4 mm clearance is authored ground spacing, not literal zero-gap contact. Geometric stance checks do not establish physical center of mass or dynamic balance. These static crouch frames also do not certify blending from standing Ready; the planted endpoint has its own native knee material construction.

## Rifle, grip and visor measurements

The original rifle points **19.956 degrees downward**. Calibrated physical landmarks are measured through the native inverse-bind skinning transform, not the legacy Muzzle or RifleSocket helper origins. Stock-center world height is 0.989771 m; actual muzzle height is 0.723586 m.

The true rear-pad center retains localized shoulder-gear backing at approximately **2.192 mm axial gap**. All 29 samples in the central 3 mm-radius patch hit its supporting shoulder component; 24 lie within 5 mm. The full rear face has no surface crossings, but sampled gaps range approximately **0.755–24.807 mm**. This is partial/localized support on curved gear, not flush or force-simulated stock seating.

Actual glove/rifle surfaces preserve the sealed N fit to submicrometre transport error, supporting its prior dense opposing-contact qualification. Existing shallow glove contacts remain (standing N confirmed maximum depths 0.538 / 0.456 mm, no tested sample above 1 mm). This package does not reinterpret raw grip-center candidates as new hand IK targets. Current hand/finger transforms are the selected pose's exact N transforms.

The visor reference is the virtual midpoint of actual lens-surface vertices **23263 and 23516**, each weighted entirely to Head. It agrees with the calibrated native Head attachment within **0.000015 mm**. It is a goggle-surface proxy, not an anatomical eye, optical nodal point or gameplay camera. The proxy lies about **147.868 mm perpendicular to the rear/front sightline**; its surface normal differs from that line by about **14.584 degrees**. Neither figure is an eye-relief or ADS calibration. The gameplay camera remains authoritative.

## Complete surface review and retained model limits

Fresh full-surface review of the saved selected file finds zero opposing left/right-leg crossings and zero arm/leg crossings. Rifle/body crossings occur only in the already-qualified glove regions. Installed and spare magazines have zero body crossings. Dominant upper/forearm versus vest crossings remain zero; the broader connected sleeve/vest regions retain 50 left / 63 right shoulder-root contacts exactly as standing N.

Same-leg brace/cloth intersections, waist/upper-thigh layers and localized sleeve folds remain. Leg/torso-dominant intersections are confined to existing pants and own upper-thigh layers, with no new torso-gear interaction family. The exposed light underarm panel and elongated/flattened mechanical elbow link remain visible. The selected pose is not globally intersection-free.

The existing spare-magazine sleeve has 28 crossings against belt component 64. Exact geometry and pair classification place these at its back mounting block, vertices 40–47; they match standing N exactly. No sleeve-cavity contact is introduced. This is a retained accessory mounting overlap, not an unreported new pose defect.

Every evaluated mesh, bone world, full-body crossing pair identity and material diagnostic matches the sealed crouch compatibility source. No mesh, weight, UV, bone, gear or pose correction was introduced for this selection. The body retains 24,850 vertices, the native 70-bone hierarchy and all original influences, up to seven.

## Prop state and integration scope

The installed magazine remains on the rifle. The second magazine is visibly stored at the hip and its source transform is **not collapsed**. The earlier locomotion clips inherited a collapsed secondary prop. This distinction is preserved and explicitly handed off; visual source props do not assign runtime inventory or persistent pouch IDs.

The action has one key at frame 0 and no authored temporal duration. Scene playback bounds are only an editing convenience. No breathing, stance transition, crouch-forward motion, gameplay height scaling, camera movement or controller change is implied. The selected source and all references remain separate from earlier checkpoints.

The main front/side panel, same-scale standing comparison and contact-detail panel show the measured source without changing body scale. Independent full-body, shoulder, knee and accessory views are included. Source reconstruction and measurements are reproducible from the bundled files; any future export must resolve its own skin-joint slots, use inverse binds correctly, and revalidate the calibrated visor and physical rifle attachments.
