Use **stock Bend CPU for the environment**, with PufferLib's C/CUDA trainer and a real Chromium validation runner. The experimental Bend fork and Bend GPU execution are outside this project. Start with DOM observations; add screenshots later.

The first three-task pilot is implemented in [ocean/webnav](ocean/webnav/README.md):

- Bend implements button selection, checkbox selection plus submit, and two-character text entry plus submit.
- An in-process C bridge retains the Bend runtime and processes batches of 32 independent environments. C handles transport and PufferLib integration; it does not implement a production substitute for the Bend transitions.
- A C CDP runner controls pinned Chromium with mouse/key events and extracts public DOM observations. It compares those with Bend after every action.
- The PufferLib adapter supports native training, action masks, rewards, and autoresets. A native CPU policy runner evaluates the resulting checkpoint in both backends.
- Tests cover independent state-transition checks, concurrent host calls, browser conformance, recurrent resets, and CPU/CUDA inference agreement. See the README and results for measured scope and limitations.

The first checkpoint achieves 85.3% success on 1,000 same-template evaluation seeds in both backends. It solves button and text tasks on that panel; checkbox success is 55%. Browser observations and outcomes agree with Bend throughout the policy traces. This measures a restricted generated benchmark, not original MiniWoB performance or arbitrary-site navigation.

The current implementation backlog is [ocean/webnav/TODO.md](ocean/webnav/TODO.md): **MiniWoB parity first**, with a frozen-text-encoder investigation alongside it, then site-derived simulation and richer generated sites. Encoder training or a large text model is not a prerequisite for the first task ports. The pilot remains a screener. [BENCHMARK_DESIGN.md](ocean/webnav/BENCHMARK_DESIGN.md) describes the longer-term architecture; the TODO is authoritative on sequence and completion status.

The follow-up iteration has twelve bounded matched-instance original-task models, 95 checked laws, a variable-node DOM contract, native Unicode frozen encoding with reference tests, immutable embedding caches, and initial read-only site captures/public trace recording. See [task expansion](ocean/webnav/miniwob/EXPANSION.md) and [ITERATION_RESULTS.json](ocean/webnav/ITERATION_RESULTS.json). Independent generation for the remaining task families, remaining browser interaction semantics and integration of these new components into the trainer are still open.

The broader implementation requirements are:

1. Improve checkbox policy performance and measure several training seeds. Keep sparse terminal rewards and public observations, and report any use of demonstrations separately. Compare browser-only learning against Bend pretraining under both equal-step and equal-time budgets before claiming a training advantage.
2. Expand the DOM contract with richer labels/instructions, node relationships, disabled/occluded controls, scrolling, and explicit logical delays. Add real-browser conformance cases for each behavior before relying on it during training. Keep element actions and coordinate actions as separately scored interfaces.
3. Add original MiniWoB task adapters and report original-task scores separately from generated tasks. Preserve or explicitly identify changes to its action presets, timing, and rewards. The existing pilot uses new task semantics and cannot be labeled a drop-in MiniWoB replacement.
4. Introduce independent layout, content, instruction, and workflow variation. Hold out entire templates, layout grammars, and primitive combinations. Different seeds of a fixed template are insufficient evidence of structural generalization.
5. Use real DOM/accessibility snapshots and action traces to guide executable task generation and perception training. Begin with reproducible sites and existing datasets such as Mind2Web/WebLINX. Recorded traces alone cannot answer arbitrary counterfactual actions and do not constitute an interactive RL environment.
6. Add screenshot observations using the same task specifications. Calibrate a fast renderer against Chromium for text, layout, clipping, layering, and input geometry. Evaluate visual transfer independently; DOM success does not establish visual grounding.

Performance work should follow the measured bottleneck: profile Bend batch evaluation, buffer copies, observation encoding, autoresets, policy inference, and training separately. The current bridge deliberately serializes a stock CPU runtime; CPU parallelism can be added behind the same contract if worthwhile. No Bend compiler fork or GPU environment path is needed for these milestones.

Background references: [MiniWoB++](https://github.com/Farama-Foundation/miniwob-plusplus), [Bend guide](https://github.com/bendlang/bend/blob/main/guide/GUIDE.md), [Chrome DevTools Protocol](https://chromedevtools.github.io/devtools-protocol/), [MiniWoB actions](https://miniwob.farama.org/content/action_space/), [MiniWoB rewards](https://miniwob.farama.org/content/reward/), [Mind2Web](https://osu-nlp-group.github.io/Mind2Web/), [WebLINX](https://mcgill-nlp.github.io/weblinx/).
