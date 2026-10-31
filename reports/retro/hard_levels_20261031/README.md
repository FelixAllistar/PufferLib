# Mario hard levels from scratch with key-point starts (2026-10-31)

Felix's design: a fresh policy only on the six levels nothing clears (mazes 4-4, 7-4, 8-4; pits 5-3, 8-1, 8-3).
25% of episodes play the real game from the level start until the first lost life; 75% start at one of 20 key points
(a short window before each hard section, a few safe on-ground states each, every point equally often), where a maze
loop-back counts as a death. Env: `env.archive_practice` (retro.h, retro_archive.h). Starts: `keypoint_states`
(keypoints.txt windows, from fresh_1b rollouts; trace_*.txt are fresh_1b's deaths and loops per level).

## Run 1: 100M steps (run.sh, 186K SPS, ~9 min on the 5060)

Real game from each level start (sweep_eval, 32 tries, eval.txt): 0 clears for both. Progress fresh_1b / new:
4-4 0.54 / 0.17, 7-4 0.35 / 0.16, 8-4 0.19 / 0.15, 5-3 0.31 / 0.14, 8-1 0.48 / 0.18, 8-3 0.26 / 0.24. Expected at
100M from scratch: fresh_1b had 1B steps on all 32 levels and the new policy still loses time on the level openings.

From the key points (keypoint_eval: 8 tries per state, pass = 300 px further or a clear, keypoint_eval_100m.txt):
mean pass rate 0.640 new vs 0.633 fresh_1b, so 100M on the hard levels already matches 1B of general play there.
The new policy is better on 8-3 (0.88 / 0.87 vs 0.55 / 0.29), 8-1's later points and 5-3 x 900. It is worse on 5-3
x 520 and 1100 and 4-4 x 1660. Both get 0 at two points:
- 7-4 before the third check of the first group (x 1060-1200): Mario must be on the top floor at x~1276. Neither
  policy ever reaches it, so there is no state on the correct floor to learn from.
- 8-4 at x 1100-1170: the route continues through a pipe before the x~1276 check (y=0xF0 can never pass).
Metric caveat: at 7-4 x 800-940 "pass" only means 300 px further. A wrong floor at the second check is punished at
the group's end, so that point reads 1.00 regardless.
