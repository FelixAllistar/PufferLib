# Crouch Forward / Shared Ready N C1 Loop A

**Qualified motion, continuity and fitted-carry candidate with a substantive stored-accessory collision. It is not clearance-approved.** The unchanged Hips-skinned spare sleeve overlaps raised left outer-thigh hardware: a source sleeve point lies up to **18.392 mm inside closed hardware component 17904 at frame 3 / 0.100 s**. Visible stored spare B and the sleeve are preserved. No mesh mutation, prop hiding, source-stride change or accessory offset is baked into this export.

- GLB: `crouch_forward_shared_n_c1_loop_a.glb`, 5,847,176 bytes
- SHA256: `ea828e53f305de77b35ba55abdf67470ae98e871911c43fae5505c5022d47dd1`
- Exact action: `Crouch Forward / Shared Ready N C1 Loop A`
- Clip ID: `crouch_forward_shared_n_c1_loop_a_original_1s`
- Source editable SHA256: `897771dc55846b4ad2b1181fcb560a55d364a4e22fc5510177b905bbb28ee442`
- Source checkpoint: 17,482,700 bytes, SHA256 `e63cd72bbfcc43245a6eebc206cc5c89d15522c1bba10e3f2a6f43e08a1cf858`
- Duration: exactly 1 s, frames 0–30 at 30 fps; original human source 60 Hz, frames 1–61

## Accessory and body qualification

The 18.392 mm figure is a sampled inward distance established against a closed hardware shell after coincident-seam welding, with accepted containment samples checked by three parity rays. It is **not** a crossing chord or a minimum displacement required to separate the assembly. Body surfaces also lie up to approximately 4 mm inside the sleeve's solid walls. Ambiguous samples were excluded. The spare magazine/pants shells are open, so no solid inward depth is claimed for their 12 actual crossings.

Sleeve/body pairs range 28–148 over the source review:28 are the old belt mount; 120 additional pairs at frame 4 involve raised-thigh hardware/pants. This substantial dynamic accessory problem exists in native carry before the seam repair and persists in the unchanged middle interval. The package includes source phase/contact evidence in `SOURCE_accessory_inward.json`, `SOURCE_compact_cycle_contacts.json`, `SOURCE_cycle_contact_changes.json`, and worst-frame 3 views prefixed `source_spare_worst_`. These are preserved source-review measurements, not a new runtime collision solver or export depth certification.

The source review also retains upper-hip garment crossings, shoulder-root folds, native lower pants/brace compression, stretched/flattened connecting panels and local normal-transport reversals. Arm/forearm-dominant vest and rifle/nonhand crossings pass the stated source samples, while complete sleeve roots retain 50/63 vest pairs. Opposing leg-weight tags resolve to existing upper-hip garment families; there is no global or continuous collision-clearance claim. `SOURCE_INDEPENDENT_CROUCH_FORWARD_REVIEW.md` and its qualification JSON define the full scope.

The engine may apply its own runtime accessory-visibility policy. This source GLB and its reference replay keep the authored sleeve and visible spare, independent of a renderer choosing to hide an attachment. Sampling this clip does not grant inventory, persistent magazine/pouch IDs, ammo or chamber state. Gameplay authority owns those states and any eventual accessory representation decision.

## Source timing and preservation

The source has 700 curves / 168,700 keys; **683 scalar curves** receive boundary edits only in 0–0.125 s and0.875–1 s. The source center 0.125–0.875 s is unchanged in its exact saved evaluation/checked surfaces; exported floating-point differences are measured separately. `SOURCE_SAVED_CURVE_DIFF.json` preserves every key/handle/interpolation edit. The original 170 human body/leg/spine/gaze curves are preserved before the bounded repair, including the existing native-knee-plane grounding treatment.

Original baked displacement is approximately (−0.0000000815,−2.0390741825,−0.0000002384)m in Blender coordinates over 1 s. Nominal baked pace is **2.03907418251 m/s**; fresh double-precision endpoint subtraction gives 2.03907425329 m/s, a 0.071 micrometre stride difference. Both measurements and their provenance are retained. Root travel was removed before this source; the GLB is in-place with identity armature root. These are native source references, not selected gameplay speed or newly animated root motion. No runtime retiming, phase bake, controller movement or blend is applied.

All shared native geometry, complete weights, binds, UVs and static glTF normal/material data are preserved. A visible stored spare is authored in the hips frame; it is not the zero-scale spare state from upright walk. Independent replay finds about 0.082 mm of native hips-relative spare positional variation between keys, also present in source interpolation, so exact rigid relative motion is not claimed. All source meshes remain enabled, with no ownership transfers or authored footstep/fire/reload events. Loop wrap preserves identities; runtime owns magazine_seated, inventory, replay and save/restore persistence.

## Actual cubic binary and reimport checks

All 210 TRS channels are CUBICSPLINE, each with 241 keys. Independent binary evaluation includes all 70 joints and every one of 48,755 exported vertices with all influences. Maximum joint endpoint matrix element gap is **0**; skinned endpoint positions are equal under this evaluator. Worst analytic joint wrap residual is **5.24048149e-05 m/s** and **0.00712811949°/s**. Body surface residual is **5.56256789e-05 m/s**, rifle **1.19746517e-05 m/s**, visible spare **1.20454827e-05 m/s**. No collapsed joint is excluded here.

`EXPORTED_CUBIC_VALIDATION.json` and the independent review retain analytic derivatives plus practical 60/240/480/960 Hz wrap chords and shrinking-epsilon convergence. Finite chords include boundary acceleration and are not the same quantity as the mathematical one-sided derivative. `finite_difference_convergence.png` shows this distinction. The tiny-epsilon evaluation uses float64 arithmetic on stored float32 values; it does not certify runtime float32 differentiation.

Independent saved-scalar comparison spans 961 phases, including 721 middle samples. Middle body error reaches **0.428857 micrometres**; sampled middle derivative differences reach **0.000106940722 m/s** and **0.0301876873°/s**. Exact algebraic polynomial preservation is not claimed.

The separate native and Blender reimport check covers **1,243 poses**: complete 960 Hz grid, all source/export keys, boundary/epsilon probes and 16 actual Blender-evaluated controls, including the 0.1 s accessory-defect phase. Maximum native-to-GLB error is **3.772125 micrometres**, explicit cubic-aware reimport **3.916969 micrometres**. Exact native rest positions, raw weights, oriented triangles, rest matrices and parent hierarchy agree with the shared static template.

**Stock Blender 4.3.2 import discards cubic tangents.** Its separate maximum vertex error is **2.372351 mm**. That stock round-trip is not qualified. The successful cubic-aware check explicitly drives the imported bind skeleton/geometry from serialized Hermite evaluation. Matched `native_phase000/950.png` and `cubic_reimport_phase000/950.png` were inspected; stock counterparts are diagnostics. Static images do not prove velocity. The included source comparison video and source collision views are not engine playback.

## Conversion and skin contract

There are 40,980 source BEZIER spans and 127,020 LINEAR spans. All vector component grids already match. Linear portions use their secant derivatives in CUBICSPLINE. Saved Bezier time handles differ from exact thirds by up to 6.35782879e-07 frame; 15 probes per cubic span found Hermite approximation error up to 8.80810724e-09 scalar units. Raw quaternion key norm deviation is 8.24907544e-08. Unit keys and derivatives projected through normalization are transformed by the fixed rest quaternion; resulting numerical errors are measured above.

Tangents are derivatives per second. Multiply each span's tangent terms by its duration once, evaluate component Hermite and normalize the resulting rotation, including exact key evaluation. Preserve incoming/outgoing tangents; do not normalize tangent vectors, switch to SLERP/LINEAR or independently flip signs. Unused first incoming/last outgoing tangents are zero. `conversion_report.json` records all channels; `source_representability.json` records source limits. Literal losslessness or exact-arithmetic C1 is not claimed.

The body retains all seven influences and JOINTS_1/WEIGHTS_1. There are 444 seam-split body vertices above four influences. Normalize across all combined sets; truncation is invalid. All rendered bind/weight vertices and oriented triangles are preserved; the same six unused rifle vertices are omitted. Eight-influence preflight passes; four-influence preflight deliberately rejects. Static position/normal/UV/index arrays and neutral material assignments remain compatible with the separate nine-map 2K texture package. No image or tangent is fabricated; runtime material/normal appearance remains consumer validation.

## Measured fit and corrected attachments

`measured_cycle_fit.json` gives actual exported hips, visor, stock, muzzle and sight relationships over all 1,243 capture times. Physical rifle elevation is approximately **-20.294396° to -18.931747°**. The native 241-sample body-top range is 1.306250–1.353069m, hips 0.643519–0.689466m, visor surface 1.125936–1.172498m. Source 960 Hz sole minima are 3.336839 mm left/3.891718 mm right, with no samples below 3 mm. This remains authored spacing, without a new ground solve or motion foot-lock guarantee.

The source rear-stock center stays approximately 2.191733–2.192016 mm from supporting gear. Actual gloves transfer N's rifle-relative fit within the recorded micrometre-level native tolerance; no new IK target or flush full-pad seating is approved. The visor-to-sightline gap remains approximately 128.486–136.378 mm, and its surface normal differs from the sightline by 8.006–8.802°. This is low-ready reference geometry, not ADS or an anatomical/camera calibration.

Units are metres; Blender(x,y,z) becomes glTF(x,z,−y), with +Y up. The engine reference is +X forward/+Y up/+Z right. `measured_preview_fit.json` supplies an unbaked scale-one root/heading/sole preview transform. Native foot-support vertex IDs explicitly map to exported primitive/vertex IDs; do not treat original Blender indices as seam-split indices. The source QA's own selection and sample scope remain in its reports.

Both rifle and optical attachments use **W_node × IBM[resolved skin slot] × B_bind_mesh**, equivalently W_node × J with J=IBM×B. W_node is actual scene-node world before inverse bind. Prop_Rifle currently maps node 67→slot 67; Head maps node 1→slot 5. Apply external actor/preview placement once, and do not apply IBM twice to an existing deformation matrix. Full maps, both representations and physical points are supplied. Legacy Muzzle/RifleSocket helpers are not physical-tip/stock calibration.

The optical reference is the virtual midpoint of two actual Head-weighted goggle-surface vertices, not an anatomical eye, IPD, optical nodal point, camera origin or eye relief. `optical_proxy_bindings.json` and its portable 16-control replay distinguish dense source references from explicit Blender-evaluated controls. Camera/controller stance, collision representation, transitions, runtime blends/GPU behavior and ADS approval remain outside this fixture.

`REPRODUCE.md` gives independent actual-binary replay and native/reimport commands. All source poses, previous archives and shared geometry remain unchanged. Source-only flags such as GLB_export_qualified:false describe earlier source review; this package establishes numerical export fidelity separately, without clearance approval. The material is private licensed source with no public redistribution authorization.
