# Punisher Electrical intake — 2026-09-28

Source: user's fitting export and initial plus follow-up fitting screenshots. These
retain UI rounding. Profile: `data/profiles/punisher_t0_electrical_xray.ini`;
applied to `config/abyss.ini`. Fresh training is configured (`load_model_path=None`).
The former Dark fit is preserved in `tests/legacy_dark_profile.ini` so mechanics
regressions do not depend on the current training fit. Sanderling now loads the
same profile values for native weather/speed encoding and rules/PPO lock range;
see `HANDOFF_T0_ELECTRICAL.md`. Live weather strength remains assumed (default
50%, CLI override for 30%). Release controller suite: 317 tests passed.

Fit: 3 Extruded Compact Heat Sinks; Small Armor Repairer II; Multispectrum
Coating II; 1MN Y-S8 Compact Afterburner; Small Compact Pb-Acid Cap Battery;
4 Small Focused Beam Laser II; 2 Small Auxiliary Nano Pump I;
Small Energy Metastasis Adjuster I. Listed implant: CPU Management EE-601.
Ammo inventory: Aurora S x4, Imperial Navy Xray S x8, Imperial Navy
Multifrequency S x4. Listed filaments: Tranquil Electrical x15.

| Field | Screenshot value | Configuration interpretation |
|---|---|---|
| Shield | 367 HP, 531 s recharge | `ship_shield_hp`, `shield_recharge_time`; rounded |
| Armor / hull | 600 / 517 HP | `ship_armor_hp`, `ship_hull_hp` |
| Shield EM/Th/Kin/Exp | 0/20/40/50% | .00/.20/.40/.50 |
| Armor EM/Th/Kin/Exp | 65/54/47/44% | .65/.54/.47/.44; obtain precise values |
| Hull resists | 33% each | .33 each; verify precision |
| Capacitor | 611 GJ, 2m16s recharge | capacity 611; displayed recharge 136 s |
| Targeting | 27.50 km, 704 mm, 4 targets | lock_range 27500, scan_resolution 704 |
| Signature | 37 m | signature 37 |
| Navigation, AB off | 426 m/s, 1190 t, inertia 2.3171 | base_speed 426, ship_mass_kg 1190000 |
| Navigation, AB on | 969.53 m/s, 1690 t | prop_speed 969.53; added mass 500000 kg |
| Align / warp | 3.82 s off, 5.43 s on / 5 AU/s | recorded as cross-checks |
| Afterburner | 14 GJ / 8.5 s, +500000 kg | prop_cap_cost 14, prop_cycle 8.5 |
| Armor repairer | 119.59 HP / 4.8 s, 40 GJ | rep_amount 119.59, rep_cycle 4.8, rep_cap_cost 40 |

AB fitted velocity bonus shown as 143.8%, thrust 1500000 N. Use resolved
969.53 m/s directly, not another application of its bonus. Signature shown
as 37 m in both propulsion states gives prop_signature_multiplier 1.
Armor repair uses layer 1 and effect timing 1 (cycle end).

Displayed cap depletion 1m50s and -3.0 GJ/s are fitting-state summaries, not
recharge attributes or module costs. The defense panel says 1 HP/s, so do not
assume the displayed cap summary includes a cycling armor repairer.

| Crystal | DPS per gun | Four-gun DPS (derived) | Optimal | UI falloff-range endpoint | Tracking |
|---|---:|---:|---:|---:|---:|
| Imperial Navy Xray S | 35.7 | 142.8 | ~12 km | ~15 km | 124.20 |
| Imperial Navy Multifrequency S | 42.8 | 171.2 | 7920 m | ~11 km | 124.20 |
| Aurora S | 24.8 | 99.2 | ~29 km | ~31 km | 31.05 |

The UI's “falloff range within” is an endpoint, not the falloff attribute to
copy into `weapon_falloff`. Get precise optimal/falloff from fitted attributes.
Aurora tracking is one quarter of the faction crystals' displayed tracking;
its displayed optimal exceeds this fit's lock range.

Per-gun displayed EM/thermal volley components: Xray 65/43 HP,
Multifrequency 76/54 HP, Aurora 47/28 HP. Group Xray panel shows 433 HP volley;
4*(65+43)=432 differs due to rounding. Do not manufacture exact damage mixes or
cycle times from rounded components. The subsequent Xray crystal Attributes
screenshot resolves base damage to 6.9 EM + 4.6 thermal: use exact damage
fractions .60/.40. It also shows -25% capacitor need and -25% range; do not
apply these again to already-resolved fitted gun attributes. Xray's group volley/DPS implies roughly
3.03 s per cycle, useful only as a cross-check.

The fitted gun Attributes screenshot resolves activation cost to 3.25 GJ per
turret, optimal to 11880 m, falloff to 2875 m, tracking to 124.2 and displayed
damage modifier to 9.42. Four guns consume 13 GJ per paid cycle. Do not reapply
the crystal's -25% capacitor/range modifiers to these fitted values.
Cycle displays 3.0 s at one decimal; use an explicit estimate 3.0322 s from
433 / 142.8 until live cycle telemetry gives more precision. Group volley stays
433 (display-rounded); 9.42 * 11.5 * 4 = 433.32 is a consistency check using a
rounded multiplier, not a more precise measurement. Other crystals are deferred.
Rounded hull/pool/resistance attributes remain labelled as such. Screenshots
show fitting simulation in ordinary space; use the intended character and
unheated state when obtaining remaining attributes.

## Proposed staged experiment

1. Complete the fit and fix shared live/sim profile/weather inputs described in
   `HANDOFF_T0_ELECTRICAL.md`. T0 Electrical is tier 0, weather type 1.
2. Use fixed Imperial Navy Xray S for the initial T0 implementation and evaluate
   all recorded templates at both penalty strengths. User reports Xray-only is
   sufficient for T0; neither Aurora-only nor Multifrequency-only can clear T0
   in this fit. Treat this as live experience to reproduce, not a new sim result.
   Defer swaps. Aurora is a situational reach option; Multifrequency speeds up
   damage against suitable slow targets and can be necessary to kill before
   dying. Future swap objectives must include survival, not just completion time.
3. If necessary, add conservative ammo switching as a weapon transaction:
   request stop → observe cycle ended → select crystal using fresh UI → observe
   charge identity/whole-group readiness → settle → resume. No repeated clicks
   on ambiguous state. Keep navigation/repair independent of the weapon transaction.
4. Measure switch latency/failures live under supervision before choosing guard
   times; the user's reported fast-click bug is not a verified timing constant.
   Require sustained expected applied-damage gain to repay lost firing time,
   minimum dwell time and hysteresis. Prefer idle windows where useful, but
   do not assume the next room's composition is known in advance.
5. Start with a deterministic switch controller and explicit ammo/pending/delay
   state in sim. A learned ammo head is a later ABI/retraining change. Crystal
   breakage and group recovery also need observation, even for fixed-ammo runs.

Drones are a separate larger sim/control project: per-drone position, travel,
attack cycles/application, health, NPC target selection, launch/engage/recall,
recall delay and suppressor exposure. Begin with recorded drone runs to bound
those mechanics if chosen; a constant extra-DPS approximation cannot validate
drone survival or reliability. Keep T1 deferred until T0 live parity and T1
scenario generation are both addressed.

The rules evaluator now emits complete weapon/propulsion/repair desired state
every tick and sets weather strength before reset applies resistance effects.
It remains an armor-only heuristic baseline, not the live policy or a fit oracle.
Terminal metrics now come from accumulated `env.log`, because the environment
automatically resets world state on episode termination. The evaluator asserts
the expected episode count and nonzero elapsed ticks.

## First simulation baseline

Native mechanics smoke and ASan/UBSan passed. Rules strategy 2 (`threat_rush`),
100 episodes per template for all 28 templates at each penalty, RNG seed 1 per
invocation. Results: `data/punisher_xray_rules_initial.json`.

| Electrical penalty | Episodes | Completion | Combat/all deaths | Timeouts | Mean ticks |
|---|---:|---:|---:|---:|---:|
| 30% | 2800 | 92.6071% | 7.1071% (all deaths) | 0.2857% | 549.65 |
| 50% | 2800 | 90.7857% | 9.1429% (all deaths) | 0.0714% | 500.83 |

No cap-dry or repair-starved episodes in this baseline. Mean ticks include
early deaths and must not be read as successful-run completion time. Weak
templates include 5, 7, 9, 16 and 23. This tests a simple rush/approach heuristic,
not a trained Punisher policy, and does not overturn the user's Xray-only live
experience. Diagnose steering/damage application and replay parity before
attributing failures to the fit or implementing swaps. No training/live run yet.
