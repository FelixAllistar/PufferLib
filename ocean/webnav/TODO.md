# WebNav benchmark worklist

Modern benchmark integration (2026-10-03): [survey](benchmarks/SURVEY.md).
Use MiniWoB as the primitive curriculum and WebArena-Verified as the first
resettable, realistic evaluation target. The initial declared preset is the
18 shopping-admin navigation tasks, with original sites and the official
response/HAR scorer. The public browser bridge reuses the native policy encoder
and checkpoint; it is not a new learner environment. Data and site storage live
on Windows D:. Follow [setup and current results](benchmarks/README.md).

The initial three-task real-site pilot is complete: the shared checkpoint and
uniform legal actions each scored 0/3 with the unchanged official evaluator;
the scripted customer-list positive control scored 1/1. One million additional
native training steps produced 14.0% sampled average task success versus 12.0%
random on 604 initial instances. This establishes working training/evaluation
plumbing, with weak learned performance. See [measured evidence](benchmarks/RESULTS.json).

The target is a reusable low-fidelity application engine for exploration
research. MiniWoB families are compatibility fixtures, not the granularity of
the new engine. [The primitive contract](primitives/README.md) defines four
layers: controls, pages, persistent data and composed workflows. The
[catalog](primitives/catalog.json) records proposed units and dependencies.
The [full pinned WebArena inventory](benchmarks/requirements.json) accounts for
812 tasks/190 templates (374 mutation, 325 retrieval, 113 navigation; 48
cross-site tasks). Its site/outcome envelopes are planning estimates, not ports.

The [browser primitive](primitives/browser/Model.bend) now passes twelve laws and
9,600 native differential steps for navigation/history, delayed loads,
cancellation, failure, retry and reload without duplicate current-URL history.
The [tab composition](primitives/browser_contexts/README.md) passes nine laws,
32,000 differential steps and three Chromium fixtures with 34 settled tab/history
comparisons. Its public controls pass 32 generated-world checks and 512 legal
actions from the unchanged random/sampled shared policies. Original-browser
control binding, address entry, async commit fidelity and application/training
composition remain open. Shared text editing has been extracted behind
the MiniWoB compatibility layer. Records/queries and single-record draft
transactions pass their laws and independent native fixtures. The composed
record-browser application now has a Bend world generator and a public WF
binding, with 64 generated-world checks. Public page metadata carries table
positions, spans, headers and URLs; its browser adapter passes 21 fixtures.
The response form composes explicit finish with Unicode editing and the existing
actions. Both direct and form submissions pass 28 pinned public-schema cases.
Five response-form laws include draft-preserving focus handoff. The optional
`response-v1` browser preset passes four composition fixtures, including explicit
submission, exact JSON, expiry and the shared random/checkpoint interface. The
pinned response scorer records null submissions as failures for all three types.
The unchanged random/checkpoint RPC drives 16 application/form episodes through
965 legal choices. These are interface checks, without rewards or learned success
scores. Original-site workflow comparisons and full environment registration
remain open; no WebArena template is qualified yet. Root validates serially under
the normal guard (up to 6 GiB, no swap, one CPU). If system resources constrain
progress, pause the goal and alert the user; follow [the standing rule](AGENTS.md).
No training was started for the refactor.

The new browser-control RPC and composed browser shell are implemented but not
qualified or exposed by the evaluation runner. Native RPC checks pass. The
2026-10-07 unchanged retry passed four of five browser fixtures, but the 16-tab
capacity fixture again hit the existing 128-task process/thread cap: the kernel
rejected a fork and Chromium's page crashed. The earlier October 4 run peaked
near 664 MiB; this retry did not retain a memory peak. The goal is paused under
the resource rule. Limits, browser
settings and intended tests are unchanged. Evidence:
`build/webnav/benchmarks/browser-shell-resource-pause-20261007.json`.

The first live qualification attempt on 2026-10-03 paused under the resource rule. The website
container recorded 393 hits on its existing 2304 MiB memory cap, with no OOM kill.
The scripted response-preset run timed out waiting for navigation after the
second click; the memory hits do not establish the timeout's cause. Native and
browser fixture checks passed, but the real-site response integration remains
unqualified in that attempt. Evidence is in
`/mnt/d/puffertank/webnav-bench/runs/20261003-response-calibration/resource-pause.json`.
The user-requested retry passed with unchanged sources, limits and timeouts:
two browser clicks plus native Finish scored 1/1 under the pinned official
grader. Website memory peaked at 1.70 GiB with zero limit hits or OOMs.
`runs/20261003-response-calibration-retry/qualification.json` records this
scripted transport check; it is not learned performance or a qualified native
workflow port. The goal was reset and verified active, retaining the resource rule.

The next CPU Bend composition work should reuse these primitives in a shared
browser shell and persistent data models:

- Action contract engineering: describe effective commands, public targets,
  required arguments, text capacity, finish/answer outputs and stale-reference
  rejection. Preserve public wrong choices; invalid commands must not partly
  mutate application state. The first million-step evaluation spent 81.3% of
  steps on local parameter/text registers, an interface measurement rather
  than a prescription for an exploration algorithm. Policy architecture,
  exploration, reward shaping and curriculum choices belong to the user.
- Browser shell: URL/history, page transitions, tabs, asynchronous loading and
  recoverable failures. The checked response form is bound through the optional
  `response-v1` runner preset, preserving exact answer bytes and explicit expiry.
  One scripted live-site NAVIGATE submission passes; complete shell controls
  and application workflow comparisons remain to check;
  the default legacy preset keeps its declared fixed-budget navigation convention.
- Record browser: search, filter, sort, paginate, inspect a detail page and return
  to a list while preserving query state. Support grid row/column relationships
  and record identity through DOM changes.
- Transactions: cart/order/form drafts, field validation, confirmation and
  cancellation. Laws should check persistence, reversible edits, commit
  idempotence and that cancel cannot commit a draft.
- Workspace composition: mail/forum/admin operations across pages with
  references to previously observed records; no private answer shortcuts.
- Generate goals/content independently from held-out benchmark task instances.
  Split by workflow template, entity sets and site appearance; measure each
  transfer step with the same policy and public observation/action contract.

These are planned compositions, not already implemented WebArena ports. The
official sites remain the evaluation authority; simulator wins are reported
separately from browser wins.

All **125 registered MiniWoB++ task names** at pinned revision
`33c3b4ddef8c6eb67c57a29663d844b1eda7e614` now have checked stock CPU Bend
implementations across 23 families (2026-09-29). Each family has generators,
laws, independent native checks, original-page comparisons and a public-view
scripted controller. The source inventory has 130 HTML pages; five are not
registered tasks. See [coverage and evidence](families/registry.json) and
[the integration record](families/PARALLEL_PLAN.md).

This completes task-name implementation coverage. It does **not** establish
full browser rendering, DOM/accessibility, input, timing or source-generator
parity. Family READMEs and RESULTS files record those limits. Geometric and
visual tasks use explicit scene descriptions; some interactions use normalized
gestures or selection offsets. Their scripted controllers are not trained RL.

**125/125 task names now route through one native trainable environment and
shared checkpoint:** [shared learner](../webnav_unified/README.md). This is
learner integration across bounded models, not learned success or full browser
parity. **51/125 names also have older diagnostic adapters:** click (10), forms (8),
panels/menus (11), numeric (9), catalog (3), and email (10). This counts validated
transport and training integration, not solved tasks. Results and checkpoints:
[click](../webnav_family/RESULTS.md), [forms](../webnav_forms/RESULTS.md),
[navigation](../webnav_navigation/RESULTS.md), [numeric](../webnav_numeric/RESULTS.md),
[catalog](../webnav_catalog/RESULTS.md), and [email](../webnav_email/RESULTS.md).
Those six adapters use separate policies. In their latest million-step
pilots, numeric scored 44/180 held-out native and 11/45 original-browser wins;
catalog scored 1/60 and 0/15; email scored 0/200 and 0/50. Catalog/email greedy
policies mostly time out: curricula and structured actions are the immediate
learning work, not evidence that these task families are solved.

1. **Consolidate into one environment and one shared policy before adding more
   standalone adapters.** The six existing learner formats are diagnostic
   scaffolding, not the intended product. Follow [the unified learner contract](UNIFIED_LEARNER.md):
   one suite dispatcher, public capability/action schema, observation encoder,
   mixed-task training run and checkpoint. Preserve the checked Bend modules.
   Demonstrate shared-weight learning across families and original-page evaluation
   with that same checkpoint before counting unified task coverage.
   The native `webnav_unified` path now batches all 23 families through one
   policy; its first short mixed training run completed. Remaining integration:
   shared original-browser/headed replay and public gesture/option gaps. Measure
   learning with per-task evaluations and a random baseline, then improve the
   oversized flat policy using shared node encoding/scoring and curricula.
2. Measure and improve text encoding independently of task correctness.
   Preserve exact strings, identifiers and numeric features alongside frozen
   embeddings. Compare encoder size, cache cost, latency and semantic accuracy
   on held-out paraphrases, negation, ordering and numeric constraints. Current
   tokenizer parity and scripted success are not evidence of understanding.
   The shared click policy's soft-checkbox and widget gaps remain useful tests.
3. Close documented fidelity gaps by family: source template/content coverage,
   DOM/AX projection, raw pointer/selection paths, animation and event timing,
   rendering and numerical boundaries. Keep an explicit action/observation
   preset in every result. Preserve upstream scoring quirks in compatibility
   mode; any corrected benchmark rules need a separately versioned mode.
4. Build site-like benchmarks from public page structures and behavior
   templates: stores, search/catalog, inbox, account forms and travel flows.
   Randomize labels, ordering, layout, distractors and accessibility quality
   independently. Use template-held-out and site-held-out splits. Keep real
   browser tasks as transfer evaluations and a separately reported training
   source, rather than treating simulated success as browser success.
5. Measure CPU Bend reset/step throughput, encoder/cache cost, native learner
   SPS and browser success for each new tier. Add a unified headed replay
   once the shared observation/action interface supports these families.

Completed final packages: editing passed 16 laws, 100 original-page episodes
and 5,000 full-credit scripted episodes; visual passed 26 laws, 180 original
page episodes and 9,000 full-credit scripted episodes; drag passed 15 laws,
188 original-page episodes and 10,000 full-credit scripted episodes.
Geometry reports partial rewards honestly rather than relabeling them as
full wins, and tic-tac-toe reports draws separately.

Build coordination remains unchanged: the user cleared the sweep hold on
2026-09-29. Root alone compiles and tests, serially through the existing
6 GiB/no-swap guard. Future workers use the `bend-cpu-families` skill and
cached guide, remain source-only, and hand over a small model/law/wire
increment before expanding a new family. No worktrees are required.
