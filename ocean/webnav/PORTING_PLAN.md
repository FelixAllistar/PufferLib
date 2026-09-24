# Coverage acceleration plan

Priority after the first training ablation: expand original-task coverage. Pause
additional encoder comparisons and long training runs until the next port batch
is integrated. Learned success is a separate measure from environment correctness.

The existing 12 ports required shared infrastructure: stock CPU Bend transport,
original-browser oracles, typed models/laws, public observations/actions, text
features and checkpoint/evaluation integration. Treat that as startup cost, not
an acceptable per-twelve-task cost going forward. Several original task pages
inspected for this plan are only 41–150 lines, but their jQuery/browser behavior
and reward details still need checking.

## Next batch: 27 candidate task names

These are implementation groups, not newly completed tasks or a delivery-time
promise. All are registered in the pinned source and distinct from the existing
twelve. Completing this batch would reach 39 task names; coverage restrictions
still need to be reported per task.

### Click, focus and checkbox family (10)

`click-test`, `click-test-2`, `click-test-transfer`, `click-dialog`, `click-dialog-2`, `click-widget`, `focus-text-2`, `click-checkboxes-transfer`, `click-checkboxes-large`, `click-checkboxes-soft`.

Reuse target-click/focus/toggle cores. Extend label storage/counts and per-task deadlines; preserve synonym targets, overlap and dialog popup scopes.

### Text and form family (8)

`enter-text-dynamic`, `enter-text-2`, `enter-password`, `text-transform`, `copy-paste`, `copy-paste-2`, `read-table-2`, `login-user-popup`.

Generalize field lists, text capacity and public text sources; support case conversion through public actions, popup visibility and task-specific deadlines. Copying a private expected answer is never an action.

### Tabs and collapsibles family (9)

`click-tab`, `click-tab-2`, `click-tab-2-easy`, `click-tab-2-medium`, `click-tab-2-hard`, `click-collapsible`, `click-collapsible-nodelay`, `click-collapsible-2`, `click-collapsible-2-nodelay`.

One active-panel/visibility core with per-task generation and reward. Keep delayed and nodelay variants distinct; test hidden nodes, tab identity and callbacks.

## Execution changes

1. Use the previously requested three Luna/max workers on separate families,
   with one integration owner. Each reads the installed stock Bend guide and
   existing model/law examples. Assign directories and deliverable contracts;
   workers do not independently edit the shared registry, ABI or build script.
2. Reuse typed widget/form/panel transitions and their meaningful laws. A task
   supplies public instruction/data generation, initial state, goal predicate,
   reward and deadline. Shared absorption, frame, focus, text and visibility
   laws apply to the family; per-task checks target actual source differences.
   Do not inflate the proof count with aliases and call that stronger parity.
3. Remove shared capacity/deadline blockers once. The prototype text wire is
   limited to one 64-character or two 32-character fields, with per-tag 256-word
   rows; training assumes a 10-second deadline. Those choices cannot silently
   truncate larger forms, clipboard text or 15/20-second source tasks. Version
   the expanded contract and retain explicit checkpoint compatibility checks.
4. Give families independent compile/test entry points and a reusable browser
   fixture/replay harness. Cache unchanged generated C. The current combined
   build regenerates the full runtime and compiles every oracle/test; do this
   once per integrated batch, not for every family edit.
5. Each task delivery includes Bend behavior and reset generation, public
   observations/actions, source-linked reward/deadline specification, relevant
   laws, scripted solvability and original-browser differential cases. Include
   success, failure, repeat/recovery and boundary cases. Run the combined suite
   after merging a batch. Policy learning is not a prerequisite for counting a
   correct environment implementation.
6. Report one row per actual task name: behavior checked, generator coverage,
   observation/action coverage, training enabled and known mismatches. Use the
   same 125-name denominator. No credit for an alias that omits distinct source
   behavior; no waiting for PPO to solve a task before porting the next one.

## Later families

After the low-reuse-cost batch, group arithmetic/text predicates and sequential
forms, then search/catalog/inbox/social workflows. Scrolling, date/time native
controls, hover, drag/resize, canvas/geometry and timers need additional shared
primitives. These are the genuinely larger behavior gaps, not 113 equally large
independent environments. Preserve them in the denominator and backlog.

Encoder improvements, policy speed and real-site capture remain useful separate
work. They should resume on measured bottlenecks and should not gate these ports.
