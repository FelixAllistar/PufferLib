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
