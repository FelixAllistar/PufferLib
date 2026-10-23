# Next runs after the window / free-pick v2 results (2026-10-23)

Both boxes were synced to local `5.0` first (commit e3c68f02a: src, ocean/pokemon, config, tests,
vendor, scripts, build.sh) and rebuilt with `./build.sh pokemon`; previous sources are in
`/workspace/before_5.0_sync_20261023.tgz` and `puffer.before_5.0_sync` on each box.

| Run | Box | Question | Compare |
|---|---|---|---|
| `league_gen2_gar` (`run_league_gen2.sh`) | window box 57306307 | Does training against the upgraded league (best of B0/B1 per team) beat B1's round-26 league? Only the league changes. 700M from scratch. | B1 gar_egg_zap @700M 61.8 / 58.0 |
| `freepick_warm` (`run_freepick_warm.sh`) | B1 box 56007315 | Free-pick v2 settings started from the B1 gar_egg_zap 700M policy, 1B. Is free-pick's ~40 plateau the from-scratch start, or drafting its own team? | free-pick v1 33.7 / 49.7, v2 33.7 / 33.3; B1 61.8 / 58.0 |

## Results

(pending)
