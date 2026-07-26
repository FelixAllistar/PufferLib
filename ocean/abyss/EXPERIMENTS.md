# Abyss experiments

All evaluations are stochastic and use the native 5c evaluator. Completion,
survival, and cache counts are measured over complete three-room episodes.
Fixed-scenario evaluations repeat the same generated three-room template.

| Run | Physics and objective | Episodes | Completion | Survival | Caches | Length | Notes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `1785003669124` | old best, missile range/application and passive shield recharge added after training | 10,061 random | 0.9894 | 0.9924 | 2.9915 | 172.2 | Policy was trained with learner-clipped terminal rewards. |
| `1785007043453` | fresh, corrected missile physics, shield recharge, and learner-visible rewards | 10,025 random | 0.9832 | 0.9883 | 2.9772 | Entropy 1.339 at 100M steps. |
| `1785007043453` | same, fixed scenario 7 | 5,020 | 0.9922 | 0.9922 | 2.9962 | Deaths concentrate in room 3. |
| `1785007043453` | same, fixed scenario 9 | 5,006 | 0.9005 | 0.9005 | 2.8140 | 39.0% cap-dry rate; 26.1% repair-starved rate; deaths concentrate in room 1. |
| `1785007043453` | same, fixed scenario 11 | 5,013 | 0.9202 | 0.9202 | 2.8290 | 16.4% cap-dry rate; 12.3% repair-starved rate; deaths concentrate in room 1. |
| `1785007688050` | fresh, same as above with entropy annealed to 1% | 10,006 random | 0.9848 | 0.9889 | 2.9786 | Small improvement; cap-dry rate fell to 2.4%. |
| `1785007688050` | same, fixed scenario 9 | 5,010 | 0.9162 | 0.9162 | 2.8267 | Combat deaths remain concentrated in room 1. |
| `1785007688050` | same, fixed scenario 11 | 5,000 | 0.9304 | 0.9304 | 2.8380 | Combat deaths remain concentrated in room 1. |
| rules oracle | threat wait, 25% capacitor repair reserve, 30% Dark, scenarios 9 and 11 | 10,000 | 1.0000 | 1.0000 | 3.0000 | Both hard templates are reliably solvable at 30% weather. |
| rules oracle | same, 50% Dark, scenarios 9 and 11 | 10,000 | 0.4761 | 0.4767 | 1.4295 | The 10 km Pacifier orbit is outside 50%-Dark optimal and near the fit's stochastic survival limit. |

## Findings

- Native PPO clamps each learner-visible reward to `[-1, 1]`. The previous
  `+25/-25` terminal rewards and `+1.1276` loot reward therefore did not express
  the intended hierarchy to the learner even though the dashboard displayed
  their unclipped sum.
- Missile-only NPCs previously dealt guaranteed full DPS at unlimited range.
  Turret and missile damage are now separated; missiles use range, explosion
  radius, explosion velocity, damage-reduction factor, target signature, and
  target speed.
- Passive shield recharge was absent and is now integrated using the nonlinear
  EVE recharge curve.
- The remaining failures are not uniformly distributed. Generated scenarios 9
  and 11 are the primary robustness tests and currently fail by combat death,
  not by gate or cache timeout.
- Entropy annealing improves random completion and hard-scenario survival only
  modestly. It is not the root cause of the hard-template failures.
- Raw live captures confirm that the Attacker Pacifier settles near 10 km. At
  50% Dark, the configured Scorch profile falls to 7.95 km optimal plus 1.44 km
  falloff. A replayable rules trace shows the fit exhausting capacitor while
  repairing and firing through low hit chance. Weather-stratified oracle tests
  complete both hard templates 100% at 30% Dark but only about 47.6% at 50%.

## Collision timeout investigation

Checkpoint `1785012063718/0000000099876864.bin` completed each of the 28 fixed
scenario templates without a timeout over 56,000 episodes, while mixed random
evaluation retained a roughly 0.35--0.60% timeout rate. Gated failure records
reproduced the random-only failures and showed the same geometry every time:

- a 30-sphere giant-rock collider was present;
- the ship had exactly zero obstacle clearance;
- one or more spheres blocked the active cache or conduit segment;
- the persistent interaction and navigation target were already correct.

The old response projected the ship onto each sphere and removed inward
velocity. At seams in the overlapping-sphere union this could pin the ship
forever. Collision response now preserves speed along a deterministic surface
tangent, and randomized collider placement rejects layouts that block any
mandatory origin/cache/conduit corridor.

An unchanged-checkpoint evaluation after the geometry correction produced:

| Episodes | Completion | Survival | Death | Timeout | Cache timeout | Gate timeout | Length |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20,051 | 0.997207 | 0.997257 | 0.002743 | 0.000050 | 0.000050 | 0.000000 | 190.41 |

The single remaining timeout had no collider blocker and no pending
interaction, so the reproducible geometry defect is isolated from ordinary
policy error. Native, ASan/UBSan, corridor-generation, and persistent
interaction regression tests cover the correction.

## Next measurements

- Evaluate every generated episode template independently.
- Add weather-conditioned completion/death telemetry.
- Establish the best reachable rules baseline for the 50% Dark Pacifier case.
- Train with measured hard-template exposure only after separating policy error
  from unavoidable fit/combat-RNG risk.
