# Pokémon Transformer A/B on zam_egg_mie (2026-10-22)

**Question.** Does the entity Transformer encoder beat PufferNet, scored against Metamon?
- Team: zam_egg_mie (Alakazam/Exeggutor/Starmie/Chansey/Snorlax/Tauros, lead Alakazam).
- Opponents: SyntheticRLV2 and TaurosV0, 48 games per panel team, argmax, seed 20261025.

| Arm | Encoder | Opponents in training |
|---|---|---|
| `transformer` | Entity Transformer (2 layers, width 128, 4 heads) → fusion MLP → MinGRU 256×2 | Current-policy self-play |
| `puffernet` | Unchanged PufferNet encoder → MinGRU 256×2 | Current-policy self-play |

**Recipe.** Both arms use B1's trial-0 recipe for 700M steps (`run_arm.sh` holds the exact arguments),
with checkpoints at about 350M and 700M. That includes magnet KL (coef 0.1, tau 0.01) and the same LR
for both arms, which also tests Research Q1 (do PufferNet hypers transfer?).

Neither arm uses the frozen league. Its banks are PufferNet checkpoints, and they run through the
learner's encoder code, so a Transformer learner cannot load them. The `puffernet` arm is the
matched control: the Transformer-vs-PufferNet difference comes from this table, not from the B1
league run. B1's league result for this team (60.4% / 53.1% at 700M) is a third reference point.

## Trainer

Pokémon runs use the `magnet-kl` trainer, not `5.0`. The Pokémon box tree is:
- `magnet-kl`'s `src/` and `config/default.ini`;
- plus the live-eval dashboard hooks in `src/pufferl.cu`;
- plus `ocean/pokemon` (here also the attention encoder);
- plus the box's `config/pokemon.ini` with the three `pokemon_attention_*` keys added.

The new box gets the same tree with the attention encoder added. The trainer core is unchanged
(see `reports/core_changes_audit.md`).

## Preflight (local GTX 1060, FP32)

- `make -C ocean/pokemon encoder-test`: CPU/GPU parity and numerical gradients, with attention off and
  on (shapes 2×16×2, 1×32×4, 3×24×3 with embed 64).
- `semantic-legacy-test`: the CPU digest is identical to the pre-attention code.
- **5.0 trainer:** a 1M-step attention smoke run beat random 180/200 when loaded through the CPU
  evaluator loader.
- **magnet-kl trainer:** both arms ran through `run_arm.sh`. The Transformer arm needs about twice the
  GPU memory, because the magnet doubles the training activations; at full size that is about
  5–6 GB, which fits 16 GB.

## Setup (new box)

1. **Bundle (locally).** Build the deploy tree:
   - `git archive magnet-kl`;
   - the Pokemon box's `src/pufferl.cu` and `config/pokemon.ini`, with the attention keys added;
   - `git archive HEAD ocean/pokemon` and this directory.

   Then `tar czf pokemon_ab_bundle.tgz build.sh src config vendor ocean/pokemon ocean/breakout <this dir>` (about 0.7 MB).
   A fresh extract builds, and trains both arms.
2. **Copy.** Copy the bundle to the box's `/workspace` and run `setup_box.sh` there. It does the build, the
   encoder tests and the Metamon env.
3. **Metamon cache and panel teams.** Copy `build/pokemon/metamon-cache` and
   `reports/pokemon_benchmarks/20261014_all_roster` (the three panel-team files the evaluator reads) from the Pokemon box with
   `ssh old 'tar c -C .../build/pokemon metamon-cache' | ssh new 'tar x -C .../build/pokemon'`.

## Run (new box)

```
tmux new -d -s ab_t 'bash reports/pokemon_experiments/20261022_transformer_ab/run_arm.sh transformer'
tmux new -d -s ab_p 'bash reports/pokemon_experiments/20261022_transformer_ab/run_arm.sh puffernet'
```

- **Outputs:** each arm writes `<arm>/train.log` and `<arm>/checkpoints`, then runs `eval_arm.sh`,
  which appends to `evals.log`.
- **Resume:** a `trained` marker skips retraining, and the evals skip any `summary.json` that already
  exists.

## Launched (2026-10-22)

- **Box:** Vast 57154204, `ssh -p 28489 root@ssh2.vast.ai`. tmux `ab_t` (transformer) and `ab_p` (puffernet).
- **Box checks:** setup passed. That covers FP32 parity and gradients with attention off and on, plus the
  private-evaluator checks. BF16 parity (`tests/test_encoder_bf16.cu`, built by hand) also passed at the
  run shape: 2×128×4 with H256, max scaled forward error 0.26%, and all 40 tensors get finite nonzero
  gradients.
- **Speed with both arms sharing the GPU:**
  - transformer: 15.8K steps/s; training is 85% of wall time, about 14 s per epoch;
  - puffernet: about 97K steps/s, about 2.3 s per epoch.
- **Estimated finish:** PufferNet in about 2 h. The Transformer has about 12 h at the shared rate, less
  once it has the GPU alone.
- **Why the Transformer is slower:**
  - About 30× more encoder compute per observation (21 tokens × 2 layers at width 128, against the
    fusion MLP).
  - The magnet doubles the forward passes.
  - `native_transformer.cuh` uses 256-thread blocks for 21-token softmax and 128-wide LN rows, which is
    inefficient at this size. It is core code, so it is left unchanged.
- **GPU memory:** 11.7 GB for both arms together.

## Results

| Arm | Checkpoint | SyntheticRLV2 | TaurosV0 |
|---|---|---|---|
| puffernet (self-play) | 350M | 53.1 | 45.1 |
| puffernet (self-play) | 700M | 50.7 | 45.5 |
| transformer (self-play) | 350M | **55.6** | **52.8** |
| transformer (self-play) | 700M | **54.2** | 46.2 |
| *B1 PufferNet with league, for reference* | *700M* | *60.4* | *53.1* |

The self-play-only control is about 9 points below the league-trained B1 run on both models. The
Transformer must therefore be compared with the control, not with B1.

**Verdict.** The Transformer leads the matched control in all four cells: +2.5 / +7.7 at 350M and
+3.5 / +0.7 at 700M. The two-model mean is 54.2 vs 49.1 at 350M and 50.2 vs 48.1 at 700M. Each
difference is within noise alone (the standard error of a difference is about 5.7), but the direction
is consistent.

PufferNet's hyperparameters transferred unchanged (Research Q1). The Transformer peaked at 350M.
Alone on the GPU it trained at about 32K steps/s (about 7× slower than PufferNet) and finished 700M in
about 6.5 h. Both self-play arms trail the league-trained PufferNet (60.4 / 53.1). The league matters
more than the encoder so far.

Next test: a Transformer with the frozen league. That needs per-policy encoder selection, which is a
trainer core change.
