# CPU no-limit Hold'em

Small C simulator, native rules opponent, acting-seat batch API, and Puffer PPO
adapter. Compile the same source for 2, 6 or 9 seats. The simulator uses SoA blocks
of 64 tables, integer chips, bit masks, and a table-free exact hand evaluator.
There are no allocations during dealing, stepping, observation, or default bot
inference. The optional heads-up CFR river mode allocates a temporary solver
workspace for each eligible decision.
Simulator state is 88/196/277 bytes per table for 2/6/9 seats; policy and trainer
buffers are additional.

Open the [connected working notes](../../reports/poker/index.html) in a browser
for the starting architecture, decision paths, current Slumbot results, and
measured throughput. The self-contained HTML page can be refreshed after edits.

Default game: independent 200 BB hands, 50/100 blinds, rotating button, no rake
or ante. Stacks reset each hand. The kernel accepts unequal starting stacks from
1 through 65,535 chips; the trainer accepts a common starting stack of at least
100. Set `--env.starting_stack=3000` for 30 BB or `10000` for 100 BB.

HU has the normal heads-up blind/action order. A six-seat hand with two survivors
retains its original positions, contributions and observation format.

## Build and run

From the repository root:

```sh
bash ocean/poker/build.sh
bash build.sh poker build/poker/puffer
build/poker/puffer train --base.eval_episodes=0
```

The first command builds native libraries and benchmarks for all three seat
counts. The second builds the existing GPU PPO learner with this **CPU** env;
it defaults to six seats. To specialize the trainer for HU:

```sh
NVCC_EXTRA=-DPOKER_PLAYERS=2 bash build.sh poker build/poker/puffer2
build/poker/puffer2 train --env.num_players=2 --base.eval_episodes=0
```

Use `POKER_PLAYERS=9` and `--env.num_players=9` for nine seats. Changing the seat
count changes observation width and checkpoint compatibility.

## Optional heads-up river CFR

```sh
POKER_RIVER_CFR=1 bash ocean/poker/build.sh
```

This flag defaults to zero and is a standalone alternative to the optional
expectimax modes. The heads-up policy constructs a public two-player river
game and samples an averaged strategy after a fixed numerical best-response
check. Unsupported or rejected solves use the original range heuristic;
six- and nine-player policies do not invoke CFR. See the
[source guide](RIVER_CFR.md) for the ranges, legal action menu, iteration cap,
privacy boundary, fallback behavior, memory use and continuation limitations.

## Frozen snapshot self-play

```sh
python3 ocean/poker/league.py --output build/poker/league_run \
    --stints 10 --steps 1000000
```

This uses the existing PPO trainer. The first stint trains against rules bots;
subsequent stints add learner snapshots to a bounded bank (eight by default).
Weights stay frozen throughout each stint. Each opponent seat samples either
the fixed rules baseline (25% by default) or a frozen network for the entire
hand. Starting a stint reloads learner weights and starts a new optimizer.

`--learner checkpoint.bin --opponents a.bin b.bin` starts from an existing
learner and opponent field. All supplied networks must match the selected seat
count and `--hidden`/`--layers`. The runner saves commands, exact bank manifests,
checkpoint hashes and native logs. Its output directory must be new.

Banks are shared across tables; each opponent seat has separate recurrent
state, cleared at hand reset. Only acting opponents run inference. This is
snapshot self-play with one learning seat per table, and avoids inactive policy
rows or mixing different players' rewards/RNN states. The button rotates, so
the learning seat covers every position. Frozen inference currently runs on CPU.

The bank is capped at 16 models, H128/L4; default six-seat H64/L2 weights occupy
127,744 bytes/model. Eight default models occupy 1,021,952 bytes total. Per-table
opponent recurrent state adds 2,560 bytes at H64/L2. These are additional to
simulator state, observations and trainer buffers. No CFR tables or blueprint
downloads are used.

The existing trainer's simultaneous-row `[selfplay]` pool stays disabled. Use
the native bank above. A future collector can use `poker_requests` to train all
seats and batch frozen inference on the GPU. Payoff-matrix scheduling and
dedicated exploiter training are not implemented in this initial snapshot pool.

## Actions, observations and rewards

The exact kernel action is `-1` fold, `0` check/call, or a positive **total
commitment on this street** for a raise. Generated masks cover eight slots:
fold, check/call, minimum raise, 1/3-pot, 2/3-pot, pot, twice-pot, all-in. Raise
sizes use the pot after calling, are clipped to stack, and are deduplicated.
The kernel also accepts arbitrary legal integer raises for external opponents.
Only the learned policy has the eight-size abstraction.

No folding when a check is free. Short all-ins preserve the last full raise;
cumulative short raises reopen betting when the full threshold is reached.
Side pots retain folded contributions, and split-pot odd chips go one at a time
clockwise from the button. A lone player with chips only acts when facing a call;
otherwise the board runs out automatically. A short posted BB retains the nominal
100-chip opening price while multiple players still have chips.

`POKER_OBS = 16 + 15 * players` floats, relative to the observing seat:

| Slice | Contents |
|---|---|
| First 16 | Public state, own hole cards, visible board, acting/count/terminal fields |
| Next `7 * players` | Per-seat stack, bet, total, initial, live/active/pending flags |
| Last `8 * players` | Public events: relative actor, street, kind, street commitment |

The event window includes all events since this seat's previous decision,
including its own previous action.

Chip observations are divided by 20,000 (200 BB), cards by 52, street/action
kinds by 3, live count/event actors by seat count, button by `players - 1`, and
action count by 1,024. These fixed scales prevent initial network saturation and
preserve information. Card IDs are `4 * rank + suit`, ranks `23456789TJQKA`,
suits `cdhs`; hidden cards have observation value 1. Unused event rows have actor
zero; real event actors use relative seat + 1 before scaling. Event action kinds
are fold/check/call/raise = 0/1/2/3 before scaling. No opponent hole cards or unrevealed board enter
observations or bot decisions. Keep recurrent state keyed by table and seat.

Rewards are **terminal net chips / 100**, with zero intermediate rewards and
`gamma=1`. There is no shaped reward. Environment logs use BB/hand; multiply by
100 to report BB/100. A hand ending before the learner has a decision contributes
to raw hand logs without fabricating an RL transition.

`core.h` exposes `poker_requests`/`poker_respond` for batched self-play. Requests
contain only acting seats, with table IDs, seat IDs, observations, masks and
exact amounts. Responses return every seat's terminal profit, including folded
and all-in seats. Consume these before resetting a completed table. The trusted
kernel uses debug assertions; validate untrusted inputs at the boundary.

## Native opponents and external evaluation

Three native policies remain active. Numeric IDs are stable implementation
selectors, not difficulty levels or human personalities.

| ID | Name | Role |
| --- | --- | --- |
| 1 | Rules control | Original cheap heuristic, retained for reproducible comparisons. |
| 4 | Fullhouse | External learned policy; requires its compiled weights. |
| 7 | Range | Current equity-based opponent and training default. |

The redundant experimental IDs 0/2/3/5/6/8 have been removed from active native
builds. Sampling is now a setting on the range policy: `equity_samples=32`, `64`
or `128`; zero uses the pinned build's default. Historical source snapshots,
binary hashes, results and old libraries remain available. An old numeric ID
requires the matching archived library; IDs are never silently reassigned.

The [HTML notebook](../../reports/poker/index.html) records strategy revisions,
Slumbot measurements and six-seat checks. The
[behavior audit](../../reports/poker/index.html#behavior) describes the nine-ID
snapshot before cleanup; `audit_behavior.py` defaults to that frozen library.
A policy's identity is its source/binary hash, numeric ID and sample setting.

Use `--env.bot_policy=7` and optionally `--env.equity_samples=128` in the trainer.
`--env.native_baseline_mask=146` mixes IDs 1/4/7 when Fullhouse is compiled in;
130 mixes only rules and range. A nonzero mask samples the native policy once
per table/opponent seat and keeps it across hands. Zero uses `bot_policy`.
The snapshot league exposes `--bot-policy`, `--equity-samples` and
`--baseline-mask`. Its native-versus-snapshot fraction still applies; frozen
network identities are sampled per hand and recurrent state resets each hand.

### Range equity and public table history

The range policy samples complete unknown deals, weighted by a hand-written
opponent range model. It accounts for card removal, ties, folded contributions
and side-pot eligibility. No opponent holes or unrevealed board cards are read.
Simulation RNG is separate from dealing. Decision-time code is native C with no
allocation, Python, network, learned weight file or GPU dependency. Range
likelihoods and the betting rule remain approximations, not full action values.

The default budget is 128 samples in heads-up builds and 32 at six/nine seats.
The [October 18 iteration series](../../reports/poker/index.html#strength) tried
four candidates and retained this sampling change. A fresh 6,000-hand Slumbot
run scored -20.13 BB/100 versus -32.33 for a clean 6,000-hand control window;
the +12.20 +/-48.48 difference is inconclusive. Fresh local heads-up comparisons
improved; six-seat default results match the old range policy. Bet-context,
blind-rule and exact-river experiments remain archived rather than active.
The compiled current trainer is `build/poker/puffer_range`.

Set `--env.native_history=1` (or league `--native-history`) to retain public
per-seat statistics across hands until the table is destroyed. The range model
uses shrunk VPIP, preflop-raise and postflop-raise rates. Fold and size statistics
are also recorded, but are not policy inputs. All learner and opponent actions
update profiles without inspecting cards or outcomes. Profiles cost 64 bytes per
seat and are off by default. They do not enter the learner's observation vector;
consistent strength gains from history and human realism remain unestablished.

### Fullhouse and duplicate-deal benchmarks

The native Fullhouse port uses the published five-action policy and short-stack
chart. Its checkpoint was developed for six-player play; heads-up and 200 BB
results are measured separately. Clone the pinned source into the ignored build
tree and export it:

```sh
git clone https://github.com/advitrocks9/fullhouse-bot.git build/poker/external/fullhouse-bot
git -C build/poker/external/fullhouse-bot checkout e504793d480b1b975f25258d25939b45c6dbd5a4
uv venv build/poker/bench-venv
uv pip install --python build/poker/bench-venv/bin/python numpy==2.2.6 eval7==0.1.10 requests pokerkit==0.7.7
OPENBLAS_NUM_THREADS=1 build/poker/bench-venv/bin/python ocean/poker/build_baselines.py
OPENBLAS_NUM_THREADS=1 build/poker/bench-venv/bin/python ocean/poker/test_baselines.py
build/poker/bench-venv/bin/python ocean/poker/test_equity.py --build build/poker
python3 ocean/poker/benchmark.py --candidates 1 4 7 --hands 60000 \
  --output build/poker/new_baseline_match
```

The exporter pins the source revision, records hashes and embeds 588,180 bytes
of fp32 weights/tables. Inference uses C, optional AVX2 and libm. The source
license is retained in `third_party/fullhouse-LICENSE.txt`; external source and
model files stay outside tracked source. To embed Fullhouse in the trainer:

```sh
NVCC_EXTRA='-DPOKER_FULLHOUSE_MODEL="/home/felix/puffertank/pufferlib/build/poker/fullhouse_weights.h"' \
  bash build.sh poker build/poker/puffer_baselines
```

`benchmark.py` rotates each tested policy through every seat on duplicate deals.
Intervals use independent block means, and policy differences are paired on the
same deals. Current fields are rules, Fullhouse and a mixed six-seat field
(1/4/1/4/4); the older positional/varied fields belong to archived experiments.
`--stack 10000` tests 100 BB; `--build` selects a frozen build. Output directories
must be new. `--session-blocks 50 --native-history` groups 50 hands per seat into
independent sessions for history ablations; each rotation owns separate profiles.
`policy_bench2` and `policy_bench6` report fixed-position process CPU and wall time
separately from integrated match throughput.

The [checkpoint audit](../../reports/poker/20261017/equity_v1/checkpoint-audit.json)
found two advantage checkpoints and no average-strategy checkpoint in the pinned
main revision and 13 archive tags. The supplied model matches the author's stated
shipped policy. Repeat after fetching the external repository's tags with
`build/poker/bench-venv/bin/python ocean/poker/audit_fullhouse.py --output build/poker/checkpoint-audit.json`.

### Slumbot runner

```sh
python3 ocean/poker/eval.py --hands 1000 --policy range --games 5 --seed 991 \
    --output build/poker/slumbot_eval.jsonl
```

Requires `requests` and the native HU library. This plays anonymous live hands
against Slumbot and saves the binary hash, completed hand actions and raw chip
returns. Log schema 2 also saves own cards, visible boards, opponent cards when
revealed, and each decision's public history, chip state, exact action and policy
RNG before/after sampling. Interrupted attempts retain the same decision trace.
This supports exact offline replay without exposing future cards to the policy;
older logs without cards cannot be reconstructed retroactively.
HTTP stays outside training. `--games 20` runs 20 independent games in
one client process; each game owns its token, connection and bot RNG. Requests
within a game are sequential, with no fixed delay by default. `--contact` adds a
public contact URL to the descriptive User-Agent header. Rate-limit responses
honor `Retry-After`; transient
server/transport errors use exponential backoff. Ambiguous actions are never
replayed: the unresolved hand is logged and play continues in a fresh session.
Summaries report unresolved counts and worst-case score bounds separately.
Six consecutive failed hand attempts in a game stop the run. Use `--resume` with the same
output, policy, sample setting and library to retain completed results and continue to `--hands`
total hands. Running net BB, BB/100 and ETA print every 25 completed hands and
are written to a `.progress.txt` file, or the path selected with `--progress`.
SIGINT and SIGTERM drain current hands and then stop, preserving a boundary for
resuming. A forced kill cannot drain: a log without a final summary has unknown
pending outcomes and must not be reported as a clean completed run.
Use `--policy rules`, `--policy fullhouse` or `--policy range`;
`--style` accepts historical numeric IDs only when the selected library supports
them. `--equity-samples` selects the range budget. Live runs and their uncertainty
are retained in the HTML notebook; development scores are separate from final
evaluation scores.

## Correctness and measurement

```sh
uv venv build/poker/venv
uv pip install --python build/poker/venv/bin/python pokerkit==0.7.7 requests
POKER_PYTHON=build/poker/venv/bin/python bash ocean/poker/build.sh --test
build/poker/bench6 1000000 rules
build/poker/bench6 100000 call
build/poker/bench6 100000 minraise
```

The test command uses stock Bend 2.0.6 through the existing serialized CPU
resource guard, capped at 2 GiB with swap disabled. It checks the laws, compiles
independent fixtures, runs ASan/UBSan native/adapter tests, compares frozen
inference to PufferNet, and runs independent PokerKit checks. Bend verifies the
stated model laws; it is **not a proof of equivalence of the entire C simulator**.

The report lists the PokerKit 0.7.7 short-raise, short-blind and odd-chip
differences explicitly. Differential checks use common legal trajectories and
independent pot/rank calculations; dedicated fixtures cover the differing rules.

Kernel benchmarks include dealing, actions and settlement, but exclude
observation construction and neural inference. See the
[implementation report](../../reports/poker/20261014/implementation.md) for
measured rates, validation evidence and remaining work.
