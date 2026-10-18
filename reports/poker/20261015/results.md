# Slumbot style-1 evaluation — 2026-10-15

The unchanged handwritten style-1 rules bot has **4,000 cumulative completed
hand results**, totaling **-2,687.77 BB**, or **-67.19 BB/100**.
The latest run retained the original 2,000 hands and added 2,000 more with
**50 concurrent games**, as requested. All workers have finished.

| Sample | Completed hands | Net BB | BB/100 |
| --- | ---: | ---: | ---: |
| Earlier results (serial, then 20 concurrent games) | 2,000 | -1,334.55 | -66.73 |
| Additional run with 50 concurrent games | 2,000 | -1,353.22 | -67.66 |
| Cumulative | 4,000 | -2,687.77 | -67.19 |

The additional 2,000 hands took **169.2 seconds**
(11.82 completed hands/s). Requests within each game
remained sequential, with separate tokens, connections, game state and policy RNG.
The original results remain intact in the append-only log; global hand IDs are
unique and contiguous from 0 through 3,999.

The approximate 95% interval from recorded hand variance is
**-110.09 to -24.30 BB/100**. This applies to the completed-hand sample;
interrupted attempts create additional uncertainty. No neural policy was evaluated
or trained for this result.

There are **23 unresolved attempts** in total: `{'504': 19, 'OperatorRestart': 1, '400': 1, 'ReadTimeout': 1, '502': 1}`.
The latest run added **5** unresolved attempts: `{'504': 5}`.
Some failed before a hand was confirmed started; all are conservatively retained
as potentially unresolved hands. Transport and server errors triggered backoff;
ambiguous actions were not replayed. The latest run reached 4,000 completed hands
without a fatal worker failure or another HTTP 400.

If every unresolved attempt represented a full 200 BB win or loss, the
all-attempt mean could range from **-181.15 to 47.53 BB/100**.
This is a missing-outcome bound, not a confidence interval. The completed sample
lost chips, but missing outcomes prevent treating it as a clean, uncensored
estimate of long-run strength. The runner reports `target_reached=true` and
`complete=false` to preserve that distinction.

The User-Agent remained:

`PufferTank-PokerEval/1.0 (+https://github.com/FelixAllistar)`

The profile was checked against the authenticated GitHub user and origin remote.
The binary hash, resumes, every completed hand, backoffs, unresolved attempts
and final summary are in [the raw log](slumbot_style1_seed20261016_paced.jsonl).
The [progress file](progress.txt) shows the final count and score.

Four offline client checks passed before these runs: concurrent game/token
isolation and resume, ambiguous-action recovery, Retry-After handling, and
redacted HTTP 400 diagnosis. The simulator and bot policy were unchanged during
this measurement.
