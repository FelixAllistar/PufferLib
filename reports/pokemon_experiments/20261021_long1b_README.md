# Pokémon long run: sweep trial 0 settings for 1.0B steps (2026-10-21)

Pokémon box (`ssh -p 33431 root@ssh6.vast.ai`), `/workspace/PufferLib/reports/pokemon_experiments/20261021_long1b/`.

- **Training:** tmux `long1b`, `run.sh`.
- **Settings:** self-play sweep trial 0's full config: 256×2, 32/64 encoder, Starmie team, 25%
  round-26 peers, magnet 0.1 / τ 0.01, entropy 1e-3 annealed, LR 5e-4 cosine.
  - `total_timesteps` = 1,000,079,360 (3,815 rollouts of 8,192×32).
  - A checkpoint every 382 rollouts (~100M steps).
  - The built-in TaurosV0 hook is off. The derived key `env.catalog_sha` was dropped from the
    saved config.
- **Evals:** tmux `long1b_eval`, `watch.sh`. Each checkpoint is scored against SyntheticRLV2 and
  TaurosV0 with `evaluate_private.py`: argmax, 48 games per panel team (144), seed 20261025, the
  average over the three panel teams. Results go to `evals.log` and `<checkpoint>.eval_<model>/`.
- **Speed:** about 236K steps/s, so roughly 75 min of training plus evaluation.

Reference with the same eval: trial 0 at 200M scored 39.6% vs SyntheticRLV2 and 54.9% vs TaurosV0.
The other person's benchmark is 54% vs SyntheticRLV2.

## Results

| Checkpoint | vs SyntheticRLV2 | vs TaurosV0 |
|---|---|---|
| 100M | 19.4% | 26.4% |
| 200M | 37.8% | 42.4% |
| 300M | 42.4% | 46.5% |
| 400M | 47.2% | 51.4% |
| 500M | 47.2% | 56.2% |
| 600M | 54.5% | 47.9% |
| **700M** | **57.3%** | 50.7% |
| 800M | 54.5% | 52.1% |
| 900M | 56.6% | 49.7% |
| 1.0B | 46.5% | 52.4% |

From 600M to 900M the run matches or beats the 54% SyntheticRLV2 benchmark. Each cell is about ±4%,
so the 1.0B drop is partly noise. Longer training was the missing piece. Before this the longest run
on these settings was 263M steps.
