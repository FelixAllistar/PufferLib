# Goofspiel on PufferLib 5.0

The CPU simulator, observations, legal-card masks, renderer and standalone
exact exploitability evaluator are ported from `5c` at `036cf4251`.
The simulator and observation code are unchanged. The CPU adapter uses the
existing `MY_VEC_INIT` interface to assign four frozen banks consistently
across rollout buffers; no shared trainer changes are needed.

```bash
make -C ocean/goofspiel test sanitize viewer exploit
./build.sh goofspiel puffer_goofspiel --float
./puffer_goofspiel train
./ocean/goofspiel/build/exploit uniform
./ocean/goofspiel/build/exploit PATH.bin --policy.hidden_size=32 --policy.num_layers=2
./ocean/goofspiel/build/viewer
```

Run commands from the repository root. Omit `--float` on GPUs supporting the
default precision. This trainer uses CPU simulation and GPU inference/PPO;
the GPU-simulation adapter is still pending. The standalone evaluator uses
CPU inference and supports perfect-information, zero-sum games up to five
cards (the default compiled observation/action ABI is four cards).

The config retains the old ordinary gameplay and supported PPO settings, with
upstream selfplay-pool keys. Old EMAg, priority-replay and epoch-sampling
settings are not imported into the new optimizer. This is not an identical
training algorithm to the old fork.

Still pending: GPU simulation/evaluation, exact-response pool refresh and
checkpoint persistence, and exploitability-driven sweep orchestration.
`env.exact_exploiter=1` explicitly fails until that integration is ported;
ordinary frozen-checkpoint selfplay works. Do not interpret a standard
upstream sweep's return metric as exploitability. The old robust-training
workflow remains preserved in `archive/5c-before-unification-20260924`.

Verification: core/adapter/exact-solver tests pass with ASan/UBSan; a bounded
4,096-step async FP32 training run with four frozen banks completes. The
evaluator's recurrent state and probabilities match upstream CPU inference
for that checkpoint. Interactive rendering still needs visual qualification.
