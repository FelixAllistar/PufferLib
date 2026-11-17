# Puffer Survivors: walk-away work, 2026-11-16

Probe: `./ocean/puffer_survivors/build/probe CKPT N GREEDY [--env.KEY=VALUE]`. "Fighter" = episode with
4000+ kills; "walker" = under 4000 (low level, walks one way). Greedy unless noted.

## Findings

- Episodes split by build. Poison Oil (Ink) offered in the first few level-ups -> fighter; otherwise
  walker. Forcing one starting level of each weapon (16 eps): ink 14/16 fighters; spikes 9, whirl 8,
  glacier 8, bubble 7, sonar 6, orbit 3.
- Weapon bench, scripted pilot, no levelling, 6000 ticks, kills. Kiting pilot (`circle`): Bubble L1
  alone 110; +1 level of bubble 157, whirl 164, orbit 128, ink 452, sonar 130, glacier 149, spikes 148.
  Holding ground (`circle:12`): alone 373; bubble 414, whirl 612, orbit 533, ink 751, sonar 463,
  glacier 392, spikes 431. Ink is the only second weapon with reach; glacier is weakest everywhere.
- Spawning every enemy ahead of the player barely hurts a walker (67% of game survived vs 77%).
- Late enemy speed growth separates the two: `enemy_speed_late_growth_per_wave=0.06` (after wave 18):
  fighters survive 12/14, walkers 2/18 (current rules: 13/14 and 12/18). Zero-shot on run 2.
- gamma was 0.985: about a 1 second horizon at 60 ticks/s.

## Runs (500M steps each, 128x2, local GTX 1060)

| Run | Change | Greedy: game survived, finished | Mean level | Fighters | Sampled survived |
|---|---|---|---|---|---|
| dashfix2_20261115 | dash fix, ent 0.082 | 77%, 7/16 | 6.6 | ~0 | 24% |
| entropy005_20261116 | ent 0.005 | 91%, 35/48 | 29.9 | 21/48 | 75% |
| gamma997_20261116 | + gamma 0.997 | 95%, 35/48 | 38.4 | 25/48 | 83% (13/24) |
| ramp06_20261116 | + late speed ramp 0.06 (probed under the ramp) | 89%, 31/48 | 39.7 | 23/48 (walkers: 8/25 survive) | 87% (14/24) |

- Scripted pilots with levelling on (first card, 100000 HP): holding ground reaches level 31 by tick 4800
  with or without an Ink start, but takes ~28 damage per 1000 ticks; kiting reaches 14.7 without Ink and
  27.9 with it. Ink is what makes the safe (kiting) style pay; the reward charged 1.45 per hit vs 1 per
  level-up, so without Ink the policy would not go near the swarm.
| hurt04_20261116 | gamma 0.997, hurt -0.4, death -4, no ramp | 96%, 40/48 | 49.0 | 38/48 (21 without early Ink; walkers: 3/10 survive) | 90% (18/24) |
| hurt02_20261116 | hurt -0.2, death -4 | 99%, 46/48 | 57.4 | 43/48 (walkers: 3/5 survive) | 75% (17/24; 7 die near tick 3000) |
| hurt02_entanneal_20261116 | hurt -0.2, death -4, entropy annealed 0.005 -> 0.0005 | 100%, 48/48 | 67.5 | 48/48 | 100% (24/24) |
| final_seed7_20261116 | final config from the ini (adds late ramp 0.06), seed 7 | 100%, 47/48 | 77.1 | 48/48 | 99% (22/24) |

## Outcome

Final config (in `config/puffer_survivors.ini`): gamma 0.997, ent_coef 0.005 annealed to 10%, reward_hurt -0.2,
reward_death -4, enemy_speed_late_growth_per_wave 0.06, dash cooldown 300 with a 120-tick floor and no
level scaling. Two seeds: every greedy episode fights (straightness 0.14-0.21) and all but one survive.

- The run-7 policy under harder rules, zero-shot, greedy, 24 eps: enemy damage x2 -> 20/24 survive;
  enemy_hp_scale 1.3 (x2) -> 22/24; late ramp 0.10 -> 24/24.
- Its upgrade preference: Ink 19% of the first six picks (7% if random); Orbit, Glacier, Area least.
  Damage taken peaks at player level 6-30 (~1.4 HP per 1000 ticks). Dash is used 0.1 times per game.
- Not changed: weapon balance. Ink's edge did not respond to single numbers (pool tick rate 8 -> 24:
  454 -> 371 kills on the kiting bench; trail damage 0.36 -> 0.2: 454 -> 425).

`supervise.sh` / `launch.sh` / `evalrun.sh` here are how the runs were made: one run at a time in tmux
`ps_train`; the supervisor pauses the run when Windows free memory drops under 700 MB and resumes it
exactly once memory is back (it copied paths from a scratch directory; adjust `S` before reuse).
