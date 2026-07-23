# Local 5c migration surface

The migration deliberately retains only Abyss, Puffer Survivors, and native
MinGRU ONNX export. It does not carry forward PTCG, pendulum experiments,
Box3D, self-play registry changes, WSL sweep workarounds, deterministic eval,
or legacy checkpoint migration.

## Environments

- `abyss`: native CPU 5c environment, six discrete action heads, calibrated
  scenarios/catalogs, rich episode telemetry, and regression tests.
- `puffer_survivors`: native CPU and compile-time GPU 5c environment. The CUDA
  path uses the standard `puf_envs_*` ABI and 5c's generic log reducer.

## Native workflow

The environment is compiled into the native `./puffer` executable. Do not use
an editable 4.0 `puffer` Python command from another checkout.

```bash
cd ~/puffertank/pufferlib-5c
bash build.sh abyss
./puffer train abyss
./puffer eval abyss base.load_model_path=latest
./puffer sweep abyss
```

Build Puffer Survivors with its CUDA environment path before using the same
commands:

```bash
bash build.sh puffer_survivors --gpu
./puffer train puffer_survivors
```

Rebuilding another environment replaces `./puffer`, so rebuild the environment
you intend to run.

## Checkpoints and ONNX

5c writes a contiguous fp32 master-weight checkpoint in encoder, fused decoder,
then MinGRU-layer order. Upstream 5c does not provide an ONNX exporter. The
current development-time reference exporter uses PyTorch only to construct and
serialize the equivalent graph; training, evaluation, sweeps, and the live
runtime do not depend on it. Export either environment with:

```bash
source ../venv/bin/activate
python scripts/export_onnx.py abyss --checkpoint latest
python scripts/verify_onnx.py exports/abyss.onnx
```

The sidecar JSON records observation size, recurrent-state layout, and the
offsets of every discrete action head. Runtime consumers should sample each
head stochastically from its logits; deterministic argmax is not part of this
migration.

If a sweep changes `policy.hidden_size` or `policy.num_layers`, pass the winning
values explicitly to the exporter because raw 5c `.bin` files do not embed an
architecture header.

## Sweeps and metrics

No old Python sweep patch is carried forward. Native 5c already writes numeric
metric histories to `logs/<env>/<run>.ini` and Protein optimizes the configured
`sweep.metric`. Environment-specific reward and telemetry fields stay in each
environment's config and `puf_log` implementation. The native executable does
not currently upload W&B runs; use the local INI histories or Constellation for
reviewing native runs.
