# Cretaceous policy encoder

`cretaceous_encoder.cu` is a structured encoder for the policy contract in `sim.h` (contract 4, 243 floats).
Contract 4 (progression) appends a self block (22 floats at [197:219)) and 4 flags per neighbour ([219:243)); the self MLP and the per-slot neighbour MLP take them with their contract 3 features (2026-11-08), so contract 3 encoder checkpoints do not load.
It replaces PufferLib's default encoder, which is one bias-free linear layer from the obs to the hidden size.
The MinGRU core and the decoder are unchanged.

Turn it on with `policy.cretaceous_net = 1` in `config/cretaceous.ini` or `--policy.cretaceous_net=1` on the command line.
The default, `0`, keeps the linear encoder, so checkpoints from before 2026-11-07 still load.
A checkpoint only loads into the encoder it was trained with: the trainer rejects a file of the wrong size.

The flag is read through the trainer's `PUF_CONFIGURE` hook (train, eval and match), which the encoder file defines.
`src/ocean.cu` only includes the file and dispatches to it under `PUFFER_CRETACEOUS`.
`build.sh` already defines `PUFFER_CRETACEOUS`, so one binary serves both encoders.

## Architecture

```
obs (243) ─┬─ self [0:39)+[197:219) ─ [x,1] 64 ─ FC 64 ─ ReLU ─ FC 64 ─ ReLU ─ self_e (64) ─────────────┐
           │                                                                    └─ FC 64 (linear) ─ q   │
           ├─ species one-hot [16:22) ─ [x,1] 8 ─ FC 16 (linear) ─ sp (16) ───────────────────────────┐ │
           │                                                      │                                   │ │
           ├─ patch [39:137) 7x7x{height, water}                  │                                   │ │
           │     conv3x3 2→16 valid, ReLU (5x5) ─ conv3x3 16→32 valid, ReLU (3x3)                     │ │
           │     ─ flatten 288 ─ FC 64 ─ ReLU ─ patch_e (64) ─────────────────────────────────────────│─┤
           │                                                      ▼                                   │ │
           └─ neighbours [137:197)+[219:243) 6 x (10+4) ─ per slot [nb 14, sp 16, 1] 32 ─ FC 64 ─ ReLU ─ FC 64 ─ ReLU ─ h_n
                    mask_n = present flag (feature 0)                                                 │ │
                    4-head attention pool over present n, query q, keys = values = h_n ─ attn (64) ───│─┤
                    max pool over present n ─────────────────────────────────────────────── max (64) ─│─┤
                                                                                                      ▼ ▼
           cat [self_e 64 | patch_e 64 | attn 64 | max 64 | sp 16] = 272 ─ [x,1] 280
               ─ FC 256 ─ ReLU ─ FC hidden ─ ReLU ─► MinGRU (num_layers) ─► decoder (19 logits + value)
```

Implementation notes:
- Every FC has a bias. Its input carries a constant 1 column, padded to a multiple of 8 (`CE_AUG`), so the bias is a column of the weight matrix.
  Muon then updates it together with the matrix, as `pokemon3_encoder.cu` does.
  It also keeps every parameter's size a multiple of 8, which the flat bf16 param and grad buffers need.
- The convs are im2col + GEMM, as in `chess_encoder.cu`; the column order is (ky, kx, channel).
  The patch rows are forward and the columns are lateral (`CRET_OBS_PATCH` in `sim.h`).
- Attention: the score is q_g · h_{n,g} / 4 per head (16 dims each), with a masked softmax over present slots.
  When no neighbour is present, the attention and max outputs are 0.
- Backward is hand-written, because the 5.0 encoder API has no autograd.
  It overwrites the dW buffers each step, registered in parameter order.
  The softmax and the attention weights are recomputed in the backward rather than stored.
  Max-pool ties go to the first slot.
- Init: kaiming uniform with gain √2 (ReLU layers) and 1 (species and query), like the default encoder.
- Obs are used as the sim scales them, with no normalisation layer, as the other encoders do; see the audit below.

### Parameter counts

| Tensor | Shape (out × in incl. bias, padded) | Params |
|---|---|---|
| species | 16 × 8 | 128 |
| self1, self2 | 64 × 64, 64 × 72 | 4,096 + 4,608 |
| query | 64 × 72 | 4,608 |
| conv1, conv2 | 16 × 24, 32 × 152 | 384 + 4,864 |
| patch_fc | 64 × 296 | 18,944 |
| neighbour1, neighbour2 | 64 × 32, 64 × 72 | 2,048 + 4,608 |
| trunk1 | 256 × 280 | 71,680 |
| trunk2 | hidden × 264 | 67,584 (256) / 270,336 (1024) |

| hidden | Structured encoder | Default linear encoder | Whole policy, structured (2 layers) | Whole policy, default |
|---|---|---|---|---|
| 256 | 183,552 | 62,208 | 581,888 | 460,544 |
| 1024 | 386,304 | 248,832 | 6,698,240 | 6,560,768 |

At 1024 the encoder is about 6% of the policy; the MinGRU (2 × 3H × H) dominates.
`test_encoder` prints these numbers from the real registration code.

## Why each piece is there

- **Self MLP.** The 39 self features mix scales and meanings: heading, velocity, survival meters, flags and egocentric offsets to the carcass and rally point.
  Two ReLU layers can form conjunctions such as "hungry and a carcass is near" before the recurrent core.
  The default encoder can only sum the features linearly.
- **Species embedding.** It is fed into every neighbour's input, so each species reads the same neighbour differently.
  For example, a carnivore 2 size steps larger is a threat to a gallimimus and a non-event to a tyrannosaur.
  The embedding also enters the trunk.
  On its own, a linear embedding of a one-hot equals a linear layer on the one-hot (the self MLP already sees the one-hot).
  What it adds is one shared 16-d species code that the self path and every neighbour use.
- **Patch conv.** The 7×7 patch is an image: slope, cliffs and shoreline are local patterns that matter wherever they appear around the animal.
  Two valid 3×3 convs (receptive field 5×5 cells = 30 m) share weights across positions.
  The FC after them keeps where things are (ahead or behind, left or right), which steering needs.
- **Neighbour set encoder.** The 6 slots are sorted by distance, but which animal sits in which slot changes every tick.
  A shared per-entity MLP and a permutation-invariant pool learn one "how to read an animal" function.
  The default encoder instead learns 6 separate copies, and the mask stops empty slots (all zeros) from looking like animals at distance 0.
  - **Attention pool**, with the query from the agent's own state: the agent picks which animal matters given its own condition, such as the predator when injured or the packmate when calling.
    Four heads let it attend to a threat and to food at the same time.
  - **Max pool** keeps per-feature extremes, such as "some carnivore is very close", which a softmax average can dilute.
    It is also the robust fallback while the attention is still untrained.
- **Trunk 272 → 256 → hidden** mixes the branches nonlinearly before the MinGRU, so the recurrent core sees features rather than raw obs.

## Tests

From `ocean/cretaceous`:

```bash
make encoder-test PYTHON=../../../venv/bin/python   # RAYLIB=... if raylib-5.5_linux_amd64 is not in the repo root
```

The target builds `build/cretaceous/test_encoder` (fp32) and `test_encoder_bf16`.
It includes the whole trainer (`src/pufferl.cu`) with `minimal.h` as the env header and `sim.h` for the layout constants, so it links no world, sim or Box3D.

The CUDA test checks:
- the parameter counts, and that the rest of the policy layout is unchanged;
- that the train and rollout activations give the same forward;
- that reversing the neighbour slots does not change the output (permutation invariance);
- that garbage in empty slots changes nothing;
- that each branch reaches the output and the backward reaches every weight;
- in fp32, 132 finite-difference gradient checks (12 elements in each of the 11 weight tensors);
  where a ReLU or argmax switch falls inside ±eps, the analytic gradient must match one side of the kink;
  up to 2 samples within 5% are reported, not failed, when kinks within 1e-4 on both sides fool every candidate (contract 4: self2[1485], exact at eps 1e-5 in float64);
- that CUDA graph capture and replay of forward + backward work.

It then writes a dump that `tools/encoder_reference.py` checks:
- a float64 NumPy forward, written from this document rather than from the kernels;
- a PyTorch autograd version of the same forward, whose gradients are compared with every element of every CUDA weight gradient.

Results on the local GTX 1060 (CUDA 12.6), 2026-11-07:
- **fp32:** the forward matches NumPy to 9.5e-8 and every weight gradient matches float64 autograd to at most 9.5e-8 absolute, element by element.
- **bf16** (the trainer's precision): the forward is within 1.8e-3 of the float64 reference. Each gradient tensor has a relative L2 error of 0.5–8.4% and a cosine of at least 0.996.
  The worst is neighbour1, whose gradient passes through the softmax backward and four bf16-stored intermediates.
  The tool's bf16 bar is a relative error under 0.12 and a cosine over 0.99.
- Contract 4 (2026-11-08): fp32 gradients match float64 autograd to 1.6e-7; bf16 forward within 1.6e-3, gradients 1.1–7.3% relative L2, cosine at least 0.997.
- `./build.sh cretaceous` builds the real trainer with the hook, and it trains with `--policy.cretaceous_net=1`; see the commit message for the smoke runs.

## CPU inference (`policy_cpu.c`)

`policy_cpu.[ch]` runs a trained checkpoint without the trainer or a GPU: plain C at `-O2`, no BLAS, for either encoder.
The player (`--policy`, README "Play against a trained policy"), the dedicated server and `cret_eval` (`cpu:FILE`) use it.

- **Checkpoint.** The trainer writes the fp32 master weights flat (`puf_save_weights` in `src/pufferl.cu`), tensors back to back in the order `weights_create` registers them (`src/algo.cu`): the encoder, the decoder ((28 logits + value) × hidden), then each MinGRU layer (3·hidden × hidden).
  The encoder is hidden × 243 for `cretaceous_net = 0`, or the 11 tensors of the table above for `1`.
  With hidden a multiple of 8 every tensor is a multiple of 8 floats, so the allocator's 16-byte alignment adds no padding; other hidden sizes are refused.
- **Sizes.** The file carries no header, so the sizes are found from its length: exactly one (encoder, hidden, layers) must fit it.
  `PATH:NET:HIDDEN:LAYERS` names them instead.
  A file of another contract, encoder or size is refused with both counts, for example (sizes named) `holds 448768 floats; contract 5 (obs 243, actions 28) with cretaceous_net 0, hidden 256 x 2 layers needs 462848` for a contract 3 checkpoint.
- **Step.** The observations of all driven agents go through as one batch: encoder, MinGRU (the trainer's `mingru_gate`: stable sigmoid, its `lerp`, highway output), decoder.
  The recurrent state is kept per slot and cleared when the slot's animal dies or starts a new life, as the trainer clears it on terminals.
  Actions are sampled per head as `sample_logits` does (the first action whose cumulative probability passes a uniform draw), with the sim's xorshift generator, or argmax with `--policy-greedy` / `cpu-argmax:`.
  The player and the server draw from `sim->rng`, as the bots do; `cret_eval` gives each policy its own stream so the sim's stream is the bots' alone.
- **Kernel.** One dense routine (`pc_mm`): four agents share each weight load and every dot product sums 8 lanes in a fixed order, which the compiler vectorises without `-ffast-math`.
  Layers of 2M multiply-adds or more split their output rows over OpenMP threads; each output is computed the same way on any thread, so results are bit-identical for any thread count (`test_policy` checks 1 against 4).
  Below that size the fork and join cost more than they save.
- **Correctness** (`tests/test_policy.c`, in `make test`; `tools/policy_reference.py` writes the fixtures).
  The reference builds a whole policy in float64 NumPy: `encoder_reference.forward` for the structured encoder, plus the linear encoder, the MinGRU, the decoder and the sampling.
  Its weights come from the sim's xorshift generator, which the C test reproduces bit for bit, so a fixture stores only the seed, the tensor shapes, the observations, the resets and the expected outputs (two fixtures of 41 KB each, hidden 32 × 2, 8 agents × 4 steps, two agents reset mid-sequence, one agent with no neighbour and one with all six).
  Max abs error of the C path against float64 NumPy, encoder output / logits + value:

  | Encoder | hidden 32 (stored fixture) | hidden 256, 16 agents | hidden 1024, 16 agents |
  |---|---|---|---|
  | linear (0) | 6.0e-7 / 7.2e-7 | 6.0e-7 / 7.2e-7 | 7.2e-7 / 1.2e-6 |
  | structured (1) | 5.7e-7 / 3.6e-7 | 7.2e-7 / 6.0e-7 | 8.3e-7 / 9.5e-7 |

  Every greedy and sampled action agrees with the reference (767 or 768 per run; draws within 1e-4 of a boundary are left unchecked, at most one per run).
  The test's bar is 2e-4 + 1e-4 · |reference|.
  The linear path also matches PufferNet (`src/puffercpu.c`, the CPU forward of `./puffer eval`) to 1.2e-6 over 4 recurrent steps with terminals.
  The larger checks: `python3 ocean/cretaceous/tools/policy_reference.py --net 1 --hidden 1024 --batch 16 --out F` then `build/cretaceous/test/test_policy F`.
- **Speed** (`build/cretaceous/test/test_policy --bench --runs 9`; the cloud sandbox: 4 cores of a Xeon @ 2.1 GHz, AVX2, clang `-O2 -march=native`, 2 layers, `OMP_WAIT_POLICY=PASSIVE`).
  Milliseconds per step for the whole batch, median over 9 runs (min-max), and per agent:

  | Encoder | hidden | agents | 4 threads | per agent | 1 thread | per agent |
  |---|---|---|---|---|---|---|
  | linear (0) | 256 | 1 | 0.048 (0.047-0.054) | 0.048 | 0.045 (0.044-0.048) | 0.045 |
  | linear (0) | 256 | 8 | 0.190 (0.186-0.197) | 0.024 | 0.194 (0.188-0.208) | 0.024 |
  | linear (0) | 256 | 64 | 1.35 (1.09-1.55) | 0.021 | 1.59 (1.56-1.63) | 0.025 |
  | linear (0) | 1024 | 1 | 0.67 (0.54-0.72) | 0.67 | 1.02 (0.99-1.24) | 1.02 |
  | linear (0) | 1024 | 8 | 1.29 (1.23-1.42) | 0.16 | 2.94 (2.30-3.14) | 0.37 |
  | linear (0) | 1024 | 64 | 8.0 (7.2-10.3) | 0.13 | 29.4 (22.9-29.9) | 0.46 |
  | structured (1) | 256 | 1 | 0.106 (0.100-0.114) | 0.106 | 0.109 (0.107-0.112) | 0.109 |
  | structured (1) | 256 | 8 | 0.421 (0.415-0.433) | 0.053 | 0.436 (0.433-0.458) | 0.055 |
  | structured (1) | 256 | 64 | 2.06 (1.85-2.81) | 0.032 | 3.36 (3.29-3.67) | 0.053 |
  | structured (1) | 1024 | 1 | 0.55 (0.55-0.59) | 0.55 | 1.44 (1.23-1.46) | 1.44 |
  | structured (1) | 1024 | 8 | 1.38 (1.29-1.72) | 0.17 | 2.85 (2.72-4.28) | 0.36 |
  | structured (1) | 1024 | 64 | 9.8 (8.0-10.6) | 0.15 | 30.9 (28.3-33.0) | 0.48 |

  The sandbox is shared: an earlier session of the same build measured 21.3 ms (one thread) for linear 1024 × 64, so read ±30% between runs.
  At 1024 the MinGRU (2 × 3H × H) is most of the cost and the structured encoder adds little; at 256 the structured encoder costs about twice the linear policy per agent.
  One agent at 1024 streams 26 MB of weights per step and is bound by memory bandwidth; from 8 agents on it is bound by arithmetic (one thread at 64 agents reaches 39 GFLOP/s at best, about 60% of the core's AVX2 FMA peak at 2.1 GHz).
  For the game: the island's 48 animals at 256 × 2 cost about 1 ms (linear) or 1.6 ms (structured) of the 33 ms tick on 4 threads; at 1024 × 2, about 6-8 ms.
  `cret_eval` on the test world, 16 slots on one random linear 256 × 2 checkpoint for 3000 ticks: 21.5 s through PufferNet (`net:`, 0.44 ms per agent step) against 1.2 s through this path (`cpu:`), and 0.25 s with bots; the survival and damage columns agree within sampling noise.

## Ablations to try

All at hidden 256, `num_layers = 2`, `replay_ratio = 1`, one run at a time.
Compare `perf` (survival), `food`, `kills` and SPS against `cretaceous_net = 0`.
1. **Pooling vs attention:** zero the attention output (skip `ce_pool_fwd`'s attention half), then the max half.
   Expect the max pool alone to be close early, with attention helping once herds and packs matter (`group` reward, calls).
2. **Mean pool** as a third option: it is cheap and counts neighbours, which max cannot.
3. **Patch conv vs MLP:** replace conv1 + conv2 with an FC 98 → 64 over the flat patch.
   The patch is small and egocentric, so an MLP may match the conv; the conv should generalise better across islands (`world_seed`).
4. **Species embedding:** drop it from the neighbour input (the self one-hot stays).
   This tests whether per-species reading of neighbours matters or the trunk can learn the conditioning.
5. **Attention query:** use the self obs directly instead of `self_e`, or add the species embedding to the query.
6. **Carcass and rally as entities:** they are now 3 floats each in the self obs.
   Embedding them as extra entity slots (type flag + offset) would let the attention weigh food against threats.
7. **Width:** CE_D 32 vs 64 vs 128.
   This is the branch width, not the hidden size, which stays fixed (256 to test, 1024 for good runs).

## Obs scaling audit (contract 3, `cret_sim_observe` in `sim.c`)

Most features are already in [-1, 1] or [0, 1], or clamped to [-2, 2].
The ones that are not, or that are poorly conditioned:

| Feature (index) | Current scaling | Issue | Suggested fix |
|---|---|---|---|
| Neighbour offset dx, dz (n·10+1, +2) | /100 m, clamped ±2 | The 6 nearest of 16 animals on an 8 km island are often more than 200 m away. Both components then saturate at ±2, so bearing and range are lost, and a far animal still counts as present | Cut neighbours off at a range (e.g. 150 m: present = 0 beyond it). Add log-range `log1p(d/10)/3` and unit bearing (dx/d, dz/d) |
| Neighbour heading `cos(Δyaw)` (n·10+4) | cos only | Facing left and facing right look the same, so the agent cannot tell whether a predator is turning toward it | Add `sin(Δyaw)` (contract bump) |
| Neighbour identity | diet + same-species flag | All herbivores look alike: a stegosaur's tail and a sauropelta's spikes look like a harmless gallimimus | Species one-hot (6) per neighbour (contract bump); the encoder's entity MLP takes it without other changes |
| Neighbour motion | none | No closing speed, so the agent cannot tell a charge from a passer-by | Relative velocity in the agent frame, /10 |
| Terrain height `g/500` (7) | metres / 500 | Its range depends on the island's relief. Coastal land sits near 0, so the feature barely varies for most agents | Measure the height distribution of a baked world. If 95% is under 100 m, use g/100 clamped to [0, 2], or drop the feature: the patch already gives relative height |
| Water depth `(wl-g)/5` (8), patch water `(wa-h)/3` | clamped (-1, 2) | Dry land reads TERRA_DRY = -1e4, so most cells sit at -1. In range, but "dry" and "5 m above a lake" look the same | Fine for a ReLU net. Optionally add a separate wet/dry bit |
| Hit flinch `hurt_ticks/12` (33) | 0..2 | A headbutt stagger reads 2, other hits 1 | Fine; /24 would put it in [0, 1] |
| Bleeding `bleed/max_hp*100` (27) | clamped [0, 2] | It is a rate and often saturates on big hits | Fine. `log1p` scaling would keep small bleeds visible |
| Absolute position x, z (5, 6) | [-1, 1] | In range, but it invites memorising one island (`world_seed = 1`) | Keep while training on one world; drop or randomise when generalising across seeds |
| Carcass (30..32) | present + offset /60 within 60 m | In range. There is no meat amount, so a nearly eaten carcass looks like a fresh one | Add meat / max_meat |
| Group size `/8` (35) | 0..1 | Fine | - |
| Rally offset (37, 38) | /100, clamped ±2 | It saturates beyond 200 m, like the neighbour offsets | Log range + bearing |

None of these is changed here, since `sim.c` belongs to other agents.
All the "contract bump" items should land together in one contract 4 change.
Update the `static_assert`s in `cretaceous_encoder.cu` with it; they fail the build on any layout change.

## Open issues

- **Not trained yet.** Only the integration has been smoke-tested (see the commit message for the local check). Whether it beats the linear encoder at equal steps is an open question; run ablation 0 (`cretaceous_net` 0 vs 1) first.
- **CPU inference** (closed 2026-11-15): `policy_cpu.c` runs both encoders, the MinGRU and the decoder on the CPU for the player, the server and `cret_eval`; see "CPU inference" above for the checks and the timings. It runs fp32; a bf16-trained policy is evaluated on its fp32 master weights, as `./puffer eval` does.
- **Speed.** The encoder costs about 5x the default's FLOPs per row; its largest GEMMs are the two trunk layers and conv2.
  On the local GTX 1060 (Pascal: no native bf16, no tensor cores), with the default config (4096 agents, 256x2), the rollout "Model" time was 9.6 s vs 0.9 s.
  Wall-clock to 5.8M steps was 89 s vs 56 s.
  Re-measure on a 5070 Ti before drawing conclusions.
  Nothing is fused: the pooling kernels use one thread per (row, head), and conv1 is a GEMM with N = 16.
- **bf16 attention gradients** are the noisiest (8% relative). If attention training looks unstable, keep `d_h` and `d_e1a` in fp32 (`Float` buffers) first.
