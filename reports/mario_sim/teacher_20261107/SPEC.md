# mario_sim curriculum teacher: implementation spec (2026-11-07)

Env-local change to `ocean/mario_sim` only. The encounter-practice pool keeps its fixed actor share, but which
practice state an actor resets to is chosen by a bandit teacher over *groups* of bank states instead of the per-actor
four-band promotion machine. The math is shared with retro: `ocean/retro/curriculum_teacher.h` (already written and
tested by `ocean/retro/tests/test_teacher.cpp`; constants in `reports/retro/teacher_20261107/expected.py`).
**Do not modify that header or its test.** Read `reports/retro/teacher_20261107/SPEC.md` once for the shared design;
the retro implementation is being done by someone else in parallel, so do not touch `ocean/retro`.

## Hard constraints

- Touch only files under `ocean/mario_sim/`, `config/mario_sim.ini`, and `reports/mario_sim/teacher_20261107/NOTES.md`
  (your report). Nothing under `src/`, no other env, no `build.sh`, nothing in `ocean/retro`.
- Never write `./puffer`. The trainer compile check uses an output name:
  `bash build.sh mario_sim build/mario_sim/puffer_mario_sim_teacher_check` (compile only; do not train).
- No git commits, no `git add`. Other sessions have uncommitted work in this tree; leave it alone.
- With `practice_teacher = 0` (default) behaviour is unchanged and every existing test keeps passing.
- CPU and CUDA paths must run the same arithmetic (SMB_HD functions, float CDF, identical rng draws). Device
  code only does: sample a group from the CDF, pick an entry, add integer counters. All EMA/decay/ranking runs on the
  host at a refresh.

## Groups from the practice bank (host, in `fpt_setup`)
For every practice entry (after the existing validation): `m = e.scene.initial.ram`, `stage = m[0x75f]*4+m[0x75c]`,
`data = m[0xe7] + 256*m[0xe8]`, `x = fpt_x(m, 0)`, `section = x / practice_group_width`,
`area = ((uint32_t)stage << 16) | data` (the same layout as `smb_game_track_progress`'s frontier key),
`key = ((uint64_t)area << 16) | section`, `level = stage`. Copy practice entries into `runtime_bank` **group-major**
(all entries of a group contiguous) and build `offsets[G+1]`. Every group starts `eligible = 1`. Validate `G <= 4096`.
Keep the legacy band `offsets[stage][band]` path for `practice_teacher = 0`.

## Device/host handle
```
struct SmbTeacherDevice {           // passed by value to the kernels; pointers are device memory under CUDA,
    int enabled, groups, group_width, success_distance, attempt_frames, upstream;   // host memory on the CPU path
    const float* cdf; const uint32_t* offsets; const uint32_t* area; const int32_t* section;
    uint32_t *selected, *attempts, *wins, *death_weights, *events;   // events[0] = natural deaths credited
};
```
Host side (`FpgTimeHost`): `std::vector<TeacherGroup> groups; TeacherParams params; std::vector<float> cdf;` the
host mirrors of the counters (previous snapshot for deltas), `uint64_t agent_steps; int refresh_steps, since_refresh;`
and logging aggregates. Allocate device buffers in `puf_vec_create`, free in `puf_close`.

Define `#define TEACHER_HD __host__ __device__` before including the header in the CUDA build (plain build leaves
it empty). Use a small `SMB_HD` helper `smb_teacher_add(uint32_t* p, uint32_t v)` that is `atomicAdd` under
`__CUDA_ARCH__` and `*p += v` otherwise.

## Practice reset (SMB_HD)
With the teacher on: `u = (float)(smb_random(rng) >> 8) * (1.0f/16777216.0f)`, `g = teacher_sample(cdf, G, u)`,
`entry = offsets[g] + smb_random(rng) % (offsets[g+1] - offsets[g])`, `smb_teacher_add(&selected[g], 1)`.
Reset the scene from `bank[entry]` exactly as `smb_practice_reset` does today (same episode/progress initialisation),
then set the attempt record on the Env (new POD member, **not** inside `SmbPracticeProgress`, whose size is checked
by the `.practice` sidecar): `teacher_group = g; teacher_pending = 1; teacher_frames = 0; teacher_x0 = fpt_x(m,0);
teacher_area = (stage<<16)|data`. Ignore `practice_replay` with the teacher on.

## Attempt outcome (SMB_HD, practice actors, in `smb_after_frame` after the status update)
While pending: `teacher_frames++`;
- win if `status == SMB_EPISODE_SUCCESS`, or `game.level_exits > 0`, or (`m[0xe] == 8` and
  `((m[0x75f]*4+m[0x75c])<<16 | (m[0xe7]+256*m[0xe8])) == teacher_area` and `fpt_x(m,0) >= teacher_x0 + success_distance`);
- loss if `status == SMB_EPISODE_DEAD || status == SMB_EPISODE_TIMEOUT` or `teacher_frames >= attempt_frames`.
Record once: `smb_teacher_add(&attempts[g], 1)`, `smb_teacher_add(&wins[g], win)`, `teacher_pending = 0`.
Episode semantics are unchanged (first death or clear ends it, as today).

## Failure crediting (SMB_HD, ordinary game actors only)
Around the `smb_game_after_frame` call keep `deaths_before`, `key = env->game.last_key`, `x = env->game.last_x`
(the last tracked gameplay position; it persists through the death animation). If `env->game.deaths > deaths_before`
and `key != 0`: `section = x / group_width`; for every group `k`: `d = section - section[k]`; if `area[k] == key`
and `0 <= d <= upstream`: `smb_teacher_add(&death_weights[k], upstream + 1 - d)`; then `smb_teacher_add(&events[0], 1)`.
FPG and practice actors never credit failures.

## Host refresh
Every `practice_refresh_steps` calls of `puf_step` (CUDA: per call; CPU: scale by the number of CPU envs so the cadence
is per actor), with `now = agent_steps` (host step count times actor count):
synchronise the stream, copy the four counter arrays to the host, take deltas against the previous copy, and for each
group: `if (Δattempts) teacher_record(g, Δattempts, Δwins, params)`, `if (Δdeath_weights) teacher_add_mass(g,
Δdeath_weights, now, params)`, `if (Δselected) g.last_selected = now`. Then `teacher_priorities(groups, now, params)`,
`teacher_cdf(groups, &cdf)`, and copy the CDF to the device on the same stream. Do the first refresh before the first
reset (uniform floor only). Keep the refresh cost out of the per-frame path.

## Sidecar
With the teacher on, write `checkpoint + ".teacher"` instead of `.practice`: header `{magic 0x54434831, version 1,
count, sizeof(TeacherGroup), bank_hash, checkpoint_hash (fpg_time_file_hash), payload_hash}` and the raw group array.
On load (`practice_resume = 1`) require matching bank hash and checkpoint hash like `smb_read_practice`, match groups
by `key`, copy `fast, slow, attempts, wins, deaths`, and set `death_stamp = last_selected = 0`. Then refresh.

## Logging (`puf_log`, from the host singleton)
`teacher_groups`, `teacher_entropy`, `teacher_attempts`, `teacher_attempt_win_rate`, `teacher_death_events`,
`teacher_mean_progress`, `teacher_top_stage`, `teacher_top_section`, `teacher_top_prob`, and `teacher_W_L_share` for
every stage present in the practice bank (sum of `prob`). Leave `sweep_score` and all existing metrics unchanged.

## Bank builder
`build_practice_bank` gains `--per-stage N` (default 8 keeps today's output). With `N`: keep up to `N` candidates per
stage spread over progress, at most two per 64-px bin preferring distinct sources, and set the band flag to the
progress quartile so the existing loader validation passes. Build `build/mario_sim/practice_v2` with
`--per-stage 48` from the reference routes (no local model exists; if `summary.json` shows fewer candidates than
requested, report the actual count). The builder's own 240-frame CPU ROM check must pass for every state; run
`verify_cuda` on the validation clip if the CUDA module builds locally (GTX 1060, `sm_61`).

## Config (`[game]`, defaults keep today's behaviour)
```
practice_teacher = 0
practice_group_width = 256
practice_success_distance = 512
practice_attempt_frames = 2400
practice_upstream_groups = 6
practice_death_half_life = 20000000
practice_lp_fast = 0.05
practice_lp_slow = 0.005
practice_lp_min_attempts = 5
practice_weight_deaths = 0.5
practice_weight_lp = 0.35
practice_weight_stale = 0.15
practice_rank_power = 1
practice_floor = 0.1
practice_refresh_steps = 256
```
Validate with `teacher_validate` plus ranges. In `config/mario_sim.ini` set `practice_teacher = 1`,
`practice_bank = build/mario_sim/practice_v2/bank.bin`, add the keys above, and update the file's header comment.

## Tests
Add `test_teacher_sim.cpp` (CPU, headless; wire it into `fpg.mk` under `game-test`) covering:
1. Group construction: every practice entry in exactly one group, contiguous offsets, unique keys, `G` as expected
   for the v2 bank, stage/area/section as specified.
2. Reset sampling: with a hand-set CDF putting all mass on one group every practice reset lands in that group's entry
   range; `selected` counts it; with the real CDF 20k draws match `prob` within 1%.
3. Attempt outcome: pending record with `teacher_x0 = current x - 600` records a win on the next frame; `ram[0xe] = 11`
   (the existing test already pokes this) records a loss; the frame cap records a loss; exactly one record each.
4. Failure crediting: a game actor with `game.last_key`/`game.last_x` set and a lives decrement credits the groups
   of that area with the integer kernel weights `upstream + 1 - d` and nothing else.
5. Refresh: after counters, the host refresh updates EMAs, death mass and probabilities; the CDF ends at 1.
6. Sidecar round trip and rejection of a different bank hash; `practice_teacher = 0` still writes/reads `.practice`.
7. CUDA (extend `test_runtime.cu` or `test_curriculum_cuda.cu`): a mixed batch with the teacher on runs 2000 steps
   without faults; after a forced refresh `sum(selected) > 0`, `cdf.back() == 1`, `attempts <= selected`.
Run: `make -f ocean/mario_sim/fpg.mk game-test curriculum-test`, the new test, `make -f ocean/mario_sim/fpg.mk test`
(try `CUDA_ARCH=sm_61`), `make -f ocean/mario_sim/runtime.mk environment` if it builds locally, and the trainer compile
check. Report every command and result in `NOTES.md`, including anything that could not be run.

## README
Add a "Curriculum teacher" subsection to the encounter-practice part of `ocean/mario_sim/README.md`: groups, the three
signals, refresh cadence, config keys, the v2 bank and the `.teacher` sidecar. Keep the existing text.
