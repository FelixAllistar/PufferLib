# WebNav

The current `webnav_dom` profile trains across **twelve bounded MiniWoB task ports**, with CPU Bend generators and transitions, public text/node observations, and native C/CUDA policy training. See [training, evaluation and watching](TRAINING.md). This is 12 of 125 registered task names with partial coverage, not full MiniWoB++ parity. Original-browser policy evaluation currently supports five of the twelve tasks.

The sections below describe the **older three-task `webnav` pilot**, whose commands and checkpoints are separate.

WebNav is a three-task browser-interaction pilot with its environment written in **stock Bend 2.0.6, compiled for CPU**. PufferLib supplies native C/CUDA policy training. A separate C runner controls Chromium through CDP pipes and checks the same tasks with real mouse and keyboard events. No Python, Selenium, Bend GPU execution, or local Bend compiler fork is required.

The current tasks are letter-button selection, checkbox selection plus submit, and two-character text entry plus submit. This is a deliberately restricted foundation inspired by MiniWoB. It is not a port of the original MiniWoB task suite or a general browser simulator.

Development: [TODO and parity milestones](TODO.md), [Bend law audit](BEND_AUDIT.md), and [frozen text encoder investigation](TEXT_ENCODER.md). The next priority is original-task parity, with encoder measurements alongside it.

The newer implementation adds a separate [original-MiniWoB conformance lane](miniwob/README.md), [DOM-v2 contract](DOM_V2.md), and native Unicode text encoding/caches. Independent [checkbox/radio generators](miniwob/GENERATORS.md) now run in CPU Bend and export public form records. Twelve original task models pass matched-instance browser comparisons under limited presets. They now have a separate `webnav_dom` trainer/checkpoint; the older pilot remains separate. Run `make -f ocean/webnav/Makefile test-miniwob`, `test-text-forms`, `test-dom`, or `test-text` to exercise those components. Measurements are in [ITERATION_RESULTS.json](ITERATION_RESULTS.json); [site capture instructions](SITE_DATA.md) describe the initial data tooling.

To watch your most recently saved policy, from the repository root:

```sh
bash ocean/webnav/run.sh watch
```

This opens a visible Chrome window for 20 episodes, pauses between actions, and displays the action and success/failure beside the controls. It selects the newest checkpoint by modification time under `build/webnav/checkpoints/webnav/` and prints its path. It downloads full Chrome on first use; the original headless shell cannot show a window. On WSL this requires WSLg (or an X11 display). The policy still receives only the normalized task DOM, not the viewer panel or pixels.

```sh
# Score your latest checkpoint on 1,000 real-browser episodes, without a window.
bash ocean/webnav/run.sh eval
# Fast evaluation in the Bend simulator.
bash ocean/webnav/run.sh native
# Watch a particular checkpoint for 50 episodes.
bash ocean/webnav/run.sh watch path/to/checkpoint.bin 50
```

There is one PufferLib environment, `webnav`, mixing three tasks on reset:

| Task | Example instruction | What the agent must do |
| --- | --- | --- |
| Buttons | `Click c` | Click the button labeled c among four letter buttons. |
| Checkboxes | `Check ac` | Select exactly a and c, then Submit. |
| Text entry | `Type bd` | Focus the input, type b then d, then Submit or Enter. |

Targets and letter order vary. Each episode has at most 16 actions, with +1 for success and -1 for failure. Actions include clicking a control, typing a–d, Backspace, Tab, Enter, and waiting. No shopping sites, navigation workflows, or original MiniWoB tasks are included yet.

Training runs these widgets as Bend state transitions, with no browser running. Browser evaluation reconstructs the same task using real HTML controls and feeds actual DOM observations to the saved policy. The separate `build/webnav/puffer` executable keeps this experiment from replacing the normal repository `./puffer`. Its generic render hook is empty; use `run.sh watch` for visible evaluation. The training dashboard's `Evaluate` column is rollout/inference time, not a separate browser evaluation. Its `perf`/`score` is a rolling training success fraction; terminal evaluation below uses deterministic masked argmax.

Run these commands from the PufferLib repository root:

```sh
# Compile stock Bend, check its proof, exercise the C ABI and PufferLib adapter.
make -f ocean/webnav/Makefile test

# Download a version/checksum-pinned Linux Chromium into build/webnav/.
make -f ocean/webnav/Makefile setup-browser

# Real-browser differential checks, including keyboard/editing edge cases.
make -f ocean/webnav/Makefile browser

# Build a separate trainer and run the configured 15M-step experiment.
# The environment stays on CPU; policy inference and training use CUDA.
make -f ocean/webnav/Makefile train

# Evaluate a saved float32 checkpoint in either backend.
build/webnav/evaluate native 1000 path/to/checkpoint.bin
build/webnav/evaluate browser 1000 path/to/checkpoint.bin

# Check CPU evaluation logits/actions against CUDA inference.
make -f ocean/webnav/Makefile test-policy CHECKPOINT=path/to/checkpoint.bin
```

The local pilot checkpoint is [build/webnav/pilot.bin](../../build/webnav/pilot.bin). Evaluation uses masked argmax and clears recurrent state after each episode. The evaluator defaults to hidden size 128, two recurrent layers, and seeds 100000..100999 for a 1,000-episode run. Its optional arguments are documented by running it without arguments. `WEBNAV_TRACE=1` prints actions; `WEBNAV_CHROME` overrides the Chromium executable. All generated binaries, browser downloads, checkpoints, and raw logs are under ignored `build/webnav/`.

Dependencies: installed stock Bend 2.0.6 (default `~/.bend/bin/bend`, override with `BEND_BIN`), its Bun dependency, Clang 19, POSIX threads, and standard shell tools. Browser setup additionally uses curl/unzip and Chromium's usual Linux shared libraries. Only training and the CPU/CUDA parity test require CUDA and PufferLib's GPU dependencies. The parity test defaults to `CUDA_ARCH=sm_61`; override for another GPU. Use a fresh environment to reproduce training; do not load checkpoints from a different observation contract.

The first measured checkpoint trained for 14,974,976 transitions in about 97 seconds on the local GTX 1060 3GB system. On 1,000 evaluation seeds, it produced the same result in native Bend and Chromium:

| Task | Successes | Episodes | Success rate |
| --- | ---: | ---: | ---: |
| Buttons | 332 | 332 | 100% |
| Checkboxes | 180 | 327 | 55.0% |
| Text entry | 341 | 341 | 100% |
| Total | 853 | 1,000 | 85.3% |

Every observation and terminal outcome matched during those 4,865 browser actions. These are evaluation seeds from the same task templates and fixed layout, with a small finite vocabulary. The checkpoint was selected after a few exploratory runs, so these seeds are not an untouched final test set. They do not establish unseen-template, natural-language, visual, or original-MiniWoB generalization. Checkbox learning remains an obvious next policy improvement; the observation-only scripted baseline solves all three tasks.

Verification also covers 102,400 transitions against an independent C test specification, four concurrent host callers, terminal-state absorption checked by Bend, PufferLib autoresets, and scripted browser editing/keyboard cases. CPU/CUDA inference agreed on every action across 64 steps and 13 episode resets, with maximum logit error `4.20e-5`. The final three-second environment benchmark measured roughly **0.79M updates/second**, including the bridge, feature packing, rewards, and autoresets. Initialization is excluded. This is one local sample, not an end-to-end training rate or a controlled speedup comparison. Full measurements and provenance are in [RESULTS.json](RESULTS.json).

Implementation details:

- [bend/Game.bend](bend/Game.bend) owns seeded resets, widget transitions, instructions, state, outcomes, public observations, and structural action masks. [bend/LAWS.bend](bend/LAWS.bend) states terminal absorption; [bend/PROOF.bend](bend/PROOF.bend) proves it. These proofs concern the Bend model, not the compiler, C bridge, or Chromium.
- [bridge.c](bridge.c) embeds the generated CPU runtime in process. It retains one runtime for the process lifetime and serializes calls with a mutex. Each call handles 32 independent environments with persistent caller-owned state buffers. The only foreign effects copy bounded buffers in and out. There is no per-step process launch, compiler invocation, socket, JSON, or GPU call in the simulation path.
- The adapter packages those independent lanes as one PufferLib `Env`. Agent counts must divide into groups of 32 within each rollout buffer. The initial runtime uses one CPU execution thread; concurrent Puffer workers are safe but do not make the Bend runtime run in parallel. Batched parallelism or isolated runtimes can be investigated after profiling.
- The bridge is coupled to generated runtime internals, so builds require stock Bend 2.0.6 and record source/generated-C hashes. An upstream compiler upgrade requires revalidating this small boundary. Upstream reserves a large virtual address range; that is not resident RAM usage. The runtime remains allocated until process exit.
- [contract.json](contract.json) specifies the supported observation/action ABI. The pilot has five possible controls, 128 public observation words, and 13 actions. A byte-preserving 640-feature baseline adds four letter indicators; it is not a general language encoder. Instruction and widget labels are visible; private goal state never enters policy features or action masks.
- [web/index.html](web/index.html) independently implements the browser widgets and success checks. [cdp.c](cdp.c) dispatches actual browser mouse/key events. Observations come from DOM geometry, ARIA labels, input values, focus, and checkbox state; the runner does not substitute Bend's node table. It is a bounded normalized DOM view, not Chromium's full accessibility-tree API.
- Editing is explicitly append/backspace-at-end, maximum two characters, with Tab cycling within the task. Ordinary unrestricted caret movement, scrolling, disabled/occluded widgets, menus, CSS layout variation, arbitrary text, and screenshots are outside version 1. The browser test runner uses disposable profiles and local fixtures, with Chrome's sandbox disabled for this local WSL test setup.

Next work should improve checkbox learning, broaden instruction/text encoding, and add held-out structural layouts and workflows. Original MiniWoB adapters and a browser-only training comparison are still needed before claiming benchmark compatibility or a training-efficiency advantage. Screenshot observations and realistic harvested site data remain later milestones in [the plan](../../WEB_ENV_PLAN.md).

The [first-wave MiniWoB expansion](miniwob/EXPANSION.md) now contains twelve
bounded task models and 95 checked laws, including button sequences, native
select lists, trees and autocomplete. These models have separate original-
browser conformance tests; the current training/watch adapter still uses the
three-task pilot. See [TODO.md](TODO.md) for integration work and remaining
full-suite parity gates.
