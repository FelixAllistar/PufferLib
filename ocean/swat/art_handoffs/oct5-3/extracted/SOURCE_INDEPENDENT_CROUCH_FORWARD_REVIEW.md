# Independent crouch-forward Shared Ready N review

Reviewed `Crouch Forward / Shared Ready N C1 Loop A`, SHA256 `897771dc55846b4ad2b1181fcb560a55d364a4e22fc5510177b905bbb28ee442`.

**Qualified as a native motion and loop-continuity candidate with a meaningful stored-accessory collision limitation. It is not clearance-approved.** All source/candidate files remained unchanged by this review.

## Source and continuity

Fresh FBX import confirms frames 1–61 at 60 Hz: 1.000 second and 2.039074 m of forward travel. The old grounded body is a sound baseline: its actual foot surfaces remain above 3.701892 mm left/3.891718 mm right at 960 Hz. The wrap velocity kink comes from the original motion: at 1/240 second, raw/grounded left-foot differences are 1.486111/1.486104 m/s and 473.363/473.362 degrees/s.

Final seam A keeps the 0.125–0.875 second center exactly unchanged in 960 Hz bone matrices and five complete body-surface checks. Its soles remain at least 3.336839 mm left/3.891718 mm right, with no samples below 3 mm. It changes boundary foot surfaces by up to 26.297 mm left/4.210 mm right. The author's saved analytic FK report matches this exact hash and records maximum endpoint derivative residuals 0.00005250 m/s and 0.007136 degrees/s with no unsupported FK flags.

## Carry and materials

Across all 61 source-rate phases, native body/rig/rifle identity matches static N. Arm/forearm-dominant vest and rifle/nonhand crossings are zero. Complete sleeve roots retain 50 left/63 right vest pairs, so this is not a globally clean garment. Rear-stock center support stays 2.191733–2.192016 mm. Actual whole-glove surfaces remain rifle-relative within 0.001028 mm left/0.000857 mm right at 960 Hz; the existing N grip qualification transfers within that tolerance.

The loop retains the earlier source material limitations. Opposing leg-weight tags resolve to existing upper-hip garment component families 5118/11165 and 11165/11623, not a new whole-leg family. Raw native carry has contacts at frames 29/29.5 (peak 22 pairs); seam A has contacts at 28.5/29/29.5 (peak 28). Observed maximum intersection chord decreases 14.998→13.447 mm. These are crossing chords, never penetration depths. No new body component-pair family appears over the sampled cycle. Native sleeve folds, lower pants/brace compression and local normal-transport reversals remain.

## Stored-accessory limitation

The fixed hip-relative spare sleeve intersects the raised left outer-thigh hardware and pants. Its body pairs range 28–148; 28 are the inherited belt mount. At frame 4, 120 additional pairs involve thigh hardware 17904 and pants 5118. The spare itself reaches 12 pants crossings. This issue exists before the seam repair and persists in the unchanged center.

The strongest confirmed sampled inward distance is **18.392 mm**: a sleeve surface point inside closed outer-thigh hardware 17904 at frame 3 (0.100 second). That hardware is closed after coincident seam welding and has no self-crossing pairs; three parity rays agree at accepted samples. Body surfaces also lie up to 4.000 mm inside the sleeve's actual solid walls. Ambiguous parity samples are excluded. This establishes substantial geometric overlap; it is not a minimum translation needed to separate the assembly.

The spare magazine and pants shells are open (30 and 74 boundary edges), so no solid inward depth is claimed for their 12 actual crossings. These are not measured zero-depth contacts.

Direct original-geometry pixels show the sleeve bottom meeting the thigh rail. The assembly should retain an explicit fit limitation until a consistent accessory representation/offset is chosen separately. This review does not move the sleeve, hide the spare, change native legs/timing, or modify a shared mesh.

## Evidence and scope

`INDEPENDENT_CROUCH_FORWARD_QUALIFICATION.json` is the compact gate. `compact_cycle_contacts.json` retains per-phase variable accessory pairs and body family ranges. `accessory_inward.json` records actual sample points, topology and containment methods. `cycle_contact_changes.json` records inherited/new pair and material changes. Source, grounding and 960 Hz surface reports are separate.

Viewed original final-pin pixels: `spare_left_side.png`, `spare_front_oblique.png`, `spare_rear_oblique.png`, `opposing_leg_front.png`, `opposing_leg_back.png`. Worst inward-distance phase renders are `spare_worst_frame3_side.png` and `spare_worst_frame3_oblique.png`.

This is a native editable motion study. Sampled checks do not establish continuous collision clearance, literal sole contact, physical balance, ADS readiness or engine/runtime acceptance.
