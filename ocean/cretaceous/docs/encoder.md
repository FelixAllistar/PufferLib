# Cretaceous policy encoder

`cretaceous_encoder.cu` is a structured encoder for the policy contract in `sim.h` (contract 3, 197 floats).
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
obs (197) ─┬─ self [0:39) ─────────── [x,1] 40 ─ FC 64 ─ ReLU ─ FC 64 ─ ReLU ─ self_e (64) ─────────────┐
           │                                                                    └─ FC 64 (linear) ─ q   │
           ├─ species one-hot [16:22) ─ [x,1] 8 ─ FC 16 (linear) ─ sp (16) ───────────────────────────┐ │
           │                                                      │                                   │ │
           ├─ patch [39:137) 7x7x{height, water}                  │                                   │ │
           │     conv3x3 2→16 valid, ReLU (5x5) ─ conv3x3 16→32 valid, ReLU (3x3)                     │ │
           │     ─ flatten 288 ─ FC 64 ─ ReLU ─ patch_e (64) ─────────────────────────────────────────│─┤
           │                                                      ▼                                   │ │
           └─ neighbours [137:197) 6 x 10 ─ per slot [nb 10, sp 16, 1] 32 ─ FC 64 ─ ReLU ─ FC 64 ─ ReLU ─ h_n
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
| self1, self2 | 64 × 40, 64 × 72 | 2,560 + 4,608 |
| query | 64 × 72 | 4,608 |
| conv1, conv2 | 16 × 24, 32 × 152 | 384 + 4,864 |
| patch_fc | 64 × 296 | 18,944 |
| neighbour1, neighbour2 | 64 × 32, 64 × 72 | 2,048 + 4,608 |
| trunk1 | 256 × 280 | 71,680 |
| trunk2 | hidden × 264 | 67,584 (256) / 270,336 (1024) |

| hidden | Structured encoder | Default linear encoder | Whole policy, structured (2 layers) | Whole policy, default |
|---|---|---|---|---|
| 256 | 182,016 | 50,432 | 580,352 | 448,768 |
| 1024 | 384,768 | 201,728 | 6,696,704 | 6,513,664 |

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
- that CUDA graph capture and replay of forward + backward work.

It then writes a dump that `tools/encoder_reference.py` checks:
- a float64 NumPy forward, written from this document rather than from the kernels;
- a PyTorch autograd version of the same forward, whose gradients are compared with every element of every CUDA weight gradient.

Results on the local GTX 1060 (CUDA 12.6), 2026-11-07:
- **fp32:** the forward matches NumPy to 9.5e-8 and every weight gradient matches float64 autograd to at most 9.5e-8 absolute, element by element.
- **bf16** (the trainer's precision): the forward is within 1.8e-3 of the float64 reference. Each gradient tensor has a relative L2 error of 0.5–8.4% and a cosine of at least 0.996.
  The worst is neighbour1, whose gradient passes through the softmax backward and four bf16-stored intermediates.
  The tool's bf16 bar is a relative error under 0.12 and a cosine over 0.99.
- `./build.sh cretaceous` builds the real trainer with the hook, and it trains with `--policy.cretaceous_net=1`; see the commit message for the smoke runs.

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
- **No CPU inference path.** The player and server do not run policies today. Running this net outside the trainer would need a CPU port; `mario_sim/encoder_cpu.h` is the model to follow, and the NumPy reference is a spec for it.
- **Speed.** The encoder costs about 5x the default's FLOPs per row; its largest GEMMs are the two trunk layers and conv2.
  On the local GTX 1060 (Pascal: no native bf16, no tensor cores), with the default config (4096 agents, 256x2), the rollout "Model" time was 9.6 s vs 0.9 s.
  Wall-clock to 5.8M steps was 89 s vs 56 s.
  Re-measure on a 5070 Ti before drawing conclusions.
  Nothing is fused: the pooling kernels use one thread per (row, head), and conv1 is a GEMM with N = 16.
- **bf16 attention gradients** are the noisiest (8% relative). If attention training looks unstable, keep `d_h` and `d_e1a` in fp32 (`Float` buffers) first.
