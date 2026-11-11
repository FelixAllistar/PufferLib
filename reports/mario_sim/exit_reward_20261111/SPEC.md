# mario_sim: exit reward, gate signal and progress cap (spec, 2026-11-11)

Builds on `reports/mario_sim/maze_20261110/SPEC.md` (including its "Review round 1") as implemented and committed in
`55cbb283a`. Read that spec's Part A/B and R1/R2 first; names below (`regained`, `k0`, `x0`, `k1`, `x1`,
`smb_route_pair`, `teach`) are the ones in `smb_after_frame`.

## Why

First mario_sim teacher run, 7.5B of 10B steps (`reports/mario_sim/maze_20261110/sim_groups.py` on the `.teacher`
sidecars): from practice starts the 4-4 sections are all at 0.74-1.00 and most of 7-4 at 0.94+, but

- 8-4 section 4, right before the first pipe: 0 wins in about 3,800 attempts. The rooms after it are winnable from
  their own starts (room 2 start 0.55, final room 0.44-0.81). The policy never takes the pipe.
- mario_sim pays nothing for a pipe: reward is only rightward progress and level clears. Retro pays
  `novel_area_reward = 1`, and its run did start taking that pipe.
- A section at 0.00 gets about 0.1% of the practice budget, because `4p(1-p)` fades it and it has almost no death
  mass (natural play never gets that far). That is right for a doomed state and wrong for a gate whose next section
  is winnable.
- Rightward progress stops paying at x = 3400 in every area. The final room of 8-4 (x 4152-4802) and the second
  half of 8-1 therefore pay no progress reward at all. The cap was there against coordinate-wrap jackpots, which
  signed x (R1) and the 16-px continuity rule already prevent. Retro dropped its cap for this reason.

## Hard constraints

- Touch only `ocean/mario_sim/**`, `config/mario_sim.ini`, `ocean/retro/curriculum_teacher.h`,
  `ocean/retro/tests/test_teacher.cpp`, and `reports/mario_sim/exit_reward_20261111/NOTES.md`. Nothing else under
  `ocean/retro`, nothing under `src/`.
- The header change is additive: with `weight_gate = 0` and no gate vector every existing result is bit-identical
  and every existing constant in `test_teacher.cpp` still passes unchanged.
- Never write `./puffer` (compile check: `bash build.sh mario_sim build/mario_sim/puffer_mario_sim_exit_check`, and
  `bash build.sh retro build/retro/puffer_retro_gate_check --float` to prove retro still compiles). Never train.
- No `git add`, commit, stash or checkout. `-j2`, `OMP_NUM_THREADS=2`, `CUDA_ARCH=sm_61`.
- The practice bank is unchanged (`build/mario_sim/practice_v3`, do not rebuild it): `.teacher` sidecars written by
  the running job must still load (same bank hash, same `TeacherGroup` layout).
- Code defaults keep today's behaviour; only `config/mario_sim.ini` turns the new things on.

## E1. Progress cap (`game.h`, config)

`SmbGameConfig` gains `int progress_cap = 3400;`. The reward x in `smb_game_track_progress` becomes
`clamp(smb_game_x(m), 0, c->progress_cap)`. Config key `game.progress_cap` (range 256..16384, default 3400).
`checkpoint_distance`'s upper validation bound becomes `progress_cap`. Ini: `progress_cap = 8192`.

## E2. Exit reward (`game.h`, `mario_sim.h`)

`SmbGameProgress` gains `int arrival;` (append). In `smb_game_track_progress`, on every tracked call set
`p->arrival = 0` first; then in the existing-frontier branch, when `!continuous`, set `p->arrival = novel` (the
pixels by which this arrival exceeds the area's old frontier, computed before the frontier is raised, as the code
already does); in the new-area branch (frontier appended) set `p->arrival = -1`.

`SmbGameConfig` gains `float exit_reward = 0;` (config key `game.exit_reward`, range 0..10). In `smb_after_frame`,
inside the `teach && !env->is_fpg` block after `regained` is known:

```
new_ground = env->game.arrival < 0 || env->game.arrival >= 256;
exit_fwd   = regained && new_ground
             && ( (k1 != k0 && smb_route_pair(teacher, k0, k1)) || (k1 == k0 && x1 - x0 >= 256) );
if (exit_fwd) { reward += game->exit_reward; smb_teacher_add(&teacher->events[4], 1); }
```
(`game` is the config pointer in effect for this actor, so practice actors get it too. Counter block `4G + 5`.)
The reward goes through exactly the path the checkpoint reward takes (episode return, logs). Because `arrival`
measures new ground against the episode's own frontier, each pipe pays once per episode: re-taking a pipe after a
wrong exit or a death pays nothing. Nothing else about wins, losses or crediting changes.

Metrics: `teacher_forward_exits` (from `events[4]`) and `game_exit_reward` (mean exit reward per completed ordinary
game episode). Ini: `exit_reward = 0.5`.

Expected on the reference movie (assert in the real-engine test): 8-4 pays exactly 4 times (pipe 1: x 276 → 1848
with the frontier at 1282; pipe 2: 2435 → 3128; pipe 3: `a3fc → aec2`; water exit `aec2 → a3fc` at x 4152, which
needs E1), 1-2, 2-2, 4-2 and 7-2 pay once each, 4-4 and 7-4 pay 0.

## E3. Gate signal (shared header + mario_sim host)

Header (`ocean/retro/curriculum_teacher.h`):
- `TeacherParams` gains `double weight_gate = 0;`. `teacher_validate` requires it finite and `>= 0` (the existing
  "sum of weights > 0" check keeps using the three old weights).
- `teacher_priorities(groups, now, p, const std::vector<double>* gate = nullptr)`. When `gate` is non-null and
  `p.weight_gate > 0`: `gate->size()` must equal `groups.size()` (throw otherwise); take the values of the eligible
  groups, rank them with `teacher_rank_priorities` like the other signals, and add `p.weight_gate * rg[k]` to
  `pr[k]` before the total. Otherwise the function is exactly today's.
- New helper: `teacher_gate(const TeacherGroup& g, const TeacherGroup* next, const TeacherParams& p)`:
  `0` if `g.attempts < lp_min_attempts`, or if `next` is non-null and `next->attempts < lp_min_attempts`; else
  `(1 - pl(g)) * (next ? pl(*next) : 1.0)` with `pl(x) = (x.wins + 1) / (x.attempts + 2)`. A null `next` means the
  goal follows this group.

Test constants (add to `test_teacher.cpp`, tolerance 1e-12): three eligible groups with identical fresh signals
(no attempts, no deaths, equal `last_selected`), weights deaths 0.4, lp 0.2, stale 0.1, gate 0.3, power 1, floor 0.1,
gate vector `[0.9, 0.0, 0.5]` → probabilities
`[0.37295597484276727, 0.30503144654088050, 0.32201257861635220]` (ranks 1, 3, 2; `pr = [1.0, 0.8, 0.85]`, total
2.65). Same call with `gate = nullptr` → `[1/3, 1/3, 1/3]`. `teacher_gate`: g = 0 wins of 98 attempts, next = 49 of
98 → `0.99 * 0.5 = 0.495`; next null → `0.99`; g with 3 attempts → 0.

mario_sim host:
- Route order of groups: for each group take the smallest `source_frame` among its `SMB_BANK_ROUTE` entries (groups
  without any are off-route: gate 0, and they are nobody's `next`). Within one stage, sorted by that frame,
  `next[g]` is the following on-route group; the last one of a stage has no next (the level exit follows).
- At each refresh, before `teacher_priorities`: `gate[g] = teacher_gate(groups[g], next)` for on-route groups, 0
  otherwise; pass it in. Config keys `practice_weight_gate` (default 0, range 0..10). Log once at start-up how many
  groups are on-route.
- Metrics: `teacher_top_gate_stage`, `teacher_top_gate_section`, `teacher_top_gate_value`, `teacher_gate_share` (sum
  of `prob` over groups with `gate >= 0.25`).

Ini: `practice_weight_deaths = 0.4`, `practice_weight_lp = 0.2`, `practice_weight_stale = 0.1`,
`practice_weight_gate = 0.3`.

## Tests

1. `test_teacher.cpp`: the constants above; all existing checks unchanged.
2. `test_teacher_sim.cpp`:
   - `arrival`: first arrival in an area → −1; a continuous step → 0; a gap landing 300 px past the frontier → 300;
     a gap landing behind the frontier → 0.
   - Exit reward: same-area forward gap of 600 px onto new ground pays once; the same jump a second time (frontier
     already there) pays 0; a forward pair into a new area pays; a pair into an area whose frontier is already
     beyond the landing pays 0; a gap after a lost life pays 0; `exit_reward = 0` pays 0 everywhere.
   - Progress cap: with `progress_cap = 8192` progress from x 3400 to 3528 earns one checkpoint; with 3400 it earns 0.
   - Route order: `next[]` on the v3 bank: 8-4 `a3fc` section 4 → section 7 (print the whole 8-4 chain); the last
     7-4 group has no next; a hand-built case with an off-route group.
   - Gate end to end: a hand-set table where 8-4 section 4 has 0/1000 and its next 500/1000 gives that group the
     top gate value `(1001/1002) * (501/1002) = 0.499501…` and, with the ini weights, a probability at least 10x what it gets with
     `practice_weight_gate = 0`.
   - Real engine (movie present): replay the movie through 8-4 with the teacher on and assert exactly 4 exit
     rewards at the places listed in E2; 4-4 and 7-4 give 0.
3. `.teacher` sidecar written before this change (use one from a round-1 test run or write one with the old layout:
   it is the same layout) loads and refreshes with the gate on.
4. CUDA: the existing teacher parity cases stay bitwise equal with the new counter and `exit_reward = 0.5`; the maze
   case's 4-4 events are unchanged.

Run and report: `make -C ocean/retro teacher-test teacher-archive-test`, both compile checks,
`make -f ocean/mario_sim/fpg.mk -j2 game-test curriculum-test teacher-test progress-test`, `fpg.mk test`,
`test_practice`, `runtime.mk environment` + `test_runtime`, and `verify_cuda` is not needed (bank unchanged).

## README and report

README: the three additions, the new keys, that the reward and gate need the teacher on, and that
`game.progress_cap` changes the progress part of evaluation scores (evaluations must compare checkpoints under the
same config). `NOTES.md`: files changed, every command with its result, the 8-4 `next` chain, deviations with
reasons, anything not run.

## Not in this change

No change to the death signal's `4p(1-p)`, the learning-progress rates, retro's environment code, or the bank.
