# Next runs after the window / free-pick v2 results (2026-10-23)

Both boxes were synced to local `5.0` first (commit e3c68f02a: src, ocean/pokemon, config, tests,
vendor, scripts, build.sh) and rebuilt with `./build.sh pokemon`; previous sources are in
`/workspace/before_5.0_sync_20261023.tgz` and `puffer.before_5.0_sync` on each box.

| Run | Box | Question | Compare |
|---|---|---|---|
| `league_gen2_gar` (`run_league_gen2.sh`) | window box 57306307 | Does training against the upgraded league (best of B0/B1 per team) beat B1's round-26 league? Only the league changes. 700M from scratch. | B1 gar_egg_zap @700M 61.8 / 58.0 |
| `b1_repro_5.0` (`run_b1_repro.sh`) | window box | Control added after league gen 2 scored 48.6 / 52.1: B1's exact command (original round-26 league) on the 5.0 build. Reproduces B1 → the upgraded league hurt; ~50 → the build changed training. | B1 61.8 / 58.0 |
| `freepick_warm` (`run_freepick_warm.sh`) | B1 box 56007315 | Free-pick v2 settings started from the B1 gar_egg_zap 700M policy, 1B. Is free-pick's ~40 plateau the from-scratch start, or drafting its own team? | free-pick v1 33.7 / 49.7, v2 33.7 / 33.3; B1 61.8 / 58.0 |
| `fixed_fpteam` (`run_fixed_fpteam.sh`) | B1 box | Team or moves? B1's recipe with species fixed to the warm start's free-pick team (Articuno, Jolteon, Jynx, Lapras, Tauros, Zapdos); the policy picks moves. ~60: the team is fine, free-pick's gap is move choice and skill. ~40: the team is weak. | b1_repro_5.0 |
| `meta_rotation` (`run_meta_rotation.sh`, queued by `queue_window_meta.sh`) | window box, after b1_repro_5.0 | Free-pick warm start whose forced 3-species cores come from 15 standard OU species (0.75 of games, constant): does piloting the meta species fix the team and the movesets? | freepick_warm, B1 |
| `move_pairs` (`run_move_pairs.sh`) | B1 box, after fixed_fpteam is scored | Move exploration like forced species cores: in 75% of learner games each drafted Pokémon is dealt 2 random legal moves and picks the other 2 (annealed to 0 by half), plus forced species cores (0.75 → 0 by half) and slight opponent randomness (0.3 → 0 by 20%). Warm start from B1 gar, 1B. Drafts must fill all 4 move slots. Does it learn Smogon-quality sets in free drafts (`smogon_set_rate_free`), and does that raise the free-pick score? | freepick_warm: 15% exact Smogon sets, ~40; B0 gar 83% |

### Branch 2: capacity and experience against the good league (planned 2026-10-23)

| Run | Box | Question | Compare |
|---|---|---|---|
| `big_b1_1024` (`run_big_b1.sh`) | window box, started 16:46 UTC | B1's gar_egg_zap recipe with a 1024x2 learner vs the frozen upgraded league, B1 hyperparameters, 1B, scored every 250M. Are we capped by model size? If 1024 learns, sweep it (learning rate first). | B1 61.8 / 58.0, b1_repro_5.0 55.9 / 52.8 |
| `omni_master_256` (`run_omni.sh 256`) | B1 box, after move_pairs is scored | OMNI_MASTER: one policy plays all 13 league teams (`env.learner_team_pool`, a team drawn per game), same recipe, 2B. Scored on gar_egg_zap, zam_egg_cloy_don and zam_egg_zap every 500M (`eval_omni.sh`). Then 1024 if big_b1 learns. | Specialists on those teams: 62.5 / 57.6, 58.3 / 58.0, 58.7 / 57.3 |

### Queued 2026-10-23 20:25 UTC on the B1 box after OMNI trains (`queue_b1_after_omni.sh`, tmux `after_omni`)

- `freepick_omni` (`run_freepick_omni.sh`): free-pick warm-started from OMNI's final checkpoint, move-pairs
  settings, but 31.25% league games so 81.5% of the learner's play is self-play (earlier free-pick: 93.75%
  league games). 1B, scored every 125M.
- Pilot generality (`eval_teams.sh`, `pilot_teams.txt`, tmux `pilot_eval`): OMNI final and B0 gar_egg_zap each
  pilot the 13 league teams plus free-pick's argmaxed team (drafting their own moves), vs SyntheticRLV2 and
  TaurosV0. Which pilots more generally, and does any (policy, team) pair set a new high score?
- Note on "is ~40 just the ceiling for the free-pick team": `fixed_fpteam` already trained that exact team
  (move_pairs' converged team) B1-style and got 34.9, so free-pick's ~40 is at or above a specialist on it.

Code for these (commits 200de5799, 62d12d788, env-local in `ocean/pokemon`): no early stop in move drafting;
`env.force_move_pairs_prob/_until`; `smogon_set_rate_free/_forced` and `move_pair_team_fraction` logs;
team-log rows gain `move_pairs`. Both boxes are redeployed with these once their current runs are scored.

## Results

- **league_gen2_gar:** 48.6 / 52.1 at both 350M and 700M (different per-team results; the totals match by
  coincidence). About 9 below B1; `b1_repro_5.0` decides whether the league or the build is the cause.
- **b1_repro_5.0:** 58.0 / 52.8 at 350M, 55.9 / 52.8 at 700M (mean 54.4). Original B1 got 59.9 and the
  ε 0.5 rerun 58.7, so the 5.0 build is about 4–5 lower: roughly 1.5 standard errors, not clearly a
  regression. League gen 2 (50.3) is about 4 below this control, also within noise. Seed-to-seed spread
  on this recipe looks like ±3–5 points; single runs cannot separate effects that small.
- **meta_rotation:** first launch failed at load (`config.ini` was not copied next to the warm-start
  checkpoint on the window box; kept as `meta_rotation.failed_noconfig`); relaunched 15:35 UTC.
- **fixed_fpteam:** 41.0 / 34.0 at 350M, 29.2 / 40.6 at 700M (mean 34.9), about 20 below the gar team with
  the same recipe (b1_repro_5.0, 54.4). Its moves still locked into non-standard sets with species fixed:
  21% exact Smogon sets (Lapras 74%, Jolteon and Zapdos 0-1%; Zapdos Thunderbolt / Thunder Wave / Hyper
  Beam / Double-Edge with no Drill Peck in 93%). So team and moves are confounded here; fixed species alone
  does not make it find good sets for these Pokémon, which is what `move_pairs` targets.
- **move_pairs:** trained to 1B. Exact Smogon sets in free drafts 4% → 25% (487M) → 28% (1B); freepick_warm
  ended at 15%. Metamon: 125M 30.2 / 34.4, 250M 36.5 / 38.9, 375M 40.6 / 43.8 (best), 500M 35.8 / 37.2,
  625M 37.8 / 38.9, 750M 32.3 / 39.2, 875M 39.6 / 35.8, 1B 41.7 / 35.8. Better sets, same 35-44 score range.
  Final sets are mostly settled and near-standard (Jynx Lovely Kiss / Psychic / Blizzard / Ice Beam 99%, Jolteon
  Body Slam / Rest / Thunder Wave / Thunderbolt 94%, Zapdos back to Drill Peck), on the same off-meta team.
- **omni_smogon_256 (live, B1 box, started 23:01 UTC):** OMNI recipe from scratch on 256 random Smogon OU teams
  with fixed Smogon sets (`smogon_pool/`, `ocean/pokemon/build_team_pool.py`), 2B; then `run_freepick_smogon.sh`
  fast / none / slow (1B each, 81.5% self-play), queued by `queue_b1_smogon.sh` (tmux `smogon_queue`).
- **omni_master_1024 (live, window box, started 02:42 UTC):** `run_omni.sh 1024`, the OMNI_MASTER recipe and
  hyperparameters at 1024x2, 2B (~96K steps/s, about 6 h), scored by `eval_omni.sh`. Box redeployed to the latest
  commit first.
- **big_b1_1024_cont750 (done):** +1.5B 57.6 / 47.2, +1.75B 59.0 / 48.3, +2B 57.6 / 50.7. Best stays +750M
  (65.3 / 56.6); 1024x2 on one team does not beat 256x2 B1 (61.8 / 58.0).
- **omni_smogon_256 (live, 1.4B at 01:10 UTC):** 500M on teams with Smogon OU sets: gar_egg_zap_ou 55.9 / 67.0,
  zam_egg_cloy_don_ou 41.0 / 55.6, freepick_omni_team 42.7 / 43.4, smogon_000 (random pool team) 18.1 vs
  SyntheticRLV2. Each checkpoint takes ~1.5 h to score (7 teams, GPU shared with training and the pilot evals).
- **OMNI pilot test, all 13 league teams at 2B:** 55.6–71.5 vs SyntheticRLV2, 51.4–73.3 vs TaurosV0; also
  jynx_egg_zap 68.1 / 60.1, jynx_mie_don 66.0 / 51.4, mie_egg_cloy 68.8 / 64.6, jynx_egg_gar_zap 60.4 / 52.8,
  zam_egg_gar 55.6 / 55.9, mie_egg_don 66.7 / 59.0. Free-pick team and B0 gar's 14 teams still scoring.
- **big_b1_1024_cont750:** +1B 56.2 / 56.2, +1.25B 56.9 / 55.2, +1.5B SyntheticRLV2 57.6: flat near 56–57 after
  the +750M peak (65.3 / 56.6).
- **freepick_omni (done):** 125M 61.8 / 49.0, **250M 62.8 / 56.6**, 375M 53.5 / 50.7, 500M 58.3 / 38.2,
  625M 53.8 / 52.4. Draft trajectory (`draft_trajectory.py`): 3,649 distinct teams in the first 10% of free
  drafts, never an exact OMNI team; Exeggutor and Snorlax (in 12 of 13 league teams) dropped at once, Lapras
  (in none) rose 20% → 100%. It explored and picked a team that may counter the league.
- **OMNI pilot test at 2B (`pilot_eval/omni_master_256`):** gar_egg_zap 67.4 / 55.9, zam_egg_cloy_don 64.2 / 73.3,
  gar_egg_mie_zap 64.9 / 68.8, zam_egg_zap 71.5 / 59.7, zam_egg_mie 61.1 / 56.2, gar_egg_mie 62.2 / 59.7,
  mie_egg_zap 68.1 / 60.4 (rest and B0 gar pending). OMNI 1.5B zam_egg_cloy_don 71.2 / 67.4, zam_egg_zap 68.4 / 63.9.
- **big_b1_1024_cont750:** +250M 56.2 / 50.0, +500M 59.4 / 55.2, **+750M 65.3 / 56.6**.
- **freepick_omni (first notes, 731M at 22:00 UTC):** 125M **61.8 / 49.0**, 250M SyntheticRLV2 62.8: free-pick at the
  level of our best fixed-team policies (earlier free-pick runs peaked around 40). It drafts a standard RBY OU
  team, 100% of free drafts: Alakazam, Chansey, Lapras, Rhydon, Starmie, Tauros, with near-textbook sets
  (Tauros Body Slam / Hyper Beam / Earthquake / Blizzard 99%, Lapras Blizzard / Body Slam / Sing / Thunderbolt
  88%, Alakazam Psychic / Recover / Seismic Toss / Thunder Wave 84%, Starmie Surf / Thunderbolt / Thunder Wave /
  Recover 83%). Exact Smogon OU sets 64%, any format 74%. Warm start from OMNI + 81.5% self-play did it.
- **omni_master_256 (trained, 2B):** gar_egg_zap 61.1 / 63.9 (500M), 63.2 / 64.2 (1B), 66.7 / 62.2 (1.5B);
  zam_egg_cloy_don 54.2 / 70.5, 64.6 / 69.4, 71.2 / pending; zam_egg_zap 62.8 / 51.4, 62.5 / 53.8.
  Pilot test at 2B so far: gar_egg_zap 67.4 / 55.9, zam_egg_cloy_don 64.2 / 73.3, gar_egg_mie_zap 64.9 / 68.8.
- **big_b1_1024_cont750 (live):** +250M after the restart: 56.2 / 50.0 (below the 750M start, 60.4 / 60.1;
  the restart dip). 571M at 22:00 UTC.
- **omni_master_256 (launch):** started 18:03 UTC on the B1 box, ~207K steps/s. 500M on gar_egg_zap: **61.1 / 63.9**
  (gar specialists: B1 61.8 / 58.0, B0 62.5 / 57.6). The first zam_egg_cloy_don / zam_egg_zap evals were
  invalid: `eval_omni.sh` symlinked the weights and evaluate_private.py resolves symlinks, so it read the
  run's own config (gar team) and repeated the gar score; moved to `omni_eval_symlink_bug/`, script fixed to
  copy the weights (37074b047), those evals rerunning.
- **meta_rotation:** 1B final 44.8 / 41.0 (its best; 875M 40.6 / 38.9), slightly above the other free-pick runs.
- **big_b1_1024:** 250M 36.5 / 43.8, 500M 53.8 / 46.5, **750M 60.4 / 60.1**, 1B 50.0 / 58.0 (B1 256x2 at 700M:
  61.8 / 58.0). Exact Smogon sets in its gar drafts 77% at 1B. The queue chose the warm restart (means rose
  40.1 → 50.2 → 60.3); per the user it now restarts from the 750M checkpoint at 1.25e-4 → 2.5e-5 over 2B
  (`big_b1_1024_cont750`, started 20:13 UTC, ~96K steps/s, about 6 h). The 1B-start version ran 0.8M steps
  and was stopped (`big_b1_1024_cont_from1b_stopped`). Queued after it
  (`queue_big_b1_next.sh`, tmux `big_b1_next`): if the mean score rose 250M → 500M → 750M, warm restart to 3B
  (`run_big_b1_cont.sh`: 1B weights + .magnet, 2B more at 2.5e-4 → 5e-5); otherwise the LR scan
  (`run_big_b1_lr.sh` 1.5e-4 then 2.5e-4, 1B each from scratch).
- **freepick_warm:** trained to 1B; evals pending (the first watcher never started: `eval_dir.sh` was only
  on the window box, and the tmux session lacked `-c /workspace/PufferLib`; fixed and restarted).

## Drafted teams and movesets (`/workspace/moves.py` over `teams.jsonl`, last 20k log lines)

B0 gar_egg_zap (species fixed, the policy picks moves; `log_drafts.sh`, learning off), near-deterministic:
Gengar Hypnosis / Psychic / Night Shade / Explosion; Exeggutor Sleep Powder / Stun Spore / Psychic /
Explosion; Chansey Ice Beam / Sing / Soft-Boiled / Thunder Wave; Zapdos Thunderbolt / Drill Peck /
Thunder Wave / Rest; Snorlax Body Slam / Earthquake / Hyper Beam / Self-Destruct; Tauros Body Slam /
Hyper Beam / Earthquake / Blizzard.

| Run | Team (share of free drafts) | Notes on moves |
|---|---|---|
| Free-pick v1 (ε 0.9, 2B) | Alakazam, Articuno, Jolteon, Lapras, Zapdos + Tauros (66%) | Sets still varied (top set per species 4–13%) |
| v1 continued +2B at ε 0 | same five + Golem (97%) | Locked in: Jolteon Thunderbolt / Thunder Wave / Double Kick / Rest 96%, Lapras Blizzard / Body Slam / Sing / Thunderbolt 94%, Alakazam Psychic / Recover / Seismic Toss / Thunder Wave 93% |
| Free-pick v2 (2B) | same five + Gengar (55%) or Starmie (17%) | Sets still varied |
| Warm start from B1 gar (1B) | Articuno, Jolteon, Jynx, Lapras, Tauros, Zapdos (100%) | Kept only Zapdos and Tauros from its gar team; dropped Chansey, Snorlax, Exeggutor, Gengar. Articuno often carries both Blizzard and Ice Beam, and 12% of its sets leave a slot empty |

Every free-pick run avoids Chansey and Snorlax, the two most-used Gen 1 OU Pokémon, and converges on a
team that beats the frozen league it trains against.

## Final results (overnight 2026-10-24; both boxes backed up to pufferlib/runs/pokemon_{b1,window}_box_*_backup, then destroyed)

- **OMNI_MASTER 1024x2 (2B):** gar_egg_zap 69.8 / 65.6 (1B), 76.0 / 57.3 (2B); zam_egg_cloy_don 72.9 / 68.8 (1B),
  **77.1 / 69.4** (2B, best score so far); zam_egg_zap 76.0 / 59.0 (1B), 71.5 / 59.7 (2B). Beats OMNI 256 on most cells.
- **Free-pick from Smogon OMNI (1B each, 81.5% self-play):** fast best 63.9 / 54.9 (500M); **none best 72.6 / 55.2**
  (500M), final 57.3 / 57.3; slow best 70.5 / 55.6 (625M). Slow to start (~30 at 125M for fast/slow), then 57–73,
  similar to or above free-pick from OMNI_MASTER (62.8 / 56.6).
- **Smogon OMNI 256 (2B):** gar_egg_zap with Smogon sets 59.4 / 59.7, zam_egg_cloy_don 47.2 / 50.7, free-pick's
  Lapras team 50.7 / 49.0; random pool teams 12–45.
- **Pilot test, B0 gar (single-team policy) on other teams:** its own team 62.5 / 57.6, every other team 2–51 vs
  SyntheticRLV2 (e.g. zam_egg_cloy_don 2.1 / 7.6). OMNI 256 scores 55–72 on all 13. Single-team policies do not
  transfer; OMNI does. OMNI on free-pick v1's Jolteon team: 25.0 / 36.5 (that team is weak for everyone).

## Batched Metamon evaluator (2026-10-25)

`ocean/pokemon/benchmarks/evaluate_batched.py` (+ `metamon_batched.py`) plays the same games as
`evaluate_private.py` (teams, seeds, seats, 512-update cap) `--batch` at a time: one Metamon forward per turn for
every live game (each batch row keeps its own KV-cache position, view, tokenizer state and sampling generator; a
row that must not act this turn is not advanced), our CPU policy stepped in threads, resets in threads. Many
checkpoints share one run (one `--output` each) so the batch stays full; outputs have the same files and fields.

Parity on the box (OMNI-1024 2B, zam_egg_mie / gar_egg_mie vs SyntheticRLV2, seed 20261025):
- `--batch 1`: 24/24 games bit-identical to the stored eval (same opponent-observation hashes).
- `--batch 24`: 24/24 identical. `--batch 144`: 142/144 identical, same score (65.6). Two checkpoints at
  `--batch 288`: 286/288 identical; scores 65.6 (= stored) and 64.6 (stored 65.3, one game flipped).
- The rare differences are last-bit GEMM rounding changing a sampled Metamon action; per-game computation is the same.

Speed with FP-3 training on the same GPU: one 144-game eval 819 s -> 165 s; the full matrix runs at ~125 games/min vs
~21 games/min for the two sequential streams (6x). The eval matrix was switched to it
(`run_eval_matrix_batched.sh`, tmux `eval_matrix`, log `eval_matrix_batched.log`, table `eval_matrix/matrix.txt`),
keeping the 7 teams x 2 candidates already finished. pilot_teams.txt: `freepick_jolteon` renamed `art_jol_jyn_lap_tau_zap`.

## Eval matrix result (2026-10-25, `eval_matrix/matrix.txt`)

Each candidate pilots all 17 teams (it drafts the moves), 144 games per opponent, SyntheticRLV2 / TaurosV0:

| candidate | mean, 17 teams | Syn | Tauros | mean, 13 league teams | worst team |
|---|---|---|---|---|---|
| **OMNI-1024 2B** | **61.9** | 65.0 | 58.8 | 65.5 | 34.2 |
| OMNI-1024 1B | 61.6 | 64.0 | 59.3 | 65.6 | 34.4 |
| OMNI-256 1.5B | 60.0 | 61.0 | 59.0 | 63.3 | 29.3 |
| OMNI-256 2B | 58.6 | 60.7 | 56.6 | 62.3 | 30.8 |

OMNI-1024 1B and 2B are tied (within noise); 2B is kept as the league policy (it has the best single cell,
zam_egg_cloy_don 77.1 / 69.4). Every candidate is weak on art_jol_jyn_lap_tau_zap (25–35) and so-so on the
free-pick-discovered teams (45–64), which none of them trained on.

## FP-3: free-pick from OMNI-1024 (done)

Scores every 125M: 62.8 / 54.9, 55.9 / 48.3, 58.3 / 40.6, 53.5 / 48.6, 59.0 / 52.8, 49.7 / 44.8, 56.2 / 43.1,
56.2 / 52.1 (1B). Best at 125M, no better than FP-1. Late drafts: Jynx / Snorlax / Chansey teams, e.g.
Alakazam, Chansey, Jynx, Rhydon, Snorlax, Starmie (39% of the last 2000 logged drafts).

## LeagueV2 (2026-10-25)

Env change (`ocean/pokemon/pokemon_base.h`, `pk_pool_has`; test `test_pool_has`): a native-league bank whose checkpoint
was trained with `env.learner_team_pool` may pilot any team of that pool; other checkpoints still must match their
own learner_team / learner_lead. `build_league_v2.sh` writes `leagues/pokemon/league_v2_omni1024_0000001999896576/`:
13 banks, all OMNI-1024 2B (weights and config.ini copied in), each on one league team with the old names.

FP-4 (`run_freepick_league_v2.sh`, tmux `fp_league_v2` + `freepick_league_v2_eval`): free-pick from OMNI-1024 2B
against LeagueV2, same settings as FP-3 (no forcing, 81.5% self-play, 1B), 88K steps/s. Keeps the standing free-pick
run going and is the live test of LeagueV2 (all 13 banks load, per-team vs_<team>_score logs). Next: short OMNI-1024
sweeps against LeagueV2 (LR with annealing, entropy down to ~0).

## LeagueV2 sweep (2026-10-25, tmux `league_v2_sweep`, dir `../20261025_league_v2_sweep/`)

`./puffer sweep --base.env_name=pokemon` with `20261025_league_v2_sweep/pokemon.ini` installed as config/pokemon.ini
(the box's previous one is config/pokemon.ini.before_league_v2_sweep). Each trial continues OMNI-1024 2B for 100M steps
on the 13 league teams, 25% of games vs LeagueV2, 75% self-play, expert_random 0. 80 trials max; history auto-saves
under the dir's logs/pokemon/.
- Swept: learning_rate 2e-5..1e-3, min_lr_ratio 0.01..1 (LR annealed to that floor), ent_coef 1e-6..1e-2,
  min_ent_coef_ratio 0.01..1 (entropy annealed), magnet_coef 0.01..0.5, replay_ratio 1..4.
- Metric: new env log `vs_league_score` = learner win rate over all LeagueV2 games (env-local, pokemon_base.h). The
  6M smoke run read 0.499, as expected for OMNI against its frozen self.
- Monitor only: Metamon TaurosV0, 144 games on gar_egg_zap at the end of each trial. The in-training Metamon eval
  (metamon_eval.h) now calls evaluate_batched.py.
- Shares the GPU with FP-4: ~38K steps/s each, ~45 min per trial; FP-4 now needs ~7 h to 1B.

## 2026-10-25 evening: FP-4 result, LeagueV2 sweep stopped (broken)

FP-4 (free-pick from OMNI-1024 vs LeagueV2) finished 1B. Scores (Syn / Tauros): 125M 65.3 / 54.2, 250M 53.5 / 57.3,
**375M 62.2 / 63.2** (mean 62.7, best TaurosV0 of any free-pick run), 500M 63.9 / 51.7, 625M 60.1 / 56.6,
750M 62.5 / 59.0, 875M 65.3 / 56.6. Late drafts: Alakazam, Chansey, Lapras, Snorlax, Starmie, Tauros in 99%
(zam_cha_lap_lax_mie_tau; FP-1's team with Snorlax for Rhydon). 375M checkpoint backed up locally.

LeagueV2 sweep STOPPED after 10 trials: config/default.ini's own [sweep.*] sections (hidden_size, num_layers,
total_timesteps, horizon, minibatch, gamma, ...) were inherited, so trials also randomized the architecture
(hidden 32-1024, 1-6 layers) of a warm start from a 1024x2 checkpoint; two trials crashed (weights read, cudaMalloc)
and the rest collapsed. Only trial 0 (the base config) is valid: TaurosV0 59.7 on gar_egg_zap after 100M more steps
(OMNI-1024 2B: 57.3, within noise). Its sweep score equalled its Metamon result (86/144), so the Metamon final
apparently ranked trials instead of vs_league_score. A re-launch must pin every inherited dimension (min = max) and
run with metamon.enabled=0 (score the top trials afterwards with the batched evaluator).
Backup of this box's results: pufferlib/runs/pokemon_box_57608917_backup/ (no weights except FP-4 375M).

## OMNI-2 sweep (2026-10-25 19:22 UTC, tmux `omni2_sweep`, dir `../20261025_omni2_sweep/`)

Fixed re-launch of the LeagueV2 sweep, and the Kirby loop's consume step: each trial continues OMNI-1024 2B for 100M
steps on 19 teams (`pool.ini`: 13 league teams; FP-1 zam_cha_lap_don_mie_tau.s1, FP-2 zam_cha_egg_lap_tau_zap.s1 and
zam_cha_gol_lap_tau_zap.s1, FP-4 zam_cha_lap_lax_mie_tau.s1; Princess Klitty's (Smogon) off-meta counter in two versions,
zam_cha_egg_cloy_don_kang.klitty and zam_cha_egg_cloy_don_jynx.klitty), 25% of games vs LeagueV2. Every inherited
default.ini dimension pinned; Metamon off (score the top trials afterwards, batched). Per-team win rates log as
team_<name>_score. Princess Klitty's exact sets are recorded in pool.ini but not enforced: the catalog lacks the
Chansey, Exeggutor, Cloyster and Jynx sets, so the policy drafts the moves (enforcing them needs a 4-move forcing
env change). Box config/pokemon.ini = this config (previous: config/pokemon.ini.league_v2_sweep_broken).
FP-4 final eval (1B): 61.8 / 53.8.
Restarted 2026-10-25 21:50 UTC with replay_ratio pinned to 1 (never sweep it; the first launch swept 1-4 and was
stopped after 3 trials: 0.554 / 0.580 / 0.504, kept on the box as 20261025_omni2_sweep_replay_swept_stopped/).
~147K steps/s; tmux `omni2_scores` shows the scores table.
Restarted again 2026-10-25 23:51 UTC with a trustworthy score. The previous score (vs_league_score of the final log)
covered only the last partial log interval: trial 5's 0.564 was 57/101 games, +/- 0.1, so all 10 trials (0.47-0.56)
were within noise (kept as 20261025_omni2_sweep_100games_metric_stopped/). New metric `vs_league_late`
(pokemon_base.h): every LeagueV2 game in the last 20% of the trial, counted across all envs (thousands of games).
`omni2_eval.py` (tmux `omni2_eval`) runs every trial that enters the top 3 on all 19 pool teams vs SyntheticRLV2 and
TaurosV0 (144 games per team and opponent, batched) into metamon.txt; the scores view shows it as a column.

## Cross-play matrix, queued after the OMNI-2 sweep (tmux `omni2_crossplay`, `omni2_crossplay.py`)

Model vs model on the 19-team pool: `./puffer match` with env.match_team_pool (both seats draw a pool team per game;
pokemon_base.h, eval/match modes only) and a policy_0_score log; binary built separately as `puffer_match` so the
sweep's binary stayed untouched. Every candidate pair, 4096 games per order, both orders averaged (seat 0 is worth
~0.04: OMNI-1024 2B vs 1B = 0.647 / 1 - 0.431, so 0.608). Ratings by Nash averaging (fictitious play on P - 0.5) plus
the plain mean. Candidates: OMNI-1024 1B/2B, FP-3 125M, FP-4 375M, the sweep's top 3. Output: crossplay.txt.
Note: 2B vs 1B head to head is 0.61 although their Metamon means tie (61.9 vs 61.6).

FP-3's late drafts (last 4000 logs) were Alakazam / Chansey / Exeggutor / Jynx / Lapras / Snorlax in 92%
(zam_cha_egg_jynx_lap_lax), not the Rhydon team reported mid-run; it is not in the OMNI-2 pool.

## Mirrored test vs Metamon (2026-10-26, `mirror_vs_metamon.sh`, evaluate_batched.py --mirror)

Metamon's 28 competitive gen1ou teams with their exact sets on both sides; every ordered pair (we pilot X, Metamon
pilots Y), (X, Y) and (Y, X) share seeds, 2 seeds per pair: 1568 games per model (+/- 2.5 points).

| ours | vs TaurosV0 | vs SyntheticRLV2 |
|---|---|---|
| OMNI-2 trial 11 | 45.8 | 51.5 |
| OMNI-1024 2B | 48.4 | 49.1 |

On equal teams we are roughly even with Metamon. Our usual 65-77% vs Metamon comes largely from our teams and our
own drafted sets against its three panel teams. These 28 teams and forced human sets are new to our policy (it trained
on 19 teams drafting its own moves), while Metamon learned from human ladder play over many teams. Trial 11's gains
over 2B in cross-play do not carry over here. Path to a fair win: OMNI trained on far more teams (e.g. this
competitive set and the Smogon pool) with given sets.

## Each side brings its own teams vs Metamon (2026-10-26, `own_teams.sh`)

Metamon pilots its own choice, the 28 competitive gen1ou teams (32 games each); we pilot ours. 896 games per entry
(+/- 3.2):

| ours | vs TaurosV0 | vs SyntheticRLV2 |
|---|---|---|
| OMNI-2 trial 11 on zam_egg_cloy_don | **65.1** | **62.9** |
| OMNI-2 trial 11 on gar_egg_zap | 61.6 | 61.9 |
| FP-4 375M, drafting its own team | 54.7 | 54.5 |

The full game (team building + piloting) against Metamon's own team choices: we win clearly, 62-65% with OMNI-2 on our
best league teams, and FP-4's self-drafted team wins 55%. With identical teams (mirror test) piloting is about even,
so the edge is team building plus piloting our own teams well. Caveat: zam_egg_cloy_don was picked as our best team
from earlier Metamon evals (gar_egg_zap, the other choice, wins 62% too).
