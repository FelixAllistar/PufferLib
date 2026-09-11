# Fuzzy farm-development phases

Training-only `env.reward_phase_scale` defaults to **0**, including when absent
from an older config. Initial experiment uses2. Observation/action layout,
checkpoint weights, simulator rules and executor are unchanged. Native binaries
must be rebuilt because the training adapter has a new reward config field.

This is a bounded **auxiliary objective**, not potential-based shaping and not
guaranteed to preserve the cash-optimal policy. Final cash remains the principal
evaluation measure. It is possible for this curriculum to hurt profitability;
the paired control tests that rather than assuming it works.

| Phase | Center turn | Positive-credit window | Healthy cows | Healthy other animals | Healthy crops | Productive plots |
|---|---:|---|---:|---:|---:|---:|
| Establish | 96 | 49–143 | 4 | 2 | 12 | 1 |
| Productive expansion | 192 | 145–239 | 7 | 3 | 24 | 2 |
| Build herd and farm | 288 | 241–335 | 10 | 6 | 40 | 3 |
| Sustain | 384 | 337–431 | 10 | 6 | 40 | 3 |

Times scale with turns-per-day: centers day4/8/12/16, half-width2days.
Targets are hypotheses inspired by replay106946670 (Matthew Huang/SpaTaro),
not universal optimal requirements. No crop type, tile location, market action,
or sheep-versus-goose choice is prescribed. Targets live in `phase_rewards.h`;
strength is an INI/CLI knob. These are not learned economic value estimates.

Each component uses a smooth saturated fraction of its target. Readiness is
half the mean component fraction plus half the least-complete fraction: partial
progress pays, but fully satisfying only one branch cannot earn full credit.
Extra animals beyond a target do not earn additional points. Crop/animal counts
require no accumulated missed watering/feed day, or repair today. This health
proxy is not a promise that output will be harvested or profitable.

Each unlocked plot contributes a smooth0..1 occupancy fraction, saturating at
12 healthy occupied tiles. Empty pasture/coops/weeds/locked land do not count.
Crop and livestock targets separately prevent buildings alone from satisfying
the readiness score. This does not require a fixed fill-before-expand action
order: partial credit remains available for partially developed farms.

The time weight is triangular, strongest at the center and zero at its ends.
Its discrete sum is1 for each full window. Per-step credit is
`scale * window_weight * current_readiness`. Four windows therefore pay at most
`4 * scale` total (8 at scale2), before discounting. There are no independent
peak counters: cows/plants lost before later windows stop contributing. Keeping
a state productive over the window intentionally earns more than touching it
once. Earlier earned bonuses are not clawed back; later neglect can still occur,
so final cash and survival must be evaluated. Rewards end after day18 to leave
the late game to the cash objective. State-reset training is off in this test.

## Controlled comparison on Vast

`logs/kaggriculture/fuzzy_phases_v1`: two matched300M-step continuations from
`profit_ablation_v1_t1_a0/0000000299892736.bin` (best deterministic panel cash in
the preceding ablation), with its EMAG reference. Optimizer state restarts equally.
Control phase_scale0 versus treatment phase_scale2; old expansion reward0 in
both; terminal cash4, terminal win1. LR0.0004 constant, EMAG0.01/tau0.1,
4096agents, horizon256, minibatch2048,256x3,obs1/macro2. Same frozen opponents.
Current editable config is not rewritten.

`phase_ablation.py` prepares the explicit new binary/reward generation and uses
the resumable profit runner. Parent baseline and100M/200M/final checkpoints are
evaluated in deterministic/stochastic modes. `cash_ranking.tsv` includes phase
strength, and `herd_timeline.tsv` exposes cow/herd/plant/plot/cash timelines.
No automatic promotion, submission, or indefinite continuation.

Old trainer and remote adapter sources are retained under
`logs/kaggriculture/phase_install_v1/`. Old experiment plans deliberately reject
the new binary hash rather than silently mixing versions; their checkpoints and
reports remain intact. CPU and CUDA tests cover reward bounds, timing, partial
credit, empty land, lost/neglected farms, scale0 and player isolation.
# Occupancy semantics fixed after v2

The post-v2 reward audit fixed valid-species occupancy checks. V2 training
counted empty coops/pastures as non-cow animals and productive occupancy;
new builds exclude them. See `REWARD_ACCOUNTING_AUDIT_20260909.md` for tests
and deployment. After the fix, the user requested next-run config preparation;
that new fresh run explicitly enables phase scale 2 with terminal cash 4/win 1.

# Integration correction (v2)

The original `fuzzy_phases_v1` comparison is INVALID: phase-on and phase-off
produced byte-identical final checkpoints. The CUDA config accepted the scale,
but `kag_cuda_transition` omitted the phase reward addition present in CPU
`puf_step`. Standalone helper tests did not cover this integration.

The corrected CUDA transition adds the same post-step phase reward as CPU.
`tests/test_cuda_adapter.cu` now enables phases in alternating cases, checking
actual transition reward parity. `tests/check_phase_training.py` additionally
runs two identical disabled controls and one enabled short training run; it
requires identical control weights and different enabled weights. Keep v1
outputs intact; use a fresh v2 experiment directory after these gates pass.

V2 validation on Vast: the strengthened adapter test fails against the old
GPU path (case 8, turn 39, reward mismatch), then passes with the missing call
restored (12 cases x 1440 turns). Three 4,194,304-step training checks produce
identical phase-off hashes and a different phase-on hash; evidence lives in
`logs/kaggriculture/phase_gate_v2/PASS.json`. The corrected matched 300M pair
uses `logs/kaggriculture/fuzzy_phases_v2`, the same parent/hypers as v1, with
phase scales 0 and 2. Active `config/kaggriculture.ini` remains unchanged.
