# Migration notes

## File replacement

Replace the corresponding environment files with the files in this directory.
The old CUDA pair is replaced by four files:

```text
old puffer_survivors_cuda_single.cu  -> cuda/ps_cuda_sim.cu + cuda/ps_cuda_sim.cuh
old puffer_survivors_cuda_vec.cu     -> cuda/ps_cuda_vec.cu
old puffer_survivors_cuda_vec.h      -> cuda/ps_cuda_vec.h
```

Update the extension build so both new `.cu` files are compiled and linked.
`binding.c` already includes the new header path.

## C state/config access

Simulation settings moved from direct members such as:

```c
env->enemy_cap
env->player_speed
env->reward_kill
```

to:

```c
env->cfg.enemy_cap
env->cfg.player_speed
env->cfg.reward_kill
```

Code outside this folder that directly accesses configuration fields must be
updated accordingly.

## Observation v4 to v5

v4 had 336 inputs. v5 has 344 inputs. The first 26 inputs retain their old
positions. Eight boss inputs were inserted after them.

To expand a linear input weight matrix whose input dimension is the last axis:

```python
new_weight[..., :26] = old_weight[..., :26]
new_weight[..., 26:34] = 0
new_weight[..., 34:344] = old_weight[..., 26:336]
```

For a standard PyTorch `nn.Linear(336, hidden)`, weight shape is
`[hidden, 336]`, so the concrete mapping is:

```python
new_weight = old_weight.new_zeros(old_weight.shape[0], 344)
new_weight[:, :26] = old_weight[:, :26]
new_weight[:, 34:] = old_weight[:, 26:]
```

The bias is unchanged. Apply the same mapping to every model component that
projects the raw observation directly. Do not load a 336-input checkpoint into
a 344-input model without explicitly migrating or intentionally reinitializing
its first projection.

## Behavior changes that require retraining/evaluation

- Bosses now have explicit observations.
- Polar ring boundaries put more resolution near the player.
- Radial damage uses the enemy grid. Floating-point accumulation and enemy-hit
  order can differ from the old full-capacity scan.
- Enemy kind flags now reserve three kind bits:

```c
PS_ENEMY_KIND_MASK = 7
PS_ENEMY_ELITE_FLAG = 8
PS_ENEMY_BOSS_FLAG = 16
```

This supports up to eight base enemy kinds but changes the encoded elite/boss
flag values. Do not mix old serialized enemy `type` bytes with this build.

## Recommended rollout

1. Build and run the CPU smoke test.
2. Run several fixed-seed CPU evaluation episodes and inspect boss behavior.
3. Rebuild the model for 344 inputs or migrate its first projection.
4. Compile the two CUDA objects with `nvcc`.
5. Run CPU and CUDA with matched seeds/actions and compare aggregate metrics.
6. Start a short PPO run before committing to a long training job.
