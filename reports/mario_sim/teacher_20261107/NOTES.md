# mario_sim curriculum teacher: implementation notes (2026-11-07/08)

Implements `SPEC.md` in this directory. Nothing was committed or staged. Nothing under `src/`, `ocean/retro`,
`build.sh` or `./puffer` was touched; `ocean/retro/curriculum_teacher.h` and `ocean/retro/tests/test_teacher.cpp`
are used unchanged.

## Files changed

| File | Change |
|---|---|
| `ocean/mario_sim/practice.h` | Includes `../retro/curriculum_teacher.h` (with `TEACHER_HD = __host__ __device__` under nvcc). New `SmbTeacherDevice` (spec layout), `smb_teacher_add` (`atomicAdd` on device, `__atomic_fetch_add` on host, see deviation 3), `smb_teacher_choose` (float u from `smb_random>>8`, `teacher_sample`, uniform entry, `selected[g]++`). The scene/episode/progress initialisation of `smb_practice_reset` is factored into `smb_practice_load` (same statements, same order); the band reset calls it. |
| `ocean/mario_sim/practice_state.h` | `.teacher` sidecar: `SmbTeacherSavedHeader {magic 0x54434831, version 1, count, sizeof(TeacherGroup), bank_hash, checkpoint_hash, payload_hash}` + raw group array; reader checks magic/version/count<=4096/size/bank hash/checkpoint hash/payload hash/exact length and finite stats. |
| `ocean/mario_sim/mario_sim.h` | `Env` gains `teacher_group, teacher_pending, teacher_frames, teacher_x0, teacher_area` (outside `SmbPracticeProgress`). `SmbTeacherHost` (groups, params, cdf, offsets/area/section, counters + previous snapshot, agent_steps, since_refresh, refreshes, death_events, mutex, CPU shared_mutex, device handle) held as `FpgTimeHost::teacher`. Config parsing + `teacher_validate`; group construction in `fpt_setup` (group-major copy, `offsets[G+1]`, `G<=4096`, eligible=1, first refresh before any reset); legacy band path kept for `practice_teacher=0`. `smb_teacher_practice_reset` + attempt record; `fpt_reset_state`/`smb_after_frame` take an optional `const SmbTeacherDevice*` (default nullptr = old behaviour). Attempt outcome and failure crediting exactly as specified; `smb_practice_result` skipped with the teacher on. `smb_teacher_fold` (deltas -> `teacher_record`/`teacher_add_mass`/`last_selected`, then `teacher_priorities` + `teacher_cdf`), `smb_teacher_restore` (by key, stamps -> 0). `fpt_teacher_log` adds the teacher metrics to `puf_log`. CPU path: counters/refresh cadence via a new `PUF_CPU_POST_STEP` hook, forced `fpt_teacher_refresh_now`, `.teacher` save/load. |
| `ocean/mario_sim/mario_sim.cu` | Device buffers (cdf, offsets, area, section, counters[4G+1]) + pinned mirrors, allocated in `puf_vec_create`, freed in `puf_close`. Kernels take `SmbTeacherDevice` by value. Stream-ordered refresh (counters D2H -> `cudaLaunchHostFunc` fold -> CDF H2D), see deviation 1. Forced synchronous refresh; `.teacher` save/load. |
| `ocean/mario_sim/build_practice_bank.cpp` | `--per-stage N` (default 8 = old code path, verified byte-identical). N != 8: at most two per (area, 64-px) bin preferring a second source, spread evenly over the progress ranking, band = progress quartile; summary gains `per_stage` and per-stage `candidates`/`states`. |
| `ocean/mario_sim/fpg.mk` | `-lz` on the six ROM-tool link lines (pre-existing breakage, see "Pre-existing problems"); new `test_teacher_sim` target, `teacher-test`, and `test_teacher_sim` added to `game-test`. |
| `ocean/mario_sim/test_teacher_sim.cpp` | New CPU test (spec cases 1-6 plus cadence, metrics, parameter validation, legacy mode, eval). |
| `ocean/mario_sim/test_runtime.cu` | Existing four modes pin `practice_teacher=0` (the band machine they test). New `teacher_case` (spec case 7) with bitwise CPU/CUDA parity, see below. |
| `ocean/mario_sim/test_practice.cpp` | One line: `practice_teacher=0`, because the default config now enables the teacher and this test checks the band machine and `.practice`. |
| `ocean/mario_sim/README.md` | New "Curriculum teacher" subsection under Encounter practice; existing text kept. |
| `config/mario_sim.ini` | Header comment; `practice_bank = build/mario_sim/practice_v2/bank.bin`; `practice_teacher = 1`; all spec keys with spec defaults and comments. |

Build artifacts written: `build/mario_sim/practice_v2/` (new bank), `build/mario_sim/fpg/test_teacher_sim`, rebuilt test
binaries, `build/mario_sim/puffer_mario_sim_teacher_check{,_cu}` (compile checks), test sidecars
(`build/mario_sim/fpg/teacher_test.bin*`, `build/mario_sim/teacher_test_checkpoint.bin*`), and a regenerated
`build/mario_sim/fpg/pipe_targets.bin` (stale; old copy kept as `pipe_targets.bin.stale_20261009` + `.json.stale_20261009`).

## The v2 practice bank

```
build/mario_sim/fpg/build_practice_bank build/mario_sim/practice_v2 --per-stage 48 config/mario_sim.ini \
  checkpoints/mario_sim/sweep_1792016021935_0000/0000006442450944.bin \
  checkpoints/mario_sim/sweep_1792052933835_0001/0000009999745024.bin       (OMP_NUM_THREADS=2, 2m38s)
```
summary.json:
```
{"stages":13,"states":308,"CPU_ROM_frames":73920,"CUDA_reference_frames":4928,"bytes":15800302,"synthetic_RAM_edits":0,"per_stage":48,"stage_counts":{"1-1":{"candidates":66,"states":48},"2-2":{"candidates":39,"states":36},"4-3":{"candidates":8,"states":8},"4-4":{"candidates":8,"states":4},"5-4":{"candidates":24,"states":24},"6-3":{"candidates":12,"states":12},"6-4":{"candidates":41,"states":41},"7-2":{"candidates":20,"states":18},"7-4":{"candidates":8,"states":8},"8-1":{"candidates":50,"states":48},"8-2":{"candidates":11,"states":9},"8-3":{"candidates":39,"states":36},"8-4":{"candidates":16,"states":16}},"guaranteed_full_level_solutions":false,"CUDA_validation_required":true}
```
308 states (v1: 104). Only 1-1 and 8-1 reach 48; the rest are limited by distinct candidates (4-4: 8 candidates
collapse to 4 after the two-per-bin rule, one per band). The builder's 240-frame CPU ROM check passed for all 308
states (73,920 frames). `verify_cuda` on the clip:
```
CUDA 308/308 selected cases, 4928 frames, 0 failures
{"scope":"video_frame_clips","cases":308,"frames":4928,"failures":0,"bitwise_observations":true,"source_cases":308,"source_frames":4928,"case_begin":0,"case_end":308}
```
Groups at `practice_group_width = 256` (from test_teacher_sim): **G = 79**
`1-1=13 2-2=6 4-3=2 4-4=2 5-4=5 6-3=3 6-4=9 7-2=5 7-4=3 8-1=15 8-2=3 8-3=8 8-4=5`.
Bands per stage are quartiles (e.g. 1-1 12/12/12/12, 4-4 1/1/1/1, 8-2 3/2/2/2), so the legacy loader also accepts v2.

Default-output check: `build_practice_bank <scratch>/practice_v1_repro config/mario_sim.ini <same two models>`
(no `--per-stage`) reproduced `build/mario_sim/practice/` exactly: `cmp` identical for bank.bin, summary.json,
states.jsonl and validation.bin; routes.jsonl identical apart from the model path column.

## Commands run and results

All from the repository root, `-j2` everywhere.

1. Baseline before any change: `make -f ocean/mario_sim/fpg.mk -j2 game-test` -> **failed to link** `test_game_rom`
   (`undefined reference to 'gzread'` ... `gzclose`), pre-existing; `build/mario_sim/fpg/test_practice` (old binary) ->
   `PASS practice pools, no reset rewards, metric isolation, curriculum retention, sidecars and ordinary evaluation`.
2. `make -f ocean/mario_sim/fpg.mk -j2 build/mario_sim/fpg/build_practice_bank` -> built (after adding `-lz`).
3. v1 reproduction and v2 build (above). Reference-routes-only attempt:
   `build_practice_bank <scratch>/practice_v2_reference_only --per-stage 48` ->
   `practice bank: stage 5 lacks eight safe natural approach states` (exit 1; see deviation 4).
4. `make -f ocean/mario_sim/fpg.mk -j2 game-test curriculum-test` -> exit 0 (12m47s):
```
build/mario_sim/fpg/test_modes
{"CPU_modes":true,"config_sections":true,"mixed_metrics":true,"sweep_score":true,"legacy_resume":true,"failures":0}
build/mario_sim/fpg/test_game_rom
[retro] ROM ocean/retro/roms/smb1_ntsc.nes fingerprint=6e01246e5d215cb3 mapper=0 World/NTSC; 60.09881390 fps
{"stages":32,"ROM_frames":72110,"clears":1,"life_losses":96,"level_transitions":1,"gameovers":31,"checkpoints":177,"semantic_and_lifecycle_parity":true,"failures":0}
build/mario_sim/fpg/test_teacher_sim
groups=79 entries=308 per_stage: 1-1=13 2-2=6 4-3=2 4-4=2 5-4=5 6-3=3 6-4=9 7-2=5 7-4=3 8-1=15 8-2=3 8-3=8 8-4=5
sampling: 20000 draws, max |freq-prob|=0.00248, prob range 0.00345..0.06401
crediting: section=14 of area 001caa01 credited 7 groups
PASS teacher groups, reset sampling, attempt outcomes, failure crediting, refresh and cadence, metrics, sidecars and legacy mode
build/mario_sim/fpg/build_table
{"cache_hit":true,"entries":423,"optimality_proven":false}
build/mario_sim/fpg/test_curriculum_cpu
{"curriculum_levels":17,"decisions":4961,"teacher_successes":73,"promotions":16,"practice_episodes":7,"checkpoint_roundtrip":true,"fixed_evaluation":true,"failures":0}
build/mario_sim/fpg/test_curriculum_cuda
{"decisions":40960,"successes":227,"resets":688,"teacher_agents_at_final_level":2,"bitwise_native_states":true,"bitwise_curriculum":true,"bitwise_observations":true,"direct_and_graph":true,"checkpoint_resume":true,"failures":0}
```
5. New test alone (`build/mario_sim/fpg/test_teacher_sim`, also `make -f ocean/mario_sim/fpg.mk teacher-test`): same
   four lines as above, PASS.
6. `build/mario_sim/fpg/test_practice` (rebuilt) ->
   `PASS practice pools, no reset rewards, metric isolation, curriculum retention, sidecars and ordinary evaluation`.
7. `make -f ocean/mario_sim/fpg.mk -j2 test` (CUDA_ARCH default `native` works on the GTX 1060, sm_61):
   first run **failed** in `test_cpu`: `FPG timing CPU test: FPG timing table engine changed; rebuild the table`
   (pre-existing, see below). After regenerating the pipe timing table with the command the `pipe` target uses
   (`build/mario_sim/fpg/build_table --bank build/mario_sim/fpg/pipe/bank.bin --teacher build/mario_sim/fpg/pipe/teacher/clips.bin --output build/mario_sim/fpg/pipe_targets.bin --variants 0 --plans 0`
   -> `{"cache_hit":false,"templates":102,"covered":1,"fallback_templates":101,"teacher_successes":1,"trials":1,"successful_trials":1,"simulated_frames":425,"seconds":40.926,"optimality_proven":false}`), exit 0:
```
build/mario_sim/fpg/build_table
{"cache_hit":true,"entries":423,"optimality_proven":false}
build/mario_sim/fpg/test_cpu
{"teacher_successes":2,"teacher_frames":850,"reset_stream_comparisons":4096,"corrupt_tables_rejected":6,"start_guards":3,"timeout_frames":1800,"zero_bonus_ablation":true,"cache_free_fallback":true,"failures":0}
build/mario_sim/fpg/test_cuda
{"decisions":33792,"successes":2,"resets":36,"reward_tolerance":0.00001,"bitwise_native_states":true,"bitwise_observations":true,"direct_and_graph":true,"failures":0}
```
8. `make -f ocean/mario_sim/runtime.mk -j2 environment` -> built `test_runtime`, `bench_env`, `test_bank` (no warnings);
   `build/mario_sim/runtime/test_runtime` -> exit 0 (4m06s):
```
mode=game actors=128 FPG=0 practice=0 parity=passed
mode=fpg actors=128 FPG=128 practice=0 parity=passed
mode=mixed actors=128 FPG=32 practice=0 parity=passed
mode=practice actors=128 FPG=32 practice=32 parity=passed
mode=teacher actors=128 practice=32 groups=79 parity_steps=256 steps=2000 parity_attempts=165 parity_wins=134 selected=1284 attempts=1266 wins=1046 death_events=20 stream_refreshes=27 agent_steps=242688 entropy=4.3433 parity=passed
{"decisions":229376,"resets":3675,"stages":32,"modes":4,"practice_checkpoint_resume":true,"teacher":true,"failures":0}
```
   The teacher case: 256 steps of bitwise CPU/CUDA parity (native states, Env game/practice/episode/curriculum, the
   five attempt-record fields, rewards, terminals, logs, observations, and all 4G+1 teacher counters after every
   step) with direct and CUDA-graph launches alternating and the CDF frozen; then 1744 more steps with
   `refresh_steps=64` (27 stream-ordered refreshes ran, both from direct calls and from the captured graph's host
   node); a forced refresh: `sum(selected) > 0`, `cdf.back() == 1`, device CDF == host CDF, `attempts <= selected` and
   `wins <= attempts` per group, host attempts == device attempts; `.teacher` written by `fpt_gpu_save` (no
   `.practice`) and restored by `fpt_gpu_load`. (The case uses `practice_success_distance=48` and game
   `max_frames=400` so wins and natural deaths occur within the test.)
9. Trainer compile checks: see "Compile checks" at the end.

## Deviations from the spec, with reasons

1. **CUDA refresh is stream-ordered, not "synchronise + copy" in `puf_step`.** The config has `cudagraphs = 1`, and
   `src/pufferl.cu` captures the whole rollout (H × `puf_step`) into a CUDA graph on the first rollout of each slot
   and afterwards only replays it, so `puf_step`'s host code never runs again; `cudaStreamSynchronize` inside a
   capture is also illegal and would abort the capture. The refresh is therefore enqueued on the env stream as
   counters D2H (pinned) -> `cudaLaunchHostFunc` (fold, priorities, CDF into pinned memory; no CUDA calls) -> CDF
   H2D. Direct calls enqueue it every `practice_refresh_steps` calls (exactly the spec cadence). Under capture one
   such node is placed after the first step of each captured rollout, and the callback counts the steps that capture
   held, refreshing once `since_refresh >= practice_refresh_steps`; with H=128 and 256 that is exactly every 256
   steps, in general the cadence is rounded up to whole rollouts. Arithmetic, counters and the device's work are as
   specified; `now = agent_steps` (steps × actors). The host state is guarded by a mutex because the callback runs on
   a driver thread while `puf_log` reads the groups.
2. **CPU refresh runs in a `PUF_CPU_POST_STEP` hook** (env-local macro the trainer already supports), not inside
   `puf_step`: CPU envs step in parallel under OpenMP and several buffer threads, so rewriting the CDF inside
   `puf_step` would race with other envs' resets. `puf_step` only counts calls (atomic); the hook refreshes when the
   count reaches `refresh_steps × number of CPU envs`. Practice resets hold a shared lock on the CDF, the refresh an
   exclusive one.
3. **Host `smb_teacher_add` is an atomic add** (`__atomic_fetch_add`, seq_cst) instead of `*p += v`, for the same
   OpenMP reason; the integers are identical. The CPU refresh reads wins, then attempts, then selections, so a delta
   never shows more wins than attempts (which `teacher_record` would reject).
4. **The v2 bank uses the two local policies, not reference routes only.** The spec assumed no local model exists;
   both models that built v1 are in `checkpoints/mario_sim/`. A reference-routes-only build fails in the builder's
   existing (unchanged) guard: `practice bank: stage 5 lacks eight safe natural approach states` (2-2), and even
   where it passes it would give far fewer than 48 states per stage. v2 was built with exactly v1's inputs plus
   `--per-stage 48`.
5. **The sidecar is written from the groups as of the last refresh** (no extra fold at save), so up to one refresh
   interval of outcomes is not in the file. Resume restores by key and then does a full refresh as specified.
6. `teacher_top_stage` is logged as the stage index 0-31 (numeric metric). The per-stage share keys are
   `teacher_W_L_share`, e.g. `teacher_8_4_share`, for the 13 stages in the bank.
7. Range checks chosen where the spec gave none: group_width 32..2048, success_distance 1..65536, attempt_frames
   1..108000, refresh_steps 1..1e8, upstream 0..1024, half-life 1..1e18, weights 0..1000, rank_power (0,16], floor
   [0,1]; then `teacher_validate`.
8. With the teacher on, legacy `practice_promotions/demotions` stay 0 (the band machine is not run); the other
   practice metrics (`practice_perf`, `practice_W_L_perf`, ...) are unchanged and still count episodes.

## Pre-existing problems found (not caused by this change)

- **ROM tools did not link**: `ocean/retro/retro_archive.h` (committed) now uses zlib, and the six ROM-tool link lines
  in `fpg.mk` (`build_pipe_bank`, `build_curriculum_bank`, `sim2rom`, `test_parity`, `build_practice_bank`,
  `test_game_rom`) lacked `-lz`, so `game-test` failed at baseline. Fixed by adding `-lz` in `fpg.mk`.
  `runtime.mk`'s `build_bank` has the same issue and was left alone (not needed here).
- **Stale pipe timing table**: `build/mario_sim/fpg/pipe_targets.bin` (2026-10-09) was fingerprinted against engine
  sources last changed 2026-10-21 (none touched here), so `test_cpu` failed with "FPG timing table engine changed".
  Regenerated with the `pipe` target's own `build_table` command; the old file is kept beside it.

## Could not run / not verified

- No training run (not allowed locally), so the teacher's effect on learning, its real refresh cadence inside the
  trainer's graph, and its throughput cost are untested. The graph path is exercised only by the test's one-step
  captured graph.
- `make -f ocean/mario_sim/fpg.mk test` and `runtime.mk environment` used `CUDA_ARCH=native`, which compiled for the
  local sm_61; `CUDA_ARCH=sm_61` was not needed.
- `semantic-test`, `progress-test` and `test_parity` were not run (not required; no observation/encoder code changed).

## Open questions

1. **Deaths past x = 3400 are credited to section 13.** `smb_game_track_progress` caps `last_x` at 3400, so a
   natural death beyond x = 3584 is credited as if at section 13. In v2 only one group is beyond that point
   (8-1, data 0xaa01, section 14), and it can never receive death mass of its own. Keep the spec's `game.last_x`, or
   track an uncapped x for the teacher (env-local, no reward change)?
2. The per-actor band machine is gone with the teacher on; `practice_replay` is ignored. Is a fixed 25% practice
   share still right now that the teacher concentrates it?
3. 4-4 has 4 states in 2 groups and 7-4/4-3 have 8; the bank is thin exactly on the maze stages. More policy routes
   (newer checkpoints) would add candidates there.
4. Not touched, but noted against standing rules: `config/mario_sim.ini` still has `[sweep.train.replay_ratio]`
   (0.25..2) and `[sweep.policy.hidden_size]`/`num_layers` sweep ranges.

## Compile checks

- `CUDA_HOME=/usr/local/cuda bash build.sh mario_sim build/mario_sim/puffer_mario_sim_teacher_check` (CPU env,
  `mario_sim.h`): exit 0, `Built: ./build/mario_sim/puffer_mario_sim_teacher_check` (2m52s).
- `CUDA_HOME=/usr/local/cuda bash build.sh mario_sim build/mario_sim/puffer_mario_sim_teacher_check_cu --cu` (CUDA env,
  `mario_sim.cu`, the training build): exit 0, `Built: ./build/mario_sim/puffer_mario_sim_teacher_check_cu` (2m42s).
- Only warnings are pre-existing ones in `src/pufferl.cu` (#550 `explicit_render`, `-Wformat-truncation` in
  `run_sweep`); none from `ocean/mario_sim`. `./puffer` untouched (timestamp 2026-11-04).
