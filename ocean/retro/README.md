# Retro (Super Mario Bros) — PufferLib Ocean Env

C + CUDA only. No Python emulation path. Designed to parallelize **thousands** of SMB envs with minimal overhead: egocentric stats + a small collider window (SOA/GPU-friendly).

## Layout

* `retro.h` — shared spec + **CPU env** (`!PUFFER_GPU_ENV`). Synthetic SMB-lite physics; runs without any ROM or libretro core. If `env.rom_path` points to a valid `.nes`, it is detected (iNES header) and can be parsed by a future level decoder; otherwise procedural levels are used.
* `retro.cu` — **GPU env** (`-DPUFFER_GPU_ENV`, `ocean/retro/retro.cu` as `GPU_ENV_HEADER`). Native CUDA physics, collider templates uploaded once, per-env mutable tiles in a SoA buffer, coop multi-lane obs write (like `ocean/breakout`).
* `retro.c` — standalone CPU eval/demo entry (`./build.sh retro --local/fast`).
* `config/retro.ini` — train defaults (`puffer train retro` / `./puffer retro`).

## Observation

```
OBS_SIZE = 16 (ego) + 12*12 (tiles) = 160
```

* Ego (16 floats): `x,y / level`, `vx,vy`, `on_ground`, `dir`, `powerup`, `coins`, `score`, `tick`, `world`, `level`, `scroll_x`, `mario_offset_in_view`, `has_flag`, `is_dead`.
* Window (144 tiles): `12×12` collider patch centered on Mario, tiles encoded as `id/9.0` (`EMPTY/SOLID/BRICK/QUESTION/PIPE/ENEMY/COIN/FLAG/...`).

To increase observability, bump `RETRO_WINDOW_W/H` (keeps the coop kernel but widens the lane stride).

## Actions

Single discrete head `ACT_SIZES {12}`:

```
0 NOOP
1 RIGHT           5 A                9 B
2 RIGHT+A         6 LEFT            10 UP
3 RIGHT+B         7 LEFT+A           11 RIGHT+DOWN
4 RIGHT+A+B       8 DOWN
```

Maps to the NES joypad mask (`A/B/Up/Down/Left/Right`). `Start`/`Select` are not in the training set. For full 8-bit joypad control, switch to `NUM_ATNS 8` + `ACT_SIZES {2,2,2,2,2,2,2,2}` and decode the mask bitwise in `puf_step`/`retro_step_kernel`.

## Ultra-fast data loading & SOA notes

* **Bake colliders at init, not per step.** Host generates `32 × 256×16` byte level banks once in `puf_envs_create`, single `cudaMemcpy` to `d_templates` (~131 KB), single `cudaMalloc` for per-env mutable `d_env_tiles`. No file IO on the step path. Reset is just `level_id = rng % 32` + `memcpy` from template (device-side, lane 0).
* **SoA outside AoS.** Per-env mutable tiles live in a SoA byte buffer `d_env_tiles[agents * W*H]` so the physics sampling (`retro_sample_tile_device`) is a single coalesced byte load. The `Env` AoS itself stays tiny (~64 B: `Log` first + physics scalars) so 8k envs < 600 KB.
* **Coop obs write.** Like `breakout.cu`, `RETRO_THREADS_PER_ENV=16` lanes cooperatively store the `16+144` observation floats with adjacent lanes → adjacent 4-byte stores for coalescing. Single-thread AoS stores dominated kernel time in the original breakout port (~80% on a 5090); the coop variant fixes it.
* **ROM path (optional).** `env.rom_path` (DICT key `rom_path`) is read in `puf_init`/`puf_envs_create`. Valid WSL paths: `/mnt/c/Users/sunde/Downloads/Super Mario Bros. 3 (USA) (Rev 1).nes`. Windows native paths (`C:/Users/sunde/Downloads/...`) are also retried via a WSL translation fallback in the GPU path. Today the header is only validated; the procedural fallback is used. A real SMB level decoder can be dropped in to replace `retro_host_generate_level` / `retro_generate_fallback_level` and the `fread`/mmap stays at the one-time host init (still a single `cudaMemcpyToSymbol`/`cudaMemcpy`).

### When to bring in libretro

You only need libretro proper if you want bit-accurate NES emulation (PPU/APU cycle accuracy, scrolling tricks, enemy AI that depends on sub-pixel timing). For that case:

1. Build/get a libretro NES core for Linux (e.g. `fceumm_libretro.so` or `mesen_libretro.so`) — or `quicknes_libretro.dll` on Windows. Place under `ocean/retro/cores/`.
2. Implement `load_retro_core()`/`run` via `dlopen` + `dlsym` for `retro_init`, `retro_load_game`, `retro_run`, `retro_get_memory_data`, `retro_serialize_size` etc. Call once per `VecEnv` at create time, drive per-env via save-state snapshots per `Env` slot (fastest on GPU: host-serialize one base state + per-env deltas).
3. Obs then becomes a downsampled framebuffer tile hash → collider window (same `RETRO_TILES` layout) so the policy architecture is unchanged. This preserves the throughput story: the heavy part is still a single bulk copy, not per-step `fread`.

Without that, the current SMB-lite physics trains the same `OBS_SIZE=160` policy and you can swap in real ROM levels later without changing the model.

## Build & run

```bash
# CPU standalone demo (fast, no CUDA, uses fallback levels)
./build.sh retro --fast
./retro              # or ./build/retro depending on flag

# CPU native train binary (no GPU kernels, still uses puffercpu eval path)
./build.sh retro            # produces ./retro (+ ./puffer if MODE=native)
./puffer train retro        # reads config/retro.ini

# GPU train (recommended: thousands of envs)
./build.sh retro --gpu              # produces ./puffer (CUDA, PUFFER_GPU_ENV)
./puffer train retro --vec.total-agents 8192 --vec.gpu-env 1

# With a real ROM (optional, not required):
./puffer train retro --env.rom_path "/mnt/c/Users/sunde/Downloads/Super Mario Bros. 3 (USA) (Rev 1).nes"
# Native Windows (not WSL) equivalent:
# ./puffer train retro --env.rom_path "C:/Users/sunde/Downloads/Super Mario Bros. 3 (USA) (Rev 1).nes"
```

Config overrides are via `DICT` keys on the CLI (`--env.frameskip`, `--env.gravity`, etc.) — see `config/retro.ini`.

## Folder requested in the prompt

The prompt asks for `ocean/` inside the PufferTank wrapper; the canonical Ocean envs live at `puffertank/pufferlib/ocean/<env>`. To satisfy both, the env is at `puffertank/pufferlib/ocean/retro/` and a mirror is kept at `puffertank/ocean/retro/` (symlinked or copied).
