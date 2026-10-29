# Poker gen5: reward scale vs the raise-size collapse (2026-10-29, box 57608917)

gen4 recipe (config/poker.ini, states off, 256x3 from scratch), one change per run. Scripts gen5_ab.sh, gen5b.sh;
results.txt / results_b.txt / eval_*.txt; checkpoints leagues/poker/gen5/*.bin (gitignored).

| Run | Steps | Entropy | Raises all in | Panel worst (offline) | vs web field | All in vs rules / range |
|---|---|---|---|---|---|---|
| control | 300M | 0.022 | 98% | -1.15 | -0.09 | 28 / 27% |
| ent_coef 0.02 | 300M | 0.024 | 98% | -1.01 | -0.38 | 28 / 26% |
| reward_scale 0.1 | 300M | 0.038 | 91% | -0.65 | -1.46 | 26 / 27% |
| reward_scale 0.03 | 300M | 0.44 | 16% | -1.08 | -1.09 | 7 / 12% |
| reward_scale 0.01 | 300M | 0.94 | 11% | -3.61 | -2.98 | 8 / 12% |
| **reward_scale 0.01** | **1B** | 0.68 | 10% | **+0.20** | **+0.12** | **4 / 7%** |

- The collapse to fold / call / all in comes from the reward scale: with raw BB advantages (the trainer does not
  normalize them) the entropy bonus is negligible and the raise sizes die in the first ~100M steps. A 10x entropy
  coefficient does not fix it; training in units of 100 BB does.
- reward_scale 0.01 at 1B is the first policy positive against every panel bot offline (rules +0.20 is the worst; gen4
  -0.90), with sized raises: unopened vs the web field 29% call / 36% small / 28% medium / 4% big / 3% all in.
- Still not converged (entropy 0.68). Self-play copies still end all in in 31% of hands (re-raise wars).
