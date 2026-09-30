# WebNav benchmark worklist

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

**51/125 task names now have trainable adapters:** click (10), forms (8),
panels/menus (11), numeric (9), catalog (3), and email (10). This counts validated
transport and training integration, not solved tasks. Results and checkpoints:
[click](../webnav_family/RESULTS.md), [forms](../webnav_forms/RESULTS.md),
[navigation](../webnav_navigation/RESULTS.md), [numeric](../webnav_numeric/RESULTS.md),
[catalog](../webnav_catalog/RESULTS.md), and [email](../webnav_email/RESULTS.md).
The remaining 74 names still need learner integration. In the latest million-step
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
