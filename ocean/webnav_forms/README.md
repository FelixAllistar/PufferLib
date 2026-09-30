# Trainable forms family

This adapter trains PufferLib's native PPO policy on the eight checked stock-CPU-Bend forms tasks: `enter-text-dynamic`, `enter-text-2`, `enter-password`, `text-transform`, `copy-paste`, `copy-paste-2`, `read-table-2`, and `login-user-popup`. Bend generates instances and applies every task transition. C reads the public family view, builds a 34-action catalog, and routes selected clicks, Select All, waits, and text insertions through the family ABI. The catalog's text comes only from quoted instructions and visible control/table values. The policy never receives private goal strings, row words, or seed data.

The 2,768-float observation contains the instruction, up to 16 public nodes, and up to 16 candidate texts with their visible source labels. The text actions are a high-level interface: one action inserts an entire selected candidate through `WF_INSERT` or Chromium `Input.insertText`. Scores from this interface should not be compared with raw-keystroke agents without noting that difference.

`data_mode=0` is the original two-empty-field table task. `data_mode=1` pre-fills one correct table field for a Bend training curriculum, and `data_mode=2` mixes the two modes. `progress_reward=1` adds an optional potential-based PPO reward derived from public table cells and visible field values, using the configured `gamma=0.95`; it leaves Bend's raw score unchanged. Evaluation uses `data_mode=0` and `progress_reward=0`.

From the repository root:

```sh
node ocean/webnav/families/build.cjs forms --test
make -f ocean/webnav_forms/Makefile test browser-build
./build.sh webnav_forms build/webnav_forms_train --float
build/webnav_forms_train train --env.task_mask=255 --env.data_mode=2 --env.progress_reward=1
bash ocean/webnav_forms/eval_tasks.sh CHECKPOINT 256
build/webnav_forms/browser_eval CHECKPOINT 100
```

Run Bend builds only through the guarded builder; it enforces the documented 6 GiB, no-swap limit. The original-browser evaluator uses the pinned MiniWoB++ pages, controlled logical time, public DOM values, and CDP input. It removes the fixture's private `goal` and `popup_mode` fields before CDP serializes each observation. It is an external page check for these bounded tasks, not a full DOM/accessibility or live-website benchmark.
