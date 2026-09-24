# Original MiniWoB conformance lane

Eight matched-instance task models run in stock CPU Bend. The click-widget lane covers `click-button`, `click-checkboxes`, `click-link`, `focus-text`, and `click-option`; the [text lane](TEXT_TASKS.md) adds `enter-text`, `login-user`, and `read-table`. This is a validation lane separate from the original three-task PufferLib pilot. It is not yet a new trainable PufferLib environment or a claim of complete MiniWoB parity.

```sh
make -f ocean/webnav/Makefile setup-browser
make -f ocean/webnav/Makefile test-miniwob
# Individual task; schedules include deliberate mistakes and timeouts.
build/webnav/miniwob_oracle click-checkboxes 1000
# Public observations/actions/rewards for replay or offline/imitation experiments.
WEBNAV_RECORD=build/webnav/traces.jsonl build/webnav/miniwob_oracle click-checkboxes 100
```

The setup script verifies a pinned upstream archive and extracts it under ignored `build/webnav/reference/`. Its root LICENSE and bundled third-party licenses are retained. No original HTML or reward function is rewritten. The pin is `33c3b4ddef8c6eb67c57a29663d844b1eda7e614`.

The browser generates each task instance using its own seeded generator. The harness extracts the public instruction, controls, geometry and initial state, and derives the target condition from that instruction for the caption-based tasks. Checkbox/radio private index goals are captured from the original generator for the simulator specification, preserving ambiguous duplicate-label semantics; the scripted controller still uses public labels. Bend receives a private task specification and typed widget state; the policy-facing DOM structure has no goal field. This avoids assuming two different RNG implementations produce identical pages from the same number. Independent [CPU Bend checkbox/radio generators](GENERATORS.md) are now available as a separately reported mode; other task generators and browser layout modeling remain open.

Actions are real CDP mouse events. The supported lane is control-targeted click and wait/timeout. Bend predicts state changes; the harness combines these with imported initial text/layout and compares the complete normalized DOM-v2 projection, focus, checkbox state, termination, raw reward and time-adjusted reward after each action. This is not complete DOM/AX equivalence, a renderer, or unrestricted keyboard/coordinate parity. Geometric visibility in DOM-v2 is not an occlusion oracle.

A controlled clock supplies deterministic elapsed milliseconds. The harness substitutes Date, clears the real task timer, and invokes the original timeout callback behavior when logical time reaches 10,000 ms. The original `core.endEpisode` still computes browser reward. Bend independently enforces the same deadline and computes raw/time-adjusted rewards from host-supplied elapsed time. The comparison also verifies the browser elapsed time against an independent host counter, with 1e-6 tolerance for Bend F32 versus JavaScript double rewards. Correct event handling under the browser's actual wall-clock scheduler and arbitrary concurrent asynchronous events remains untested. Do not describe this lane as untouched wall-clock evaluation.

The five schedules are public-label solving, arbitrary clicks, immediate/wrong submission, reversible checkbox edits, and timeout. Full-credit/partial/failure counts are coverage evidence, not agent accuracy. Current development seeds 100000..100999 have been used in tests and are not an untouched generalization split.

Measured results: 1,000 episodes per original task (five tasks, 8,039 actions), plus 1,000 per Bend-generated form mode (two modes, 4,862 actions); all pass. Total: 7,000 episodes / 12,901 compared actions. Checkbox tests exercise positive partial credit, not just exact success. Native tests also enumerate all 5,456 checkbox target/state combinations with 2..6 controls, toggle recovery, invalid indices, outcome absorption, 32 independent lanes, deadline precedence at 10,000 ms, and frozen terminal rewards. Click/generator results are in [../ITERATION_RESULTS.json](../ITERATION_RESULTS.json); text-task results are in [../TEXT_TASK_RESULTS.json](../TEXT_TASK_RESULTS.json).

The pure model is [Widgets.bend](Widgets.bend); [Wire.bend](Wire.bend) handles numeric packing. The original thirteen widget laws check: toggle goal preservation, toggle involution, finished/timeout absorption, finished-outcome preservation under timeout, goal-list preservation under indexed toggling, empty-list click identity, and task preservation under guarded/direct clicks; zero running reward, negative-one timeout reward, preservation of unscaled reward, and deadline precedence. These are model requirements, not compiler/browser proofs. ABI validation rejects unsupported task tags, too many controls, malformed booleans and inconsistent output counters before entering Bend.

The wire uses 32 rows of 256 uint32 words. A row has task/count/outcome/focus/matches/total/scaled/command/index at offsets 0..8, input elapsed milliseconds at 9, saved elapsed milliseconds at 10, and F32 raw/timed reward bits at 11/12. There are at most 16 `(role,selected,goal)` triples from offset 16. It is private simulator transport, NOT the DOM observation ABI. Actions reference imported controls; role 4 denotes a non-focusable pointer target such as MiniWoB's styled link spans. State stays in the caller buffer across calls. Elapsed time must be monotonic; terminal reward/time fields are frozen after completion. The bridge uses one serialized stock CPU runtime.

Remaining first-wave requirements are audited in [TASK_AUDIT.md](TASK_AUDIT.md). Do not add text/select/menu tasks by bypassing browser events and then report keyboard equivalence. Choose and version the high-level action preset, implement its transition model, and test its actual browser realization.

The follow-up adds nine laws (22 total in this lane) for radio assignment, overwriting prior radio selection, preserving goals and checkbox state, and generated initial state/node count. See [GENERATORS.md](GENERATORS.md) for independent generator and export contracts. Task tag 3 is radio selection; role tag 5 is a radio input. Reset command 3 is supported only for checkbox/radio form generation.

Text-task follow-up: three additional models (`enter-text`, `login-user`, `read-table`) now use the shared CPU Bend text editor. They pass 600 original-browser episodes / 4,040 actions under a bounded ASCII focus/edit/submit preset. Twelve text laws bring the new-task lane to 34 checked laws. See [TEXT_TASKS.md](TEXT_TASKS.md) for the action, capacity, selection-observation and training-integration limits, and run `make -f ocean/webnav/Makefile test-text-forms`. Eight original task models have now been compared; this is not eight fully certified MiniWoB++ ports.

## Current first-wave expansion

The later [four-task expansion](EXPANSION.md) brings the combined library to
12 bounded matched-instance task models and 95 checked laws. The earlier
8-task/34-law counts above describe the preceding iteration. Use
`make -f ocean/webnav/Makefile test-new-tasks` for the four new models or
`test-first-wave` for all twelve. Full observation/generator parity and
integration into policy training remain open.
