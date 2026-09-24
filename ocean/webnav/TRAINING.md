# Training the first twelve MiniWoB task ports

This is the new `webnav_dom` profile. The old `webnav` three-task pilot and its
checkpoints remain separate. Task behavior, rewards and instance generation
run in stock CPU Bend; native C presents observations and actions; the existing
C/CUDA Puffer trainer learns the policy. No Python runtime is used.

The task list has twelve names, in this bit order for `env.task_mask`:

0. click-button
1. click-link
2. focus-text
3. click-button-sequence
4. choose-list
5. click-checkboxes
6. click-option
7. enter-text
8. login-user
9. read-table
10. navigate-tree
11. use-autocomplete-nodelay

A mask of 4095 selects all twelve. These generators cover explicit subsets of
the original task distributions, not exact upstream RNG/layout reproduction.
Conformance of existing transition models does not prove new generator parity.

## Public interface and actions

`WTView` retains the visible instruction, up to sixteen visible nodes with
name/value, role, state, parent reference and selection endpoints, and eight
copyable public text spans. The latter are quoted instruction spans and table
cells, including distractors. Private goal bits, reward answers, episode seed,
private task tag and reward are not policy observations. Hiding a tree branch
hides its labels. Copy candidates are never selected by the reward predicate.

Actions are wait; click a visible node; insert a public span into the focused
field; and backspace/select-all/home/end/left/right/delete/accept/up/down.
Native select-option and autocomplete suggestion clicks use the tested
semantic presets. Text insertion preserves caret selection; copying does not
automatically clear a field. The ASCII field limits and node/string limits
are versioned restrictions. This is not unrestricted keyboard/pixel control.

A step advances a controlled logical clock by 250 ms, with the existing
10,000 ms deadline and original raw/timed reward formulas. Wall-clock browser
speed does not change reward. No demonstration loss or reward shaping is used.

## First encoder ablation

Both runs use the existing linear input projection, two MinGRU layers, and
categorical action/value outputs. The ordered text tokenizer is UTF-8 bytes,
with eight bit features per byte. It is distinct from Potion's WordPiece
semantic tokenizer. Features keep the complete 128-byte instruction, the first
32 bytes of each node name/value and copy span, and explicit truncation flags.
Views retain up to 64 bytes per name/value/span for exact comparisons/actions.
Exact whole-string matching, control state, selection and table row relations
remain available in both runs. These features are derived only from public data.

With `env.potion=1`, frozen Potion-8M additionally supplies instruction/name
vectors through a fixed 256-to-32 signed projection, plus full-vector cosine
matching. A bounded exact-string cache avoids re-encoding unchanged labels.
With `env.potion=0` these semantic feature slots are zero. Network dimensions,
action space and budgets are otherwise identical. This tests one specific
hybrid feature design, not every possible use of Potion.

This dense, position-aware baseline has no shared per-node neural scorer or
learned token embedding layer. It establishes a complete measurement path;
those architecture improvements can be compared later. The observation
contract remains separate from this feature profile. The first run's rollout
size is intentionally small for the local 3-GB GPU; dense text expansion will
need replacement with compact token/cache gathers before scaling node/agent
counts substantially.

Checkpoint `.webnav.json` sidecars lock source hash, feature/action versions,
semantic mode and policy architecture. A checkpoint from another feature
profile fails loading. Cache slot IDs are never policy inputs or stored in
rollouts, so cache population order cannot change checkpoint meaning.

## Measured results — 2026-09-22

Actual RL training, with a frozen encoder and no demonstrations or reward shaping.
Each evaluation has 100 episodes per simulator task.

| Features | Actual steps | Simulator success | Original-browser success | Training wall time |
| --- | ---: | ---: | ---: | ---: |
| Exact/ordered | 1,998,848 | 68.67% | 84/100 | 87.22 s |
| Exact/ordered + Potion-8M | 1,998,848 | 64.17% | 85/100 | 104.30 s |
| Exact/ordered | 4,999,168 | 80.75% | 99/100 | 365.96 s |
| Exact/ordered + Potion-8M | 4,999,168 | 80.75% | 99/100 | 259.71 s |

At the larger budget both policies scored identically by task:

| Tasks | Success |
| --- | ---: |
| click-button, click-link, focus-text, click-button-sequence, click-option, enter-text, navigate-tree, use-autocomplete-nodelay | 100% each |
| click-checkboxes | 98% |
| choose-list | 71% |
| login-user, read-table | 0% each |

Both larger-budget policies scored 20/20 on original button/link/focus/radio
pages and 19/20 on checkbox pages. This is a small transfer smoke test.
The two budget pairs restart from scratch. This is one training seed per
configuration, using the same development evaluation seeds, with recurring
lexicons/templates; it does not establish performance on unseen websites or
statistical equivalence between encoders. Wall times reflect shared-machine
load and concurrent CPU validation, so they are not an isolated speed comparison.

For the larger exact-text checkpoint, sampled actions also scored 0/100 on
login and 0/100 on tables (sampling seed 73). Greedy playback alone does not
explain those failures. The next experiment should isolate multi-field copying,
table key/value selection and select-list stalls, then compare curriculum or
explicitly labeled demonstration warm-start against unshaped RL.

Keep exact/ordered features as the default pending stronger evidence for
Potion. The optional backend stays available for broader semantic tasks.
Replacing the dense byte expansion with a compact shared node/span encoder
is the next architecture/speed experiment.

Validation passed: 110 combined task/generator laws, 1,536 generated instances,
61,440 legal-action transitions, hidden-target observation/mask/feature tests,
12,000/12,000 public-scripted solvability episodes, existing native task/DOM
regressions, and CPU/CUDA inference parity for all four final checkpoints
(maximum observed logit error below 2.7e-5). The laws have the limits described in
[BEND_AUDIT.md](BEND_AUDIT.md).

Single-thread environment/reset/public-feature throughput measured 63,224
steps/s without Potion and 54,013 with it, at approximately 4.7/38.9 MiB peak
RSS. These figures exclude policy inference, GPU copies, browser execution
and action-selection overhead. Full training is slower.

[Machine-readable results and checkpoint paths](TRAINING_RESULTS.json) retain
per-task outcomes, contracts, hardware, configuration and evidence paths.

## Commands

```sh
make -f ocean/webnav/Makefile test-training
NATIVE_OUTPUT_NAME=build/webnav/puffer_dom HEADLESS=1 bash build.sh webnav_dom --float
build/webnav/puffer_dom train webnav_dom env.potion=0 base.checkpoint_dir=build/webnav/training/lexical base.log_dir=build/webnav/training/logs
build/webnav/puffer_dom train webnav_dom env.potion=1 base.checkpoint_dir=build/webnav/training/potion base.log_dir=build/webnav/training/logs
build/webnav/training_eval expert 100
build/webnav/training_eval path/to/checkpoint.bin 100
```

Native evaluation defaults to seeds beginning at 2147583648; training clears
the high seed bit. This guarantees separate reset seeds, not unseen strings
or templates: finite lexicons may recur. Scores are per task. The expert is
an observation-only generator diagnostic, not a trained policy or evidence
of general language understanding. Browser evaluation has a separate command
and must be identified as original-generator or Bend-generated instances.

## Browser evaluation and watching

```sh
# New twelve-task profile only; latest searches its own checkpoint sidecars.
bash ocean/webnav/training_run.sh eval latest 100
# Original MiniWoB pages, headless:
bash ocean/webnav/training_run.sh browser latest 20 click-checkboxes
# Same policy and original pages in a visible window (WSLg/X11 required):
bash ocean/webnav/training_run.sh watch latest 20 click-checkboxes
```

Browser/watch currently supports `click-button`, `click-link`, `focus-text`,
`click-checkboxes` and `click-option`. These use original page generation,
real CDP clicks and the controlled reward clock. The remaining seven learned
browser adapters are unfinished. Headed playback waits 300 ms between actions
and prints the instruction, chosen target and episode outcome in the terminal.
`WEBNAV_TRACE=1` enables those logs in headless evaluation too. Full Chrome
must exist at `build/webnav/chrome-linux64/chrome`, or set `WEBNAV_CHROME`.
The headed path was not opened on the user's desktop during these tests;
the browser path and trace logging were exercised headlessly.

WTView currently uses ordinal node positions for most tasks, actual button
positions for the sequence task, and explicit table/tree relations. It is a
compact training projection, not full DOM-v2 geometry or complete accessibility
tree fidelity. The browser adapter independently constructs the projection
from public DOM data; five-task transfer success does not establish complete
observation parity.

For a sequential, matched-budget rerun after building the tools, trainer and
CPU/CUDA parity executable:

```sh
bash ocean/webnav/training_ablation.sh build/webnav/training/my_ablation 5000000
```

The destination must be new. This script trains each configuration from
scratch, checks CPU/CUDA inference, evaluates 100 episodes per simulator task
and 20 per browser task, and writes `results.json`. Individual commands and
per-task records are retained so failures are inspectable.

The evaluator also supports sampled-policy diagnostics without changing the
checkpoint. Its positional arguments after the checkpoint are episodes/task,
task mask, first reset seed, trace flag, and sampling seed; sampling seed zero
keeps greedy evaluation. For example, `100 768 2147583648 0 73` evaluates the
login/table tasks with reproducible policy sampling. This is labeled
`learned-sampled` separately from the default `learned-greedy` results.
