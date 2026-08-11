# Kaggriculture experiment DAG

This is the execution graph for repairing the GPU training path and improving
the standing champion. A branch is promoted only from a both-seat evaluation
against the same native `top` opponent and the current champion.

## Standing baseline

- Champion: `saved/kaggriculture_hall_of_fame/expL_anim_top_73m.bin`
- Architecture: 128 hidden, 2 MinGRU layers
- Historical fixed-eval result: 9,005 money versus `top`
- Fresh CPU diagnostic (seed 42, 100 games/seat): $9,005 as player 0 and
  $7,254 as player 1; conservative both-seat mean $8,130
- Kaggle public score: 600.0

## Gates

1. Correctness gates must pass before training starts:
   - CPU/CUDA reward parity with nonzero terminal margin and inactivity knobs.
   - CPU/CUDA state, observation, mask, reward, terminal, and reset parity.
   - BC held-out active-head accuracy and closed-loop opening replay.
2. Screening uses at least 100 games per seat against `top` and ExpL.
3. A candidate is a champion only if it improves conservative both-seat
   money against `top` without a catastrophic head-to-head regression.
4. Every experiment records its parent, exact INI, checkpoint, seed, result,
   and conclusion in `RESEARCH.md`.

## Graph

```text
P0 preserve remote artifacts
  -> P1 repair CUDA reward parity
      -> D0 scripted top opening -> ExpL handoff diagnostic
      -> B0 repair opening BC pipeline
          -> B1 ExpL + recurrent opening BC patch
              -> R0 root/reset mixed PPO, no EMAg
              -> C1 first corrected champion
                  -> E0 matched no-EMAg / EMAg A/B
                  -> B3 BC dose/basin refinement
                      -> C2 second corrected champion
```

## Node definitions

### P0: preserve and identify artifacts

Copy the Vast hall of fame and experiment INIs locally. Record hashes before
changing code. Do not rely on the instance filesystem as persistent storage.

### P1: reward parity

Make CUDA apply the same terminal relative-money margin and configured
inactivity threshold as CPU. Add targeted cases that would fail if either
knob were ignored. Re-run the existing randomized adapter parity suite.

### D0: opening bottleneck diagnostic

Evaluate the unchanged ExpL weights in two modes:

- normal root control;
- the native top opening controls both model seats through turn 26, then ExpL
  takes over.

Run both seat assignments against `top`. This is diagnostic scripting, not a
trained policy. A material gain proves that root opening quality is a primary
bottleneck.

Result (2026-08-11): **rejected**. Exact native top actions through turn 26
gave $9,095 as player 0 but only $2,010 as player 1, a $5,553 both-seat mean.
The unmodified ExpL control scored $9,005/$7,254 ($8,130 mean) under the same
100-game-per-seat diagnostic. The opening alone is not the bottleneck: ExpL's
continuation is incompatible with the scripted farm/economy state. BC remains
a controlled low-weight branch, not the presumed mainline.

### B0: repair BC

- Honor `bc.steps`; opening datasets stop at the requested prefix.
- Preserve episode boundaries and train contiguous recurrent sequences.
- Compute imitation loss only for conditionally reached heads.
- Support initializing from an existing PufferNet checkpoint.
- Split train/validation games.
- Report active-head accuracy, exact executed-action accuracy, and closed-loop
  opening divergence on held-out seeds.

### B1: supervised opening patch

Initialize from ExpL and imitate top opening sequences through turn 26. Keep
later competence with either a small update budget or frozen ExpL distillation
on mid/late states. Do not use EMAg as a substitute for a frozen teacher.

Result: opening-only BC is rejected, but the repaired trainer exposed the
successful branch B2: imitate complete 719-decision top trajectories. The
25-epoch ExpL warm start reached $9,226 versus top over 500 games, versus
ExpL's $8,049 (+14.6%), and stayed effectively tied head-to-head (49.6%).
Training longer was harmful ($8,553 at 50 epochs; $6,874 at 100). This model
is promoted as C1: `expR_fullbc_top_e25.bin`.

### R0: DAGS-style reset mixture

Use an explicit `reset_opening_prob` independent of the sampled prefix range.
Initial branch:

- 50% genuine turn-zero episodes;
- 50% top-opening reset states sampled from turns 10 through 80;
- one observation bit identifying root versus reset source;
- full 720-turn episodes;
- 100% native `top` opponent;
- no EMAg.

The reset branch teaches continuation. Root episodes plus BC teach the actual
opening. Early episodes are not truncated as terminal games.

Result (2026-08-11): **small transient gain, not promoted**. Root-only
screening put the 5.24M checkpoint at $8,523 versus the sanitized ExpL's
$7,973 on 100 common-seed games, but all later checkpoints decayed. A
500-game confirmation reduced the advantage to $8,134 versus $8,049, while
the candidate lost the direct matchup 48%-52%. The explicit mixture works,
but the current update/reward recipe is not stable enough to clear the
champion gate.

### E0: controlled EMAg comparison

Start two runs from the exact same C1 checkpoint with identical seed, data,
opponents, and PPO hyperparameters. The only difference is EMAg coefficient
and tau. The conditional objective was source-audited before the comparison:
it computes `KL(magnet || policy)`, uses the `p - q` logit gradient, and weights
each child by its probability of being reached under the magnet rather than by
the sampled rollout path. The BC teacher, if used, remains frozen and separate
from EMAg.

Result: the moving EMAg reference stabilized R0's later checkpoints but did
not improve the confirmed policy. At 500 fixed-top games, ExpL scored $8,049,
R0@5.24M $8,134, and EMAg@20.97M $7,991; EMAg ranked last in their direct
matrix. Moving EMAg is not promoted. A second run used C1 as an actually
frozen (`emag_tau=0`) conditional KL reference during root PPO. Its screened
10.49M checkpoint scored $9,088 over 500 fixed-top games versus C1's $9,226
and tied C1 exactly head-to-head, so frozen-reference PPO is also rejected.

### B3: BC dose and basin refinement

The full-trajectory BC peak was refined without changing architecture, data,
initial policy, or optimizer. A 15/20/25/30/35 epoch curve confirmed that 25
epochs is the closed-loop optimum even though held-out imitation accuracy
continues increasing. Twelve independent episode split/shuffle seeds were
then trained at that fixed dose.

Seed 303 cleared two independent fixed-top confirmations. In the final
2,000-game block it earned $9,345 versus C1's $9,208. Their direct match was
statistically tied (49.95% C2 wins), but C2 earned $13,103 versus $13,045;
combined with the preceding 1,000-game block, C2's direct win rate is 50.43%.
It is promoted as the stronger top-specialist while C1 remains preserved:

`saved/kaggriculture_hall_of_fame/expS_fullbc_seed303_e25.bin`

Fixed-opponent evaluation now uses common random numbers for every candidate,
with `KAG_FIXED_SEED_A/B` overrides for genuinely fresh confirmation blocks.
Previously, `eval_population.sh` varied the world seed by policy index, which
added avoidable ranking noise to close promotion decisions.

### P2: top-opponent oracle parity repair

Closeout testing found that the Python top export and native C opponent
diverged at turn 26. The Python continuation schedules fertilizer collection,
animal-product harvest, feeding, and care before crop jobs; native C omitted
that entire pass. The native planner now uses the identical row-major animal
job order and tie-breaking. Full 719-decision parity passes for seeds 7, 42,
and 707.

The prior fixed-top dollar figures describe the pre-repair opponent and are
retained as experiment history, not current benchmark values. A fresh common-
seed 1,000-game panel against the oracle-exact opponent gives:

| policy | money | win rate |
|---|---:|---:|
| ExpL | 20,121 | 89.0% |
| C1 | 21,593 | 98.6% |
| C2 | **21,814** | **98.7%** |

C2 also beats C1 directly 53.5%, earning $13,311 versus $12,749. Thus the
second promotion remains valid—and becomes much clearer—after oracle repair.
A regenerated animal-oracle dataset was tested at 1/3/5/10 BC epochs from C2.
Every dose was harmful; one epoch already fell to $21,587 and lost C2 44%-56%,
with further doses degrading monotonically. The animal oracle remains an
opponent, not an imitation target.

This game remains non-transitive: ExpL narrowly beats C2 directly (51.6%) even
though C2 is dramatically stronger against the target top bot and beats C1.
Keep ExpL, C1, and C2 rather than replacing the hall of fame with one file.
The Kaggle-ready C2 archive is
`ocean/kaggriculture/submission/pufferlib_expS_fullbc_seed303_e25.tar.gz`.

## Rejected shortcuts

- One-action opening masks do not teach the opening policy.
- Reset-only training does not teach actions before the reset state.
- `magnet_path` is only EMAg initialization; it is not a frozen BC anchor.
- Productive-action bonuses invite action/order spam.
- Short terminal games change the objective unless treated as bootstrapped
  rollout cuts rather than environment terminations.
