# Entity-v3 / policy-v5 / 2/2 Kaggle export

Submitted to Kaggriculture on 2026-09-22: **56467774**, initially `PENDING`.
One submission was made; training, remote runtime/config, rewards, and datasets
were not changed. This is a deterministic export (highest-probability legal
actions); it is not a fine-tune and does not contain a heuristic substitute.

## Exact artifacts

- Source checkpoint: remote
  `checkpoints/kaggriculture/1790058597151/0000000299335680.bin`.
- Local checkpoint and all ABI sidecars:
  `/home/felix/puffertank/checkpoints/kaggriculture/1790058597151/`.
- Checkpoint SHA256, confirmed equal on remote and local:
  `bf96ec0d25a93688d0be69625920ea8dc332ba9a6c86f91e9138c627265c8238`.
- Submitted archive:
  `/home/felix/puffertank/submissions/entity22_299m_20260922_v2.tar.gz`.
- Archive SHA256:
  `529deaa5a39a25eb0196eb6ec24099fb632eb05e802a3ca1222023d7a35cca3a`.
- `main.py` SHA256:
  `9ad8c1b1613647f849e4c0b286b3b0c4c6fad49f435bac45fb1911b6180125cb`.

The earlier package without `_v2` was a local test artifact, **not submitted**.
Do not deploy it: the replacement handles the Kaggle compile/exec loader
(which supplies no `__file__`) and direct seat-1 observations without the
framework-expanded shared `step` field.

## Implementation and boundaries

`package_entity_model.py` is the new exporter for mode 2 / executor 2. The old
`package_native_macro_model.sh` remains a legacy exporter; its 1280-byte input
and 1058-logit runtime must not be used for this checkpoint.

The archive contains four root files: `main.py`, `model.bin`,
`entity_bridge.so`, `policy_metadata.json`. The model is H256/L2, with 1424 float
inputs, 1978 action logits and one value output. The Python implementation
loads the padded entity MLPs, fusion, MinGRU and three decoder branches using
the checkpoint's explicit shape/alignment sidecars. It rejects malformed sizes
or nonfinite weights rather than guessing a legacy layout.

The shared library is built directly from production observation, prefix-mask,
sampler and multi-intent executor functions. It requires only libc/libm at
runtime (highest observed GLIBC symbol version 2.29), not CUDA, raylib, Torch,
or a local repository checkout. It is Linux x86-64, compiled without
`-march=native`. NumPy inference uses one BLAS thread.

The adapter imports only both farms' public fields and the acting player's own
private inventories, preserving their item insertion order. It does not infer
an opponent's private inventory. It maintains recurrent state, macro history,
episode coverage/idle totals, and uncapped producer peaks. These observation
histories are independent of reward coefficients; no training-only reset-bank
states are used. Missing mid-game history is rejected instead of fabricated.

Current package support is intentionally checked for the trained fixed rules:
mode 2, executor 2, observation 3, policy 5, decision interval 1, score features
off, 10 market slots, 16 hands, no land-buy gate, default 720-step game rules.
Changing these constraints requires another export/parity qualification, not
just editing package metadata.

## Verification

1. Remote/local checkpoint hash equality; archive contents, package/source
   Python equality, explicit ABI sidecars, and no unexpected runtime links.
2. Two 719-transition native games, both seats, deterministic: public adapter
   observations, sampled heads, prefix masks, decoded actions and CPU RNG matched
   the full-state native reference exactly. NumPy versus native CPU recurrent
   logits had maximum absolute error `8.392333984375e-05`.
3. The same two-game/two-seat native test with stochastic sampling also passed;
   maximum logit error `9.918212890625e-05`. RNG is aligned per seat for this
   comparison; this does not claim CPU LCG and CUDA Philox emit identical draws.
4. Four full official Python-environment smoke games passed (seeds 7/42,
   seats 0/1). They were repeated through the actual file-agent loader via
   `env.run`; all ended `DONE`, with exactly the same final money.
5. The installed `kaggle-environments==1.32.7` game source was byte-identical
   to upstream master when checked. Source SHA256:
   `bc8a54879ef02c7ea64b8b333d6a976f0ea65c4949149d01f463f23bccee653e`.

| Smoke seed | Seat | Money vs pass-only opponent |
| --- | --- | ---: |
| 7 | 0 | 82,201 |
| 7 | 1 | 87,437 |
| 42 | 0 | 83,306 |
| 42 | 1 | 78,097 |

Mean action time was 1.95–2.79 ms, worst observed 25.3 ms including first-call
loading. These are local timing and execution checks, **not a competitive
strength estimate**, not a comparison with old rewards, and not bit-exact
CPU/GPU neural inference. Kaggle's external evaluation is still needed.

## Reproduce

From the repository root, using a new output directory:

```sh
uv run --no-project --python /home/felix/puffertank/.venv/bin/python \
  ocean/kaggriculture/package_entity_model.py \
  /home/felix/puffertank/checkpoints/kaggriculture/1790058597151/0000000299335680.bin \
  /absolute/path/to/new_export \
  --config /home/felix/puffertank/checkpoints/kaggriculture/1790058597151/1790058597151.start.ini

OPENBLAS_NUM_THREADS=1 uv run --no-project --python /home/felix/puffertank/.venv/bin/python \
  ocean/kaggriculture/submission/test_entity_kaggle.py /absolute/path/to/new_export --runner
```

Add `--stochastic` to the packaging command only for a separately labeled,
separately tested sampled-action candidate. The submitted package was greedy.
Packaging/tests never submit automatically.

The test-only `entity_export_oracle.c` reuses the CPU network and full simulator
as a reference. It is deliberately excluded from the archive. Compile it and
`entity_bridge.c` separately to `ocean/kaggriculture/build/entity_export/` with
the same flags/includes as the packager; then run `test_entity_export.py` with
the checkpoint path, with and without `--stochastic`.
