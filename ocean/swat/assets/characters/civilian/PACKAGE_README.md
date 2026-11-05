# Briar Court civilian 01 · review candidate

Original, clothed, stylized adult civilian. Olive pullover, blue trousers, plain shoes, short hair. This is a separate candidate. Native game acceptance and final art approval are still required.

## Integration holds

- A four-weight-only parser cannot admit this five-influence asset; preserve both weight sets.
- Tall-kneel eye 1.251 m and body top 1.362 m do not fit the generic crouch eye 0.8128 m / height 1.016 m.
- Natural transition durations are 3.6 / 2.8 / 4.2 seconds, not the existing 1.2-second timer.
- The measured cuff endpoint gap is 126.21–127.06 mm. The original 60 mm connector is incompatible; the optional rigid-link reference uses the measured gap.

## Start here

- `runtime/BC_CIV01_candidate.glb`: neutral, texture-free, dense runtime character and twelve clips.
- `source/BC_CIV01_original.blend`: editable mesh, explicit weights, original rig, all actions, packed original maps, review scene and optional dimensional cuff fixtures.
- `runtime/material_bindings.json`: exact material-index/name binding to separate original sRGB maps. The GLB can be used without these maps.
- `source/authoring_contract.json`: actual source bone hierarchy/rest matrices, basis and clip timing.
- `qa/runtime_skin_map_and_bind.json`: exported node/joint map and actual inverse binds. Never substitute another character's joint indices.
- `review/`: actual runtime-decoded playback, off-key stills and evidence manifests. All off-key images are included as compact high-quality JPEG copies; original PNG hashes are retained in `review/stills/delivery_inventory.json`, with original PNGs retained in the working directory. Source silhouette approval alone does not establish runtime acceptance.
- `MANIFEST.json`: final file hashes and packaging inventory.

## Engine admission requirements

The character has 44 exported skin joints, 45 meshes, 31,190 triangles, 15,754 exported vertices including seams, and at most five positive influences per vertex. Both JOINTS/WEIGHTS sets must be read. An eight-influence consumer passes the audit; a four-influence-only consumer rejects this asset. Do not silently drop or renormalize away the fifth influence.

The GLB has no embedded textures, image dependencies, external buffers, morph targets, sparse accessors or compression. It uses embedded dense data, neutral scalar materials, explicit animation TRS channels and LINEAR interpolation. Root is fixed. Keep every original weight and timestamp.

The source rig is original and is not N70/F72. Two non-deforming WristFrame helpers are included in the skin joint map. CuffAnchor_L/R are separate empty nodes, not interchangeable with deform joints. EyeSurface and SoleRef markers are also separate nodes.

## Coordinates and transport

- Blender source: metres; +Z up, -Y forward, +X character left.
- Runtime: metres; +Y up, +Z forward, +X character left.
- Engine heading zero +X: rotate the runtime model +90 degrees around +Y.
- Root origin is the standing sole-ground origin. The standing soles lie at Y=0 within floating-point tolerance.
- Apply world travel/heading to the complete model scene root `BC_CIV01_Rig` (node95). The animated skeleton `Root` is node49; retain its authored rest rotation.
- The engine owns horizontal world travel. No clip translates Root. Local pelvis height, sway and temporary stance shifts are intentional.
- Walk: 0.700 m/s. Restrained escort: 0.31875 m/s. Both use a two-thirds-cycle stance interval. Evidence playback adds this transport without baking it into the asset.
- Escort is follow-only. No officer grip, contact, attachment or two-character synchronization is authored.

## Clip/state contract

Six state loops:

| Clip | Seconds | Loop | Engine-owned travel |
|---|---:|---|---:|
| Civilian_StandingIdle | 4.0 | yes | 0 |
| Civilian_Walk | 1.2 | yes | 0.700 m/s |
| Civilian_KneelingSurrender | 4.0 | yes | 0 |
| Civilian_RestrainedKneelingIdle | 4.0 | yes | 0 |
| Civilian_RestrainedStandingIdle | 4.0 | yes | 0 |
| Civilian_RestrainedEscortWalk | 1.6 | yes | 0.31875 m/s |

Natural transition timing:

| Forward / reverse | Seconds each | At 60 Hz |
|---|---:|---:|
| StandToKneelingSurrender / KneelingSurrenderToStand | 3.6 | 216 ticks |
| KneelingSurrenderToRestrained / RestrainedKneelToSurrender | 2.8 | 168 ticks |
| RestrainedKneelToStand / RestrainedStandToKneel | 4.2 | 252 ticks |

All names have the `Civilian_` prefix. Keys are authored at 30 Hz, including both end samples. All six loop seams and twelve intended transition boundaries are numerically checked. The transitions deliberately exceed the engine's existing 72-tick / 1.2-second generic transition. Do not speed them up to fit that timer. Drive completion by actual clip time, or keep the candidate unavailable until the controller supports it.

Standing/walk and restrained-standing/escort entry and exit do not have dedicated authored clips. An engine blend with consistent transport and foot phase is required and is not yet accepted by these audits.

## Explicit unsupported-state fallbacks

| Unsupported request | Candidate fallback |
|---|---|
| Run/sprint | Clamp to unrestrained walk and its supported speed, or suppress request |
| Generic crouch locomotion | No crouch clip; hold last valid state and suppress movement request |
| Combat, weapon use, aiming | Suppress action; retain valid idle state |
| Seated, prone, ragdoll or injury performance | Unsupported; retain valid state or select a separately accepted asset |
| Officer-contact escort or arrest interaction | Use follow-only escort; do not imply physical hand contact |
| Dynamic/physics restraints | Unsupported; static evaluated anchors and a reviewed accessory are required |
| Interrupted kneel/rise transition | No arbitrary mid-clip reversal guarantee; complete/recover through named endpoints |

These are integration recommendations, not changes already made to the engine.

## Controller geometry mismatch

The model is not scaled to force generic controller measurements. Standing actual eye reference is about 1.680 m versus engine 1.6256 m; actual top is about 1.791 m versus engine 1.8288 m. Tall-kneeling eye reference is about 1.251 m versus engine crouch 0.8128 m; restrained tall-kneeling top is about 1.362 m versus crouch height 1.016 m. Exact samples and bounds are in the contact report. Do not use the generic crouch capsule/eye as if this were a matching crouch animation. Lower half-width 0.2032 m and upper radius ≈0.2873 m remain engine references, not source constraints.

The kneel uses knees/shins and the upper surface of plantar-flexed footwear as support. The soles face upward; the toe/sole undersides are not claimed as ground contacts. Review the side stills before accepting the support style.

## Wrist/cuff contract and unresolved accessory fit

The restrained wrists are relaxed behind the body, with elbows slightly behind/outboard, palms outward, thumbs upward and fingers downward. The forearms angle inward; they are not forced parallel. Actual wrist centres are 180 mm apart, instead of the provisional 120 mm. The hand/forearm bend is about 10.5 degrees. Do not force the source to the provisional spacing.

Contact local axes: +X points toward fingers along distal forearm; +Y is the actual hand palm normal projected perpendicular to X; +Z=X cross Y. Source and runtime contact transforms, both rest and evaluated poses, are recorded in the sidecars and numerical reports. Additional eye/sole markers are not joint frames.

The optional `runtime/BC_CIV01_cuff_fit_fixture.glb` and `runtime/BC_CIV01_connector_link_reference.glb` are dimensional review geometry, not approved gameplay cuff art. The connector uses 12 original rigid elliptical links with 5 mm centreline sag. Its placement recipe spans the evaluated endpoints without deforming any link. This is kinematic reference geometry, not a physical chain solver. An editable accessory-only Blender source is included. Inner ellipse diameters are 52×66 mm; outer diameters 64×78 mm; axial width 18 mm. Connector endpoints are contact-local [0,-0.030,0]. Their actual separation is approximately 126.4 mm, rising slightly during escort. A 60 mm connector cannot span that gap. Resolve accessory dimensions or design through review; do not stretch wrists or reuse an incompatible connector.

Fit tests sample actual wrist/palm triangles within the cuff slab and clothing/ring intersections. These are finite geometric tests, not skin compression, cuff safety, cloth simulation or continuous collision guarantees. The tightest reported clearance needs review.

Suggested visibility: hide the optional cuffs until the surrender-to-restrained clip finishes; remove them before the release transition starts. There is no authored officer fastening motion. The runtime character GLB contains no cuff mesh.

## Provenance and reproducibility

All meshes, rig layout, weight fields, poses, curves, palettes and weave maps were authored procedurally for this candidate. No FBX, downloaded character, stock animation, photographs, accepted officer geometry or third-party artwork was reused. There is no external rights dependency introduced by an asset import. Blender/ffmpeg/Python are build tools, not source assets.

Use Blender 4.3.2 and the versions recorded in `source/toolchain.json` (Python, NumPy, SciPy, Pillow and ffmpeg). The canonical, frozen mesh/rest/weights are in the packed Blender source. Re-export that source to a separate output directory for exact runtime reproduction. From this directory:

```
blender -b --python recipes/export_packed_source.py -- --out /tmp/civilian_source_reexport
python -m unittest discover -s qa -p 'test_*.py' -v
python qa/validate_civilian_glb.py runtime/BC_CIV01_candidate.glb --expected-animations 12 --source-inventory source/source_inventory.json --output qa/glb_validation.json
python qa/audit_clip_boundaries.py runtime/BC_CIV01_candidate.glb --output qa/clip_boundaries.json
blender -b -t 4 --python recipes/render_export_offkeys.py
blender -b -t 4 --python recipes/render_export_playback.py
blender -b -t 4 --python recipes/render_accessory_reference.py
blender -b -t 4 --python recipes/render_accessory_playback.py
python recipes/compose_review_panels.py
python recipes/compress_review_stills.py
python recipes/write_runtime_contracts.py
python recipes/encode_playback.py
python recipes/package_candidate.py
```

`build_civilian.py` is the original procedural construction recipe. Its voxel-remesh/decimate stage is not byte-deterministic across worker/thread settings and may yield different topology and weights; use it to make a new variant, never to replace the canonical mesh silently. It defaults to a separate temporary output root. `reanimate_packed_source.py` rebuilds the authored action bank on the frozen canonical mesh, without remeshing or rewriting weights. Actual packed-source re-export byte equality is recorded in QA.

Recipes currently use the declared `/workspace/shared/civilian_01` workspace root. Change BASE consistently when relocating. The Blender source opens independently with packed images. Scripts and source are editable; there are no baked opaque dependencies.

Two guarded export adapters are documented:
1. Blender 4.3.2 normally drops positive weights ≤0.0001. A process-local cutoff adapter preserves every positive influence, then restores the original method. Installed software is not changed.
2. A JSON-only adjustment of the two static cuff empty rotations preserves semantic local-axis labels across the exporter's Y-up basis conjugation. The animation/skin/geometry BIN chunk is copied unchanged.

Independent QA verifies actual exported weights and binds, rather than trusting those adapters. See `source/exporter_adapter.json`, `source/anchor_basis_adapter.json` and `qa/anchor_adapter_audit.json`.

## Review limits

Thumbs/fingers remain simple stylized cylindrical geometry. Raised sleeve caps remain slightly full in this stylized topology. Face, hands and clothing are deliberately simple; there are no facial expressions, morphs, LODs or authored collision proxies. Numerical passes do not certify anatomy, visual quality or engine integration. Playback evidence is encoded at 15 fps, at real 1x speed; original animation samples remain 30 Hz. The independent CPU evaluator uses shortest-path quaternion interpolation with a small-angle normalized-lerp approximation. Separate strict-slerp/source comparison quantifies that difference. Studio display recomputes smooth normals over decoded triangles and rebinds the original scalar palette; it is not a shader-fidelity comparison. Playback uses a neutral studio and original scalar palette; it is not footage from Briar Court. Final game admission requires the real parser, renderer, state controller, transport, capsule, eye and accessory tests.
