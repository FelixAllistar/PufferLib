# Retro curriculum teacher: implementation spec (2026-11-07)

Env-local change to `ocean/retro` only. It replaces the Go-Explore cell weighting of the reset archive with a
bandit teacher over *groups* of reset states, driven by where natural play dies, by learning progress and by
staleness, and it stores several snapshot variants per cell. The math lives in `ocean/retro/curriculum_teacher.h`
(already written and tested by `tests/test_teacher.cpp`; see `expected.py` here for the reference constants).
**Do not modify `curriculum_teacher.h` or `tests/test_teacher.cpp`.** If you believe something is missing there,
write it env-side in `retro_archive.h` and say so in your report.

## Hard constraints

- Touch only: `ocean/retro/retro_archive.h`, `ocean/retro/retro.h`, `ocean/retro/training_config.h`,
  `ocean/retro/tests/test_archive.cpp` (plus a new `ocean/retro/tests/test_archive_teacher.cpp`), `ocean/retro/Makefile`,
  `ocean/retro/README.md`, `ocean/retro/keypoint_states.cpp` (only if its snapshot helper is unified), `config/retro.ini`,
  and `reports/retro/teacher_20261107/NOTES.md` (your report). Nothing under `src/`, no other env, no `build.sh`.
- Never write `./puffer`. A trainer compile check uses an output name:
  `bash build.sh retro build/retro/puffer_retro_teacher_check --float` (compile only; never run training locally).
- No git commits, no `git add`. Other sessions have uncommitted work in this tree; leave it alone.
- With `archive_teacher = 0` (default) behaviour must be byte-for-byte what it is today except for the one
  intentional change below (cells hold a vector of states). Existing tests must keep passing.
- Performance: nothing in the per-decision path may take the bank mutex except when an outcome is recorded,
  a capture/deposit happens, or a reset samples a start. Priority refresh happens at most once per 2048 global steps.

## Data model

```
struct RetroArchiveCell {
    uint64_t key, visits, selected, failures, successes, last; int level; float depth;
    int group=-1;                                            // index into bank.groups
    std::vector<std::shared_ptr<RetroArchiveSnapshot>> states;  // replaces `state`; 1..archive_variants entries
};
```
`RetroArchiveBank` gains: `std::vector<TeacherGroup> groups; std::unordered_map<uint64_t,size_t> group_index;
std::vector<float> cdf; uint64_t cdf_steps; size_t snapshots;` (total stored snapshots), `TeacherParams teacher;
bool teacher_enabled; int group_width, variants, success_distance, attempt_decisions, chain_interval, chain_depth,
chain_skip; bool death_chain; double full_game_share;` and counters for logging (`death_events, chain_deposits,
attempt_wins, attempt_losses`). `b.capacity` becomes the maximum number of **snapshots**:
`archive_capacity_mb * 2^20 / (sizeof(RetroArchiveSnapshot) + 64)`.

`RetroArchiveEpisode` gains the attempt record `bool attempt_pending; int attempt_group, attempt_decisions, attempt_x0;
unsigned attempt_area;` and the death chain ring: `struct RetroChainEntry { std::shared_ptr<RetroArchiveSnapshot> state;
uint64_t key; int level, x, decision; unsigned area; }; std::vector<RetroChainEntry> chain; int chain_next, chain_count;`.

### Group identity
- `area` = `retro_area_key(m)` at capture time (32 bits: level<<24 | $0760<<16 | data). For snapshots loaded from a
  file rebuild it as `((unsigned)level<<24) | ((unsigned)(st.area-1)<<16) | st.data` (`robs_area` returns $0760+1;
  add a test that the rebuilt value equals `retro_area_key` on a live capture).
- `section` = `retro_progress_x(st.x) / group_width`.
- `key` = `((uint64_t)area << 16) | (section & 0xffff)`; `level` = `area >> 24`.
- A group is created when its first cell is inserted and never deleted; `eligible` is recomputed before each priority
  refresh as: the group has at least one cell with at least one state, and its level is in the env's spawn levels
  (same `spawn_level[]` logic the practice branch of `retro_archive_choose` uses today).

### Snapshot helper
Factor the snapshot construction used by `retro_archive_capture` (and duplicated in `keypoint_states.cpp`) into
`static std::shared_ptr<RetroArchiveSnapshot> retro_archive_snapshot(Env* e)`. Keep `keypoint_states.cpp` compiling.

## Insertion, variants and eviction

`static void retro_archive_insert(Env* e, std::shared_ptr<RetroArchiveSnapshot> s, uint64_t key, int level, unsigned area)`
(bank mutex held by the caller):
1. Look the cell up by key. New cell: needs room for one snapshot (evict first if `snapshots == capacity`), then create
   it, set `group` (create the group if needed), push the state.
2. Existing cell: `visits++`. If `states.size() < variants` push (evicting elsewhere first if at capacity). Otherwise
   replace a uniformly random slot with probability `max(variants / visits, 1/32)` (reservoir sampling with a floor so
   the bank keeps refreshing), else drop the snapshot.
3. Eviction rule (never evicts from the cell being inserted into):
   a. among cells with more than one state, the cell whose group has the smallest `prob` (ties: more states, then
      random): drop one random state;
   b. else, among groups with more than one cell, the group with the smallest `prob`: remove one random cell;
   c. else remove the only cell of the smallest-`prob` group and set that group `eligible = 0` (stats stay).
   Cell removal is swap-remove on `cells`; fix `index` for the moved cell. Active resets hold `shared_ptr`s, so
   eviction never invalidates a running episode (the existing test checks this).
4. Keep `snapshots` exact.

`retro_archive_capture` keeps its trigger (every `interval` decisions, not in practice mode, not when done) and its
state filter, then calls the snapshot helper and `retro_archive_insert`. Keep the legacy per-cell feedback assignment
after a capture (it still drives `difficulty` when the teacher is off).

## Death chain (modes 0 and 1 only, `archive_death_chain = 1`)
Every `chain_interval` decisions, when the capture filter passes, store a snapshot plus key/level/area/x/decision in
the ring (overwrite the oldest; `chain_depth` entries). On a failure event (life loss, or `maze_loops > 0`) deposit
every entry with `decision <= current_decision - chain_skip` through `retro_archive_insert`, count `chain_deposits`,
and clear the ring. Clear the ring on every reset. Default memory: `chain_depth=4` snapshots per env (~90 KB each);
document the cost in the README.

## Failure crediting (modes 0 and 1 only)
`retro_archive_step` gains parameters `int maze_loops, int old_x`. On `death || maze_loops > 0`:
- area = `e->last_area_key` (skip if `0xffffffff`);
- x = the `last_x` of the frontier whose key is `((area>>24)<<16) | (area & 0xffff)` if present, else `old_x`;
- section = `retro_progress_x(x) / group_width`;
- under the mutex: `teacher_credit_failure(b.groups, area, section, b.steps, b.teacher)`; `death_events++`.
Mode 2 episodes never credit failures (their deaths are the recorded attempt outcome).

## Attempt outcome (mode 2 only)
At selection: `attempt_pending = true; attempt_group = cell.group; attempt_x0 = retro_progress_x(st.x);
attempt_area = area of the snapshot; attempt_decisions = 0; group.last_selected = b.steps`.
Each decision while pending (no lock): `attempt_decisions++`;
- win if `progress` (the existing `saw_clear || novel_areas > 0` argument) or
  (`e->last_area_key == attempt_area` and `retro_progress_x(e->x_pos) >= attempt_x0 + success_distance`);
- loss if `death || maze_loops > 0`, or `attempt_decisions >= attempt_decisions_cap`, or `done` without a win.
Record exactly once: lock, `teacher_record(group, 1, win, b.teacher)`, bump `attempt_wins/attempt_losses`, unlock,
`attempt_pending = false`. The episode itself continues exactly as today (natural lives); only bookkeeping changes.

## Reset selection
Shares: `archived = start + (end-start)*phase` as today. With the teacher on, `full = full_game_share` (fixed) and
level starts get `1 - archived - full`; validate `archive_start + full_game_share <= 1` and
`archive_end + full_game_share <= 1` at init. Lane logic unchanged.

Mode 2 with the teacher (mutex held): if `cdf` is empty or `b.steps - cdf_steps >= 2048`: recompute `eligible`,
`teacher_priorities(groups, b.steps, teacher)`, `teacher_cdf(groups, &cdf)`, `cdf_steps = b.steps`.
Draw `u = (retro_random(&e->rng) + .5) / 4294967296.0` as float, `g = teacher_sample(cdf.data(), n, u)`.
Pick a cell uniformly among the group's cells (scan `cells` for `group == g`), a state uniformly among its states,
then set `e->start`, `a.state`, `*id = level`, `cell.selected++`, `cell.last = steps`, the legacy feedback fields,
and the attempt record. If no group is eligible fall back exactly like the legacy `mode 2 -> mode 0` path.
The legacy knobs `archive_priority`, `archive_level_focus`, `archive_depth_priority`, `archive_backward` are ignored
with the teacher on; print one stderr line saying so at init.

## Serialization
New magic `0x524554524f415202` (v2). Layout: header[5] as today (new magic; header[3] stays
`sizeof(RetroArchiveLedger)`), phase, then per cell `uint64 stats[7] = {key, visits, selected, failures, successes,
state_count, level}` followed by `state_count` snapshots in the existing per-snapshot layout (ledger, pixels, palette,
metadata[7], core size, core bytes), then `uint64 group_count`, `uint64 sizeof(TeacherGroup)`, and the raw
`TeacherGroup` array. The reader accepts v1 (one snapshot per cell; groups rebuilt from cells with zero stats) and v2.
After any load: rebuild `group_index`, cell `group` indices and `snapshots`; set every group's `death_stamp` and
`last_selected` to 0 (the step counter restarts); recompute `eligible`. `archive_init_path` may be v1 or v2.

## Logging (`retro_training_log`, under the mutex)
`teacher/groups`, `teacher/eligible`, `teacher/snapshots`, `teacher/attempts`, `teacher/attempt_win_rate`,
`teacher/death_events`, `teacher/chain_deposits`, `teacher/entropy`, `teacher/mean_progress` (mean `teacher_progress`
over eligible groups), `teacher/top_level`, `teacher/top_section`, `teacher/top_prob`, and `teacher/level_W_L_share`
for all 32 levels (sum of `prob`). Do not change the progress CSV columns.

## Config (`[env]`, defaults keep today's behaviour)
```
archive_teacher = 0            # 1 enables everything above; requires archive_enabled = 1, archive_practice = 0
archive_group_width = 256      # px per section (32..2048)
archive_variants = 8           # snapshots per cell (1..64)
archive_full_game_share = 0.3  # fixed mode-1 share with the teacher on
archive_success_distance = 512 # px of same-area progress that count a practice start as a win
archive_attempt_decisions = 600
archive_death_chain = 1
archive_chain_interval = 32    # decisions
archive_chain_depth = 4
archive_chain_skip = 16        # decisions before a death that are not deposited
archive_death_half_life = 20000000   # agent steps
archive_upstream_groups = 6
archive_lp_fast = 0.05
archive_lp_slow = 0.005
archive_lp_min_attempts = 5
archive_weight_deaths = 0.5
archive_weight_lp = 0.35
archive_weight_stale = 0.15
archive_rank_power = 1
archive_floor = 0.1
```
Validate through `teacher_validate` plus range checks; invalid values throw like the other archive options.
In `config/retro.ini`: set `archive_teacher = 1`, `archive_start = 0.5`, `archive_end = 0.5`, `archive_capacity_mb = 512`,
add the new keys with the defaults above, and in `[sweep.env.archive_priority]` set `min = max = 0` (ignored knob).
Leave `load_model_path` and everything else as is, and update the header comment of the file.

## Tests
Extend `tests/test_archive.cpp` for the `states` vector (same checks, same intent). Add
`tests/test_archive_teacher.cpp` with a `teacher-archive-test` make target. Required cases, all with the ROM
fixture the existing test uses (no RAM writes):
1. Capture the same cell three times with `archive_variants = 2`: at most two states; `snapshots` is exact.
2. Rebuilt area key equals `retro_area_key` for a live capture; section and group key as specified.
3. Capacity eviction follows rules a/b/c (set `prob` by hand on groups) and never touches the inserting cell.
4. Death chain: mode 1 from 1-1 holding Right+B without jumping dies on the first Goomba; afterwards the ring has
   deposited entries (`chain_deposits > 0`), 1-1's groups at and upstream of the death section carry mass with the
   kernel weights, the newest `chain_skip` entries were not deposited, and the ring is empty.
5. Attempt outcome: a mode-2 start from a captured 1-1 cell running Right+B+A records a win once
   `success_distance` is passed; a start holding Left records a loss at the decision cap; a death records a loss.
6. Selection with the teacher: with hand-set group stats, sampled levels match `prob` within 1% over 20k draws;
   a group whose level is outside `spawn_levels` is never chosen; the fixed full-game share routes lanes correctly.
7. Serialization: v2 round trip preserves variants, cell stats and group stats; a v1 file written by the test
   loads with one state per cell and rebuilt groups; corrupt and wrong-ROM files are rejected as today.
Run: `make -C ocean/retro -j2 test archive-test teacher-test teacher-archive-test`, then `make -C ocean/retro panel viewer`
(compile checks of the other binaries that include retro.h), then the trainer compile check above. Report every
command and its result in `NOTES.md`, including anything that could not be run.

## README
Add a short section "Curriculum teacher" to `ocean/retro/README.md` describing groups, variants, the three signals,
the death chain, the config keys, memory cost, and the v2 archive format. Keep the existing text.
