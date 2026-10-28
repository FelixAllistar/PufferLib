# Pokémon agents vs every Metamon model (2026-10-21)

Box: `/workspace/PufferLib/reports/pokemon_benchmarks/matrix_20261021/`, runner `/workspace/run_matrix.sh`.

- **Protocol:** `evaluate_private.py` (native-private-v2), learner argmax, 48 games per panel team
  (144 total), seed 20261025, alternating seats.
- **New opponents:** SmallRL, MediumRL and LargeRL were added to `metamon_opponent.py`'s pinned
  `MODELS` and qualified with `qualify_metamon.py`. All three had 100% argmax agreement with upstream
  attention, a maximum probability gap of 0.003 or less, and no unknown words.
- **Team:** every agent uses the same fixed learner team.

| Agent | SmallRL | MediumRL | LargeRL | SyntheticRLV2 | TaurosV0 | Mean |
|---|---|---|---|---|---|---|
| Self-play sweep trial 0 (256×2, 200M) | 80.6% | 72.9% | 81.2% | 39.6% | 54.9% | 65.8% |
| Self-play sweep trial 27 (512×2, 263M) | 81.9% | 86.8% | 79.5% | 45.8% | 47.9% | 68.4% |
| 20M round-robin league, round 26 master | 81.2% | 76.0% | 79.2% | 45.8% | 43.8% | 65.2% |
| r26 baseline (older round-robin league) | 83.3% | 85.4% | 78.5% | 42.4% | 46.2% | 67.2% |
| *Other person's report* | *85–95% (small/med/large)* | | | *54%* | | |

Each cell has a standard error of about ±4%.

- **Weaker Metamon models:** every one of our agents scores about 73–87% against small, medium and
  large, close to the other person's "85–95% against Metamon".
- **Strong models:** against SyntheticRLV2 we score 40–46%, about 8–14 points behind his 54%.
  Against TaurosV0 we score 44–55%.
- **No method wins:** self-play sweep, 20M round-robin and the older league are all within noise of
  each other. With a fixed team, round-robin with forced team diversity was not measurably better or
  worse than plain self-play.
- **Team controls:** with our team, the Metamon pilots score 61.1% (TaurosV0 piloting, vs TaurosV0)
  and 56.9% (SyntheticRLV2 piloting, vs SyntheticRLV2). No team in the 41-candidate ranking beats
  ours, so the team is not the limit (`../control_team_20261021/README.md`).
