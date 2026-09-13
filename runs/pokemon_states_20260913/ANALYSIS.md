# State-bank pilot: completed analysis

Completed 2026-09-13. All four runs and all 16 checkpoint evaluations finished;
the runner verified immutable input contracts and wrote `complete.json`.

## Result

Each run trained for 19,922,944 total agent steps (including frozen opponent rows).
Each checkpoint was evaluated on 512 ordinary draft-to-terminal games across
four held-out opponents, two fresh evaluation seeds, and both seats.
The parent scored 81.4453% on the same panel. Scores count draws as half a win.

| Training seed | Normal starts | 50% bank starts | Reset minus normal |
|---|---:|---:|---:|
| 6101 | 78.5156% | 79.4922% | +0.9766 percentage points |
| 6102 | 79.4922% | 80.2734% | +0.7812 percentage points |
| Mean | 79.0039% | 79.8828% | +0.8789 percentage points |

The reset arm finished slightly ahead in both seeds, but neither arm improved on
the parent. No intermediate checkpoint exceeded the parent's panel score either.
This is exploratory evidence of a small relative benefit under this exact setup,
not a demonstrated improvement in absolute strength, learning speed, or equilibrium
quality. Two training seeds and a shared finite opponent panel do not establish a
reliable general advantage. Full learning curves are in `REPORT.md` and `report.json`.

## Timing

| Seed / arm | Trainer process seconds | Median displayed agent SPS |
|---|---:|---:|
| 6101 normal | 136.52 | 169,500 |
| 6101 resets | 169.72 | 126,650 |
| 6102 resets | 147.28 | 142,700 |
| 6102 normal | 192.37 | 107,400 |

Trainer-process times include startup and shutdown; the curve report instead
records elapsed time to each saved checkpoint. Evaluation is excluded from these
times. Bank collection adds 305.22 seconds once, before treatment training.
Arm execution order was counterbalanced. Timing differences change sign across
seeds and throughput fluctuated considerably, so there is no robust end-to-end
speedup claim. The CPU-only random-action benchmark excludes neural inference and
learning and must not be interpreted as training speed.

## Bank and contract checks

- 45,938 legal-play snapshots, about 98.84 MiB: 1,079 opening, 20,574 midgame,
  and 24,285 endgame positions, collected from 8,192 games.
- Independent provenance audit verified phase partitions, per-game caps, and
  all 149 species represented in each phase. This does not imply uniform or
  comprehensive strategic coverage. Opening states are 1,024/1,079 exploratory
  legal drafts after deduplication, an important distribution caveat.
- Both treatment runs recorded about 50% reset episodes. Final logged reset-step
  shares were 40.55% and 37.76%; conditional bank-start mix matched 40/40/20.
  Controls recorded zero bank starts throughout logged telemetry.
- Final weights are finite in all four runs. All 128 reset-flag encoder weights
  stayed zero in controls and became nonzero in treatments. Root evaluations
  force ordinary starts and leave the source flag at zero throughout each game.
- Both players' recurrent memory is cleared on bank starts. This is deliberately
  auxiliary, memory-truncated continuation training, not faithful reconstruction
  of the collecting policies' private recurrent histories.
- Native sanitizer, restoration/reseeding, observation/mask, flag compatibility,
  engine, migration, and runner-contract tests passed before the formal run.

Keep the existing parent as the stronger measured baseline. The state-bank
implementation remains opt-in; this pilot does not justify enabling it by default
or launching a larger sweep automatically. Original checkpoints were preserved.
