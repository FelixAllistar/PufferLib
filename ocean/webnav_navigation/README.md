# Shared navigation learner

This native PufferLib adapter connects nine `panels` and two `menus` tasks to
PPO. Stock CPU Bend owns generation, transitions, clocks and rewards. It loads
the already checked family DSOs; C handles public features and action transport.

The interface has 4,104 float features and 129 discrete actions: wait, then one
action per visible node (up to 128). Nodes keep stable references across
visibility changes. On menus, activating a branch moves the pointer to open its
submenu; activating the public `Menu` button or a leaf clicks it. Panel actions
click. Each action advances the controlled clock by 250 ms.

Features are public roles, states, exact instruction/name and instruction/value
matches (exact case-sensitive quoted labels for links), stable references/parents and the agent's own visited-reference history.
History resets on terminal. There are no private goals, task IDs, seeds or
semantic embeddings in the policy input. Geometry is excluded because the
family views use ordinal positions. This is an exact-text baseline, not a
general natural-language encoder. The original-browser evaluator uses this
same projection and the actual original page generators, with real CDP input.
Animations/hover timers settle under the existing controlled-clock preset.

The small baseline is H64/L1 (283,264 parameters). Default training uses 128
agents and horizon 16. Configure `env.family=0` with task mask 511 for panels,
or `env.family=1` with task mask 3 for menus. Masks use family-local task IDs.
These are separate runs sharing an interface, not yet one mixed-family policy.

From the repository root, after root's serialized build permission:

```sh
make -f ocean/webnav_navigation/Makefile test browser-build build/webnav_navigation/native_eval
# Run the native trainer build under the documented resource guard:
flock -n build/webnav/families/resource.lock systemd-run --user --scope \
  -p MemoryMax=6442450944 -p MemorySwapMax=0 -p TasksMax=128 \
  -p CPUQuota=100% timeout 300 env CUDA_HOME=/usr/local/cuda \
  ./build.sh webnav_navigation build/webnav_navigation_train --float
build/webnav_navigation_train train --train.total_timesteps=3000000
build/webnav_navigation_train train --env.family=1 --env.task_mask=3 --train.total_timesteps=3000000
build/webnav_navigation/native_eval CHECKPOINT panels 100
build/webnav_navigation/browser_eval CHECKPOINT panels 20
# Replace CHECKPOINT with random for a legal-action random baseline.
# Both evaluators accept a final task-name argument.
```

Build family DSOs through `families/build.cjs`; do not compile Bend directly.
The browser executable defaults to the pinned headless Chromium shell. This
adapter does not yet provide a paced, headed watcher. Evaluation uses greedy
H64/L1 inference; training's sampled-action dashboard is a different metric.
Native evaluation uses seed stream 4,000,000 onward; original-browser seeds are
300,000 onward (panels) or 400,000 onward (menus). No evaluation score is used
as a training reward.

Results, checkpoint paths, projection revisions and measured transfer gaps are
in [RESULTS.md](RESULTS.md). `WEBNAV_SEED_OFFSET` changes browser evaluation
seeds; `WEBNAV_TRACE=1` prints public action diagnostics.
