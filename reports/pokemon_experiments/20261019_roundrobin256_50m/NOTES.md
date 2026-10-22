# Fresh 256x2 evolving league, 50M-step stints

Full project context, experiment DAG, October 19 progress, and Vast runbook:
[Pokémon handoff](../HANDOFF.md).

The user cancelled the broad sweep and requested the previous round training
with larger 256x2 models, the same other settings, 50M rounds, and LR annealing.
The sweep controller and its active trainer were terminated; no process remained
on the GPU. Its 27 completed results and saved checkpoints are retained.

## Setup

New league: `leagues/pokemon/rr50m_256x2_20261019`. The old league is unchanged.
All 14 members are trainable, fresh, independently seeded 256x2 policies. Existing
64x2 weights cannot be directly loaded into 256x2. No weight surgery/distillation
or warm-start training was performed. Expect initial weakness while they learn.

The same 14 compositions/leads and roster order are retained. Master keeps its
six exact catalog sets; specialists learn moves under their species constraints.
Each member trains 50,331,648 counted steps per round (96 complete rollouts at
16384 agents x horizon 32), with 50% current-policy self-play environments and
50% against the other 13 peers. A member is published immediately; the next member
faces its new version. Peers freeze during each stint. One full round is 14 stints,
704,643,072 counted steps. Target remains 50 full rounds, as in the previous league.

All old PPO settings are explicitly restored from the October 17 experiment:
horizon 32, minibatch 8192, replay 3.71047497, gamma 0.999899983, lambda
0.899999976, entropy 0.020000007, clip 0.12561436, value coefficient 0.52888006,
value clip 0.2, gradient norm 0.338345766, momentum 0.95. V-trace, entropy annealing,
HP/KO shaping, behavior/personality rewards, forced cores and snapshot resets are
off. One synchronous buffer, four CPU workers, recurrent state carried within a
stint, BF16 training. Optimizer and recurrent state still restart between stints.

## LR across rounds

LR changes intentionally: start 0.0005, terminal floor 0.00005, over 50 rounds
per member. Global endpoint factor at round boundary r is
`0.1 + 0.9*(1+cos(pi*min(r/50,1)))/2`.
For round r, native cosine runs between the endpoints for r-1 and r. This is a
continuous, monotone sequence of within-round cosine segments, not the exact
single global cosine at every optimizer step. It avoids restarting at the original
high rate each round. Native epochs discretize each segment. Beyond round 50 the
floor holds; the launch script itself stops at 50 completed rounds.

Round plans persist effective `learning_rate` and `min_lr_ratio`; retries use the
same values, and resuming does not reset the schedule. No optimizer code changed.

## Monitoring and safety

Master retains native Metamon checks every ~10% (6 games), then 72 games at the
end of its stint. Other members skip benchmark evaluations to keep rounds moving.
All checkpoints remain available for later specialist evaluation. Both benchmarks
are explicitly disabled during random initialization; Oak stays off throughout.
No automatic peer tournament or extra evaluation queue is scheduled.

The scheduler uses the normal native dashboard with PTY logging, not a replacement
viewer. A 2 GiB disk-reserve guard stops before a new stint if storage is low;
it does not delete data. The trainer executable is snapshotted inside the league
and recorded by SHA256 in `experiment.json`. Existing experiments are untouched.

Prepare once: `node reports/pokemon_experiments/20261019_roundrobin256_50m/setup.mjs`.
Launch/resume: `bash reports/pokemon_experiments/20261019_roundrobin256_50m/launch.sh`.
The launch script derives `50 - state.round`, so resume cannot accidentally add
another 50 rounds after partial completion. Initialization costs only a native
zero-LR 64-step serialization pass per member and is not counted as league training.

Tests cover LR monotonicity/endpoints/floor, restart continuity, retry preservation,
Metamon suppression during initialization, opt-in during stints, legacy evaluation
defaults, checkpoint publication, native manifests, and interruption/resume.

## Live verification

All seven league tests passed on Vast, including an actual native two-member,
two-round initialization/training/resume/evaluation workflow. All 14 production
256x2 initializers were published with distinct weight hashes. Round 1's master
loaded all 13 matching 256x2 peer banks and reached 5.24M steps with finite losses
at ~257K steps/sec, using ~3.7 GiB GPU memory. Its first native Metamon check
started at 10.4%. Trainer PID at verification: 810858.

Running in the existing tmux pane, renamed `pokemon:rr256`; no new viewer/window.
Round 1 effective LR is 0.0005, floor ratio 0.9991120277927222, ending near
0.0004995560138963611. The native binary is unchanged from the tested broad-sweep
build and snapshotted by hash. No production encoder or optimizer code changed.
