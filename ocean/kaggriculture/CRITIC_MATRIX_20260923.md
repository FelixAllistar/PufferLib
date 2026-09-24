# Five-way ~20M comparison

User requested E be raw PPO with critic pretraining (not a no-pretraining
control), ~20M steps per branch, and the normal Puffer dashboard in remote tmux.

Remote window: `ssh_tmux:bc-matrix`. Output root:
`/workspace/PufferLib/qualification/critic_matrix_20260923/matrix_v2`.
`status.json` and the persistent pane-border title identify the current phase.
`FAILED.json` means the queue stopped; `COMPLETED.json` means all five runs and
their reset-free evaluations finished. Native PPO stdout inherits the tmux
terminal; `tmux pipe-pane` logs it without disabling its interactive dashboard.

| Branch | Actor initialization | Critic pretraining | eMAG |
| --- | --- | --- | --- |
| A | standard 518-game BC | no | off |
| B | same BC | no | on |
| C | same BC | head-only | off |
| D | same BC | same head-only checkpoint as C | on |
| E | random seed-42 actor, no BC | head-only | off |

BC source is `bc_expansion_20260923/remote_run.CPV5Ma/comparison/actor_uniform.bin`.
C/D share exactly one pretrained checkpoint. B/D explicitly use the unchanged
BC actor as their frozen eMAG reference: KL .01, tau 0, cutoff .134. E has no
actor imitation updates and no eMAG reference.

## Critic-only implementation and gates

`kag_bc bc.mode=critic` fits only the two matrices of the nonlinear scalar
value branch. Encoder, recurrent network, and both actor branches are frozen.
Actor CE is still reported, but its gradient is zeroed. Backward stops at the
decoder; no recurrent/encoder backward pass is needed. Only critic gradients
are copied/updated. This new mode uses bias-corrected Adam (.9/.999, epsilon
1e-8); existing `bc.mode=train` retains its momentum-SGD behavior.

Returns remain in original PPO reward units; the existing train-variance loss
normalization is retained. Full fitting is 20 epochs, LR .003, coefficient 1,
batch one full episode, immutable 442-train/76-validation split. The raw actor
is initialized and saved with zero actor-training epochs before value fitting.

Every critic checkpoint must have finite weights, byte-identical non-critic
parameters, identical actor logits on a real recurrent prefix, and better
held-out RMSE than the train-mean constant predictor with positive explained
variance. This establishes offline fitting, not on-policy calibration: targets
describe the expert continuation, and offline data is root-start trajectories.
PPO resets remain enabled and its critic continues updating normally.

The 64-game GPU smoke passed: RMSE 36.72 vs constant 51.05, EV .605. Exactly
33,025 non-padding value parameters changed; actor/encoder bytes and 24-step
actor logits matched. A zero-epoch actor-only checkpoint roundtrip was also
byte-identical. Two remote matrix configuration/gating unit tests passed.

Full BC critic fit: held-out RMSE 26.5933 vs constant 45.5505, EV .707761.
The first raw-critic fit failed the constant-predictor gate; `matrix_v1`
stopped before PPO. Continuing that value head for 20 epochs at LR .0003
yielded RMSE 45.4668 vs 45.5506, EV .00420242. This is a **very weak fit**,
not evidence of useful strategic value prediction. E keeps the raw encoder
and actor unchanged and tests this weak value initialization honestly; a
negative result cannot rule out representation-learning critic pretraining.
Both checkpoints are revalidated by `--prepared` before `matrix_v2` starts.
All failed and refined artifacts are preserved in `matrix_v1`; no retraining
of the successful BC critic is necessary.

The new BC executable is isolated at `ocean/kaggriculture/build/critic_matrix_20260923/kag_bc`.
Production `./puffer` is not rebuilt or replaced. Original BC source is backed
up at `qualification/critic_matrix_20260923/kag_bc.before.cu`. Dataset source
fingerprints still match because observations, labels, rewards and controller
semantics did not change; no stale-data override is used.

## Matched PPO protocol

- H256/L2, macro2/executor2, 2048 agents, horizon720, minibatch1440.
- 14 complete updates = **20,643,840 total environment-agent steps per branch**.
- One shared PPO seed42, base seed5/environment seed707: screening, not a
  replicated statistical result.
- Frozen existing rewards/gamma; training resets .8. Default user config is
  not modified. The actual run overrides are in `commands.jsonl` and native
  `.start.ini` files.
- Fixed LR .0003 and fixed entropy coefficient during this short screen,
  rather than compressing a long-run cosine cycle into 20M.
- Same external opponent checkpoint `1790058597151/0000000299335680.bin`
  for frozen banks, external probability1, no new snapshots; existing bank
  geometry and learner-mirror fraction are retained. No new league is added.
- Checkpoints every two updates. Separate reset-free deterministic rules-bot
  evaluations: 32 games per seat for each finished candidate.
- A two-update, full-geometry eMAG smoke precedes the matrix to catch memory
  or loading failures. Its weights are **not** reused in any candidate.
- No automatic promotion or edits to the user's default load path.

Entry point: `run_critic_matrix.py`, run directly inside remote tmux using uv.
Do not pipe this runner through tee: it deliberately requires a terminal.
