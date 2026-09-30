# Numeric family PPO adapter 

This adapter presents the already checked nine-task `numeric` Bend family to
native PufferLib PPO. The family DSO still generates instances and owns all
transitions, feedback, clocks and rewards. The adapter only loads rows, projects
`WFView`, chooses legal public actions, and remembers the agent's own clicks,
seen card values and pointer choices. It never reads private answer fields,
task IDs or seeds for policy features, and it does not compute math answers.

The policy observes 1,520 floats and chooses among 541 discrete actions.
The observation contains printable instruction characters, public numeric
tokens from instructions, problem text and feedback, visible node names,
values, roles, flags, geometry, cursor position, and prior public card reveals.
The action vocabulary includes wait; one click per visible node (up to 29);
select all, backspace, delete, left, right, home, end and Enter; fixed integer
text candidates from -99 through 108; single digits, minus and decimal point;
155 x choices, 126 y choices, then pointer move or click. Integer candidates
cover every result of the family's current math presets and the pinned original
math pages. They are a fixed action domain: no per-instance answer is inserted
or selected by C. The two coordinate choices are stored in the agent's history;
the hot/cold signal appears after a pointer move. A coordinate setting action
advances the family clock by 250 ms, as all actions do.

The default [config](../../config/webnav_numeric.ini) uses 128 agents, H64/L1,
horizon 16, and all nine tasks. `env.task_mask` is a nine-bit family-local mask.
The native evaluator accepts `CHECKPOINT|random EPISODES [TASK_NAME]` and reports
full-credit rate per task. The browser evaluator uses the pinned original pages
and CDP input, with the same policy projection. Its injected `browser.js`
exports only visible DOM nodes, instructions, deadline and terminal reward.
The terminal reward is used solely for evaluation output. Original pages use
their own random instances; no browser evaluation score becomes a training
reward. `WEBNAV_CHROME` can select the Chromium executable and
`WEBNAV_SEED_OFFSET` can change browser episode seeds.

Root integration and validation, from the repository root:

```sh
# The family is already checked; rebuild only through its serialized resource guard if needed.
node ocean/webnav/families/build.cjs numeric --test
make -f ocean/webnav_numeric/Makefile test browser-build build/webnav_numeric/native_eval
# Build the trainer under the same documented single-family resource guard.
flock -n build/webnav/families/resource.lock systemd-run --user --scope \
  -p MemoryMax=6442450944 -p MemorySwapMax=0 -p TasksMax=128 \
  -p CPUQuota=100% timeout 300 env CUDA_HOME=/usr/local/cuda \
  ./build.sh webnav_numeric build/webnav_numeric_train --float
build/webnav_numeric_train train --train.total_timesteps=1000000
build/webnav_numeric/native_eval random 100
build/webnav_numeric/native_eval CHECKPOINT 100
build/webnav_numeric/browser_eval CHECKPOINT 20
```

The native test covers public projection, action masks, number editing,
coordinate selection and pointer feedback, all nine DSO routes, timeout rewards,
and history clearing on autoreset. The worker handoff was source-only. Root subsequently compiled the adapter and
trainer, passed these tests, and ran short training and native/browser evaluations;
see [RESULTS.md](RESULTS.md). These baselines do not establish solved tasks.

The `number-checkboxes` target glyph is an image in the original page and is
not represented as pixels in `WFView`; the public instruction digit remains
visible, so a policy would have to learn the ten digit-to-checkbox patterns.
The hot/cold interface supports every integer coordinate accepted by the
family, but exploration consumes two coordinate actions plus a pointer move
per probe, and the page ends after 15 seconds. Browser pointer events are
physical CDP events at the chosen page coordinates. Coordinates outside the
original canvas can miss the element, even though the bounded Bend family
accepts them. The original-page evaluator and PPO performance need root-side
validation before any transfer or learning claim.

Current root validation and measured learned results: [RESULTS.md](RESULTS.md).

Parsed numeric features are clipped to [-1000,1000] before normalization by
100, including public feedback from oversized guesses. This avoids extreme
feature magnitudes without changing Bend state or reward semantics.
