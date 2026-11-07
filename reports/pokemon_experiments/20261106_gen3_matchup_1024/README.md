# Gen 3 OU: fresh 1024x2 with matchup features (2026-11-06/07, Vast 59392729, stopped)

Same recipe as `../20261106_gen3_omni` (172,594-team pool, 30% free-pick, forced cores, 25% frozen league, EMAgnet)
plus the matchup features (`env.matchup=1`, obs 5343; see `../20261106_gen3_matchup_ab`). `train.sh`, `watch.sh`
(vs max-damage every 250M, Kakuna every 1B), `kakuna_eval.sh`. Started 23:30 UTC. **Stopped by the user at 2.36B
of 4B steps (03:45 UTC)**: "it doesn't even look that good, I don't think it's going to get better". The box was
destroyed afterwards.

| steps | vs max-damage (1,000 held-out games) | vs Kakuna (gen3ou, 30 games) | old omni run vs max-damage |
|---|---|---|---|
| 0 | 4.5% | 0 / 30 | – |
| 262M | 44.5% | – | 14.0% |
| 524M | 54.2% | – | – |
| 786M | 65.5% | – | – |
| 1.05B | 73.2% | 3 / 30 | 31.3% |
| 1.31B | 73.8% | – | – |
| 1.57B | 77.9% | – | – |
| 1.84B | 78.5% | – | – |
| 2.10B | 78.4% | **8 / 30** | 40.8% |
| 2.36B | 78.5% | – | – |
| 4.0B | – | – | 55.4% |

- Much better than the run without features. The max-damage score passed the old run's final 55% before 524M.
- Flat at ~78.5% vs max-damage from 1.57B on. It still loses about 1 in 5 to a bot that never switches.
- Kakuna improved (0 → 3 → 8 of 30) but stays far from competitive.
- Drafting collapsed onto one team again (drafted vs pool 94%).
- Kept checkpoints: 2.10B (best Kakuna; also `checkpoints/pokemon_final/20261106_gen3_matchup_2.1B.bin`, md5
  292147d00cc12977272dc442fe33ab6a) and 2.36B (last, with its magnet file).
- Next idea if Gen 3 resumes: port Gen 1's structured encoder (ocean/pokemon/pokemon_encoder.cu: shared move and
  Pokemon MLPs, type chart); the hand features help but do not close the gap.
