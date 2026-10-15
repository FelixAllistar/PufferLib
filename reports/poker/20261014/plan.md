# Poker CPU environment research and implementation plan

Research date: 2026-10-14. Benchmark heads-up no-limit Hold'em at 200 BB first,
with Slumbot evaluation available before a substantial training run. Use one
compact C implementation compiled separately for 2, 6 and 9 seats. Simulation
runs on the CPU; the existing GPU policy inference and training remain available.
Six players is the next target, followed by nine. Keep 200 BB as the main
performance target while supporting shorter and unequal stacks in the same kernel.
Train through self-play from scratch, with leagues and external bot evaluation.
BC, dataset acquisition/import and teacher distillation are outside the agreed
scope. Follow the current KAG/Pokemon league approach.

Keep opponents small as well as the simulator. Multi-gigabyte strategy tables
are outside the initial local league budget. A native rules bot, iterated against
Slumbot and then frozen, is a required initial baseline and league member. Add
small frozen neural self-play policies. CFR training, imported tabular blueprints
and solver ports have been removed from the implementation scope by the user.

Episode scope is independent hands. Settle each hand, record every seat's net
chip return, then restore all seats with configured or sampled starting stacks
and deal again. Tournament progression and payout objectives are outside scope.

Tournament adaptation remains a useful future direction. The hand engine,
observation conventions, league infrastructure and learned poker representations
can be reused; policy transfer must be measured. Tournament prize equity is not
generally proportional to chips, so a chip-profitable action can lose expected
prize money, especially near payout thresholds. The
[Independent Chip Model](https://arxiv.org/abs/0911.3100) approximates this effect;
it is not an exact model of tournament play. A later adaptation needs shorter
stacks, antes, payout/remaining-field context and tournament-specific training
and evaluation. Keep the current implementation focused on independent hands.
The [AIVAT paper](https://dmorrill10.github.io/assets/publications/aaai18-burch-aivat.pdf)
explicitly distinguishes reset-stack chip-profit evaluation from tournaments.

The main performance problem is useful decisions per second, including inference
and collection. A fast dealer attached to nine mostly inactive policy rows would
miss the objective. The main strength problem is obtaining qualified opponents:
public source, downloadable weights and demonstrated strength are different things.

Implementation is now in `ocean/poker/`; see its README and the implementation
report in this directory for measured status. The Puffer adapter uses one fixed
learner seat against native rules opponents and small frozen PufferNet banks.
Banks stay immutable per training stint; only acting opponents run inference,
with separate recurrent state per seat. `ocean/poker/league.py` maintains the
bounded snapshot pool. The acting-seat API also supports a future collector
that trains all seats and batches neural opponents on the GPU.

## Local code and constraints

Read [SKILL_ISSUES.md](../../../SKILL_ISSUES.md), the CPU
[Breakout environment](../../../ocean/breakout/breakout.h),
[Connect4](../../../ocean/connect4/connect4.h),
[Goofspiel](../../../ocean/goofspiel/goofspiel.h), its core and observations,
[the agent ABI](../../../src/pufferenv.h), and relevant collection/inference/GAE
code in [pufferl.cu](../../../src/pufferl.cu) and [algo.cu](../../../src/algo.cu).
Checkout HEAD was `428fadc8f01286297594c3fc8a0d3bd20433ead3`, with existing local
changes. Those changes are part of the inspected working tree.

- Follow the guide: preallocate, inline one-use helpers, keep related code
  together, avoid wrapper layers, use assertions for programming errors, and
  preserve determinism. Source length is a measured cost alongside time and RAM.
- Breakout supplies the native environment lifecycle. Its per-table struct is
  not an example of SoA across independent environments.
- Connect4 advances a built-in opponent inside `puf_step`; that works for its
  fixed opponent, but does not solve batched neural self-play.
- Goofspiel supplies useful policy-bank, mask, terminal and evaluation patterns.
  Its simultaneous actions and small exact evaluator do not transfer to poker.
- `vec_rollout` calls `puf_step` per environment. `pufferl_forward_step` evaluates
  every row in each policy group. Groups are laid out at initialization. Existing
  action masks do not skip inference or make GAE follow a changing acting seat.
- `MY_VEC_INIT` can allocate storage but does not itself introduce batched CPU
  stepping. A poker batch needs an explicit collection path, not empty per-table
  calls with the real work hidden in a post-step hook.

The inspected host has four i5-7400 cores, AVX2, 256 KiB L2 per core, 6 MiB shared
L3 and about 8 GiB RAM under WSL. These are the initial measurement conditions,
not a claim about eventual training hardware.

## Game and action contract

Training uses one hand per episode and maximizes chip EV. The initial training
and Slumbot benchmark profile is 50/100 blinds, 20,000 chips per seat, no rake or
antes, one board, and stacks reset after every hand. Rotate the button. Report
net winnings in BB, including blinds. Use `gamma=1` for this finite-hand objective
and disable reward clipping; a fixed reward scale is acceptable.

A showdown resolves one hand by comparing the remaining players' cards, possibly
with three or more contenders. Ordinary cash tables carry winnings/losses between
hands and permit players to join or leave; automatic stack resets are our training
and benchmark convention. See Ignition's
[hand and table rules](https://static.ignitioncasino.eu/poker/how-to-play).

Heads-up means 1v1. A 200 BB starting stack is 200 times the big blind:
20,000 chips with a 100-chip big blind. This initial depth matches Slumbot's
published interface; depth is a game parameter, not a different poker variant.

Support 30, 50, 75, 100 and 200 BB starting-depth presets with the same chip
arithmetic. Select configured or sampled starting stacks at reset; avoid per-step
depth-mode dispatch. Keep the fixed 200 BB throughput/evaluation profile intact.
Expose actual per-seat stacks and commitments in BB to the policy, retain unequal
stacks, and train/evaluate at each supported depth. Training only at 200 BB does
not establish strength when starting at 30 BB: a deep hand that spends down to
30 BB has a different pot and action history. In HU, the smaller starting stack
limits the amount the two players can contest; multiway play needs every stack.

Compile the table's starting seat count as a constant. The two-seat build uses
smaller state, fixed two-player operations and simpler settlement; six-/nine-seat
builds retain multiway betting and side pots. This is code specialization for
speed, using shared rules and encoding conventions, not three engine rewrites.

A six-seat hand can become heads-up after four folds, but can also reach
showdown with three or more players. Two remaining players do not turn it into
a fresh two-seat game: retain the pot, previous actions, contributions and
original positions. At a table starting with two players, the button posts the
small blind, acts first preflop and last postflop. Do not change the button,
blinds or action order when four players fold in a six-seat hand. Reuse
two-contender operations where valid without discarding that history.

Heads-up policy weights are not automatically a six-player strategy: the earlier
multiway decisions and resulting distributions of possible hands matter. Permit
unequal bounded stacks in the multiway engine, even if the initial preset uses
equal stacks.

The engine accepts fold, check/call and an exact integer **raise-to amount for
the current street**. It must accept legal opponent amounts outside our policy's
bet menu. No arbitrary raise-count cap. Correct rules include the heads-up
button/SB order, BB option after a limp, minimum raises, short all-in calls,
per-player reopening after cumulative short raises, uncalled-bet refunds, side
pots, ties and deterministic odd chips. These betting rules are explicit in the
[TDA rules, especially 45, 49 and 50](https://www.pokertda.com/view-poker-tda-rules/);
the cash-game profile and external adapter must still agree on their conventions.

Use one masked categorical head initially: fold, check/call, minimum raise,
one-third pot, two-thirds pot, pot, twice pot, all-in. This is a policy action
abstraction over an exact no-limit engine. Construct legal amounts, remove
duplicates after rounding/clipping to legal endpoints, and retain one probability
per distinct action. The menu can be tuned once the external benchmark works.

For pot-fraction raises, if `P` includes all chips currently committed and `C`
is the full call amount, the target is `current_bet + fraction * (P + C)`.
Short stacks and reopening rights determine whether raising is available.
Keep exact incoming amounts in public history; do not rewrite an opponent's bet
to the nearest menu entry.

## CPU layout and execution

Allocate the table batch once. Start with contiguous SoA fields across tables:
`stack[seat][table]`, `street_paid[seat][table]`, `total_paid[seat][table]`,
hole cards, board, current bet, last full raise, actor, button and masks.
Keep training pointers, history, rendering and logs outside the frequently
accessed simulator fields. Assign disjoint contiguous table blocks to workers.

| Choice | Initial implementation and measurement |
| --- | --- |
| Amounts | Integer arithmetic. Benchmark 16-bit per-seat stack/contribution storage under an asserted bound against 32-bit storage. Pot arithmetic and signed returns need wider types; nine 20,000-chip stacks exceed 16 bits. |
| Cards | Byte IDs and a 52-bit used-card mask. Deal holes at reset and board cards only when reached. Benchmark this against partial Fisher-Yates; avoid assuming a full 52-card shuffle is cheapest. |
| RNG | Per-table deterministic stream, unbiased bounded draws, and a separate policy-sampling stream. Scheduling must not change a table's deals. |
| Seats | Small live/actionable/pending masks with bit operations. Betting completion and raise-reopening rights are separate state. |
| Settlement | Award uncontested pots immediately. Evaluate only surviving hands at showdown. Construct at most nine contribution layers when multiway settlement needs them. |
| Hand evaluator | Benchmark a compact bit-mask evaluator against the native seven-card PHE evaluator. Share lookup tables across tables and workers. |
| Observations | Write only the next actor's view. Benchmark compact byte transport with exact split integer fields; count the trainer's widened rollout storage too. |
| Layout | Compare SoA with small tiles of SoA if scattered per-table accesses become expensive. Keep the measured winner on representative trajectories. |

If stacks use 16-bit storage, accumulate terminal payouts in wider registers and
emit returns before resetting the hand. Never store a nine-way pot winner's
terminal stack back into a field bounded by the starting stack. This optimization
assumes bounded independent-hand starting stacks.

[PHE](https://github.com/HenryRLee/PokerHandEvaluator) reports a 144k footprint
for its seven-card library and roughly 56 million random seven-card evaluations
per second on its benchmark machine. Those are upstream evaluator results,
not environment SPS on this host. Its Apache-2.0 native implementation is a
reasonable starting candidate. Do not evaluate all 21 five-card subsets in the
training path, and do not put Monte Carlo equity estimation in each observation.

Resolve chance events, all-in runouts and forced transitions inside the CPU step
until another actual policy decision or termination. Avoid allocation, text
parsing, networking, rendering, repeated config reads and generic dispatch in
that loop. Validate external records/actions at their import boundary. Debug
assertions and test oracles carry the expensive invariants.

Keep the kernel, observation writer and thin Puffer adapter together in the
initial `ocean/poker/poker.h`. Add an evaluation/benchmark entry point and tests
as needed. Generated evaluator data is separate from handwritten source. Do not
build a general card-game framework or duplicate three implementations.

## Self-play collection is the first integration spike

Batch the acting seats from many tables, grouped by policy. Each request carries
its table, seat and policy identity. Recurrent state belongs to **table plus
seat**, never to a temporary batch position. A player receives only its own
hole cards, currently visible board, public chip state and public action history.
Advance its history representation with all public events since its previous
decision. Preserve their order and exact sizes.

Store transitions in preallocated per-seat streams and pack ready sequences for
the existing optimizer. A transition ends at that same seat's next decision or
its terminal payoff. A hand ending must finish every affected seat's pending
record, including players who folded or went all-in earlier. Rollout boundaries
need correct same-seat bootstraps and recurrent state. Do not connect player A's
value to player B's next observation or propagate B's private recurrent state.

This requires a narrow poker collection/inference integration; the current
fixed-row collector cannot provide it unchanged. Measure packing, state gathers,
small policy batches and CPU/GPU synchronization before committing to its final
shape. For tiny frozen policies, compare batched native CPU inference with GPU
inference using the same model and trajectories. Merely moving scalar neural
inference into each environment can dominate the simulator cost.

Retain stable opponent versions during each hand. Start with the learner and a
small frozen snapshot pool, distribute learning across positions, and train
separate exploiters against saved targets. A single acting row per table is
insufficient unless ownership, returns and recurrent state are handled above.
Inactive-seat no-ops may help diagnose an adapter, but do not count as useful
decisions or become the production throughput claim.

Begin with the native learner and self-play from scratch. Judge PPO by external
profit, cross-play and attack results. More self-play steps alone do not establish
equilibrium convergence. Use our own rules baseline, self-play, frozen neural
opponents and independent attack policies. Do not add a solver-training branch.

## Opponents and incorporation

### Required native rules baseline

Build one compact, position-aware rules policy before the first substantial
self-play run. This is an independent fixed opponent from day one. Keep its
decision function native and usable by both the local league and the thin
Slumbot client, so external evaluation measures the same policy used in training.
No network calls, allocations, search or Monte Carlo equity in native decisions.

Use a small 169-hand preflop classification with position, prior aggression,
effective stack and active-player count. Postflop, use cheap made-hand, draw and
board-texture features, the price to call, pot size and stack-to-pot ratio.
Choose value bets, calls and a controlled fraction of bluffs/semi-bluffs; sample
mixed actions from a per-seat RNG. Keep heuristics explicitly heuristic rather
than treating a hand-strength score as calibrated showdown equity. Consume only
the acting player's cards and public state. Size rules data in KiB, shared once
across environments, and measure the full decision cost.

Run a bounded development loop: a short protocol smoke test, then two or three
versions evaluated against Slumbot at HU200 with the same hand budget per version.
An initial 2,000 hands per version is a diagnostic budget, not proof of a small
win-rate difference. Record raw BB/100 and uncertainty, action frequencies and
losses by street/pot size; fix concrete leaks rather than selecting lucky seeds.
Use a fresh final sample for the frozen baseline and report its uncertainty even
if it remains large. Keep earlier versions for regression/cross-play. A measured
loss to Slumbot is still a usable baseline; beating Slumbot is not a prerequisite
for starting RL. Preserve Slumbot as a separate stronger external reference.

The same rules policy must handle six-player positions and multiway pots, with
tighter continuation/value thresholds as appropriate. Score it in rotated local
six-player lineups; its HU Slumbot result does not establish six-player strength.
Retain the frozen bot in the league alongside learner snapshots and exploiters.

Use our own native rules bot as the small fixed local baseline and Slumbot as the
external HU200 reference. HTTP stays outside training. The official
[Slumbot protocol](https://slumbot.com/sample_api.py) uses
`/slumbot/api/new_hand` and `/slumbot/api/act`, with optional login.
`bN` is a street-total contribution; `client_pos=0` is the big blind.
Never retry an action after an ambiguous timeout: it may already have executed.
Preserve partial logs and label interrupted matches incomplete.

Keep original Pluribus and the previously researched solver repositories as
historical research only. They are not implementation dependencies. Do not obtain
large policy tables, train CFR, buy weights, or introduce imitation learning.

Native league members share immutable parameters across tables. Keep each seat's
RNG and recurrent state private, freeze opponents within a training stint and
record exact versions. A policy stays in the league only if its measured speed,
memory and behavior justify it; retain earlier rules versions for regressions.
Six-/nine-player strength still needs measured multiway lineups and attack tests.

## League integration

Use the existing patterns in [KAG's league runner](../../../ocean/kaggriculture/run.py)
and [Pokemon's sequential league](../../../ocean/pokemon/freeleague.mjs),
with [native frozen banks](../../../ocean/pokemon/pokemon_base.h). KAG documents
checkpoint registration, cached matchup results and opponent-mixture selection
outside PPO. Pokemon supplies explicit member budgets, fixed opponents within
each training stint, sequential publication, frozen anchors and resumable
orchestration. Reuse that approach without importing their game-specific
curricula, data dependencies or actor/critic assumptions.

Keep a bounded resident bank of frozen opponents alongside current self-play.
Train only learner-owned transitions. Select seat assignments when starting a
hand and keep them through settlement; freeze bank weights for the training
stint, following the qualified existing workflow. Private recurrent state remains
per table and seat even when several seats share policy weights. Group active
requests by bank for inference. Registration, matchmaking, checkpoint loading
and score updates stay outside simulator stepping.

For HU, collect a seat-balanced payoff matrix and reuse offline mixture selection
where useful. For six/nine players, sample explicit opponent lineups, rotate
seats and score the candidate against those lineups; a pairwise HU matrix is
not a multiplayer payoff model. Start with simple fixed mixtures and fresh
evaluation deals. Preserve diverse anchors and exploiters instead of retaining
only the most recent winner. Report learner decisions separately from opponent
inference and total environment decisions.

## Measuring strength and exploitable weaknesses

Nash equilibria exist in finite games with six or nine players too. What changes
is tractability and the protective value of a single equilibrium strategy against
arbitrary opponents. [The original Pluribus explanation](https://ai.meta.com/blog/pluribus-first-ai-to-beat-pros-in-6-player-poker/)
explicitly makes this distinction. Full heads-up no-limit exact best response
is also too large for the small-game evaluation used by Goofspiel.

Use three complementary measurements:

1. **Profit against a fixed field.** BB/100 with confidence intervals, exact
   stack/reset/rake rules and opponent versions. HU gets Slumbot plus independent
   local policies. Six/nine seats get rotated seating against fixed mixed fields,
   one candidate replacing a field member, and tables of candidate copies facing
   one challenger. Maintain the matchup matrix; one Elo number hides cycles.
2. **Attack strength.** Freeze the target, train several independent best-response
   approximations, and evaluate them on fresh deals. Also use
   [local best response](https://arxiv.org/abs/1612.07547) and restricted HU
   endgame probes. Positive attack results demonstrate a leak. A weak attack
   failing to win does not certify low exploitability.
3. **Small-game correctness.** Use Kuhn/Leduc and independently specified small
   subgames with exact information-set best responses to test the learner and
   evaluator. An omniscient response seeing all hole cards measures a different
   game and is invalid here.

For a multiplayer profile `pi`, unilateral deviation gain for seat `i` is
`max_beta u_i(beta, pi_-i) - u_i(pi)`. NashConv sums these gains; see
[OpenSpiel's implementation](https://github.com/google-deepmind/open_spiel/blob/master/open_spiel/python/algorithms/exploitability.py).
Our trained attackers approximate that maximization. Freeze the other five/eight
policies, attack one seat at a time, and report per-seat estimated improvement
and uncertainty. A sum of sampled approximate gains is not exact NashConv.
Multiple cooperating attackers or shared private cards are a separate coalition
experiment, not this unilateral metric.

Use duplicate deals and seat swaps/rotations in the local arena. Cluster
uncertainty estimates by shared deal block, not individual correlated hands.
Evaluate stochastic policies by sampling their distributions. The public Slumbot
sample does not expose deal injection, so local duplicate scheduling must not
be assumed to work through its API.

[AIVAT](https://arxiv.org/abs/1612.06915) is a useful later variance-reduction
upgrade when the necessary policy probabilities and value baselines are
available. Start with straightforward paired estimates and fixed evaluation
budgets. Keep raw chip outcomes alongside any adjusted score.

## Implementation order and acceptance evidence

1. **HU engine, rules bot and Slumbot adapter.** Implement exact rules,
   deterministic deals, eight-action decoding and the compact native rules policy.
   Add Slumbot replay/inference integration; verify complete hands, legal actions,
   hidden-information isolation and settlement before scoring.
2. **Establish the fixed baseline.** Evaluate and revise the rules bot through
   two or three bounded Slumbot runs. Freeze a version, score fresh hands and
   record uncertainty plus native decision throughput/memory. Use it as a league
   member and regression opponent from the first RL run.
3. **Collection spike.** Prove same-seat returns and private recurrent-state
   ownership through alternating actions, folds, all-ins, resets and rollout
   boundaries. Measure the complete actor-only collection path with a small model.
4. **Optimize the measured kernel.** Compare layouts, dealing and evaluators on
   identical legal trajectories; retain the fastest small implementation. Inspect
   cycles, instructions, branches and cache misses where hardware counters work.
5. **HU self-play and league.** Train from scratch with the frozen rules bot,
   current self-play and frozen snapshots using the KAG/Pokemon workflow. Save
   cross-play, Slumbot and attack results. Do not spend a large training budget
   without a working external score.
6. **Six seats.** Enable the compiled multiway path, finish unequal-stack side-pot
   and reopening cases, train six-player self-play and evaluate diverse lineups.
   Qualify any imported multiway opponent before using it as a strength anchor.
7. **Nine seats.** Reuse the rules and collection design with a separate build and
   policy. Benchmark and evaluate it separately; six-player results do not qualify
   nine-player play.

Correctness evidence should concentrate on chip conservation, settlement against
an independent [PokerKit](https://github.com/uoftcprg/pokerkit) reference under
matched rules, raise reopening, hidden-information isolation, action-mask
consistency, deterministic replay and reward ownership. Keep differential/random
trajectory tests and sanitizers outside release stepping. Include all-fold,
limp/check, all-in/call, split pots, multiple short all-ins and multiway side-pot
fixtures. Compare hand ranks against an independent evaluator.

Performance reporting must separate core action transitions, reset/deal cost,
showdown cost, observation/mask writes, collection/transfer, inference and full
training. Report useful decisions/s, hands/s, cycles/decision, memory/table,
rollout/recurrent memory and handwritten source length. Include realistic
mixed trajectories and showdown-heavy/min-raise-heavy stress cases, not only
instant-fold hands. Measure one core and all four local cores with warmup and
repeated runs; treat WSL timing/counter limitations explicitly. No numeric SPS
promise is justified before these measurements.

## Bend verification boundary

Use stock CPU Bend 2.0.6 for an independent model and checked laws, outside release
stepping. The current laws include general natural-number chip-transfer
conservation, terminal absorption, hidden-card independence, raise-size updates,
short/cumulative all-in reopening and side-pot/odd-chip examples. Compile model
fixtures and compare them with actual native hand outcomes. Keep the independent
PokerKit transition/hand-rank checks, explicit rule fixtures, metamorphic tests
and sanitizers alongside the proofs.

The proofs establish their stated properties of the Bend model. They do not
prove the entire C kernel, observation transport, RNG or RL collector equivalent
to that model. Record oracle rule differences explicitly; do not silently change
the game or discard failing cases to obtain a pass.
