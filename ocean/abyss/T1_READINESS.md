# T1 Electrical implementation checklist

Implementation update: T0 and T1 are implemented in the same `abyss` environment,
selected by `filament_tier`, with shared mechanics and observation ABI v2.
See [tier usage and calibration limits](TIERS.md). T1 has offline tests, not live
validation. The separate environment packaging was removed.

The remainder records the original pre-implementation audit, for provenance.
Corrections found during implementation: there are 19 sampled families; Marshal
missile DPS was already populated separately from its blank turret-DPS field.

## Encounter generation

- The QSNA snapshot has 18 sampled Calm archetypes and 53 sampled NPC types;
  all 53 names are present in `data/npc_stats.csv`.
- Current runtime still samples the 28 recorded T0 episodes. Setting tier=1
  alone does not produce T1 encounters. Generated rooms cap hostiles at 3;
  sampled Calm compositions reach 8 before considering spawned auxiliaries.
- QSNA probabilities are marginal summaries, not recorded joint compositions.
  Build a constrained synthetic sampler, validate room count bounds, and label
  synthetic layouts/compositions explicitly. Keep recorded T0 sampling intact.
- Expand the live bot's NPC mapping together with the generated catalog.

## Mechanics that must precede a trustworthy T1 evaluation

- Retain the new local/remote repair model; remote target selection remains an
  approximation that needs live validation.
- Model neut range, falloff, cycles and the player's battery resistance. Current
  sim subtracts a flat capacitor amount each tick regardless of range.
- Add tracking/range disruption, webs, painters and sensor damping, with explicit
  activation/range/stacking assumptions sourced from NPC dogma. Scrams also need
  correct effects for the propulsion module actually fitted.
- Add Triglavian damage buildup and its target-switch/reset behavior.
- Preserve encounter-specific initial damage. CSV armor_dmg, shield_charge and
  item_damage currently do not affect initial NPC HP; damaged bosses spawn full.
- Account for Vila auxiliaries and their damage; parent gun DPS is insufficient.
- Audit special weapon mechanics and incomplete data. Attacker Marshal Disparu
  Troop has blank aggregate DPS fields in the CSV; resolve its weapon/missile
  data rather than silently accepting zero damage.

## Observation parity / EWAR sensing

The recorder has dogma decoding code, but that is not proof of reliable sensing.
The saved `captures/eve-dogma-final-test.json` has empty attribute maps in the
inspected module locations. Current canonical logging does not expose these
weapon fields, so absent log keys cannot establish whether recent reads succeed.
The decoder also has a fallback that accepts another item's attribute map when
the module's exact item is missing: remove that ambiguity before trusting it.

Preferred inputs are effective optimal, falloff, normalized turret tracking,
lock range and propulsion capability, each with availability/freshness. Validate
against the same fitted gun before/during/after disruption. Attribute 160's
normalized tracking must not be treated as radians per second.

If direct reads remain unavailable, derive an explicitly estimated EWAR state
from identifiable NPCs/ranges and any verified effect indicators. Run the same
estimator in training and live execution; randomize uncertain activation,
strength, latency and missing observations in the underlying simulation. Never
feed perfect hidden EWAR state to training while the live bot only gets estimates.
Unknown sensing must not silently mean an undisrupted weapon.

Adding observation fields requires a versioned sim/bot ABI and fresh compatible
training. Preserve the current 1224-float T0 policy contract; do not reinterpret
weather slots as tracking/range inputs.

## Rollout order

1. Complete mechanics/catalog tests and synthetic T1 generation in isolation.
2. Validate sensing or the shared estimator, including sensor loss and delay.
3. Train Xray-only Punisher on T1; inspect results per archetype, worst capacitor,
   armor, completion time and both Electrical penalty strengths. A high aggregate
   win rate alone can hide a consistently lethal rare encounter.
4. Compare limited live T1 recordings against matched simulated encounters before
   treating simulated clearance as evidence for progressing to higher tiers.
5. Add ammo changes only when encounter results establish a need, with confirmed
   deactivation/change/reactivation and measured delays. Player drones are a
   separate simulation/control task, not a shortcut around EWAR.

Later tiers can reuse the encounter/mechanics machinery, but still require a
per-tier coverage audit and fit viability checks; they are not guaranteed to be
only larger spawn counts.
