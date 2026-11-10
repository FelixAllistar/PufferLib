# mario_sim: mazes, pipes and route states for the curriculum teacher (spec, 2026-11-10)

Follow-up to `reports/mario_sim/teacher_20261107/SPEC.md` (read it and its `NOTES.md` first; this spec only adds to
that implementation). Env-local: `ocean/mario_sim` only.

## Why

The first retro teacher run (2026-11-10) showed three gaps that mario_sim has in a worse form:

1. **A wrong maze route is not a failure in mario_sim.** In 4-4, 7-4 and 8-4 the ROM throws Mario back 1,024 px
   when he passes a hidden check on the wrong floor. Retro counts that as a failure; mario_sim only counts deaths
   and timeouts, so the teacher never learns where the maze goes wrong.
2. **Pipes are scored wrongly.** A practice attempt wins on a level exit or +512 px *in its starting area*. Taking
   the correct pipe into a different area can therefore only time out (a recorded loss); taking a wrong pipe that
   sends Mario back is not a failure at all.
3. **The teacher only has groups where the bank has states.** The v2 bank has 4 states in 4-4, 8 in 7-4, none in
   the later rooms of 8-4, and none at all in 19 of the 32 levels. Nothing can be practised past a wall.

Facts measured for this spec by replaying the warpless TAS movie (`/mnt/d/puffer_media/mario_tas/
smb_warpless_aknell.fm2`, 67,117 lines; with `TAS_OFFSET=-2` it runs 67,115 frames in our core, never dies, and ends
in 8-4 with `m[0x770]==2`, `x=4804`, `errors=0`). "Tracked" below is exactly the predicate of
`smb_game_track_progress`; area pointer = `m[0xe7]+256*m[0xe8]`:

- 4-4 is one area (`0xa212`, x 40→2753) and 7-4 is one area (`0xa371`, x 40→3266). The movie never loops in them.
- 8-4 is mostly ONE area pointer `0xa3fc`, entered at increasing x after each correct pipe: tracked x runs are
  40→1282, 1848→2435, 3128→3407, then the water area `0xaec2` (56→1074), then `0xa3fc` again 4152→4802. So a
  correct 8-4 pipe is a forward jump in x inside the same area, and a wrong one is a backward jump.
- Quirk: at the first 8-4 pipe the movie triggers the loop-back itself (x 1282→261 on consecutive tracked frames)
  while entering the pipe. This only matters for the builder (it is on the reference route); do not "fix" it.
- Mario's form on the movie (`m[0x756]*2+m[0x754]`): small in 1-1 to 1-3, big in 1-4, fire from 2-1 on. Real
  arrivals at the hard levels are almost always small.
- Area changes on the movie, per level (same-pointer re-entries not listed): 1-1 `a690→ad7b→a690`,
  1-2 `ac37→a690`, 2-2 `ae47→a690`, 4-2 `acda→a690`, 7-2 `ae47→a690`, 8-4 `a3fc→aec2→a3fc`.
  Your builder must reproduce this list (print it); report any difference.

## Hard constraints

- Touch only `ocean/mario_sim/**`, `config/mario_sim.ini`, and `reports/mario_sim/maze_20261110/NOTES.md` (your
  report). Nothing under `src/`, `ocean/retro/**` (including `curriculum_teacher.h`), `build.sh`, or other envs.
- Never write `./puffer`. Compile checks use an output name:
  `bash build.sh mario_sim build/mario_sim/puffer_mario_sim_maze_check` (compile only; never train locally).
- No git commits, no `git add`, no stash. Other sessions have uncommitted work in this checkout.
- Local box: 4 cores. Use `-j2` and `OMP_NUM_THREADS=2`. Local GPU is a GTX 1060 (`CUDA_ARCH=sm_61`).
- The movie file is third-party: read it from the path above, never copy it into the repository.
- `practice_teacher = 0` behaves exactly as today and every existing test keeps passing.
- CPU and CUDA run the same arithmetic. Device code only reads tables and adds integer counters.
- Do not write a new `build/mario_sim/practice_v2`; the new bank goes to `build/mario_sim/practice_v3`.

## Part A: position tracking (game.h)

Append to `SmbGameProgress` (end of the struct): `int raw_x, maze_loops;`.
In `smb_game_track_progress`, after the tracked-predicate check and before `p->tracking=1;...`:

```
int raw = m[0x6d]*256 + m[0x86];                      // uncapped
if (p->tracking && p->last_key == key && p->raw_x - raw >= SMB_LOOP_DROP) p->maze_loops++;
p->raw_x = raw;
```
with `enum {SMB_LOOP_DROP = 900}` (same value and meaning as retro's `RETRO_LOOP_DROP`: a drop between two
consecutive tracked frames in one area; `tracking` is already cleared by any untracked frame, so respawns and pipes
never count). The capped `x`/`last_x` logic and all rewards stay exactly as they are.

## Part B: events in `smb_after_frame` (mario_sim.h), teacher on only

Capture before the frame call (next to the existing `deaths_before` capture):
`was_tracking = env->game.tracking; k0 = env->game.last_key; x0 = env->game.raw_x; loops_before = env->game.maze_loops;`
Use `x0` (uncapped) instead of the capped `death_x` for the existing death crediting. This removes the known
3,400-px limit; group sections already come from the uncapped `fpt_x`.

New Env member (POD, outside `SmbPracticeProgress`): `int teacher_track_deaths;`, set to 0 at every game and
practice reset. After the frame call compute, for non-FPG actors:

```
k1 = env->game.last_key; x1 = env->game.raw_x;
loop     = env->game.maze_loops > loops_before;
regained = !was_tracking && env->game.tracking && k0 != 0
           && (k0 >> 16) == (k1 >> 16)                       // same stage
           && env->game.deaths == env->teacher_track_deaths;   // no life lost during the gap
forward  = regained && k1 != k0 && smb_route_pair(teacher, k0, k1);
backward = regained && ( (k1 == k0 && x0 - x1 >= 256)
                      || (k1 != k0 && smb_route_known(teacher, k1) && !smb_route_pair(teacher, k0, k1)) );
if (env->game.tracking) env->teacher_track_deaths = env->game.deaths;
```
- `smb_route_pair(t, a, b)`: `(a,b)` is one of `t->pairs` entries `(pair_from[i], pair_to[i])`.
- `smb_route_known(t, k)`: `k` appears as a `pair_from` or `pair_to`. An area the route never visits (bonus rooms
  off the route, vines) is unknown and produces neither outcome. With no route table (`pairs == 0`) only the
  same-area rules can fire.

Outcomes:

- **Practice actor with a pending attempt** (extend the existing block): `win` also if `forward`; `loss` also if
  `loop || backward`. Win has priority if both happen on one frame, as today.
- **Practice actor, `practice->loop_terminal`**: if `loop || backward` and the episode is still active, set
  `env->episode.status = SMB_EPISODE_DEAD` (same place as the existing `ram[0xe]==11||game.deaths` rule). Count it
  in the existing `practice_deaths` log like any other death.
- **Ordinary game actor**: a failure event is `deaths increased` (today) **or** `loop` **or** `backward`. Credit
  with the existing integer kernel at area `k0`, section `x0 / group_width`. Add to counters: `events[0]` all
  failures credited (as today), `events[1]` loop-backs, `events[2]` wrong exits. Several conditions on one frame
  credit once. The episode and its rewards are unchanged for game actors.

`SmbTeacherDevice` gains `int pairs; const uint32_t *pair_from, *pair_to;` and the counter block grows to
`4G + 3`. `SmbPracticeConfig` gains `int loop_terminal = 0;`.

## Part C: route table (host, `smb_setup_teacher`)

New bank flags in `practice.h`: `SMB_BANK_ROUTE = 32` (entry comes from the reference movie),
`SMB_BANK_EDITED = 64` (entry had the small-Mario edit below). Both are below `SMB_PRACTICE_BAND_SHIFT`, so the
legacy band path is unaffected. The loader's entry validation stays as is.

Build the pair table from the practice bank itself: take entries with `SMB_BANK_ROUTE`, sort by `(stage,
source_frame)`; within each stage, whenever two consecutive entries have different area keys `a != b`, add the pair
`(a, b)` once. `area = (stage << 16) | data pointer`, as for groups. Validate `pairs <= 256`. Upload next to the
other teacher tables (CUDA) / point at host memory (CPU). Log the table once at start-up to stderr.

## Part D: bank builder (`build_practice_bank.cpp`)

New option `--route MOVIE.fm2`. Without it the output is byte-identical to today for the same arguments (verify
with `cmp` against a rebuild of v2's command into a scratch directory).

With it:

1. Parse and replay the movie from power-on on a standalone core exactly as `ocean/retro/tas_replay.cpp` does with
   `TAS_OFFSET=-2` (drop the first two movie frames; `emulate_frame(mask, 0)`). After the last frame require:
   world 8, stage 4, `m[0x770] == 2`, no death after frame 0, `error_count() == 0`. Otherwise fail.
2. After each replayed frame `f`: `state = smb_import_logic(core)`. Candidate if the tracked predicate holds and
   `useful(state)` (the builder's existing function). Bin = `(stage, area pointer, x / 64)` with
   `x = fpt_x(ram, 0)`. Keep the first candidate of each bin; skip a bin whose `(stage, area pointer, x / 256)`
   section already holds 4 route states. All 32 stages.
3. For each kept candidate take the snapshot as small Mario: if `ram[0x754] != 1 || ram[0x756] != 0`, save the core
   state, write `low_mem[0x754] = 1; low_mem[0x756] = 0`, import the logic and save the `Nes_State` from that edited
   core, then restore the saved state so the replay continues untouched (this is what `tas_states.cpp` does with
   `TAS_SMALL=1`). Set `SMB_BANK_EDITED` on those entries.
4. Each route entry goes through the builder's existing per-state path unchanged (native reset equals the ROM
   snapshot, 240-frame native-vs-ROM rollout, 16-frame validation clip), with: `flags = SMB_BANK_PRACTICE |
   SMB_BANK_ROUTE [| SMB_BANK_EDITED] | (band << SMB_PRACTICE_BAND_SHIFT)`, `band` = quartile of the entry's
   position among its stage's route entries (0 = earliest), `source_frame = f`, source name `route_movie`.
   A state that fails the reset or rollout check is dropped and counted, not fatal; more than 5% dropped is fatal.
5. Route entries are appended after the existing sources' picks and do not count toward `--per-stage`.
   Do not write `states/N.state` for route entries. Raise the artifact size cap to 64 MiB when `--route` is given.
6. `summary.json`: `synthetic_RAM_edits` = number of edited entries, plus `route_states`, `route_dropped`, and per
   stage `{route_states, areas}`. Print the per-stage area sequence and the pair list.

Build the new bank (about 3 minutes plus the replay):
```
OMP_NUM_THREADS=2 build/mario_sim/fpg/build_practice_bank build/mario_sim/practice_v3 --per-stage 48 \
  --route /mnt/d/puffer_media/mario_tas/smb_warpless_aknell.fm2 config/mario_sim.ini \
  checkpoints/mario_sim/sweep_1792016021935_0000/0000006442450944.bin \
  checkpoints/mario_sim/sweep_1792052933835_0001/0000009999745024.bin
```
Run `verify_cuda` on its validation clip (`sm_61`).

## Config

`[game]`: new key `practice_loop_terminal` (code default 0, range 0..1). In `config/mario_sim.ini` set
`practice_loop_terminal = 1` and `practice_bank = build/mario_sim/practice_v3/bank.bin`, with one comment line each.
Change nothing else in the ini (in particular leave every `[sweep.*]` section alone).

## Logging

Add `teacher_loop_events`, `teacher_wrong_exit_events` (from `events[1]`, `events[2]`), `teacher_route_pairs`, and
`game_maze_loops` (mean `game.maze_loops` over completed ordinary game episodes). Existing metrics and
`sweep_score` unchanged. `teacher_W_L_share` now covers every stage present in the bank.

## Tests (extend `test_teacher_sim.cpp`; CUDA case in `test_runtime.cu`)

1. Tracking: two consecutive tracked frames in one area with x 1300 → 276 give `maze_loops == 1`; the same drop
   with an untracked frame in between gives 0; a 899-px drop gives 0; `raw_x` is uncapped at x = 3600.
2. Pair table from a hand-built bank: route entries of one stage with areas A, A, B, A give pairs `(A,B)` and `(B,A)`;
   entries without the route flag add nothing; a second stage's keys never pair with the first's.
3. Practice attempt: forward pair → one win; backward different-area (known, not a pair) → one loss and, with
   `loop_terminal = 1`, episode status DEAD; with `loop_terminal = 0` the loss is recorded and the episode goes on;
   unknown area → no outcome; same-area gap with x0 − x1 = 300 → loss; same-area gap forward by 600 with
   `teacher_x0 = x0` → win through the existing distance rule; loop → loss.
4. A life lost during the gap (deaths differ from `teacher_track_deaths`) produces no transition outcome.
5. Game actor: a loop credits the groups of area `k0` at section `x0 / width` with the integer kernel, bumps
   `events[0]` and `events[1]`, leaves rewards and episode status untouched; a wrong exit bumps `events[2]`; a
   death at x = 3600 credits section 14 (not 13).
6. v3 bank: every stage 0..31 has route states; groups per stage printed; 4-4, 7-4 and 8-4 have groups in every
   256-px section the movie passes on the ground; the 8-4 pair list is exactly `(a3fc→aec2), (aec2→a3fc)` with the
   stage bits; the legacy loader (`practice_teacher = 0`) still accepts the bank.
7. Real-game check on the CPU path, no policy: load a v3 route state in 8-4 just before the first pipe and replay
   the movie's own inputs from that entry's `source_frame` through the pipe; a pending attempt started there must
   record a win, not a loss. Do the same for one 4-4 state across the first maze check (win by distance). If the
   small-Mario edit makes the movie inputs fail on a state, use an unedited snapshot for this test only and say so.
8. CUDA: the teacher case in `test_runtime.cu` keeps bitwise CPU/CUDA parity with the v3 bank and the new counters.

Run and report: `make -f ocean/mario_sim/fpg.mk -j2 game-test curriculum-test teacher-test`,
`make -f ocean/mario_sim/runtime.mk environment` (`CUDA_ARCH=sm_61`), the trainer compile check, and the `cmp` of
the no-`--route` rebuild. `make -f ocean/mario_sim/fpg.mk test` needs a local-only bank; run it if it works locally.

## README

In `ocean/mario_sim/README.md`, extend the "Curriculum teacher" subsection: loop-backs and wrong exits as failures,
forward area changes as wins, the route table, `practice_loop_terminal`, the v3 bank (counts per stage, how many
entries carry the small-Mario edit). Correct the sentence that says the bank has no RAM edits. Keep the rest.

## Report (`reports/mario_sim/maze_20261110/NOTES.md`)

Files changed; every command run with its result; the per-stage table (route states, groups, areas); the pair
list; dropped states and why; every deviation from this spec with the reason; anything that could not be run.
Do not claim something passes unless you ran it.

## Not in this change

No reward changes for game actors, no new observation features, no trainer (`src/`) changes, no retro changes,
no changes to the teacher's signal weights. The retro counterpart (seeding its archive with route states past the
walls) is a separate later step.

---

# Review round 1 (2026-11-10): corrections to this spec

The implementation matched the spec; these are spec errors and gaps found in review. Same constraints as above.
Rebuild `build/mario_sim/practice_v3` (delete the old directory first) after R2.

## R1. Signed x everywhere (bug: a wrong 4-4 route can record a WIN)

When a loop-back fires with Mario's page below 4, the ROM's `Player_PageLoc -= 4` wraps and x reads 64,512 or more
for one or more tracked frames. Today that frame satisfies `fpt_x >= teacher_x0 + success_distance` (a false win,
recorded before the loop is seen), and the capped reward x becomes 3400, which pins that area's frontier at the cap.

Add to `game.h`:
```
SMB_HD int smb_game_x(const uint8_t* m) { int x = m[0x6d]*256 + m[0x86]; return x >= 0x8000 ? x - 65536 : x; }
```
- `raw_x` and the loop test use `smb_game_x` (so a loop-back is always a drop of exactly 1,024).
- The reward x in `smb_game_track_progress` becomes `clamp(smb_game_x(m), 0, 3400)` (retro already treats the wrapped
  position as 0; this is a deliberate fix of legacy behaviour, say so in the README).
- The attempt win rule uses `smb_game_x(m)` instead of `fpt_x(m,0)`.
- The same-area backward rule uses the signed values. The credit section is `max(0, credit_x) / group_width`
  (see R2 for `credit_x`).
Test: frames x 1000 → wrapped (page 0xFC) for three frames → 5: exactly one loop, no win for a pending attempt with
`teacher_x0 = 600`, credit at section 3, and the area frontier is not raised.

## R2. The first 8-4 loop-back is on the correct route

Confirmed by your probe: the first 8-4 pipe can only be entered after the loop at x 1282 has fired (the pipe is
still on screen at x 276 after the throw-back). So that loop is not a mistake unless it happens twice.

Bank flag `SMB_BANK_ROUTE_LOOP = 128` (still below the band shift). Builder: run a `SmbGameProgress` along the movie
replay; for each movie loop-back (stage S, area key K, pre-loop x `xl`) set the flag on the kept route entry of
stage S and key K with the largest `x <= xl` among entries with `source_frame` before the loop frame; fail if
`xl - x > 512`. Also drop any route candidate whose frame lies between a movie loop and the next untracked frame
(its coordinates are the thrown-back ones); report how many. Print the loops found; expected exactly one, in 8-4.

Host: loop table `loop_key[i] = area key`, `loop_x[i] = fpt_x` of each flagged entry (at most 16), in
`SmbTeacherDevice` as `int loops; const uint32_t* loop_key; const int32_t* loop_x;`.
`smb_route_loop(t, k, x)`: some `i` has `loop_key[i] == k && x >= loop_x[i] && x - loop_x[i] <= 512`.

Env members: `int teacher_route_loops, teacher_loop_land;` (both 0 at reset; `teacher_route_loops = 0` and
`teacher_loop_land = 0` again on every frame with `!was_tracking && env->game.tracking`, whatever the deaths).
In Part B, after computing `loop`:
```
passed = 0;
if (loop && smb_route_loop(teacher, k0, x0)) {
    if (env->teacher_route_loops == 0) { passed = 1; loop = 0; env->teacher_loop_land = x1 + 1; }  // +1 so 0 means "none"
    env->teacher_route_loops++;
}
credit_x = (env->teacher_loop_land && !passed && x0 < env->teacher_loop_land - 1 + 256) ? x0 + 1024 : x0;
```
A passed loop is not a loss, not terminal, not credited, and not in `events[1]`; count it in new `events[3]`
(counter block `4G + 4`, metric `teacher_route_loops_passed`). A second on-route loop before tracking is regained
is an ordinary loop failure, credited at its own `x0` (the `credit_x` rule gives `x0`, since by then
`x0 >= land + 256`). `credit_x` replaces `x0` for all failure crediting, so a death at the pipe right after the
throw-back is credited to the pipe's real section instead of the start of the level.

Tests: (a) the 8-4 "before pipe 1" case of test 7 must now record a WIN (loop passed, pipe, x 1848 by distance) and
`events[3] == 1`, `events[1] == 0`; (b) a second loop at the same place without a gap is a loss / a credited
failure at section 5; (c) a death 20 frames after a passed loop at x 270 credits section 5 (x 1294), and the same
death at x 600 credits section 2; (d) a loop in 4-4 is never "passed".

## R3. Leaving an off-route room is not a wrong exit

`backward`'s different-area branch also requires `smb_route_known(teacher, k0)`. Test: unknown → known area is
neutral.

## R4. CPU/CUDA parity with events

If it fits in about 40 lines of `test_runtime.cu`: one deterministic teacher case with scripted actions (hold Right)
from 4-4 route states until `events[1] > 0` on the CPU, compared bitwise with CUDA including the counters. If the
harness cannot script actions, say so in NOTES and skip.

Re-run the full test list of the spec, the no-`--route` `cmp`, `verify_cuda` on the new bank, and both compile
checks. Update README and NOTES.md (add a "Review round 1" section; keep the earlier text).
