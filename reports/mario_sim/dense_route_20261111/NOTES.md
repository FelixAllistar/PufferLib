# mario_sim dense route states — implementation notes, 2026-11-11

Implements `SPEC.md` in this directory on top of `55cbb283a` and `f4485bfc1`. It also keeps the uncommitted tree
changes the coordinator listed:
- `exit_fwd` counts as a practice win;
- `practice_success_distance = 1100` in the ini;
- the test pin of 512;
- the README Attempts lines.

Nothing was committed or staged. Nothing under `ocean/retro` or `src/` was touched. `./puffer` was not written (its
timestamp is unchanged, see below). No training was run. `practice_v2` and `practice_v3` were not rewritten.

## Files changed

- `ocean/mario_sim/practice.h`: `SMB_BANK_DENSE = 1<<16`.
- `ocean/mario_sim/build_practice_bank.cpp`:
  - `--dense W-L,...` and `--dense-step N` (1..64, default 4). `--dense` requires `--route`, and `--dense-step`
    requires `--dense`.
  - The small-Mario snapshot moved into `route_snapshot()` and is shared by route and dense candidates.
  - Dense candidates are taken before the `useful()` filter at every tracked frame with `(f-f0)%step==0`. Frames
    after a loop-back are kept.
  - Each candidate gets the same per-state checks. More than 5% dropped is fatal.
  - Dense entries are appended after the route entries, with no state files and source `route_dense`. The cap is
    192 MiB.
  - summary fields, plus a printed per-stage line.
- `ocean/mario_sim/mario_sim.h`:
  - D2 loader: legacy path skips dense entries and masks the band with `&255`; teacher path appends one-entry dense
    groups with the spec key; `reserved=1`.
  - D3: `smb_route_gate()` (candidates / best / base / fade), with the regular chain over regular groups only and a
    dense chain per stage.
  - D4: `smb_teacher_probabilities()`.
  - D5: config keys, start-up log line per stage, dense metrics.
- `config/mario_sim.ini`:
  - v4 bank;
  - `practice_dense_share = 0.3`;
  - `practice_gate_lookahead = 3`;
  - `practice_gate_patience = 2000`;
  - one comment line each.
- `ocean/mario_sim/test_teacher_sim.cpp`:
  - `fpt_teacher_ini` also pins `practice_gate_lookahead = 1`, `practice_gate_patience = 0` and
    `practice_dense_share = 0`, so every older case runs as before on v3.
  - New `dense_route_tests()` (spec tests 1-6).
- `ocean/mario_sim/test_runtime.cu`: the maze case's 4-4 placement list (`route44`) excludes dense entries, so its
  4-4 events are those of v3's route states.
- `ocean/mario_sim/README.md`:
  - new bullet "Dense route states (v4 bank, training config)";
  - the v3 bullet no longer says "training config".

## Bank v4

The build command is the spec's, run in 1m51s:

```
OMP_NUM_THREADS=2 build/mario_sim/fpg/build_practice_bank build/mario_sim/practice_v4 --per-stage 48 \
  --route /mnt/d/puffer_media/mario_tas/smb_warpless_aknell.fm2 --dense 4-4,7-4,8-4 --dense-step 4 \
  config/mario_sim.ini checkpoints/mario_sim/sweep_1792016021935_0000/0000006442450944.bin \
  checkpoints/mario_sim/sweep_1792052933835_0001/0000009999745024.bin
```

- 2,579 states:
  - 1,478 v3 entries, an unchanged prefix (memcmp-equal in test 1);
  - 1,101 dense entries, 0 dropped, all 1,101 snapshotted as small Mario.
- 102,689,175 bytes.
- Route states 1,170; route loops and pairs unchanged.

| stage | dense states | first frame | last frame |
|---|---|---|---|
| 4-4 | 283 | 31040 | 32168 |
| 7-4 | 328 | 55141 | 56449 |
| 8-4 | 490 | 64469 | 67109 |

- The spec estimated about 420 and 468 for 4-4 and 7-4. The real counts are lower because only tracked frames are
  candidates.
- Spacing is 4 frames, except across untracked stretches: pipe and maze transitions. Within a stage every gap is a
  multiple of 4 (checked in test 1).

**No-dense reproduction.** The same command without `--dense`/`--dense-step`, written to a scratch directory, is
byte-identical to `practice_v3`. `cmp` was run on `bank.bin`, `summary.json`, `states.jsonl`, `validation.bin`,
`routes.jsonl` and every file under `states/` and `routes/`: all identical.

**verify_cuda on v4.**
- Command: `build/mario_sim/runtime/verify_cuda build/mario_sim/practice_v4/validation.bin`.
- Result: exit 0 in 2.6 s, 2,579 cases, 41,264 frames, 0 failures, bitwise observations true.

## Commands and results

All commands were run with `OMP_NUM_THREADS=2 CUDA_ARCH=sm_61 CUDA_HOME=/usr/local/cuda` from one scratch script.
Each command's own exit code is shown.

| command | result |
|---|---|
| `make -f ocean/mario_sim/fpg.mk -j2 game-test curriculum-test teacher-test progress-test` | exit 0, 98 s. `test_teacher_sim` runs twice: PASS "teacher groups, reset sampling, attempt outcomes, ...". Progress test PASS. Curriculum CUDA: `failures 0`, bitwise native/curriculum/observations true, `checkpoint_resume` true |
| `make -f ocean/mario_sim/fpg.mk -j2 test` | exit 0, 46 s. `test_cuda`: `failures 0`, bitwise native states and observations, direct and graph |
| `make -f ocean/mario_sim/fpg.mk build/mario_sim/fpg/test_practice`, then `build/mario_sim/fpg/test_practice` | exit 0 / exit 0. PASS "practice pools, no reset rewards, metric isolation, curriculum retention, sidecars and ordinary evaluation" |
| `make -f ocean/mario_sim/runtime.mk -j2 environment` | exit 0, 22 s |
| `build/mario_sim/runtime/test_runtime` | exit 0, 153 s. All modes `parity=passed`. Teacher case on v4: 1,501 groups, share 0.3, lookahead 3, patience 2000, `route_pairs=6`, `parity_failures=0`. Maze case: 41 4-4 route states (dense excluded), `parity_failures=13 parity_loops=10`, unchanged from v3. Summary `"failures":0` |
| `build/mario_sim/runtime/verify_cuda build/mario_sim/practice_v4/validation.bin` | exit 0, 2,579 cases, 0 failures |
| `bash build.sh mario_sim build/mario_sim/puffer_mario_sim_dense_check` | exit 0, 28 s, "Built: ./build/mario_sim/puffer_mario_sim_dense_check". The only warnings are in `src/pufferl.cu` (untouched) |
| `make -C ocean/retro teacher-test` | exit 0. PASS "curriculum teacher: EMAs, ... gate". The retro tree is untouched |
| `./puffer` timestamp before/after | identical: 2026-11-04 21:45:47 |

After the script, I added a report-only block at success distance 1100 (see test 6). I then rebuilt and reran
`make -f ocean/mario_sim/fpg.mk -j2 teacher-test`: exit 0, PASS, 16 s.

**Dense test output** (`test_teacher_sim`, `dense_route_tests()`):
- **Test 1, bank:** v4 is v3's 1,478 entries plus 1,101 dense entries, all edited. Per stage: 4-4 283 (31040..32168),
  7-4 328 (55141..56449), 8-4 490 (64469..67109). Flags, stage, tracking, ascending 4-multiples and no loop flag were
  all checked.
- **Test 2, groups:**
  - v4 at share 0 equals v3: 400 groups, same keys, area, level, section, eligible, reserved, offsets, start-up
    probabilities and CDF. No dense entry was loaded.
  - The legacy loader on v4 takes exactly v3's 1,478 entries.
  - At share 0.3: 400 regular groups identical to v3 plus 1,101 one-entry dense groups, with key, area, section and
    order as specified. The dense chain is per stage. No regular `next` points at a dense group. Start-up dense mass
    is 0.300000.
- **Test 3, gate:**
  - Lookahead 3 picks the best of the next three and ignores an under-min candidate: 0.5625.
  - All candidates under the minimum give 0.
  - End of chain uses factor 1: 0.8333.
  - Lookahead 1 is the plain next group.
  - Fade at 0 wins / 2000 attempts with patience 2000: 0.749625 -> 0.374813. One win restores the full value.
  - Regular groups equal `teacher_gate` with lookahead 1 and 3. An off-route group gives 0.
- **Test 4, probabilities:**
  - Sum is 1.000000000000000 and the dense sum is 0.300000000000000.
  - The regular part is 0.7 x the single call on the regular groups (to 1e-15).
  - Dense gate 0.9 gives 0.1166 against 0.0583 for gate 0.
- **Test 5, sidecars:** see below.
- **Test 6:** see below.

## Test 6 findings (real engine, `practice_success_distance` pinned to 512 by the test ini)

Dense frames are `f0 + 4k`.
- In 8-4, `f0 = 64469`. The frames near the first pipe entry (movie frame 64993) are 64985 and 64989. The spec's
  64991/64992 are not on the grid; see Deviations.
- In 4-4, `f0 = 31040`. 31668, 31672 and 31676 are on the grid.

All cases start from the bank's edited (small-Mario) dense state. The unedited fallback was not needed in either
required case.

| case | start | inputs | outcome |
|---|---|---|---|
| required | 8-4 frame 64989 | movie | WIN after 174 frames, 1 forward exit (`events[4]` +1), reward 0.500, pipe entered (`$000E = 3`), ends at `a3fc` x 1848 |
| required | 4-4 frame 31668 | movie | WIN by distance after 214 frames, 0 exits, ends at `a212` x 2030 |
| report only | 8-4 frame 64989 | Down 30 frames, then nothing | WIN after 174 frames, 1 exit, reward 0.500 |
| report only | 8-4 frame 64985 | Down 30 frames, then nothing | WIN after 178 frames, 1 exit, reward 0.500 |
| report only | 4-4 frame 31660 | Right+B held | LOSS: timeout at 2400 frames, x 1666 |
| report only | 4-4 frame 31664 | Right+B held | LOSS after 175 frames, x 1923 |
| report only | 4-4 frame 31668 | Right+B held | WIN after 215 frames, x 2032 |
| report only | 4-4 frame 31672 | Right+B held | WIN after 211 frames, x 2032 |
| report only | 4-4 frame 31676 | Right+B held | WIN after 209 frames, x 2037 |
| report only | 4-4 frame 31680 | Right+B held | WIN after 208 frames, x 2044 |

**Answer: yes, the small-Mario dense starts win with trivial inputs.**
- **8-4:** the two dense states before pipe 1 (64985, 64989) win by holding Down. A policy that presses Down there
  wins, and the gate then has a winnable successor for earlier dense frames.
- **4-4:** holding Right+B wins from frame 31668 onward. 31668 is 12 frames before the movie reaches the floor at
  31680, while it is falling off the platform's right edge.
  - The two earlier frames lose. From 31664 the attempt ends after 175 frames at x 1923, consistent with the
    middle-corridor failure in the spec. From 31660 it times out at x 1666.
  - So the 4-4 chain has a winnable tail with a hard boundary between 31664 and 31668.
- **At the training config's `practice_success_distance = 1100` (report only, rerun added):**
  - **8-4:** unchanged. These attempts win by forward exit, which does not depend on the distance.
  - **4-4 with movie inputs from 31668:** still WIN, after 449 frames, at x 2618.
  - **4-4 holding Right+B: LOSS from every frame 31660..31680.**
    - 31668..31680 time out at 2400 frames, stuck at x 2418.
    - 31664 dies or fails after 175 frames at x 1923.
    - 31660 times out at x 1666.
  - So at 1100 the 4-4 dense starts are not trivially winnable. Running right gets past the platform drop but not
    the next obstacle near x 2418, and the 4-4 chain has no trivially winnable tail.
  - The 8-4 pipe starts are trivially winnable at both distances.

## v3 sidecar against the v4 bank

The fixture is `build/mario_sim/fpg/teacher_pre_exit.bin.teacher`, written with the v3 bank.
- `smb_read_teacher(path, v4 hash)` throws "invalid or mismatched practice teacher checkpoint". The checkpoint
  load hook `fpt_cpu_load` throws the same message.
- With `practice_resume = 0` the load skips the sidecar and the run starts with fresh group statistics.
- The bank-hash check was not weakened. A run that resumes v3 weights with the v4 bank therefore needs
  `practice_resume = 0`, and its regular groups start from fresh statistics.

A v4 sidecar written with share 0.3 (1,501 groups) round-trips exactly: memcmp of the stored groups, then restore
by key into zeroed statistics.

## Deviations and choices

1. **8-4 test frame 64989 instead of 64991/64992.** The dense grid in 8-4 is `64469 + 4k`, so 64991 and 64992 are
   not dense states. 64989 is the nearest, 4 frames before the pipe entry at 64993. 64985 is also exercised (report
   only).
2. **"One exit reward within 120 frames" relaxed to 200 frames.** The pipe transition from 64993 to 65163 is
   170 frames of untracked movie, so the forward exit resolves 174 frames after 64989. The test asserts at most
   200 frames and exactly one forward exit.
3. **Dense counts below the spec's estimates** for 4-4 (283 vs about 420) and 7-4 (328 vs about 468). These are
   real counts over tracked frames only; 8-4 matches (490).
4. **`dense_edited` added to summary.json**, next to the specified fields. It counts dense states snapshotted as
   small Mario: 1,101 of 1,101.
5. **`teacher_dense_W_L_solved`** counts groups with `p = (wins+1)/(attempts+2) >= 0.5`. An unattempted group has
   p = 0.5, so it counts as solved until it loses. This follows the spec's definition literally; a stricter
   version would need `attempts >= lp_min_attempts`.
6. **Dense metrics:**
   - `teacher_dense_top_*` describe the dense group with the highest sampling probability.
   - `top_frame` is movie frames after that stage's first dense state.
   - `top_gate` is that group's gate value.
   - The existing `teacher_top_gate_*` and `teacher_gate_share` metrics run over all groups, dense included.
7. **Test ini pins.** `fpt_teacher_ini` pins lookahead 1, patience 0 and share 0 next to the existing pins, so the
   older test cases stay on v3 semantics while the training ini uses 3/2000/0.3. `test_runtime`'s teacher cases use
   `config/mario_sim.ini` as before, so they now run with v4, share 0.3, lookahead 3 and patience 2000.
8. **Fade on regular groups.** The spec's gate formula applies `fade` to every group, so with patience 2000 a
   regular group that has never won also fades. This is implemented as written.
9. **Route pairs** use all route entries, dense included, per the spec. With v4 at share 0.3 the pair table is
   still the same 6 pairs, and on-route loops are still 1.

## Not run

- No training and no evaluation of any policy.
- The full fpg/runtime suite ran before the report-only 1100 block was added. Only `teacher-test` was rebuilt and
  rerun after it. The block changes no assertion and only prints.

---

# Review round 1 (2026-11-11): scheduling corrections S1-S3

Builds on round 0 as committed (`b2c6b26a6`). `practice_v4` was not rebuilt; `verify_cuda` was rerun on it unchanged.
Nothing was committed or staged. No training was run. `./puffer` is unchanged.

## Files changed

- `ocean/mario_sim/mario_sim.h`:
  - **S1.** New config key `practice_dense_gap` (integer 0..100000, default 0).
    - `dense_next[g]` links to the next dense group of the stage only if the gap is 0 or the frame difference is at
      most the gap.
    - New `smb_dense_segments()` fills `dense_segment[g]` (-1 for regular groups) and `segments`. A dense group starts
      a segment unless the group before it links to it.
    - The start-up log prints the segments: stage, first..last frame, group count.
  - **S2.** New config key `practice_dense_rank_power` (0.05..16, default 1).
    - `smb_teacher_probabilities` ranks each segment on its own. It uses the dense parameter copy with
      `rank_power = practice_dense_rank_power`, so `P_dense = P_k / K`.
    - Step 1 (`P_reg`) and step 3 (`prob = (1-s) P_reg + s P_dense`, everything eligible again) are unchanged.
    - It throws if segments were never built while dense groups exist.
  - `smb_route_gate` is unchanged. The lookahead walk already stops at `dense_next = -1`, and with no candidates the
    group uses `teacher_gate(g, nullptr)`.
- `config/mario_sim.ini` (S3), with one comment line each:
  - `practice_dense_share = 0.15`;
  - `practice_dense_gap = 32` (new);
  - `practice_dense_rank_power = 0.5` (new);
  - `practice_gate_patience = 5000`.
- `ocean/mario_sim/test_teacher_sim.cpp`:
  - `fpt_teacher_ini` also pins `practice_dense_gap = 0` and `practice_dense_rank_power = 1`.
  - The round-0 hand-built probability case now declares its four dense groups as one chain, so it is one segment.
  - New `dense_segment_tests()` covers review tests 1-3.
- `ocean/mario_sim/README.md`: the dense paragraph now covers gap, segments, the per-segment budget, rank power, the
  new training values and the segment list.

## Segments at gap 32 (v4 bank)

| segment | stage | first frame | last frame | groups |
|---|---|---|---|---|
| 0 | 4-4 | 31040 | 32168 | 283 |
| 1 | 7-4 | 55141 | 56449 | 328 |
| 2 | 8-4 | 64469 | 64989 | 131 (room 1, includes the on-route loop-back at 64987) |
| 3 | 8-4 | 65165 | 65429 | 67 (room 2) |
| 4 | 8-4 | 65601 | 65793 | 49 (room 3) |
| 5 | 8-4 | 65965 | 66657 | 174 (water room) |
| 6 | 8-4 | 66837 | 67109 | 69 (final room) |

**Gaps between consecutive dense states.**
- Gaps over 4 frames exist only in 8-4, all four at room transitions:
  - 64989 -> 65165 (176 frames);
  - 65429 -> 65601 (172);
  - 65793 -> 65965 (172);
  - 66657 -> 66837 (180).
- In 4-4 and 7-4 every step between consecutive dense states is exactly 4 frames. Neither has an untracked gap longer
  than 32 frames, or even longer than 4, between its first and last dense state, so each is a single chain from start
  to end.
- The last dense group before each 8-4 transition (64989, 65429, 65793, 66657) has `dense_next = -1`.
- With gap 0 the chains equal round 0's (consecutive groups of a stage), giving 3 segments, one per stage.

## Commands and results

From `scratchpad/dense/runall_r1.sh`, with OMP_NUM_THREADS=2, CUDA_ARCH=sm_61, -j2. Another session's test run was
using the CPU at the same time, so wall times are about 2-7x round 0's.

| command | result |
|---|---|
| `make -f ocean/mario_sim/fpg.mk -j2 game-test curriculum-test teacher-test progress-test` | exit 0, 691 s. Both `test_teacher_sim` runs PASS, including the new segment tests. Progress PASS ×2 |
| `make -f ocean/mario_sim/fpg.mk -j2 test` | exit 0, 163 s |
| `test_practice` (build, run) | exit 0 / exit 0, PASS |
| `make -f ocean/mario_sim/runtime.mk -j2 environment` | exit 0, 104 s |
| `build/mario_sim/runtime/test_runtime` | exit 0, 259 s, `"failures":0` (details below) |
| `verify_cuda build/mario_sim/practice_v4/validation.bin` | exit 0, 2579/2579 cases, 41,264 frames, 0 failures, bitwise observations |
| `bash build.sh mario_sim build/mario_sim/puffer_mario_sim_dense_check` | exit 0, 112 s, Built |
| `make -C ocean/retro teacher-test` | exit 0, PASS (untouched) |
| `./puffer` timestamp | unchanged |

`test_runtime` details:
- It reads the new ini: share 0.15, lookahead 3, patience 5000, gap 32, rank power 0.5, 7 segments logged at
  start-up.
- Teacher case: 1,501 groups, `parity_failures=0`, `route_pairs=6`, parity passed.
- Maze case: 41 4-4 route states, `parity_failures=13 parity_loops=10`, unchanged.
- This is review test 4: teacher parity stays bitwise with the new values.

## Test output (`dense_segment_tests`)

1. **Gap.**
   - At gap 0: every dense chain link is round 0's and there are 3 segments.
   - At gap 32: the 7 segments above. Every link holds iff same stage and step ≤ 32. 8-4 has 5 segments. The last
     group before each of the four 8-4 transitions ends its chain.
2. **Gate before pipe 1** (gap 32, lookahead 3, patience 5000).
   - Frame 64989 with 0 wins / 100 attempts: gate 0.970780469 = `teacher_gate(g, nullptr) * 5000/5100` =
     (1-1/102)·5000/5100. It is not 0.
   - Frame 64985 (0/10), whose only candidate is 64989: `teacher_gate(64985, &64989) * 5000/5010` = 0.008968990.
   - Frame 64981: candidates 64985 (p 1/12) and 64989 (p 1/102). The lookahead picks 64985, giving 0.076236416.
   - On the real bank at start-up: dense mass 0.15, each of the 7 segments 0.15/7 = 0.021428571429 (to 1e-12).
3. **Budget** (hand-set: 3 regular groups; dense segments of 3, 2 and 3 groups; s = 0.15; rank power 0.5).
   - Sum 1.000000000000000, dense 0.150000000000000, each segment 0.050000000000000.
   - In the two-group segment, learning progress and staleness are tied and the gates are 0.9 and 0.5. The top-two
     gate-only priority ratio, recovered from the probabilities, is 1.414213562373095 = sqrt(2).
   - With one segment and rank power 1 the probabilities equal round 0's mixture bit for bit.
4. **CUDA.** See `test_runtime` above.

## Deviations and notes

1. **Gap 0 with share > 0 is not round 0's distribution.**
   - S1 says gap 0 gives one segment per dense stage, and S2 always splits the budget per segment. So with gap 0 and
     share > 0, each of the 3 stages gets s/3, where round 0 ranked all dense groups together.
   - The code default (`practice_dense_share = 0`) keeps round 0's behaviour exactly, since no dense groups exist.
   - Round 0 (single dense ranking) is reproduced only when there is one segment: rank power 1 and a single dense
     stage, or a chain without gaps. This is test 3's last case.
   - I implemented the spec as written. If gap 0 must reproduce round 0 exactly with share > 0, `P_dense` would need
     a special case at gap 0. I did not add one.
2. **How the sqrt(2) check reads the priorities.** The probabilities only expose the mixture. The test ties lp and
   staleness at rank 1 and recovers the gate rank from the known dense weights (0.3 non-gate, 0.7 gate) and the
   floor 0.05: `r2 = (y2/y1 - 0.3)/0.7`, where `y = (prob·K/s - floor/n)/(1-floor)`.
3. **Earlier tests unchanged.** The round-0 dense tests still use gap 0 and rank power 1 through the shared test ini.
   Their checks (dense mass s, one-entry groups, chain = consecutive groups) still hold.
4. **No new metrics.** None were asked for. The segments are logged once at start-up.

## Not run

- No training and no evaluation.
- `practice_v4` was not rebuilt.
