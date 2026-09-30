# First learned WebNav family benchmark

`webnav_family` connects the ten checked `click` family tasks to the native
PufferLib trainer. The task state, generator, rewards and action transitions
remain in the stock CPU Bend family library. The C environment reads only the
family's public view and maps policy outputs to legal clicks or a wait. It has
17 actions (wait and 16 visible-control positions) and 1,480 float features:
bounded query bytes, per-node name/value bytes, role/state, exact text match
and geometry. An optional feature adds frozen Potion cosine similarity for
the public `Select words similar to ...` instruction and checkbox names. It
uses the existing native C text encoder and caches scores once per episode;
the task generator and transitions stay in Bend. This is an integration and
transfer benchmark, not the full DOM or general language interface.
The [measured checkpoint and per-task scores](RESULTS.md) cover both native
evaluation and pinned original Chromium pages.

Build the checked family first with its [guarded builder](../webnav/families/BUILD_SAFETY.md):

```sh
node ocean/webnav/families/build.cjs click --test
./build.sh webnav_family build/webnav_family_train --float
```

`CUDA_HOME=/usr/local/cuda` may be needed if `nvcc` is not on `PATH`. The
trainer binary has its own name so it does not replace `./puffer`.
The standalone CPU build is
`./build.sh webnav_family build/webnav_family_cpu --cpu`.

Run a short mixed-task learning check and evaluate on separate seeds:

```sh
build/webnav_family_train train --headless --train.total_timesteps=3000000
build/webnav_family_train eval latest --headless --base.eval_episodes=1000 \
    --env.seed_offset=1000000 --env.data_mode=1
bash ocean/webnav_family/eval_tasks.sh CHECKPOINT.bin 256
```

`env.task_mask` is a ten-bit mask in the order defined by the click family.
For example, `--env.task_mask=256` selects `click-checkboxes-large` alone.
`env.data_mode=0` uses the upstream training split; `1` uses its test split;
`2` mixes both at reset. The transfer tasks change their goal in mode 1.
**A policy trained with mode 2 has seen both modes**, so its mode-1 score is
useful for original-page transfer but is not an untouched MiniWoB transfer
benchmark. The default config mixes modes to expose the policy to goal changes.

An independent random-legal-click baseline and a direct original-Chromium
evaluator are included:

```sh
make -f ocean/webnav_family/Makefile test random browser-build
build/webnav_family/browser_eval CHECKPOINT.bin 20
WEBNAV_SAMPLE=1 build/webnav_family/browser_eval CHECKPOINT.bin 20
```

To test the optional semantic feature, first run
`make -C ocean/webnav text-test` to install the pinned tokenizer and frozen
weights, then train with `--env.semantic=1`. Evaluate the same checkpoint with
`bash ocean/webnav_family/eval_tasks.sh CHECKPOINT.bin 256 1` and
`WEBNAV_SEMANTIC=1 build/webnav_family/browser_eval CHECKPOINT.bin 100`.
The default remains `semantic=0` so older checkpoints retain their input
profile. `make -f ocean/webnav_family/Makefile semantic-probe` measures the
frozen vectors on 10,000 distinct-synonym six-option diagnostic trials; the
production feature does not read the original benchmark's synonym table.

The browser evaluator runs the pinned original HTML pages in Chromium, asks
their JavaScript to generate seeded instances, removes private goal flags
before serializing observations, and sends policy clicks over CDP. It reports
full-credit success separately for each task. It uses test mode and new
browser seeds. The native evaluator uses bounded Bend generators, so these
two results measure different distributions. Browser evaluation is headless;
the existing scripted browser oracles remain the conformance tests.

`test_env.c` checks policy observation, legal-action mask, reward/autoreset
plumbing, and both modes for every task in a mixed reset. All family code
generation must still use the guarded builder; these C-only targets do not
invoke Bend.
