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

## Run 2: continued to 500M (run2.sh)

Real game from each level start (32 tries, eval_run2.txt): **5-3 27/32 clears, 8-1 26/32, 8-3 2/32**: the first
clears of these levels by any of our policies. Mazes still 0 (4-4 progress 0.50, 7-4 0.36, 8-4 0.54). From the key
points: mean pass 0.816 (0.640 at 100M), and every pit point is 0.62-1.00. The two maze walls stay at 0 (7-4 third check,
8-4 x 1100), as expected: neither policy had ever been past them.

## Route search past the maze walls (keypoint_search)

Random exploration on the emulator from a key point's states, Go-Explore style (cells by area, x/8, y/16; first
state per cell), until Mario passes the wall without a loop-back or lost life. Every success was found within a few
hundred rollouts (seconds to minutes):
- 7-4 first group: from x 1060-1200 (key point 6), past x 1340. Key points 20 (route) and 21 (past).
- 7-4 second group: from 21, past x 2364. Key points 22 and 23.
- 4-4 second loop: from just after loop 1 (key point 1; from x 1660 Mario is already stuck on the upper floor),
  past x 2110. Key points 24 and 25.
- 8-4 first pipe (right after the lava lift, Felix's pointer): from key point 9, the pipe exits at x ~1850. Key
  points 26 and 27. At first the search could not get through: it counted the pipe transition as a fall (Mario's
  height byte), so every pipe entry looked like a death. It now judges deaths on playable frames only.
- 8-4 floating pipe (hidden block): from 27 to x 3128 (28, 29). 8-4 water pipe: from 29 into the water area (30, 31).
kp_f.archive.gz holds all 367 states over 32 key points. archive_frames dumps a key point's saved screens for
checking by eye.
