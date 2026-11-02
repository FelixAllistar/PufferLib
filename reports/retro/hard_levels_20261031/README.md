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

## Run 3: 500M more with every route key point (run3.sh, kp_f.archive.gz)

Real game from each level start (32 tries, eval_run3.txt): **7-4 19/32 clears (first ever)**, **5-3 32/32**,
**8-1 32/32**, 8-3 7/32; 4-4 0 (progress 0.57), 8-4 0 (0.73). From the key points (keypoint_eval_run3.txt), mean
pass 0.739 over 32 points. Routes: 7-4 groups 0.83 and 0.79, past them 0.85 and 0.97. 4-4 loop 2 route 0.63, past
0.97. 8-4: both points before the first pipe are still 0 (the policy never goes down it), and the later 8-4 points are
0.06-0.52. 4-4 dies after loop 2 although 97% of its past-loop states get 300 px further.

## TAS route, frameskip 1, behavior cloning (2026-11-01, runs 11-15) - Mario stopped here

- The published warpless SMB1 TAS (TASVideos user file 638909616499952431, 67,117 frames) syncs in our core from
  power-on with a 2-frame offset through all 32 levels (ocean/retro/tas_replay.cpp). tas_states.cpp turns its route
  into practice starts (a start every 8 TAS frames on the ground, one key point per 240 frames; TAS_SMALL=1 saves them
  as small Mario, TAS_KEEP picks key points). tas/tas_44_84(_small).archive.gz: 152 starts, 13 sections.
- Frameskip had been 4 since 2026-09-27 (config/retro.ini); runs 12+ use frameskip 1.
- Run 11 (run 10 + TAS Fire Mario starts, fs4, 100M): 0 clears, sections 0.74.
- Run 12-13 (fresh, fs1, TAS starts; 13 = small-Mario starts, 800M total): plateau 4-4 0.26 / 8-4 0.32 progress from
  the level start, small-Mario sections 0.31-0.35, 0 clears.
- Behavior cloning (bc/, VPT-style without an inverse dynamics model since the TAS has its buttons): tas_bc_data.cpp
  records 41,937 playable frames (all levels) with the TAS buttons; bc_train.py is a PyTorch copy of the CPU policy
  (parity with the C policy 5e-6), 30 epochs from run 13 chunk 7, 4-4/8-4 oversampled 4x: frame agreement with the TAS
  3% -> 90% (validation windows overlap training ones). Clone alone: progress 0.12 / 0.08, small sections 0.40.
- Run 14 (RL from the clone, fs1, small TAS starts, 400M): progress 4-4 0.55 / 8-4 0.60 (about twice run 13), small
  sections 0.69, plateau after the first 100M; 0 clears.
- Run 15 (run 14, 90% of starts at the failing spots: 4-4 Bowser bridge, 8-4 piranha pipe, 8-4 underwater; 200M):
  no change (0.54 / 0.58, 0 clears). Blockers: 4-4 Bowser bridge 0.66-0.69 as small Mario (the TAS kills Bowser
  with fireballs), 8-4 piranha pipe (most attempts die at x ~1280; a few go down and die later), 8-4 underwater
  section (every attempt ends near the water area's start). keypoint_eval's pass test (+300-1000 px in the same level)
  cannot credit a pipe that moves Mario more than 1000 px, so those two sections read 0 even when the pipe is taken.
