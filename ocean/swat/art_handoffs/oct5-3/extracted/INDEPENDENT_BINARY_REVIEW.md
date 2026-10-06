# Independent crouch-forward cubic export review

**QUALIFIED_FAITHFUL_CUBIC_MOTION_CONTINUITY_CARRY_EXPORT_WITH_SUBSTANTIVE_STORED_ACCESSORY_COLLISION**

This is a faithful import/preview candidate with a substantial accessory-fit defect. It is **not clearance-approved**, and this review grants no engine, runtime, ADS, camera, physical-balance or animation-blending acceptance. The spare B and its Hips-skinned sleeve remain visible and noncollapsed. No native source, mesh, visibility, pose, source timing, engine or producer script was changed by this review.

## Exact inputs and scope

- Editable source: `crouch_forward_ready_n_c1.editable.blend`, 3,670,023 bytes, SHA256 `897771dc55846b4ad2b1181fcb560a55d364a4e22fc5510177b905bbb28ee442`
- Source checkpoint: 17,482,700 bytes, SHA256 `e63cd72bbfcc43245a6eebc206cc5c89d15522c1bba10e3f2a6f43e08a1cf858`. All 73 manifest-listed member hashes/sizes independently verified; the 74th file is the manifest itself
- Reviewed binary: `crouch_forward_shared_n_c1_loop_a.glb`, 5,847,176 bytes, SHA256 `ea828e53f305de77b35ba55abdf67470ae98e871911c43fae5505c5022d47dd1`
- Action: `Crouch Forward / Shared Ready N C1 Loop A`, native saved frames 0–30 at 30 fps, exactly 1.000 second. Original human FBX is 60 Hz, frames 1–61
- Native baked pace: 2.0390741825103778 m/s. Independent fresh endpoint subtraction gives 2.0390742532908934 m/s; the 0.071 micrometre stride difference is explained by native float32 subtraction, not retiming

The independent NumPy GLB parser and normalized-quaternion derivative/FK evaluator were adapted from the previous reviewed cubic fixture. They import neither the producer's converter nor `gltf_math`. All previous collapsed-B exclusions and lateral-direction assumptions were removed. B now participates in angular/FK checks.

## Binary, source curves and actual skin

The actual GLB contains 76 nodes, 70 unique skin joints, 210 CUBICSPLINE channels and six mesh primitives across five enabled source objects. GLB length, chunk alignment, buffer/accessor bounds, component types, strides, finite values, accessor min/max, hierarchy, triangle indices and every influence set pass. All 48,755 exported vertices map uniquely to native vertices using exact bind coordinates plus all normalized weights. Oriented triangle multisets match every source mesh.

The body retains both weight sets: 444 exported vertices have more than four positive influences, including two with seven. All positive weights participate and are normalized together. Maximum normalized native/export weight discrepancy is 2.21251011e-8. No four-influence truncation is accepted.

All 39 static geometry/inverse-bind accessors are bit-identical to the pinned ordinary-forward template (SHA256 `e44a396d19686b172184e241ed42e52291c96a3e27422770da899d18b5912435`), including positions, normals, UVs, indices and influence arrays. Node, skin, mesh, material, scene and scene-root JSON also match exactly. Neutral materials and absence of tangents/textures are retained; this is not a runtime normal/tangent/shader appearance test.

Source extraction matches 700 curves and 168,700 keys: 41,663 Bézier and 127,037 linear. All 42,346 saved boundary-window key values, handles and interpolation entries match the pinned source diff. Exactly 683 curves change within 0–0.125 and 0.875–1.000 second; the native central 75% is unchanged. Export float quantization is measured separately, rather than described as exact source-value identity.

Numerical extrema of every actual stored quaternion cubic (16,800 intervals) stay between 0.999978994579 and 1.000006748330 before normalization. Unit key norm deviation is below 1e-6; interpolation is normalized before rotation/FK, with its normalization derivative included.

## Seam and fidelity measurements

All 70 decoded joint endpoint matrices and every exported skinned vertex have exactly zero endpoint pose difference. Physical analytic endpoint derivative residuals are:

- Joint linear: 5.24048148577e-05 m/s, worst at LeftToeBase
- Joint angular: 0.00712811949291 degrees/s, worst at LeftFoot
- All-vertex linear: 5.56256789004e-05 m/s

These small finite-precision residuals track the saved source's approximately 0.0000524973 m/s and 0.00713567 degrees/s. They are measured residuals, not a claim of mathematical zero.

One-sided practical wrap chords remain nonzero because they include finite-time acceleration; they are separate from analytic C1 residuals:

| One-sided interval | Maximum joint linear difference | Maximum joint angular difference | Maximum surface velocity difference |
|---|---:|---:|---:|
| 1/60 s | 0.481605 m/s | 38.438789 deg/s | 0.488364 m/s |
| 1/240 s | 0.130312 m/s | 10.629989 deg/s | 0.132268 m/s |
| 1/960 s | 0.033439 m/s | 2.840702 deg/s | 0.033943 m/s |
| 0.000001 s | 0.00005905 m/s | 0.00746757 deg/s | 0.00006081 m/s |

At 961 native scalar-replay phases, maximum world position error is 0.000444864 mm and maximum all-vertex animation error is 0.000455098 mm. At 540 central off-knot samples, worst joint derivative differences are 0.000106941 m/s and 0.0301877 degrees/s.

Against separately captured native Blender deformation matrices at 1,243 phases, every exported vertex stays within 0.003772125 mm. Against direct Blender evaluated mesh vertices at 16 controls, every vertex stays within 0.000976974 mm. These include the 0.1-second phase of the source's worst confirmed sleeve/hardware inward distance.

## Accessories, bridge, optics and coordinate namespaces

B scale value keys are positive; its minimum world singular value across 961 phases is 0.9999984503. The spare follows the hips with a tiny inherited native interpolation variation: maximum hips-relative position deviation 0.0820013 mm. The export/native hips-relative matrix difference is only 7.97656e-8. The sleeve's every positive weight resolves to Hips.

The rifle bridge resolves Prop_Rifle node 67 and skin slot 67 separately, then uses `Wnode × IBM[resolved slot] × B_bind_mesh`. Its equivalent joint-local form agrees to 2.22e-16. Across 1,243 phases and all 5,346 rigid reference vertices, worst native bridge error is 0.000789858 mm. Omitting IBM produces a 1.28409 maximum matrix-element error and is incorrect.

The optical surface proxy resolves Head **node 1 to skin slot 5**. Both actual lens vertices are verified as fully Head-weighted. The midpoint agrees with the decoded optical frame within 2.55e-16 m; worst direct native lens error is 0.000305082 mm. It remains a visor/lens-surface proxy, not an anatomical eye or camera origin.

Sole IDs explicitly use the native Blender mesh namespace, with separate exported primitive/vertex matches. Left/right native foot subsets contain 654/668 vertices; seam-expanded exported subsets contain 899/916. Seven left and three right phase-zero support vertices resolve exactly; their maximum native evaluated position discrepancy is 0.0002561 mm.

The fixed unbaked preview maps the measured horizontal rifle heading to engine +X and the measured feet origin to zero. Original human travel becomes approximately (2.028195323, −0.000000238, 0.210350315) m under this preview, 5.92115 degrees from +X. No movement heading, gameplay speed or per-frame grounding retarget is implied. Events remain empty; no gameplay fire, reload or inventory ownership commit is inferred.

## Retained source defects

The pinned independent native review established sleeve surface points up to **18.392138 mm inside closed left outer-thigh hardware component 17904 at frame 3 / 0.100 second**, with agreeing parity rays, and body samples up to approximately 4.000 mm inside the sleeve's solid walls. This is a substantive geometric collision, not a crossing-chord surrogate or a measured zero-depth contact. It is a maximum observed inward sample, not a minimum whole-object separation or global maximum proof.

The visible spare has up to 12 pants triangle crossings. Magazine/pants shells are open, so no solid inward depth is assigned. Shoulder-root sleeve crossings, local sleeve/link/brace folds, normal-transport reversals and opposing upper-hip garment contacts also remain. Shared geometry and the native motion are preserved to the numerical fidelity above; collision clearance was not newly proved by this export review.

Matched native/cubic images at phases 0 and 0.95 were inspected. They show the same carried rifle, crouch and visible stored assembly without a gross pose or prop-state difference. These are rendered evidence at selected times; no live engine playback was observed. Stock Blender importer behavior is reported separately by the producer, because its cubic tangent handling is not the independent binary oracle.

## Reproduction and evidence

`replay_unit.py`, `inspect_binary.py`, and `inspect_curve_contract.py` provide portable Python 3 + NumPy actual-byte replay. See `REPLAY.md`. `inspect_native_controls.py` additionally accepts the separately captured native NPZ inputs. Reports include `binary_unit_validation.json`, `structure_validation.json`, `curve_contract_validation.json`, `native_control_validation.json`, `sole_mapping_validation.json`, `preview_metadata_validation.json`, `SOURCE_IDENTITY_CHECK.json`, and `FINAL_METADATA_REVIEW.json`.

This review qualifies faithful numerical export and metadata with the explicit native collision limitation. The final ZIP must retain the reviewed GLB hash, this qualification and the executable independent replay. Final archive integrity is a separate sealing check.
