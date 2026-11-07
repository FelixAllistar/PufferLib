# Gen 3: matchup features A/B (2026-11-06, Vast 59392729)

## Why
The fresh 1024x2 Gen 3 OU model (`../20261106_gen3_omni`, 4B steps) won 1 of 210 games vs Metamon Kakuna while beating
its own past checkpoints every time. Diagnosis:
- Engine, action mask and Showdown bridge check out. The randbat model beat a random bot 92% both in our engine and on
  real Showdown (55/60), so nothing is lost in the bridge.
- The model is weak in our own engine. The 3.4B checkpoint beats the new in-engine max-damage bot only **51%**
  (500 held-out games). A random bot it beats 98%. The max-damage bot itself beats random 99.3%.
- Likely cause: Gen 3 has no structure in the network. Gen 1 uses `ocean/pokemon/pokemon_encoder.cu` (shared
  move and Pokemon MLPs plus the type chart). Gen 3 feeds a flat 5,175-value vector to the default encoder, which has to
  learn type matchups separately for every slot and is never told how much damage a move does.
- Nothing caught it early: self-play "vs previous" always reads 51-60% and random-bot win rate is near 100% for bad
  and good models alike.

## Changes (ocean/pokemon3)
- `env.bot_policy=2`: max-damage baseline (`pk3_maxdmg_action`, full information). Every legal move is scored by
  max-roll damage x accuracy, capped at the target's HP, using the engine's damage calc. It never switches by choice,
  and forced switches go to the healthiest Pokemon. It beats random 298/300.
- Matchup features (`write_matchups`): 168 values appended after the field, so all older offsets stay. OBS goes from
  5175 to 5343.
  - Each of the 12 Pokemon, per move: type effectiveness vs the other side's active, max-roll damage as a fraction of
    max HP, and whether it KOs.
  - Each Pokemon also gets the most damage and the highest effectiveness the other active's moves would deal it.
  - The opponent's Pokemon use estimated stats, and only moves already seen are counted (same partial information as
    on Showdown).
  - Cost: 9.4 us per observation vs 1.3 us without.
- `env.matchup=0` zeroes the features (same binary, same obs size): the control arm.
- `env.draft_log=PATH` (diagnostic): one line per finished team-mode game.

## A/B
`run.sh`:
- 256x2, 500M steps per arm, arm on (matchup=1) first, then arm off.
- Same seed and recipe as the fresh run, minus the drafting: pool teams only, league 25% vs 4 frozen snapshots,
  EMAgnet 0.03.
- Built with the current trainer (5.0 HEAD incl. the multi-learner league refactor).

`watch.sh`:
- First scores the fresh 1024x2 run's checkpoints vs max-damage, with `puffer_pk3L` (same code, matchup block compiled
  out, OBS 5175).
- Then scores every A/B checkpoint: 1,000 held-out games, argmax.

Results land in `evals.txt` and `status.txt` (box copy).

## Results (box evals.txt; win rate vs max-damage, 1,000 held-out games, argmax)

| steps | arm on (matchup=1) | arm off (matchup=0) | fresh 1024x2 omni run (no features, drafting on) |
|---|---|---|---|
| 0 | 0.9% | 1.1% | – |
| 52M | 16.4% | 11.8% | – |
| 105M | 23.3% | 11.5% | – |
| 210M | 38.7% | 15.3% | – |
| 262M | 40.9% | 15.2% | 14.0% |
| 367M | 48.8% | 18.3% | – |
| 500M | **53.0%** | **18.5%** | – |
| 1.05B / 2.1B / 3.1B / 4.0B | | | 31.3 / 40.8 / 48.3 / 55.4% |

- The features nearly triple the win rate at 500M (53 vs 18.5). Arm off flattens out near 15-18%.
- At 256x2 and 500M steps (34 min), arm on equals the 1024x2 model trained for 4B steps without features (55%).
- 53% against a bot that never switches is still weak. Next: the full 1024x2 recipe with the features
  (`../20261106_gen3_matchup_1024`, started 23:30 UTC). If that stays weak, port Gen 1's structured encoder.
- Run times on the RTX 5060 Ti: arm on ~207K steps/s (34 min), arm off ~300K+ steps/s (28 min).
