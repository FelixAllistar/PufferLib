# CPU no-limit Hold'em

Small C simulator, native rules opponent, acting-seat batch API, and Puffer PPO
adapter. Compile the same source for 2, 6 or 9 seats. The simulator uses SoA blocks
of 64 tables, integer chips, bit masks, and a table-free exact hand evaluator.
There are no allocations during dealing, stepping, observation, or bot inference.
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

## Rules bot and external evaluation

The fixed initial baseline is style 1: positional/preflop hand features, made
hands/draws, call-price thresholds and randomized value/semi-bluff sizing. It
has no search or large tables. Styles 0 and 2 preserve the initial and positional
pilot variants. Its scores are heuristics, not calibrated equity or GTO claims.
The [October 17 loss audit](../../reports/poker/index.html#diagnosis) reproduces
over-folding of strong draws and overpairs in the frozen baseline, and breaks
down the original 4,000-hand Slumbot sample. The separate
[native baseline experiment](../../reports/poker/index.html#baselines) compares
Fullhouse and two defense candidates; see those measurements before choosing a
replacement. The default remains the original style 1.

### Native Fullhouse and candidate benchmarks

Styles 0/1/2 retain the old policies. Style 3 is a broad defense experiment;
style 5 makes narrower changes to draws, top pairs/overpairs and AK re-raises.
Neither candidate is a calibrated equity solver. Style 4 ports the published
Fullhouse Deep CFR policy, including its five-action abstraction and short-stack
chart. It supports two or six seats; the published checkpoint was developed for
six-player play, so heads-up and 200 BB results require separate measurement.

Clone the pinned external source into the ignored build tree and export it:

```sh
git clone https://github.com/advitrocks9/fullhouse-bot.git build/poker/external/fullhouse-bot
git -C build/poker/external/fullhouse-bot checkout e504793d480b1b975f25258d25939b45c6dbd5a4
uv venv build/poker/bench-venv
uv pip install --python build/poker/bench-venv/bin/python numpy==2.2.6 eval7==0.1.10 requests pokerkit==0.7.7
OPENBLAS_NUM_THREADS=1 build/poker/bench-venv/bin/python ocean/poker/build_baselines.py
OPENBLAS_NUM_THREADS=1 build/poker/bench-venv/bin/python ocean/poker/test_baselines.py
python3 ocean/poker/benchmark.py --hands 60000 --output build/poker/local_baselines
```

The exporter checks the source revision, records hashes, and embeds 588,180 bytes
of fp32 weights/tables in each native library. Decision-time inference uses C,
optional AVX2 and libm; it needs no Python, BLAS, allocations, GPU or network.
The source license is retained in `third_party/fullhouse-LICENSE.txt`. External
source and model files stay outside the tracked source tree.

`benchmark.py` rotates the tested policy through every seat on each duplicate
deal. Intervals use independent deal-block means; candidate differences are
paired on the same deals and field. It tests original, Fullhouse and mixed
six-player fields. `--stack 10000` tests 100 BB; `--candidates 1 5 4` selects a
subset; `--build` selects a frozen build directory. Output directories must be
new. These opponent fields are baselines, not a validated model of human play.

Fullhouse works with the same Slumbot runner:

```sh
python3 ocean/poker/eval.py --style 4 --library build/poker/libpoker_fullhouse2.so \
  --hands 4000 --games 5 --contact https://github.com/FelixAllistar \
  --output build/poker/slumbot_fullhouse.jsonl
```

The October 17 experiment stopped its 50-game pilot after connection failures;
five games then completed cleanly. Logs retain all ambiguous outcomes. An
unsupported style is rejected before opening an API game.

To embed Fullhouse in the existing PPO environment, build with its generated
header (replace this absolute example with your build path):

```sh
NVCC_EXTRA='-DPOKER_FULLHOUSE_MODEL="/home/felix/puffertank/pufferlib/build/poker/baselines_v2/fullhouse_weights.h"' \
  bash build.sh poker build/poker/puffer_baselines
```

Use `--env.bot_policy=4` for Fullhouse, or `--env.native_baseline_mask=23` for
styles 0, 1, 2 and 4 (bit i enables style i). A nonzero mask samples each
opponent's native style once at table initialization and keeps it across hands.
Zero preserves the single `bot_policy` selection. The snapshot league accepts
`--bot-policy` and `--baseline-mask`; `rules_fraction` still controls the native
baseline fraction versus frozen PufferNet opponents. Those network identities
are sampled per hand as before. The optional range baselines below can use public
table history; learner and frozen-network recurrent state still reset each hand.

### Bounded range equity and public table history

Styles 6/7/8 keep style 5's preflop policy and replace its postflop strength score
with sampled showdown equity. Style 6 samples 32 uniform opponent hands (a weak
ablation). Style 7 uses 32 range-weighted samples; style 8 uses 128. Ranges depend
on position and each seat's strongest public action on each visible street.
The joint sampler accounts for card removal, ties, folded contributions and
side-pot eligibility. Simulation RNG is separate from dealing; no opponent
cards or unrevealed board are read. Range likelihoods and the betting rule are
still heuristics: this is not CFR, a calibrated human model, or full action EV.

These styles work in the ordinary native build and in the Fullhouse build.
The [range experiment](../../reports/poker/index.html#equity) records held-out
matches, Slumbot, throughput and the uniform/range/sample-budget ablations.
Use `--env.bot_policy=7` for the faster range baseline, or 8 for more samples.
The original style 1 remains available and is still the default.

Set `--env.native_history=1` (or league `--native-history`) to allocate a public
profile per seat, retained across hands until the table is destroyed. Styles
7/8 use shrunk VPIP, preflop-raise and postflop-raise rates to adjust ranges.
Facing-bet folds and bet-size statistics are also recorded for inspection, but
are not yet policy inputs. All learner and opponent actions update the profiles;
they never inspect cards or results. Memory costs 64 bytes per seat when enabled
(384 bytes for six seats), in addition to simulator and trainer buffers. The
feature is off by default. This is the beginning of behavioral adaptation;
human realism and a consistent strength gain from history have not been established.

```sh
# After the pinned Fullhouse export above:
build/poker/bench-venv/bin/python ocean/poker/test_equity.py --build build/poker
python3 ocean/poker/benchmark.py --build build/poker --candidates 1 7 8 \
  --fields original fullhouse positional mixed varied --hands 60000 \
  --seed 202610174 --output build/poker/range_holdout
python3 ocean/poker/benchmark.py --build build/poker --candidates 1 7 \
  --fields fullhouse varied --hands 60000 --session-blocks 50 --native-history \
  --seed 202610175 --output build/poker/history_comparison
build/poker/policy_bench6
```

With `--session-blocks 50`, confidence intervals use independent sessions of
50 duplicate-deal blocks. Each seat rotation has its own table profiles, so no
profile observes the other rotations of the same deal. To measure history's
effect, run the same session configuration once with and once without
`--native-history`; compare paired session returns. Fixed-position latency from
`policy_bench2`/`policy_bench6` reports process CPU time and wall time separately.

The [checkpoint audit](../../reports/poker/20261017/equity_v1/checkpoint-audit.json)
checked all tracked NPZ files in the published main revision and 13 archive
tags: two advantage-network artifacts, no average-strategy network. The supplied
model matches the author's stated shipped policy. The trainer's newer default
average-strategy export is not evidence of a deployment bug. To repeat after
fetching the external repository's tags:

```sh
build/poker/bench-venv/bin/python ocean/poker/audit_fullhouse.py \
  --output build/poker/checkpoint-audit.json
```

```sh
python3 ocean/poker/eval.py --hands 1000 --style 1 --seed 991 \
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
output, style and library to retain completed results and continue to `--hands`
total hands. Running net BB, BB/100 and ETA print every 25 completed hands and
are written to a `.progress.txt` file, or the path selected with `--progress`.
SIGINT drains current hands and then stops, preserving a boundary for resuming.
The sole completed initial pilot was 100
hands at −41.4 BB/100, with an approximate 95% half-width of 62.8 BB/100. This
does not establish opponent strength or a win over Slumbot.

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
