# Stock CPU Bend audit

Audited against the complete locally installed guide at `/home/felix/.bend/app/2.0.6/Jj1Du8/guide/GUIDE.md` and compiler `bend 2.0.6`. [Upstream guide](https://github.com/bendlang/bend/blob/main/guide/GUIDE.md) may change; the local version is the reference for this implementation.

The current code follows the relevant language mechanics: pure state transitions, explicit Data duplication, affine array ownership, shrinking Nat recursion, helper functions for matching computed pairs, no unsafe definitions and foreign IO limited to transport. The `read0`–`read12` chain follows real syntax/ownership restrictions; it is ABI plumbing, not an elegant domain model. Game.bend mixes that plumbing with behavior and should be split before the suite expands.

The design was under-specified. Previously only terminal absorption was proved. Checking that one law did not establish reset correctness, goal correctness, observation safety, validity or MiniWoB parity. The audit adds five explicit requirements without weakening the original law:

| Law | Scope |
| --- | --- |
| terminal_absorbs | A state marked done=1 is unchanged under any step action. |
| observe_preserves_state | Dispatching observe returns the same state. |
| reset_discards_history | Dispatching reset equals reset(new seed), independent of old state/action. |
| success_preserves_state | Marking success changes only done/result to 1/1. |
| failure_preserves_state | Marking failure changes only done/result to 1/2. |
| unfocused_edit_is_identity | The edit primitive changes nothing when focus=0. |

The final law checks the primitive, not the full step: a no-op action still consumes episode time. Focus is checked before task in the edit guard so its false case reduces directly in the proof; pure boolean behavior is unchanged. These laws cover every value of their quantified fields, not sampled examples, but they are still a small specification. They do not establish that a success decision was appropriate or that all reachable states are valid.

The stock guide convention keeps specifications in LAWS.bend and implementation proofs in PROOF.bend, and requires a checker gate. The user has explicitly requested stronger law-driven implementation, so this audit adds requirements to LAWS.bend. Future fixes must preserve those requirements. A trivial reflexivity proof is acceptable when both sides reduce identically; the value lies in an independently stated requirement, not proof length.

Next laws need better types and explicit preconditions. The current raw U32 fields admit malformed states: arbitrary task tags, focus indices and text lengths. Proving a bound for every such state would be false. Define a valid initial state and show preservation, or represent the bounds structurally. Keep domain laws separate from ABI bounds; Bend arrays wrap indices, so affine ownership alone cannot prevent one row addressing another row.

The code is CPU-only but not CPU-parallel: the bridge retains one runtime and serializes calls, and batch threads one array through rows. Parallelism needs independent owned chunks and balanced work. Profile before changing it; do not introduce an experimental compiler or GPU dependency.

Trust boundaries remain the stock checker/compiler, generated-runtime bridge, C foreign functions, observation packing, native policy and browser adapter. Model proofs do not prove these components or equivalence to real browser behavior. Native differential tests and browser comparisons remain required. See TODO.md for validity, frame, focus, goal, noninterference, permutation and serialization obligations that remain open.

Validation: six laws check; 102,400 native specification transitions and four concurrent callers pass; adapter reward/autoreset checks pass; 30 expert browser episodes match after the edit-guard reordering. These test results complement the laws and are not proofs of full browser equivalence.

Follow-up: the original-task lane now has a separate typed model in miniwob/Widgets.bend and a packing module in miniwob/Wire.bend. 22 widget/generator laws check, including checkbox toggle involution, goal-list preservation by induction, task preservation and terminal/timeout absorption. The C boundary validates supported tags, node counts and fields. Exhaustive native tests cover all 5,456 checkbox target/state pairs with 2..6 controls; five original HTML tasks pass matched-instance conformance. See miniwob/README.md for exact limits. This is progress toward the law-driven design; reachable-state bounds, text editing, unrelated-widget frame conditions and full browser equivalence are not yet proved.

Text follow-up: `miniwob/Text.bend` adds a list-based editing core, separate from `TextWire.bend`'s ASCII transport. Twelve laws prove goal/frame properties, unfocused identity, terminal/deadline behavior and exact select-all replacement, with two list lemmas. An insertion implementation that dropped its payload was rejected by the replacement law. The lane now checks 34 laws plus the legacy pilot's separate six. Native tests cover 8,184 edit/selection cases and transport boundaries; three original text tasks pass 600 browser episodes. General state-validity, Unicode/caret, and full browser equivalence proofs remain open. Details: [TEXT_TASKS.md](miniwob/TEXT_TASKS.md).

## Four-task expansion

The root and three Luna/max task agents read the installed stock 2.0.6 guide
and existing source examples. Sequence/select, tree, and autocomplete behavior
live in typed Bend models with separate wire serialization. The combined proof
entry checks 95 laws; the six legacy pilot laws remain separate. C validates
private row bounds and supports native differential tests; C/JavaScript operate
the independent original-browser oracle. This is not an all-Bend browser stack.

Integration review caught and corrected tree event bubbling, nonstructural
visibility recursion, autocomplete retained-term/menu boundaries, wire caret
offsets, and reset timing validation. A negative proof check changed tree
folder toggling into an identity: the checker rejected
`wrong_folder_expands_and_runs`. Production source was unchanged. This shows
that particular property detects that mutation, not completeness of the laws.
Compiler/runtime, C packing, browser behavior and observation noninterference
still require independent testing and further properties. See
[miniwob/EXPANSION.md](miniwob/EXPANSION.md).

## Twelve-task training integration

The combined original-task entry now checks **110 laws**, plus the legacy
pilot's separate six. The 15 additions are twelve sequence-generator
mapping/instruction assertions and three tree-generator initial-state/size
properties. The twelve mapping assertions are small regression specifications,
not deep behavioral proofs. The tree size property bounds the declared size;
it does not by itself prove that every serialized tree has that length or
valid parent/subtree references. Native generated-instance tests cover those
transport invariants. Text-generator validity and formal public-observation
noninterference are still unproved.

Integration followed the stock guide's structural recursion and explicit
`Data` duplication rules. Generator checking caught missing duplication and
nonstructural symbolic proof cases. The tree size proof now eliminates a
six-constructor shape type. Native review also caught inconsistent subtree
endpoints and overlapping sequence buttons; those generators were corrected
before training. None of these fixes required the experimental Bend fork.

Task transitions, reward logic and reset generators remain in CPU Bend.
Public-view extraction, feature construction, frozen text lookup and policy
training are C/CUDA infrastructure. The C adapter is tested for private-target
noninterference by mutating hidden goals and comparing public views, action
masks and features; this test is not a formal proof. The new training profile
has 1,536 generated-instance / 61,440-transition integration checks and a
12,000-episode public scripted solvability check. See [TRAINING.md](TRAINING.md)
for learned-policy results and explicit generator limitations.
