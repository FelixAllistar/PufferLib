# Reusable WebNav simulator primitives

Build low-fidelity applications from reusable behavior, then use MiniWoB and
WebArena as compatibility and transfer evaluations. The 23 MiniWoB families
remain checked reference implementations; a family or benchmark task name is
not the intended boundary of a reusable primitive.

The chosen boundary is **a small state machine with a public interface and
independent invariants**. A text field owns editing/selection behavior. A query
owns filtering/sort/page state. A draft owns edits before commit. An application
owns rules such as who can approve a review or transition an order. Styling and
DOM layout bind these states to public controls; they do not define the state
transitions. This is behavior composition, rather than a React or CSS runtime.

## Composition layers

| Layer | Units | Example |
|---|---|---|
| Controls | Text editing, buttons, choices, menus, calendar, pointer/scroll | The same date selector binds a report range or an issue due date |
| Pages | Public hierarchy, route, history, loading, errors, tab/context | Open a record, return to the previous filtered list |
| Persistent data | Typed records, identity, relationships, queries, aggregates, drafts, commit/cancel | Products, posts and issues all reuse search/filter/sort/pagination |
| Workflows | Domain rules plus page/data composition and a goal | Find records, inspect evidence, edit selected records, commit, report a result |

[catalog.json](catalog.json) names the units, dependencies, existing source
references, generation axes and current status. Its operations are a proposed
contract, not evidence they are already extracted and reusable. Domain engines
remain necessary: generic forms alone cannot model checkout, Git commits or
OSRM routing faithfully.

Persistent entity IDs, route IDs and transient observation node refs must remain
distinct. A reordered table must not change the identity of its records. The
policy uses current public node refs; obsolete refs reject without side effects.
It receives visible labels/values/relationships, not hidden goals or private
entity annotations. Shared primitives use one public action/observation contract
and the existing shared learner; new applications do not get separate policies.

## Implemented core

[text/Model.bend](text/Model.bend) now owns the editing transitions previously
embedded in MiniWoB's goal-bearing field. The legacy module delegates editing
to this goal-free primitive while retaining its constructors, task goals and
scoring. All eight dependent MiniWoB families now pass their laws/native checks
through that compatibility layer: forms, controls, numeric, composite forms,
autocomplete, scroll, catalog and visual. The four builds pending at the pause
completed under the restored normal resource guard.

[browser/Model.bend](browser/Model.bend) implements application-local route
navigation, eight retained history entries, back/forward, a single pending
load, cancellation, failure, retry and reload. Time advances before dispatching a new
action. Starting a new navigation clears forward history and replaces the
pending load. Retry keeps the failed route and history. Reload and navigation
to the current route restart a load while retaining both history directions.

[browser/Wire.bend](browser/Wire.bend) adds a private 64-word row, dirty-row
reset, small command decoding and a public navigation-state projection. Reset
varies route count, latency and the finite failure schedule from salted seeds;
it reads no benchmark data. Latency/failure schedules are private, while current
route, load/error status and available history directions are public.

The wire is not a policy schema or a new WFFamily ABI. Its C entry validates
bounds/version and rejects malformed input without mutation. Twelve laws and
9,600 independent native differential steps pass. It does not supply page
labels, links, goals, rewards, restored query state or tabs.
[status.json](browser/status.json) keeps native, browser and learner gates
separate.

[browser_contexts](browser_contexts/README.md) composes those states into up to
16 independent tabs. Nine laws and 32,000 native differential steps pass across
64 seeds, including background loads, close/replacement, monotonic identities
and atomic rejection. Its close-selection rule follows the audited WebArena
browser wrapper. Three independent Chromium fixtures pass 34 settled tab/history
comparisons. They caught and corrected initial blank-page history and repeated-URL
navigation behavior. Public WF tab/back/forward/reload/new/close/retry controls
pass 32 generated-world checks and 512 legal actions from the unchanged random
and sampled policies. These controls project only public tab facts and reuse the
existing action space. Binding them into the original-browser runner, address
entry and complete application/training composition remain open. Synthetic
timing and pending navigation commits are not browser-qualified.

[records](records/README.md) supplies typed values, persistent identities,
revision-checked writes, composable predicates, deterministic sorting and
pagination. Eight laws and 612 independent numeric query cases pass.
[transaction](transaction/README.md) supplies isolated drafts, schema validation,
commit/cancel and conflict handling; seven laws and 260 native cases pass.
These are bounded native checks, not original-site parity claims.

[finish](finish/finish.h) supplies explicit policy submissions with navigation,
mutation or retrieval type, a declared outcome status, and JSON data/error
payloads. Five lifecycle laws and native format/boundary checks pass. A submitted
reply cannot change; budget expiry produces no submission. JSON number lexemes
and UTF-8 payloads are preserved exactly. The preset accepts 16 KiB of retrieved
data and 2 KiB of error detail, with explicit rejection rather than truncation.
The direct API and the response form each pass 28 cases against WebArena's pinned
public `FinalAgentResponse` schema, without loading task answers. This transport
does not grade claims. [Its status](finish/status.json) separates native and
browser qualification. The shared strict public-view parser serves policy and
response RPCs; random/checkpoint state-isolation checks still pass. The pinned
official response evaluator rejects a JSON `null` absence marker for all three
task types, with matching synthetic positive controls.

[response_form](response_form/response_form.h) composes text editing and finish
into host controls for type, status, JSON data, error detail and an explicit
Finish button. Five laws and native Unicode, correction, expiry, focus handoff, composition
and capacity checks pass. It uses the existing WF actions and shared policy
dimensions. It can append up to 16 controls to a public application view;
the caller assigns a separate reference interval. Drafts are never truncated:
if their display exceeds the shared observation text budget, clipping is reported
and submitted bytes remain intact. The browser runner offers `--preset response-v1`:
112 DOM nodes and 12 KiB of text plus up to 16 host controls and 4 KiB of text.
The default remains the declared legacy navigation preset. Four browser composition
fixtures check exact answer bytes, keyboard ownership, correction, expiry, stale
refs, observation capacity and the unchanged random/sampled policy interface.
Host controls appear in the public view/trace; they are not injected into site DOM.
One original-site scripted check now passes the pinned official grader: two
shopping-admin navigation clicks followed by native Finish, scoring 1/1. The
unchanged-source retry peaked at 1.70 GiB with no memory-limit hits. This qualifies
the NAVIGATE submission transport only; native workflow parity and learned
performance remain separate gates.

[page/page.h](page/page.h) adds versioned public URLs, headings, table positions,
spans, header edges, labels and descriptions alongside the unchanged WF ABI v2.
The browser's opt-in `collectPage` supplies the same metadata contract. Three new
browser fixtures and all 18 earlier regression fixtures pass, including an
explicit check that opt-in collection preserves subsequent legacy observations.
Omitted or ambiguous relationships are reported. This is a bounded DOM projection,
not the full accessibility/header-association algorithm. The current policy
encodes WF parent hierarchy; the extra companion fields are not yet new learned
features. [Page status](page/status.json) separates these qualifications.

The first [record-browser application](../apps/record_browser/Model.bend)
composes the checked cores into list/detail navigation with search drafts,
filters, sorting, pagination and a modal edit form. Seven laws and the native
composition fixture pass, including draft isolation, cancel, invalid-value
correction, committed query updates and repeated-save behavior. A Bend generator
now varies 8..16 records, their order/values/status and loading behavior. Native
checks cover 64 generated worlds and a public-only browse/edit/search trace,
including stale refs, disabled controls, table relationships and independence
from private future timing. The unchanged random/checkpoint policy RPC drives
16 application/form episodes through 965 legal choices. That is interface
qualification, not a learned success score. No training tasks or rewards have
been registered for this composition, and original-site comparisons remain open.

All builds use the existing serialized guard: up to 6 GiB, no swap, and one CPU.
If system resources constrain progress, pause the goal and alert the user,
following [the standing coordination rule](../AGENTS.md).
Use `node ocean/webnav/families/build.cjs primitive:NAME --test` or
`node ocean/webnav/families/build.cjs app:NAME --test`.

## Full WebArena planning inventory

[requirements.json](../benchmarks/requirements.json) accounts for the pinned
812 task IDs and 190 templates: 374 mutation, 325 retrieval, 113 navigation
tasks; 48 tasks span multiple sites. This is an evaluation inventory with
conservative site/outcome capability envelopes. It does not establish the
minimal primitives, original event semantics or simulator coverage of any
template. No private expected answers or grading expressions are exported.

Regenerate it with the small source inventory utility:

```sh
node ocean/webnav/benchmarks/requirements.cjs > ocean/webnav/benchmarks/requirements.json
```

The original 18-task transfer preset consists of one customer-list task, two
theme-settings tasks, five order-status tasks, five relative-date reports and
five explicit-date reports. The measured three-task pilot selected customer
list (157), Magento Blank settings (374), and fraud orders (676). These are
WebArena navigation tasks, not a long-workflow curriculum or the MiniWoB suite.

The next engineering units are companion metadata features, public browser-shell
controls and complete application/learner integration.
Audit original event/state dependencies and compare
independently advanced states before claiming a port.
Train worlds/goals from independent generators rather than importing held-out
task instances. Report simulator capability, browser fidelity and learned
performance separately.

Exploration algorithms, reward shaping and curriculum distributions remain
research choices. The engine exposes branching, prerequisites, persistence,
observable state and recoverable failures so those choices can be studied.
