# Goofspiel

CPU Goofspiel is specialized for two players and up to four cards. It supports
random/ascending/descending prizes, perfect/hidden information, egocentric
observations, discard/carry ties, and OpenSpiel-compatible terminal returns.
The fixed model interface has four actions and 48 observation values. Compact
observations use the first 27 values; OpenSpiel observations use all 48.

## Exact exploitability

Build the standalone native evaluator:

```bash
bash build.sh goofspiel --exploit
bash build.sh goofspiel --exploit-gpu
```

Evaluate a checkpoint on the four-card game:

```bash
./goofspiel_exploit saved/goofspiel1/model.bin \
  env.num_cards=4 env.num_turns=4

./goofspiel_exploit_gpu saved/goofspiel1/model.bin \
  env.num_cards=4 env.num_turns=4
```

Use `uniform` instead of a checkpoint to evaluate the uniform random policy.
The exact evaluator supports shared egocentric recurrent policies in two-player
perfect-information zero-sum games with 2-4 cards. It enumerates seeded chance
outcomes exactly, advances the MinGRU on the same pre-action observation used
during training, masks spent cards, and computes a pure best response without
revealing the opponent's simultaneous bid.

For a seat-symmetric egocentric policy, both best-response values are identical.
For the reference policy they are computed independently. In either zero-sum
case:

```text
NashConv = best_response_0 + best_response_1
exploitability = NashConv / 2
```

Four cards is small enough for fast checkpoint-by-checkpoint exact measurement.

Uniform-policy NashConv fixtures match OpenSpiel exactly:

| Cards | NashConv |
|---:|---:|
| 2 | 1.0 |
| 3 | 1.3333333333333333 |
| 4 | 1.4930555555555556 |

The exact solver now caps at 5 cards (`GS_EXACT_MAX_CARDS`). Ascending-order
5-card uniform exploitability is verified exactly against an independent brute
force (0.816666667). Random-order 5-card has a known residual: the first-prize=4
root value differs by 1/720 (native 0.801527778 vs independent 0.801805556),
which does not affect 4-card or fixed-order results.

The best-response recursion is a compact native adaptation of OpenSpiel's
Apache-2.0 `tabular_best_response_mdp` algorithm. No OpenSpiel code or runtime
dependency is included.

The CUDA evaluator stores reachable states in contiguous arrays by round.
Children are emitted in a fixed bid/bid/prize order, so backward induction uses
fixed strides rather than pointers or an edge table. Policy inference is a
sequence of dense cuBLAS operations followed by small MinGRU, masking, expansion,
and reduction kernels. There are no device allocations inside kernels.

The CPU implementation remains the reference oracle while the CUDA path is
reviewed. Verify every supported rule family against it with:

```bash
ocean/goofspiel/tests/test_exploit_gpu.sh MODEL HIDDEN_SIZE NUM_LAYERS
```

## GPU-resident environment

`goofspiel.cu` implements the `puf_envs_*` adapter so the environment itself
runs on device (mirrors `puffer_survivors.cu`/`kaggriculture.cu`). The core
state machine and observation writer are shared with the CPU path; a parity
fuzz test drives both over the same action stream and compares state, obs,
mask, reward, terminal, and log bytes:

```bash
make -C ocean/goofspiel cuda-adapter
```

The observation/action ABI is compile-time: `GS_NUM_CARDS=4` (default) keeps
the legacy checkpoint layout byte-identical, while `GS_NUM_CARDS=13` builds a
larger obs/action layout for 13-card training:

```bash
GS_NUM_CARDS=13 ./build.sh goofspiel --gpu
```

GPU-runtime config notes: the GPU env requires `vec.num_buffers=1` (the
two-player bank layout is single-buffer) and `vec.action_mask_size` equal to
the compiled ABI (`4` default, `13` for the 13-card build). Example:

```bash
./puffer train goofspiel vec.num_buffers=1 vec.gpu_env=1
```

The exact exploitability solver remains 4-card and is compiled into the GPU
trainer (`GS_EXPLOIT_NO_MAIN`) so checkpoint scoring and env stepping share one
binary. A 65,536-agent environment bench runs ~115M agent-steps/sec on the
5070 Ti.

Across 2-4 cards, shortened episodes, fixed prize orders, carry ties, and
point-difference returns, measured CPU/CUDA absolute error is below `6e-8`.
The CPU uniform-policy fixtures above are the transitive check against
OpenSpiel's exact results.

## Checkpoint cross-play

Generate a two-sided checkpoint payoff matrix:

```bash
python3 scripts/payoff_matrix.py goofspiel checkpoints/goofspiel/RUN_ID \
  --count 8 --games 65536 --output reports/goofspiel/RUN_ID
```

Use repeated `--override section.key=value` arguments when the checkpoints do
not use the configured game or policy shape.

The matrix tool is offline experiment orchestration; training and exact
exploitability remain entirely native.

## Population self-play

The native frozen-bank machinery supports rolling history, external checkpoint
pools, PFSP, and explicit external-pool weights. `frozen_bank_pct` is the total
fraction of environments assigned a frozen opponent; that fraction is divided
across `num_frozen_banks` independently loaded opponents.

External weights are aligned with the comma-separated pool:

```text
selfplay.opponent_pool=A.bin,B.bin,C.bin
selfplay.opponent_pool_weights=0.2,0.5,0.3
```

With `pfsp_alpha=0`, these are the exact sampling probabilities. A positive
PFSP exponent multiplies each base weight by the learner's measured weakness
against that opponent. All frozen opponents in one training run currently use
the same architecture.

For nontransitive experiments, use the full cross-play payoff matrix rather
than Elo. Policies with similar payoff rows are strategically redundant even
if their weights differ; cycles and novel payoff rows are candidates for the
population. The intended loop is restricted-population self-play: evaluate the
matrix, solve a meta-strategy over it, train a response against that weighted
mixture, then admit the response only if it adds strategic coverage.

Behavioral diversity can be measured independently of payoff. Build the native
reachable-state policy evaluator, then scan every raw and EMAg checkpoint in a
population:

```bash
bash build.sh goofspiel --behavior-gpu
ocean/goofspiel/eval_population.sh population64x1 0 0.002 jsd
python3 scripts/goofspiel_behavior.py \
    logs/goofspiel/population64x1_behavior.tsv \
    --similarity 0.05 --minimum-distance 0.05 --max-policies 32

# Prefer behaviorally far-apart checkpoints, including weaker early policies:
python3 scripts/goofspiel_behavior.py \
    logs/goofspiel/population64x1_behavior.tsv \
    --strategy farthest --max-policies 16 \
    --output logs/goofspiel/population64x1_behavior_farthest16
```

The native tool compares action distributions on the identical exact state tree
and reports Jensen-Shannon divergence, Jensen-Shannon distance, total variation,
and exact exploitability. The Python step is offline grouping only: it writes
near-duplicate behavior groups and a diverse representative list. It does not
change training or the environment.

## Online exact response

Four-card training can replace frozen-bank actions with the exact response to
the latest learner checkpoint:

```bash
./puffer train goofspiel \
  env.exact_exploiter=1 \
  env.exact_exploiter_history=64 \
  env.exact_exploiter_current_prob=0.5 \
  base.run_id=goofspiel4_exact
```

Checkpoint publication runs the CUDA backward pass in the trainer's existing
CUDA context and retains its argmax action at every reachable history. Frozen
environments split between the latest response and a bounded reservoir of all
historical exact responses;
ordinary environments remain shared-policy
self-play. `vec.frozen_bank_pct` therefore controls the exact-opponent fraction,
and Puffer's existing frozen-row advantage mask ensures PPO only trains the
learner's moves. The current four-card default is 25% exact games when enabled.

In a paired three-seed 10M-step test, exact-response training with every-epoch
refresh reduced mean exploitability from `0.742340` to `0.740217`, improving
all three seeds. Table refresh took approximately 3-14 ms on a GTX 1060 after
CUDA initialization.

A single moving exact response plateaued at `0.492943`, while a uniform
64-response reservoir reached `0.122190`. Splitting exact games evenly between
the latest response and the historical reservoir reached `0.058412` after a
200M-step continuation. This keeps current exploitability pressure strong
without forgetting older counters.
