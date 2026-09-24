# WebNav TODO

**Build incident (2026-09-23):** resume family compilation with the user-approved
6 GiB process-tree cap, zero swap,
and serialization; see [build safety](families/BUILD_SAFETY.md). Do not bypass
the wrapper with direct Bend compilation. The observed >6 GiB compiler RSS is
a suspected contributor to the WSL outage, not a confirmed root cause.

**Current priority: [batch the next 27 task candidates](PORTING_PLAN.md) across three shared families. Pause further encoder ablations and long RL runs until the port batch is integrated.**

Family progress: **50/125 registered names have bounded behavior checks**:
12 legacy tasks, 9 [panel tasks](families/panels/RESULTS.json), and 10
[click/focus/checkbox variants](families/click/RESULTS.json), plus 2
[menu tasks](families/menus/RESULTS.json). Menus pass 13 laws, 833 native cases,
48 original-browser episodes and 2,000 public-scripted episodes. Eight additional
[form tasks](families/forms/RESULTS.json) pass 160 original-browser episodes
and 8,000 public-scripted episodes. Nine [numeric tasks](families/numeric/RESULTS.json)
pass 32 laws, native regression checks, 180 original-browser episodes and
9,000 public-scripted episodes. The click family
passes 460,800 independent native transitions, 400 original-browser episodes,
and 10,000 public-scripted generated episodes. These are conformance and
solvability checks, not learned RL scores or full-suite parity. New family-v2
ports still need PPO integration; the existing trained profile remains 12 tasks.

Priority: **MiniWoB parity first**, investigate cheap frozen text encoders alongside it, then expand into site-derived simulation. Stock Bend CPU environments and native C/CUDA training remain fixed requirements. No encoder training or transformer dependency is required before the first ports. Existing pilot tasks are screeners, not completed MiniWoB ports.

Latest iteration: twelve bounded original-task models and CPU Bend generator presets, 110 checked laws, a public observation/action adapter, profile-locked checkpoints, and actual twelve-task RL training. See [training details](TRAINING.md) and [task expansion](miniwob/EXPANSION.md). Earlier component measurements remain in [ITERATION_RESULTS.json](ITERATION_RESULTS.json); full-suite parity remains open.

## P0 — establish the reference and contracts

- [x] Pin Farama MiniWoB++ source revision `33c3b4ddef8c6eb67c57a29663d844b1eda7e614`; create [task inventory](miniwob_inventory.json): 130 task HTML files, 125 registered in that directory, plus four flight-site source directories. Counts include variants/debug tasks and are not the historical original-100 count.
- [x] Fetch/cache checksum-pinned upstream assets with LICENSE and dependency provenance; native CDP oracle now runs twelve original task pages. See [miniwob/README.md](miniwob/README.md).
- [x] Audit first-wave HTML generation, goal/reward predicates and common timing behavior in [TASK_AUDIT.md](miniwob/TASK_AUDIT.md).
- [ ] Finish original-wrapper action/reward preset audit and implement remaining browser behaviors. Current conformance uses bounded click/text/select/tree/menu presets and a controlled logical clock, not unrestricted keyboard/coordinate or real scheduler parity.
- [x] Define and test [DOM-v2](DOM_V2.md): bounded variable nodes, public UTF-8 text spans, role/state/name provenance, structural relations, geometry and omission reporting. The separate WTView training projection now supports bounded clicking, public-span copying, caret editing and menus; the complete DOM-v2 vocabulary remains open.
- [ ] Complete integration of the full versioned schema into training (the bounded WTView projection and checkpoint contract are implemented): selection endpoints, variable observed nodes, text spans, roles, state, label/parent relationships, geometry, node masks, scroll and text payloads. Keep policy features separate from private goals.
- [x] Implement four observation-quality profiles: full names, no ARIA, no associated labels, and sparse names. Validate that degradation changes observations only and hidden rendered-state data is excluded. Real-world calibration remains open.
- [x] Add profile-locked native text encoding and immutable caches with tokenizer/model fingerprints, dimensions, token cap, ICU major version, content checks and mismatch rejection. Downloads use pinned SHA-256.
- [x] Lock the new training checkpoints to feature/action versions, source SHA-256 and semantic profile. Runtime cache slots are never rollout inputs.
- [ ] Extend checkpoint locking to any future external cache artifacts and full DOM/AX features.

## P1 — MiniWoB environment parity

First wave (12 tasks): `click-button`, `click-button-sequence`, `click-checkboxes`, `click-link`, `click-option`, `choose-list`, `enter-text`, `focus-text`, `login-user`, `read-table`, `navigate-tree`, `use-autocomplete-nodelay`.

For each task, complete these gates in [the inventory](miniwob_inventory.json):

- [ ] Audit the upstream task and write its behavioral specification.
- [ ] Implement shared Bend primitives and prove the relevant laws before composing the task.
- [ ] Replay successful, incorrect, repeated, invalid and recovery actions against original Chromium and Bend from matched initial task instances. A matching numeric seed alone does not establish matching resets across different RNGs.
- [ ] Match observations, reward and termination, or explicitly label the port's deviations. Do not replace timed rewards with our pilot's binary reward and call it parity.
- [ ] Verify solvability with a scripted baseline; report learned policy success separately. Environment parity does not require training an agent to solve every task.
- [ ] Record per-task timing, memory, disagreement cases and evidence paths. Performance comparisons need equal action semantics, concurrency and hardware, plus separate end-to-end time-to-score measurements.

Second wave: menus, tabs, scrolling, multi-layout forms, calendars/date pickers, inbox, search and longer sequences. Third wave: drag/hover, geometry/canvas, visual and real-time tasks, and captured flight sites. DOM-only parity is the first milestone; full suite parity remains open until these are addressed. Do not quietly omit hard tasks from the denominator.

- [x] Add independent CPU Bend checkbox/radio generators, public form export, and original-handler browser validation. Preserve ambiguity and distinguish source-generator from Bend-generator test modes.

- [x] Add shared CPU Bend text editing for enter-text/login-user/read-table, 12 text laws, original-browser comparisons, and table-row observation structure. See [TEXT_TASKS.md](miniwob/TEXT_TASKS.md).

- [x] Add bounded button-sequence, select-list, tree and autocomplete models, 61 additional laws, shared dispatch and original-browser differential checks. See [EXPANSION.md](miniwob/EXPANSION.md). Full observation/generator parity remains open.

- [x] Add bounded CPU Bend generator presets for all twelve task names and a public-information-only solvability controller. Validate 1,000 held-out seeds per task separately from learned-policy evaluation.
- [x] Wire the twelve presets into the separate native `webnav_dom` training profile, public action masks, ordered text features, optional frozen Potion features and CPU checkpoint evaluator.
- [ ] Finish original-browser learned-policy adapters for the other seven tasks; five simple click/form tasks are implemented.

## P1 — law-driven Bend foundation

- [x] Read the complete installed stock 2.0.6 guide; document the [audit](BEND_AUDIT.md).
- [x] Preserve terminal absorption and add checked laws for read-only observation dispatch, reset independence, success/failure field preservation and unfocused editing. Six laws now check.
- [x] Separate the new typed widget/task/outcome core from numeric packing in miniwob/Widgets.bend and Wire.bend; guard the C boundary. The legacy pilot remains unchanged.
- [ ] Complete validity predicates and typed action/text/selection primitives for the rest of the suite.
- [ ] Prove reachable-state validity: bounded text/focus/indices/ticks, outcome consistency, and valid transitions. State assumptions explicitly so laws are not false for arbitrary malformed ABI values.
- [ ] Extend the 110 checked task/generator laws (including task preservation and indexed goal-list preservation) to the full future task specification; focus selects at most one present element; unrelated widgets are unchanged; edits respect capacity; submit success agrees with an independent goal predicate.
- [ ] Prove observation noninterference (native private-target mutation tests now pass, but are not a Bend proof): two states with the same public view produce identical policy input and masks. The natural-language instruction is public; hidden reward answers are not.
- [ ] Prove renaming/permutation consistency, navigation/history behavior and reset/serialization properties as their primitives arrive.
- [ ] Verify pack/unpack round trips and row isolation at the C boundary; array indices wrap in Bend, so ownership does not prove correct ABI indexing.
- [ ] Use balanced CPU parallel work only after profiling and a semantics-preservation check. The present shared runtime is serialized and does not exploit multiple cores.
- [ ] Keep existing laws stable when code changes; strengthen them instead of weakening a requirement to make a proof pass. Run `bend PROOF.bend` and relevant native/browser differential tests on every semantics change. C, compiler internals and Chromium remain outside the model proofs.

## P1 — text investigation, not an encoder rewrite prerequisite

- [x] Review original WoB/DOMNET architectures: exact query-to-DOM matching is a legitimate first baseline; modern large language encoders are optional.
- [x] Identify Model2Vec/Potion as a frozen tokenizer + vector table + pooling candidate matching the precomputed-table idea.
- [x] Pin Potion 8M and build a native C ASCII development probe; record model size, diagnostic misses and warm throughput in [TEXT_ENCODER.md](TEXT_ENCODER.md). No text model training.
- [x] Add native C/ICU encoder; 1,120 cases match Hugging Face Rust tokenizer 0.23.2, including Unicode, accents, control characters, special tokens, empty/OOV and truncation. Vectors match independent normalized pooling. Sanitizers pass. This is tested profile compatibility, not a proof for all strings.
- [x] Measure frozen C encoding and cache lookup on 268 distinct strings from four initial-page captures; retain raw results and limits.
- [x] Research larger frozen tables, ordered token features and contextual alternatives in [ENCODER_RESEARCH.md](ENCODER_RESEARCH.md); no final encoder selection yet.
- [x] Run Potion-8M / Potion-32M / quantized MiniLM comparison on 82 development diagnostics, with tie-aware scoring and backend-specific timings: [ENCODER_COMPARISON.md](ENCODER_COMPARISON.md). The separate twelve-task policy ablation now compares ordered/exact text with the same features plus Potion-8M; no encoder replacement.
- [ ] Broaden the distinct-site corpus and semantic accuracy evaluation, not just repeated short diagnostic phrases. Compare exact match/token overlap, frozen static embeddings, and a small contextual model such as MiniLM. Preserve exact string, number, order and negation information alongside pooled semantic vectors.
- [ ] Measure batch-1 and batched latency, p50/p95, cold startup, peak RSS, download size, unchanged-node caching, cache misses, 32/128/512-node pages and sustained browser-extension or native-host performance.
- [ ] Freeze corpus/splits before tuning. Evaluate matching, attribute constraints, numbers, negation, order and downstream task success; retrieval cosine is not instruction following.
- [x] Prototype exact-string frozen static-vector caches, with matching live encoding, duplicate/miss tests and corruption/profile rejection. No private answer fields are used.
- [ ] Add contextual-model caches if that encoder is selected; wire cached IDs into training.
- [ ] Compare feature memory/bandwidth against the rollout budget. Select the smallest adequate encoder using task success and wall time, not a generic embedding leaderboard.
- [ ] Decide the shared per-node policy architecture using permutation/transfer tests. The contract should allow swapping lexical, static and contextual features without redesigning the simulator.

## P2 — site-derived simulation and richer generated sites

- [ ] Build a provenance-tracked corpus spanning large retail/search/catalog sites, marketplaces, documentation, forums, admin tools, independent stores, and Shopify/Weebly-style templates. Record source URL/date, reuse terms and captured variant; do not assume every downloadable asset is redistributable.
- [x] Add read-only native DOM capture and public observation/action/reward trace export. Capture four nonempty initial pages; record an empty Amazon response as excluded rather than successful capture. Raw text remains local. Export 100 original-checkbox episodes / 263 transitions as development traces.
- [ ] Expand collection to tasks, page/route graphs, visible text, accessibility/DOM relations, forms, control state, action traces, requests and observed outcomes. Store missing/uncertain information explicitly.
- [ ] Convert recurring behavior into Bend primitives: search, filtering, sorting, pagination, product options/cart, forms, tabs/dialogs, navigation/history, tables, messages and edits.
- [ ] Author site-derived records/layouts and behavioral models; validate against browser traces. Coverage gaps need targeted real-browser exploration. A snapshot supplies structure/text; transitions come from observed behavior plus modeled rules.
- [ ] Randomize independently: data, wording, site hierarchy, DOM nesting, widget implementation, field order, clutter, missing labels, ambiguous targets, errors and delays. Keep latent task truth and public observation corruption separate.
- [ ] Start with catalog + documentation families, then inbox/ticketing/booking. Include plain static link-based sites and stateful workflows.
- [ ] Mix high-throughput simulated rollouts with real-browser rollouts and/or recorded demonstrations. Label sources; recorded demonstrations need an imitation/offline objective rather than pretending they are fresh on-policy PPO rollouts. Sweep mixing ratios and keep a real-only evaluation set.
- [ ] Hold out whole sites/template families/platform variants and workflow compositions, not only seeds. Add failures back to development data without contaminating the frozen test set.

## P3 — make “100x better” measurable

- [ ] Separate targets: simulation steps/second, end-to-end wall time to success threshold, coverage, held-out-site success, robustness to observation quality, and resource cost. No 100x claim before a controlled result.
- [ ] Compare original MiniWoB, our matched ports, and richer site families with per-task results and several training seeds.
- [ ] Use WebShop/WebArena/WorkArena as independent transfer checks where useful, not mandatory slow infrastructure for every training step.
- [ ] Add Chromium screenshots and coordinate actions after DOM parity; test visual grounding separately.

Next concrete tickets: **MW-02:** independent generators for the ten task pages without them, second-wave browser behaviors, and text beyond the bounded ASCII preset; **ABI-02:** native policy/trainer adapter for DOM-v2 and cache-local IDs; **TXT-02:** broaden realistic accuracy splits and compare contextual features; **LAW-02:** prove validity, unrelated-widget/frame and text-edit properties. Checkbox and radio forms now have independent CPU Bend generators and public JSONL export; other ports still need generators. Trainer integration remains open. See [generation contract](miniwob/GENERATORS.md).

## Next measured training iteration

- [ ] Diagnose multi-field copying and table key/value selection with per-task
  learning curves. Compare a task curriculum or clearly labeled demonstration
  warm-start against the unshaped RL baseline; do not call scripted success RL.
- [ ] Replace the dense 12,392-float byte expansion with a compact, shared
  node/span encoder and measure end-to-end steps/second and time-to-score.
- [ ] Repeat promising encoder comparisons across multiple training seeds and
  a frozen unseen-template/vocabulary split before choosing a semantic backend.
- [ ] Scramble the reset stream before selecting tasks: the current two-draw
  LCG/modulo picker partitions even/odd task IDs by environment stream. The
  four-environment default covers all twelve, but each individual environment
  sees only six. Add an explicit per-stream coverage regression when fixing it.
- [ ] Extend the five original-browser learned-policy adapters to all twelve,
  with per-step public-observation comparisons in addition to outcome tests.
