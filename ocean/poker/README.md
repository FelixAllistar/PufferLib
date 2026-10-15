# CPU no-limit Hold'em

Small C simulator, native rules opponent, acting-seat batch API, and Puffer PPO
adapter. Compile the same source for 2, 6 or 9 seats. The simulator uses SoA blocks
of 64 tables, integer chips, bit masks, and a table-free exact hand evaluator.
There are no allocations during dealing, stepping, observation, or bot inference.
Simulator state is 84/188/266 bytes per table for 2/6/9 seats; policy and trainer
buffers are additional.

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

```sh
python3 ocean/poker/eval.py --hands 1000 --style 1 --seed 991 \
    --output build/poker/slumbot_eval.jsonl
```

Requires `requests` and the native HU library. This plays anonymous live hands
against Slumbot and saves the binary hash, completed hand actions and raw chip
returns. HTTP stays outside training. Interrupted runs are marked incomplete;
ambiguous actions are never retried. The sole completed initial pilot was 100
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
