# Self-play Metamon sweep, 2026-10-20

Vast 56007315 (`ssh -p 33431 root@ssh6.vast.ai`), `/workspace/PufferLib`, tmux
`pokemon:selfplay_sweep`, plain `./puffer sweep`, log `/workspace/selfplay_sweep.log`.
History: `reports/pokemon_experiments/20261020_selfplay_sweep/sweep.history`.
Config snapshot: `launch_config.ini` (same as `pokemon.ini` here). Replaced box
files are backed up under `/workspace/PufferLib/before_selfplay_sweep_20261020/`.

## Search (32 dims, 500 trials, fresh history)

| Group | Ranges |
| --- | --- |
| Budget | 50M–1B steps, log-scale (`scale = time`), max suggestion cost 6 h |
| Recurrent model | width 64–512; 1–3 layers |
| Encoder | `policy.pokemon_embed` 32/64/128; `policy.pokemon_mid` 64/128/256 |
| Entropy | 1e-5–3e-3; anneal off/on, floor 0.01–1 |
| Magnet KL | coef 0.01–10; tau 1e-5–0.1; forward/reverse; anneal off/on, floor 0.01–1 |
| Opponents | frozen fraction 0/0.25/0.5/0.75/1 against the 13 round-26 peers of the 20M round-robin league (256x2); rest is current-policy self-play |
| Other | rollouts, PPO, optimizer, gamma/lambda, V-trace, HP/KO shaping as in broad256 |

First trial: 256x2, 32/64 encoder, 200M steps, entropy 1e-3 annealed, magnet 0.1
forward, tau 0.01, 25% peers. The peers are 32/64, so any trial with a different
encoder size runs with `expert_fraction = 0` (pure self-play); the trainer prints this.

Score: TaurosV0, private-information v2 evaluator, argmax, final checkpoint only,
48 games per team (144 total). No interim checks (`metamon.interval_fraction = 1`).

## Code (local tree, uncommitted)

- Encoder sizes are runtime: `semantic_contract.h` (`pk_embed`, `pk_mid`, size macros
  take the embed width), `pokemon_encoder.cu` (kernels take `E`), `semantic_cpu.h`,
  `policy_view.h` (reads the checkpoint's `[policy]`, defaults 32/64), `pokemon_base.h`.
- Trainer on the box = box `src/pufferl.cu` 3-way merged with branch `magnet-kl`
  (no conflicts), plus `magnet-kl` `src/algo.cu` and `config/default.ini`.
  Binary sha256 `42303a3aecdf3555c82fa0317c746b0091a12482067f981362b954eea8a542ae`.

## Checks

- CPU semantic trace at 32/64 is bit-identical to the pre-change code
  (`9a9d18d89f446738`); batch/single parity passes at 64/128, 128/256, 8/8.
- FP32 GPU vs CPU forward parity is exact or within 3e-5 at all sizes tested. All 172
  finite-difference probes pass at 32/64 and 64/128. At 128/256 and 48/96 one probe
  misses tolerance. That is FP32 finite-difference noise: with a smaller step even
  32/64 misses a different probe.
- BF16 forward parity (max scaled error under 0.003) passes at E32/M64/H64, H256,
  E64/M128/H256, E128/M256/H512 and E128/M64/H128. E32/M256/H64 reports zero
  encoder gradients. This comes from the test's weight pattern: the fusion row stride
  808 × 0.731 ≈ 94·2π, so all fusion rows are nearly equal and die together. With
  another frequency every tensor gets nonzero gradients.
- Adapter, viewer input, fixed-team and private-eval boundary tests pass, locally and
  on the box. The private adapter was rebuilt.
- Smoke runs, 8.4M steps each:
  - Largest shape (512x3, 128/256, 16384 agents, horizon 128, minibatch 8192,
    magnet on): 14.3 GiB peak VRAM, about 42K steps/s. Metamon evaluated the 128/256
    checkpoint.
  - Default shape with the 13 peers and reverse annealed magnet: about 260K steps/s.
    Magnet weights end between the initial and final learner weights.
