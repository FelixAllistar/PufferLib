# Retained river context

The additive context API retains the first accepted river solution for one
heads-up hand. It is based on the V5 solver at
`8ae0b7ed8742a25c89b29a382eaad23442c57120`. The original stateless native entry
points remain available. Enable the context API explicitly to retain a solution.

## Source layout

`continuation.c` compiles the existing native engine together with the
context implementation. The new native API is declared in
`river_continuation.h` and implemented in
`river_continuation_impl.h`. `pc_sha256.h` supplies canonical audit
hashes. `continuation_transport.py` supplies a network-free adapter around
the existing `eval.Native` object.

`poker.h` has an optional Puffer bridge. Its default behavior is
unchanged because `POKER_RIVER_CONTINUATION` defaults to zero. The native engine and
river solver interfaces remain those of the stated commit.

## Build the heads-up context library

From the repository root:

```sh
bash ocean/poker/build_continuation.sh
```

The default output is `build/poker/libpoker2_context.so`. An optional output path
is resolved relative to the repository root unless it is absolute. `CC` selects
the compiler, with `gcc` as the default. This entry builds only the heads-up
context library and does not invoke or change the existing `build.sh` defaults.

This command targets the build machine's architecture. Bind any evaluation to
its exact compiler inputs and resulting binary; a build on another machine can
have different bytes. `PC_TESTING` and sanitizer support are proof-only build
choices and are not required by this production command.

## Python lifecycle

Construct one `Context` per policy owner and seat. `native` is the original
`eval.Native` class loaded with the context library above.

```python
from continuation_transport import Context, Session

context = Context(native, owner="run/job/worker0/lane0/seat0", seat=0,
                  policy=7, samples=0)
generation = 1
context.begin(generation)       # before requesting or dealing the true hand
session = Session(context)

# Capture this generation in the request envelope before issuing the request.
request_generation = generation
status = session.ingest(request_generation, public_response)
if not status["terminal"]:
    action = session.decide(request_id=1)

# Each later response uses the generation captured for its own request.
# Each later own decision uses a strictly increasing request ID.
```

Each response must include the same seat and immutable own pair, the complete
ordered action history, and the visible board prefix. Prefer an explicit boolean
`terminal` field. The adapter accepts the official response's `winnings` key as a
legacy terminal marker when the explicit field is absent; it does not read that
payoff value to choose actions.

`ingest` returns `terminal`, `closed`, `events`, `new_events`, and `audit`.
An exact duplicate nonterminal response is idempotent. An exact duplicate pending
decision request returns the saved action and leaves the saved post-decision RNG
unchanged. The next public history must acknowledge that exact own action.

Do not infer the generation from whichever hand happens to be active when a
response arrives. A stale response with identical cards and history must still
carry its old generation and be rejected. If the server chooses the client seat
only in its first response, begin one context for each seat before requesting
the hand, route the validated response to its seat, and abandon the unused one.

On an uncertain action response, call `session.abandon()` and stop using that
hand and token. The adapter has no HTTP implementation or wager retry path.
To reuse the owner for a new true hand, call `context.begin(new_generation)` with
a strictly greater generation and construct a fresh `Session`. A full engine
rebuild while replaying the current hand is not a new hand and must not call
`begin`.

Call `context.close()` to release native storage when the owner is finished.
Protocol faults latch the context error state. They do not invoke the fallback
policy. A later true hand requires a strictly newer generation.

## Retained solution behavior

The first accepted eligible river solve retains the full `RcfrGame`, including
all averaged strategies and public edges. Acceptance uses the existing V5
checkpoints and certificate threshold. Allocation failure, unusable
certificates, or nonconvergence before acceptance leave the context unanchored;
the original fallback handles that decision and a later eligible decision may
try again.

After acceptance, exact public events move the context to a descendant of the
same immutable game. No solver creation or iteration occurs on a retained hit.
A legal action outside that game disables continuation for the rest of the hand;
the original fallback handles later decisions without accepting another anchor.

Public snapshots and anchor hashes exclude own-card IDs, opponent holdings, and
payoffs. Own cards select a row only after the public solve has completed. Whole
context audit hashes may bind the selected-action receipt and RNG states, but
they do not encode own-card IDs as an audit field.

## Terminal state and settlement

`PcPublic` uses **unsettled** public money: terminal contributions and pot remain
present, and stack values exclude payouts. `pc_public_from_game` rejects an
already settled native terminal. Use the pure checked transition functions to
produce the terminal public edge before closing the context.

An accepted terminal response closes the policy context. The closed receipt
still validates generation, owner, own holding, and exact terminal prefix before
an identical terminal response can return idempotently. Revealed opponent cards
and actual payoffs belong in a separate settlement engine after context close.
The transport's optional `settle_revealed` method is same-engine true-card replay;
independent payout evidence must use separate contribution arithmetic and a
separate hand evaluator.

## Optional Puffer bridge

For heads-up policy-controlled bots, compile with both
`POKER_RIVER_CFR=1` and `POKER_RIVER_CONTINUATION=1`. Supply a positive integral
`river_context_namespace` in the environment kwargs. Its supported range is
`1` through `2**53 - 1`. The coordinator must assign it uniquely across relevant
jobs, workers, and vector instances; the owner also binds seed, global environment
index, lane, and seat.

The bridge begins contexts before the true deal, starts them after dealing, and
observes every real public action, including learner and league-opponent actions.
Each policy-controlled bot has its own lane and seat context. Learner action
selection does not go through the bot's context policy. True resets and policy
changes close the prior hand and advance the generation.

The lightweight Puffer proof harness in `continuation_tests/` supplies a minimal test ABI. It
is not the full Puffer runtime or a PPO training result. Six- and nine-player
builds do not enable this heads-up context path.

## Read-only inspection

`pc_audit`, `pc_certificate`, `pc_node_count`, `pc_node_info`, `pc_average`, and
`pc_allocated_bytes` expose context and retained-game facts without returning a
mutable solver pointer. `pc_average` requires the caller to supply explicit
capacity in doubles. `Context.audit()` exposes the scalar receipts and hashes
as a Python dictionary. The [synthetic checks](continuation_tests/README.md) and separate native oracle
define bounded correctness workloads; this document does not claim playing strength.

The [context evaluator](context_evaluation/README.md) provides full actual-policy
replay, a separate settlement oracle, fixed-quota series execution, and an
offline injected-HTTP pipeline test.
