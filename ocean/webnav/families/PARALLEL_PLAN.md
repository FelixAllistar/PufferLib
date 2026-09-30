# Family implementation queue, updated 2026-09-29

The user confirmed the sweep stopped on 2026-09-29. Root has integrated and
validated the email, calendar and controls handoffs, one family at a time under
the 6 GiB/no-swap guard. Workers remain source-only in this checkout, without
worktrees, and never run Bend, compilers, tests, browsers or training.

## What already exists

The pinned registry has **125 task names**. There are **125 checked family
implementations**, **no remaining legacy task ports**, and **zero pending
names**. The eleven formerly legacy entries now pass the current family gates.
125/125 describes checked behavior in the current family system; it does not
mean full DOM, layout, generator, timing, or pointer parity.

| Family | Registered | Checked family | Legacy | Pending | New trainer |
| --- | ---: | ---: | ---: | ---: | ---: |
| click | 16 | 16 | 0 | 0 | 10 |
| forms | 11 | 11 | 0 | 0 | 8 |
| panels | 9 | 9 | 0 | 0 | 0 |
| tree | 1 | 1 | 0 | 0 | 0 |
| autocomplete | 2 | 2 | 0 | 0 | 0 |
| calendar | 5 | 5 | 0 | 0 | 0 |
| menus | 2 | 2 | 0 | 0 | 0 |
| controls | 6 | 6 | 0 | 0 | 0 |
| scroll | 4 | 4 | 0 | 0 | 0 |
| drag | 10 | 10 | 0 | 0 | 0 |
| drawing | 2 | 2 | 0 | 0 | 0 |
| geometry | 5 | 5 | 0 | 0 | 0 |
| visual | 9 | 9 | 0 | 0 | 0 |
| numeric | 9 | 9 | 0 | 0 | 0 |
| editing | 5 | 5 | 0 | 0 | 0 |
| email | 10 | 10 | 0 | 0 | 0 |
| social | 3 | 3 | 0 | 0 | 0 |
| market | 1 | 1 | 0 | 0 | 0 |
| board | 1 | 1 | 0 | 0 | 0 |
| composite_forms | 5 | 5 | 0 | 0 | 0 |
| travel | 3 | 3 | 0 | 0 | 0 |
| typed_inputs | 3 | 3 | 0 | 0 | 0 |
| catalog | 3 | 3 | 0 | 0 | 0 |
| **Total** | **125** | **125** | **0** | **0** | **18** |

The twenty-three checked families have Bend models, generators and laws; ABI-v2 public
views; independent native tests; original-page differential harnesses; and
public scripted solvability checks. Evidence and bounds are in each family's
README and RESULTS.json. Historical RESULTS hashes are not automatically an
attestation of subsequent edits; later validation is documented separately.

Two separate learned adapters exist:

- [Click results](../../webnav_family/RESULTS.md): a 108,160-parameter shared
  policy, three million steps, ten original-page tasks. Soft synonym checkboxes
  score 63/100; widgets 94/100; large checkboxes 99/100; the other seven 100/100
  with greedy actions. Focused widget/focus policies reach 100/100.
- [Forms results](../../webnav_forms/RESULTS.md): a 191,680-parameter shared
  policy scores 100/100 on each of eight original-page tasks. Its final phase
  is two million steps, following earlier training/curriculum phases. Actions
  insert whole public text candidates; these are not raw-keystroke results.

The public DOM parser, native Unicode tokenizer, frozen Potion-base-8M
256-dimensional embedding lookup/pooling, and exact-string cache also exist.
Tokenizer/vector parity does not imply semantic understanding: the recorded
24-case probe misses eight cases, and the soft-checkbox learning result still
exposes that weakness. Encoder improvement can proceed separately from task
implementation. The family trainers do not yet provide a unified DOM/AX policy.

## Completed source wave and root validation

Three GPT-6 Sol High workers completed separate directories without compiling.
Root owns the integrated source now. Subsequent completed waves are recorded below.

| Worker | Exclusive directory under `families/` | Tasks | Work |
| --- | --- | ---: | --- |
| `email_family` | `email/` | 10 | Inbox, search, message view, reply/forward, star/delete, language variants |
| `calendar_family` | `calendar/` | 5 | Four datepickers and distinct daily-calendar drag/name/create behavior |
| `controls_family` | `controls/` | 6 | choose-list, two sliders, spinner, two color pickers |

This added **20 previously pending names plus the legacy choose-list port**:

| Family | Checked Bend laws | Native result | Original-page episodes | Public scripted successes |
| --- | ---: | --- | ---: | ---: |
| calendar | 18 | source regression passed | 100 | 5,000 / 5,000 |
| controls | 17 | 592 checks passed | 120 (333 state/reward comparisons) | 6,000 / 6,000 |
| email | 12 | 641 cases passed | 200, zero rejected for bounds | 10,000 / 10,000 |

Each family records exact commands, source hashes and limitations in its
`RESULTS.json`. Calendar uses a fixed wall-date and settled animations; controls
tests normalized jQuery slider/spinner events and valid hex color entry; email
uses CDP actions after automatic scrolling and original icon preloading.
Color picker pointer/HSV paths, complete NL/template distributions, native
coordinate fidelity and a learned policy for these families remain unfinished.

Root owns this plan, TODO, registry, inventory and shared integration. Workers
report shared changes instead of making them. The new `bend-cpu-families` skill
documents checked binding/wire patterns. Future workers first hand off a small
model, one semantic law and a minimal wire path for root's permitted compile;
expansion follows that feedback instead of waiting for a whole family handoff.

Every worker reads the cached stock Bend 2.0.6 guide, a checked family, and the
pinned original HTML plus its relevant dependencies. Deliverables are Bend
behavior/generation/laws/wire code, public C projection/transport, independent
test and browser harness source, a public controller where supported, and a
per-task source audit with precise bounds. No fabricated PASS results.

## Completed integration waves

The social (3), composite forms (5), and scroll (4) packages pass all family
gates: 36 Bend laws, independent native/dirty-reset/private-view tests, 240
original browser episodes and 12,000 public scripted generated episodes.
Evidence and supported action presets are in each RESULTS.json. Scroll models
Chromium153 native active-selection state, and public views can retain
offscreen options while action validation still requires visibility.

The same three GPT-6 Sol High workers subsequently completed catalog (3),
travel (3), and typed inputs (3). Root qualified 34 Bend laws, independent
native tests, 180 original-page episodes and 9,000 public scripted successes.
Catalog now follows the original adjacent phone pagination and reevaluates
search input on page changes. Typed inputs preserve native date/time
normalization and all six Unicode labels. Travel preserves source ranking,
partial ticket rewards and validation recovery within its atomic field preset.
The three workers have finished; root owns all six new directories.

This turn moved coverage from **59 to 91 task names**: eleven legacy ports and
21 previously pending implementations. Qualification across the touched
families passed 1,480 original-page episodes and 50,000 generated public
scripted episodes. These totals include regression checks for previously
implemented tasks in click/forms. They are not learned-policy scores.
Only root compiled, serially under the 6 GiB/no-swap guard. The worker launcher
has no independent service-tier/Fast setting; the workers used Sol High.

Root's eleven legacy ports are complete: click/forms/tree/autocomplete passed
1,060 original-page episodes and 29,000 public scripted generated episodes.
Click alone passed 737,280 independent transitions; the old click/forms
training adapters passed 120 and 256 native episodes respectively.

## Historical final-wave assignments (now completed)

At the start of the final wave, the following packages covered **34 pending names**. Names below are
work packages; they do not change the registry yet. Before workers start a split
of `workflows` or `drag`, root will assign separate subdirectories and own the
single family entry point and registry mapping. Two workers must never edit
the same family entry point, Generate, Wire, or Model module.

| Priority | Package | Pending names | Shared mechanics and boundary |
| --- | --- | ---: | --- |
| Next | Timed market | 1 | stock-market; evolving public prices, clock and threshold decision |
| Next | Autocomplete | 1 | use-autocomplete; the nodelay task is already ported and checked |
| Next | Editing | 5 | find-word, highlight-text, highlight-text-2, text-editor, terminal; separate selection, formatting and terminal state models |
| Pointer wave | Position/resize | 4 | drag-box, drag-circle, drag-cube, resize-textarea; capture, movement, bounds and release |
| Pointer wave | Drop targets | 5 | drag-items, drag-items-grid, drag-shapes, drag-shapes-2, drag-single-shape; identity, hit testing, collision/drop acceptance |
| Pointer wave | Sort by dragging | 1 | drag-sort-numbers; reorder state and completion predicate |
| Pointer wave | Geometry | 5 | bisect-angle, circle-center, find-midpoint, grid-coordinate, right-angle; geometric constraints and reward tolerances |
| Pointer wave | Drawing | 2 | draw-circle, draw-line; stroke sampling and source scoring |
| Visual wave | Visual selection/counting | 7 | click-color, click-shades, click-shape, count-shape, count-sides, identify-shape, visual-addition |
| Visual wave | Pie selection | 2 | click-pie, click-pie-nodelay; sector geometry and animation scope |
| Next | Board | 1 | tic-tac-toe; legal moves, opponent behavior, terminal reward |

The remaining 34 names are now authorized for implementation. Three Sol High
workers own disjoint directories: editing (5) in `editing/`, stock-market (1)
in `market/`, and tic-tac-toe (1) in `board/`. Root extends `autocomplete/` with
the delayed task. All three worker packages have passed their small first
model/law/wire/native checks; full behavior and browser qualification are still
in progress. No new registry credit is assigned for those initial checks.

After those handoffs, the same workers take `visual/` (9), `geometry/` plus
`drawing/` (7), and `drag/` (10), respectively. Root integrates and validates
serially while the workers write source. Shared registry changes remain root's
responsibility; `workflows` will become `market` only after qualification.

The eleven legacy ports completed on 2026-09-29 are click (6), forms (3),
navigate-tree (1), and use-autocomplete-nodelay (1). These improve current-runtime
coverage without reducing the 75 originally pending names. A verified manifest
of the historical original MiniWoB release is still needed before claiming that
specific subset is complete; the 125-name pinned MiniWoB++ registry is a
different denominator.

Concrete source-audit warning for composite forms: the pinned
`form-sequence-2.html` reward checks that some radio is selected, not that the
requested radio is selected. Original-compliance laws must preserve that rule.
An improved benchmark may change it in an explicitly different version.

## Integration and validation order

1. Review source and wire layouts. Reject private-goal
   leakage, generated answer shortcuts, cross-worker edits, and placeholder
   tests. Source-only handoffs remain unvalidated until root runs the gates.
2. Respect the current sweep/build coordination. The prior hold was explicitly
   cleared on 2026-09-29; no repeat permission is needed for this validation.
   Keep the 6 GiB/no-swap guard and serialize all compilation.
3. For one family at a time, run Bend proofs and native tests through the guarded
   builder. Resolve compiler issues within the cap; no unrestricted retries.
4. Run original-page comparisons including wrong actions, intermediate states,
   timeout boundaries and rewards, then public-only scripted solvability.
   A successful script alone does not establish parity.
5. Update per-task evidence and coverage only for supported, validated behavior.
   New names, migrated legacy names, trained policies, and full parity stay
   separate counts. Retain exact source revision and scope of each measurement.
6. Add learned adapters and evaluate original-page transfer. Panels (9), menus
   (2), and numeric (9) already have checked models ready for this stage. PPO
   work need not block the next independent source-implementation wave.

As those mechanics become validated, use email, catalog, travel and composite
forms as the first larger site templates; this need not wait for all 125 names.
Vary content, labels, field order, layout, irrelevant
controls and accessibility quality independently; evaluate held-out templates
and real browser pages. Keep a stable public action contract and explicit
overflow reporting before expanding the text model or observation footprint.

## Completed final-coverage wave

Delayed autocomplete, market, board, geometry, drawing and editing are qualified,
followed by visual and drag, bringing the registry to 125/125. The two autocomplete tasks passed 200
original-page episodes and 2,000 public solves. Market passed 20 original-page
episodes and 1,000 public solves. Board passed 20 original-page episodes and
1,000 public games: 808 wins, 192 draws, no losses. Draws remain separate.
Geometry passed 100 original-page episodes and 5,000 public episodes with raw
reward at least 0.95; actual mean scores range from 0.977455 to 1.0. Drawing
passed 40 original-page episodes and 2,000 full-credit public solves.

Editing passed 16 laws, 100 original-page episodes (260 actions), and 5,000
full-credit generated public episodes. The terminal fixture now correctly
imports dotted extensions; the editor preserves the original unformatted
all-text Submit exception.

Visual passed 26 laws, native reset/private-goal checks, 180 original-page
episodes (307 actions), and 9,000 full-credit public scripted episodes.

Drag passed 15 laws, all ten native task checks, 188 original-page episodes
(545 actions), and 10,000 full-credit generated public scripted episodes.
The browser set includes all six cube goals, wrong actions and timeouts.
The whole final wave moved coverage from 91 to 125 registered names.

All 23 families are now checked under their documented presets. No task name
remains assigned without implementation, but full DOM/layout/generator/raw
pointer parity remains unverified. The 18 click/forms tasks plus 11 panels/menus tasks now have
learned training adapters (29 total); the family scripted results are not PPO scores.

Next: extend the common public-view learner, establish per-task learned
baselines and original-page transfer, then expand held-out site-like templates.
See ../TODO.md for the current worklist. The final behavior-coverage wave is
integrated; the active learner wave below remains source-only.


## Completed learner adapter wave — 2026-09-29

User requested parallel adapter implementation. Three GPT-6 Sol High workers
completed their source handoffs. Root has integrated the shared build dispatch,
passed all adapter tests and original-page smoke checks, and compiled all three
CUDA trainers. Short training/evaluation runs are complete; per-family RESULTS.md/JSON files
record the final checkpoints and per-task scores. Existing checked Bend family files are
read-only for this wave, preserving their conformance evidence.

| Worker | Scope | Exclusive write ownership | Status |
|---|---|---|---|
| learn_numeric | 9 numeric tasks | ocean/webnav_numeric/; config/webnav_numeric.ini | validated; short learned baseline recorded |
| learn_email | 10 email tasks | ocean/webnav_email/; config/webnav_email.ini | validated; short learned baseline recorded |
| learn_catalog | 3 catalog tasks | ocean/webnav_catalog/; config/webnav_catalog.ini | validated; short learned baseline recorded |

These are 22 validated additions: 51/125 task names now have trainable adapters.
This does not mean 51 solved tasks. All workers have completed; no background
training or browser jobs remain from this wave. Root owns
build.sh, registry, cross-family refactoring, review and serial validation.
Workers may not run builds, compilers, Bend checks, tests, browsers or training.
No worktrees. Root retains the 6 GiB/no-swap guard and one heavy job at a time.

Handoff gates: public-only features/candidates and history; meaningful action
coverage without scripted answer injection; masks, dirty reset and terminal
history; separate full-credit and partial reward logs; held-out native learning;
original-browser transfer with independent page generation. Report any bounded
or unsupported action/text/scene cases explicitly. Do not mark a task learned
solely because an adapter compiles or the existing scripted controller solves it.
