# 4.0 to 5c migration

This port intentionally replaces the 4.0 integration rather than preserving a
compatibility layer.

- `binding.c`, `vecenv.h`, and the Python extension are gone.
- CPU state is the native 5c `Env` with one `Agent`.
- GPU training is the compile-time `--gpu` path and exposes only
  `puf_envs_create`, `puf_envs_reset`, `puf_envs_step`, and `puf_envs_close`.
- Episode telemetry is accumulated into `Env.log`, allowing 5c's generic CPU or
  GPU reducer to call the shared `puf_log` mapping.
- The old CUDA simulator API and custom log reducer are gone.
- Old observation/checkpoint migration code is not carried forward. Train a new
  observation-v9 policy on 5c.

The gameplay mechanics and tested CUDA SoA implementation were retained; only
their framework-facing ownership and lifecycle changed.
