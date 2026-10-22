# Free-pick vs the upgraded league, plus the gar_egg_zap self-play control (2026-10-22)

**Box:** Pokemon box 56007315 (`ssh -p 33431 root@ssh6.vast.ai`), tree `/workspace/PufferLib`. This is the
`magnet-kl` trainer, with no `PUF_POLICY_ARCH` hook; the learner is PufferNet, so it does not need it.

## 1. gar_egg_zap self-play control (tmux `garsp`, about 1 h)

- **Command:** `run_gar_selfplay_control.sh` is B1's exact gar_egg_zap command without the frozen league.
- **Question:** the league added about 9 points on zam_egg_mie (60.4 / 53.1 vs 50.7 / 45.5 self-play
  only). Does that hold on a second team?
- **Comparison:** B1 gar_egg_zap with league, 61.8 / 58.0.
- **Output:** `gar_selfplay_control/evals.log`.

## 2. Upgraded frozen league (`build_league.py` → `league/league.ini`)

For each of the 13 teams, the league uses whichever policy had the higher SyntheticRLV2+TaurosV0 mean:
the old rr20m member (B0) or the B1 700M retrain. Each bank plays its own team.

| Bank | Source | Mean |
|---|---|---|
| gar_egg_zap | B0 r000026 | 60.0 |
| zam_egg_cloy_don | B0 r000026 | 58.2 |
| gar_egg_mie_zap | B1 700M | 58.2 |
| zam_egg_zap | B1 700M | 58.0 |
| zam_egg_mie | B1 700M | 56.8 |
| gar_egg_mie | B0 r000026 | 56.8 |
| mie_egg_zap | B0 r000026 | 56.8 |
| jynx_egg_zap | B1 700M | 56.2 |
| jynx_mie_don | B1 700M | 55.2 |
| mie_egg_cloy | B1 700M | 51.9 |
| jynx_egg_gar_zap | B1 700M | 51.0 |
| zam_egg_gar | B1 700M | 48.1 |
| mie_egg_don | B0 r000026 | 47.6 |

## 3. Free-pick league run (tmux `freepick`, starts when the control finishes training)

`run_freepick.sh` uses B1's trial-0 recipe (magnet KL 0.1, same PPO settings), with these changes:

- **Free-pick drafting.** The learner team is `None`.
  - 75% of games deal 3 required species from the core deck: all 540,274 species triples, dealt
    without replacement and reshuffled when empty. The policy drafts the other 3 species and all
    moves.
  - 25% of games are fully free-pick.
- **League share.** 93.75% of games (3,840 of 4,096) are against the upgraded league, so 88% of the
  learner's play is vs the league and 12% is self-play.
  - An exact 85% of games is not allowed: the learner's seats (8192 − frozen) must divide into the
    128 minibatch sequences.
  - For comparison, B1's "25% league" was 25% of games but only 14% of the learner's play.
- **Budget.** 2B steps, with a checkpoint about every 250M (954 epochs). One synchronous buffer, as
  the core deck requires.
- **Team logging.** Drafted teams and moves are logged every 100th episode per env, to `run/teams.jsonl`.
- **Evals.** tmux `freepick_eval` scores each checkpoint as it appears. Scoring is fully free-pick vs
  SyntheticRLV2 and TaurosV0, 48 games per panel team, written to `run/evals.log`.

**Sanity run (20M steps):** passed.
- The trainer logs "13 frozen bank slots; expert_env_fraction=0.9375" and
  `PK_CORE_DECK … combinations=540274 probability=0.75`.
- Game mix: 72% forced-core expert games, 24% normal expert games, and about 3.7% self-play.
- 33K drafted-team records were written, and checkpoints saved.
- A 2-game free-pick Metamon eval ran; it scored 0%, as expected for an untrained policy.
- Speed was about 185K steps/s while the control shared the GPU.

**Risk.** The learner starts from scratch and almost every game is against strong frozen policies; its
early win rate is about 2%. Reward will be sparse until it learns. The 250M checkpoints will show
whether it does.

## Results

**Free-pick run: failed to learn.**
- It scored 0.0% vs both SyntheticRLV2 and TaurosV0 at every checkpoint (250M–1.75B), 0 wins on all panel
  teams.
- Its training win rate stayed about 2%, and entropy stayed near random (about 1.96) through 2B steps.
- With 88% of its play against strong frozen policies from scratch, there was almost no learning
  signal. Options: opponent-entropy annealing (banks start partly random), or warm-starting from a B1
  policy.

**gar_egg_zap self-play control** (PufferNet, no league):

| Checkpoint | SyntheticRLV2 | TaurosV0 |
|---|---|---|
| 350M | 25.3 | 37.5 |
| 700M | 49.3 | 34.4 |
| B1 with league, 700M | 61.8 | 58.0 |

The league adds about 12 points on SyntheticRLV2 and 24 on TaurosV0 here, against about 9 / 8 on
zam_egg_mie.

**Magnet-off ablation:** restarted 2026-10-22 18:18 with the per-opponent win-rate build (see below).

## Per-opponent win-rate logging (2026-10-22)

`ocean/pokemon/pokemon_base.h` now logs, per opponent, the learner's score (`vs_self_score`,
`vs_<bank>_score`) and its share of games (`vs_<bank>_share`). Bank names come from the manifest
`name =`. The code is env-local; there is no core change. It is deployed on the B1 box: `./puffer`
was rebuilt and the previous `ocean/pokemon` is backed up at `/workspace/ocean_pokemon_backup_20261022`.
