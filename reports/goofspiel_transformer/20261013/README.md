# CPU Goofspiel / GPU policy: PufferNet versus native Transformer

Started 2026-10-13; completed checks on 2026-10-14 UTC (October 13 local time).
The baseline is **the existing default PufferNet**, not a new MinGRU model.
PufferNet remains the default; `policy.architecture=transformer` selects the
new native policy. CPU environment stepping, GPU inference/training, async
actor snapshots, legal-action masks, PPO/Muon and frozen opponents are shared.
No Python was executed, no additional GPU was rented, and Mario was untouched.

## Outcome

The five-token Transformer is integrated into native PPO and learns in this
small pilot. There is **no clear strength advantage** over PufferNet here, and
this initial unfused FP32 implementation is substantially slower on the local
GTX 1060. This is an integration/learning sanity check, not a tuning study or a
controlled throughput benchmark. All pilot training used the **CPU simulator**;
the optional GPU-simulator comparison has not been run.

Both models use width 32 and two layers. The Transformer adds two attention
heads, 4x GELU feed-forward layers, affine pre-LayerNorm/residual blocks, final
LayerNorm and a shared 21-feature token projection. Its four rank tokens plus
one global token preserve the compact observation's information. The global
token feeds the unchanged action/value decoder. Attention never mixes rollout
timesteps, and the Transformer has no recurrent memory.

| Model | Trainable parameters | Mean initial exploitability | Mean final exploitability | Mean best saved-checkpoint exploitability | Median trainer time |
|---|---:|---:|---:|---:|---:|
| Default PufferNet | 7,840 | 0.756632 | 0.542078 | 0.502627 | 1.515 s |
| Transformer | 26,304 | 0.806654 | 0.504411 | 0.503233 | 13.908 s |

Exploitability is exact best-response evaluation for the configured four-card,
two-player, perfect-information zero-sum game: **lower is better**. It is not
self-play win rate. Three short seeds are insufficient for a claim that one
architecture is stronger. The best-checkpoint comparison uses the same five
saved checkpoints per run, and neither model is close to an unexploitable
policy. The models are not parameter matched and have different initial
distributions; neither architecture received a new hyperparameter search.

Trainer time is the final native `uptime`, including its startup graph work and
checkpoint saves, excluding subsequent offline exact evaluation. Median whole
process times were 2.36 s and 15.02 s respectively. The GPU was shared with
background work; these timings are **not** a modern-GPU/BF16 performance claim.

## Protocol and receipts

- GPU: NVIDIA GeForce GTX 1060 3GB, driver 581.63, local WSL; FP32, CUDA 12.6.85.
- CPU: Intel Core i5-7400, four configured environment threads.
- Each run: 262,144 total agent steps; seeds 73, 74 and 75 applied to policy,
  environment and self-play sampling; model order alternated for seed 74.
- 512 agent rows, two buffers, horizon eight, minibatches of 512, replay ratio
  four, async enabled, CUDA graphs enabled. Existing Goofspiel learning-rate,
  loss, entropy and optimizer settings were retained for both models.
- Four frozen banks; `hist_policy_percent=.5` assigns half the games to a
  historical opponent (not half the agent rows). Refresh timeout: 65,536 steps.
- Snapshots at 0, 65,536, 131,072, 196,608 and 262,144 steps. Exact evaluations
  run after training. No exact-response opponent was used in the pilot.

| Seed | PufferNet final / best | Transformer final / best | PufferNet trainer seconds | Transformer trainer seconds |
|---|---:|---:|---:|---:|
| 73 | 0.586353 / 0.536505 | 0.513732 / 0.513163 | 2.673033 | 13.957346 |
| 74 | 0.568145 / 0.499641 | 0.480881 / 0.480881 | 1.514921 | 13.907665 |
| 75 | 0.471735 / 0.471735 | 0.518619 / 0.515653 | 1.450265 | 10.850307 |

Machine-readable summary: [summary.json](summary.json). Raw curves and process
times are in [pilot_v1](pilot_v1); all checkpoint files and complete logged INIs
are retained beneath that directory. The pilot is reproducible with
`tests/run_goofspiel_transformer_pilot.sh NEW_DIRECTORY`; it refuses an existing
destination. See [native usage](../../../ocean/goofspiel/README.md#native-transformer-comparison)
for build commands and precision requirements.

## Qualification

- [Backbone checks](backbone_checks_fixed_upload.jsonl): three independent FP64 CPU
  reference cases and 314 sampled parameter/input derivatives; FP32 CUDA graph
  parity. BF16 execution correctly skipped on this GPU.
- [Adapter checks](adapter_checks_repeated.jsonl): ten consecutive passing runs,
  each with 84 derivatives, maximum absolute
  error `1.40715465e-5`; rollout/train agreement; poisoned-buffer graph replay;
  captured actor-snapshot isolation; no cross-observation leakage.
  Repetition exposed a test-upload race: a pageable host-to-device copy could
  finish staging before a nonblocking stream consumed the device data. The
  standalone upload helper and test uploads now explicitly finish that copy;
  poisoned buffers are filled on the graph's stream. Production policy
  initialization already synchronized its uploads.
- Exact-evaluator adapter probability agreement: maximum absolute difference
  `4.7438675e-8`, including a 513-row batch that exercises chunk boundaries.
  A zero policy head reproduces uniform exploitability `0.746527778`.
- [Sixteen training smokes](qualification_upload_fix.tsv) cover both architectures,
  sync/async, graphs off/on, and mirror/four-frozen-bank play. Checkpoints have
  the expected size and finite values; frozen-bank runs change weights from
  their saved initial checkpoint. Detailed logs are in `qualification_upload_fix/`.
- [Nine negative checks](guards_final.txt) reject incompatible observations,
  bad dimensions, unknown architecture, and a Transformer file loaded as
  PufferNet. They verify the expected rejection reason, not just a nonzero exit.
- [CPU tests and ASan/UBSan](cpu_tests.log) pass. The CPU exact solver gives
  `0.586353366` on the seed-73 PufferNet final checkpoint, versus the CUDA
  evaluator's `0.586353328` (difference about `3.8e-8`).
- [Weights-only reload](transformer_reload.log), [headless evaluation](transformer_headless_eval.log)
  and [exact-response refresh](transformer_exact_pool.log) pass. The latter
  saves five responses into a three-slot history under async/graphs. A default
  PufferNet [Breakout regression](breakout_default_regression.log) also completes
  a bounded 2,048-step CPU-environment run after the shared model API change.
- BF16 adapter **compilation only** was checked for SM80. BF16 training, modern
  GPU performance and multi-GPU training have not been qualified.
- [CUDA memcheck](adapter_memcheck.log) could not instrument this WSL/GTX 1060:
  it reports an unsupported device/debugger interface. This is not a passing
  memory-sanitizer result. An initial renderer launch is logged separately in
  `transformer_render_launch.log`; it was not visually qualified.

Checkpoints remain flat FP32 master weights with architecture supplied by the
matching logged INI. Reload preserves weights, not optimizer state. The CUDA
exact evaluator supports the new architecture; the standalone CPU neural
evaluator/viewer remains PufferNet-only. Frozen banks currently use the same
architecture/head setting as their learner; mixed-architecture matches are not
implemented. The backbone currently retains training-sized caches for rollout
too, so memory use is not inference-optimized. Goofspiel-specific tokenization
does not yet enable Transformer training in other environments.

## Source snapshot

Checkout HEAD observed during qualification:
`1aed9f3f7a032a19be5cd183612d3ed6c2bce0a1`, plus the local implementation.
Unrelated worktree changes were preserved; no commit was made.

| File | SHA-256 |
|---|---|
| `src/native_transformer.cuh` | `a0366c0495212bf5d47b63c07e6b69da3f34248a24cd016cd72b000bcaa71fa3` |
| `src/transformer_policy.cuh` | `f2efafd4e6ed29f727a08b127503b003e1ca81db81b7507a7f90295f55bf1724` |
| `src/algo.cu` | `3dd8e4d39f7767f7e05e45dd1c216d2a2a12f3d952cd363a8ea8786869caa170` |
| `src/pufferl.cu` | `2ed6380ad047d95beedef440a4fe472967aefd8d3c9f19c1283a447a59038187` |
| `ocean/goofspiel/transformer_features.cuh` | `cf1754fe6162b4bfdea8cb6f0082ceec5d36dee9d7d24793f4b240e7fd5aba9d` |
| `ocean/goofspiel/goofspiel_exploit.cu` | `d18ba6894afa006bbda4f711f3156ef9e52d0b02eba2449e933caeb655333510` |
| `tests/test_goofspiel_transformer.cu` | `ae18958e198501cfc1e3cbcd11dca834324844b8928c17c99774222ca65a75a8` |
