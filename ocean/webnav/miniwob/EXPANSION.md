# First-wave task expansion

The new models implement `click-button-sequence`, `choose-list`, `navigate-tree`,
and `use-autocomplete-nodelay` in stock CPU Bend 2.0.6. Pure behavior and checked
laws live in `sequence_select/`, `tree/`, and `autocomplete/`. C and JavaScript
provide the native ABI and independent browser test infrastructure. The
combined library uses private task tags 7, 8, 9, and 10 respectively; click-button
and click-link already share tag 0. Private rows include reward targets and
must never be passed directly to a policy.

These are bounded matched-instance models. Separate CPU Bend training generators now exist for all twelve presets; they cover explicit subsets rather than replicate original random distributions. Tests initialize the simulator from original
seeded browser instances and compare subsequent actions. Directed fixtures
using the original handlers test edge cases separately; they are not counted
as original-generated episodes. Scripted successes/failures are test coverage,
not learned-policy accuracy.

The tree preset uses visible-label clicks, expansion state, parent links,
visibility, event bubbling, and original timed rewards. It does not implement
arbitrary hit-testing on blank list-item regions or complete DOM/AX geometry.
The browser harness checks the public tree-node projection and reward state,
not the full DOM-v2 projection. Its test controller may inspect the entire
imported tree, including hidden nodes; this is a conformance harness, not an
observation-limited agent baseline.

The DOM exporter supports explicit public popup scopes through a fourth
argument, for example `webnavDOM('#area', '#query', 0, ['ul.ui-autocomplete'])`.
The caller must exclude reward/HUD/private containers. The new [WTView training adapter](../TRAINING.md) includes native select options, caret/selection state, tree/menu roles and public copy spans. Full DOM/AX geometry, scrolling and unrestricted browser actions remain open. The older pilot checkpoints remain separate.

The encoder diagnostic used zero RL training and zero encoder fine-tuning.
Its score measures cosine ranking of candidate labels with pretrained frozen
vectors; it is unrelated to the scripted conformance counts here. Actual twelve-task learning experiments now use the separate `webnav_dom` profile, with held-out reset seeds but recurring finite lexicons/templates.

## Reproduce

```sh
make -f ocean/webnav/Makefile test-new-tasks
make -f ocean/webnav/Makefile test-first-wave test-dom
```

The first command checks all combined Bend laws, compiles the CPU bridge,
runs native differential/boundary/routing tests and exercises each new task
against Chromium for 200 episodes. The second includes the earlier eight
models and DOM extractor tests. Results are recorded in
[../TASK_EXPANSION_RESULTS.json](../TASK_EXPANSION_RESULTS.json).

Autocomplete uses a freshly recreated original jQuery widget at each reset
to clear retained search state. Actions wait for the existing zero-delay search
callback to settle without advancing logical time. Submit includes an outside
blur click followed by the submit button click so the popup cannot intercept
it. These choices are an explicit test preset, not a claim about the original
Gym wrapper's complete reset/scheduler behavior. Comparisons include field,
caret, focus, menu count/active item/retained term, outcome and timed reward;
complete popup DOM and all label observations remain outside this check.

Sequence/list tests include topmost overlap, a fully occluded target, and
duplicate option labels. Tree tests include target ancestors reached by event
bubbling and wrong leaves that stop propagation. All fixture actions use the
original task handlers. Scripted test schedules deliberately include failure
and timeout; their success percentages do not measure a trained agent.

There are twelve bounded models out of 125 registered task HTML pages. The
subsequent [training integration](../TRAINING.md) added generator presets for all
twelve, the public policy adapter, actual RL runs and 15 more checked laws
(110 combined, plus six separate pilot laws). The next priority is the
[27-task family batch](../PORTING_PLAN.md), with exact coverage recorded per name.

The shared regression passed 8,400 episodes and 20,011 compared actions,
plus six directed fixtures with eleven actions. All native tests, malformed-row
checks, mixed-task routing, DOM extraction tests and the combined proof passed.
The four new tasks account for 800 of those episodes. These are correctness
checks; this iteration does not establish their simulation throughput.
