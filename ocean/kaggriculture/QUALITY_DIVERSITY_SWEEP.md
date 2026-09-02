# Kaggriculture quality-diversity sweep

Protein answers “which configuration has the highest scalar score?” It can
therefore concentrate on one easy crop-only or animal-only lineage. The QD
driver answers a different question: “what is the best final-cash checkpoint
we have found for every materially different farm behavior?”

`qd_sweep.py` trains isolated short probes and evaluates every retained
checkpoint with the native evaluator. It archives a policy by three observed
outcome axes:

- expansion: compact, partial, or full expansion;
- production: crop-only, dual-production, or animal-led;
- capital use: cash-heavy, balanced, or growth-heavy reinvestment.

This produces 27 niches. Final money ranks checkpoints only within the same
niche. Reward coefficients are mutation inputs, never the niche labels. That
prevents a coefficient called “animal” from being mistaken for actual animal
behavior.

The driver never edits `config/kaggriculture.ini`. Every trial has a unique run
ID containing its architecture, and cold-start discovery uses the fixed native
bot mixture instead of a changing PFSP league. It also evaluates intermediate
checkpoints, preserving a useful 30M-step policy even if the same run collapses
at 100M.

## Local pilot

Build the current GPU binary first. Then inspect commands without training:

```bash
cd /home/felix/puffertank/pufferlib
source /home/felix/puffertank/.venv/bin/activate
python ocean/kaggriculture/qd_sweep.py \
  --output logs/kaggriculture/qd_local_pilot \
  --trials 6 --steps 10000000 --eval-games 32 --dry-run
```

Run a useful 128x2 pilot:

```bash
python ocean/kaggriculture/qd_sweep.py \
  --output logs/kaggriculture/qd_local_128x2_v1 \
  --trials 18 --steps 30000000 --eval-games 128 \
  --agents 2048 --minibatch-size 2048 --horizon 64 \
  --hidden-size 128 --layers 2 --gpus 0
```

Resume by increasing the total trial count:

```bash
python ocean/kaggriculture/qd_sweep.py \
  --output logs/kaggriculture/qd_local_128x2_v1 \
  --resume --trials 60 --steps 30000000 --eval-games 128 \
  --agents 2048 --minibatch-size 2048 --horizon 64 \
  --hidden-size 128 --layers 2 --gpus 0
```

The family seeds deliberately cover balanced, crop, animal, expansion,
liquidator, and sparse teaching regimes. Crop and animal scale ranges extend
to 40 and 80 because observed 128/256 policies often need much more than the
old nominal five-point range before the corresponding behavior appears. The
archive still classifies the resulting behavior, not the coefficient name.

On a multi-GPU host use, for example, `--gpus 0,1,2,3`. One independent trial
runs on each GPU. The principal outputs are `archive.json`, a compact
`archive.tsv`, per-trial train/eval logs, and `trials.jsonl`.

After a cold-start archive has produced a few different behaviors, a compatible
league can be used for refinement:

```bash
python ocean/kaggriculture/qd_sweep.py \
  --output logs/kaggriculture/qd_league_128x2_v1 \
  --trials 24 --steps 50000000 --eval-games 128 \
  --league saved/kaggriculture_league_128x2_qd_v1/league.ini \
  --hidden-size 128 --layers 2 --gpus 0
```

The league must use the same hidden size, layer count, observation ABI, and
action ABI. An incompatibility fails the isolated trial instead of mixing
weights into an existing run directory.

The archive is discovery data, not proof of strength. Promote promising niche
elites into a new league, then compare them using fixed-seed, seat-balanced
matches and PSRO. Longer continuations should start from those finalists rather
than making every QD probe expensive.
