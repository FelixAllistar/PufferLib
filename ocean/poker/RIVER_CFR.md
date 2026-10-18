# Optional heads-up river CFR policy

Build the native evaluation libraries with:

```sh
POKER_RIVER_CFR=1 bash ocean/poker/build.sh
```

The flag defaults to zero. It is a standalone alternative to the river
expectimax modes: `POKER_RIVER_EXACT`, `POKER_RIVER_CALIBRATED` and
`POKER_RIVER_RANGE_CALIBRATED` must be zero. A direct C build must also leave
`POKER_RIVER_VALUE` disabled when it enables CFR. The build script creates
two-, six- and nine-player libraries; CFR is compiled into the heads-up policy
only. This native-library command does not build or train a PPO network.

## Runtime behavior

Policy ID 7 uses the original range policy before the river. In CFR mode, the
original focused preflop AK repair is retained and the later minimum-raise call
rescue is bypassed. The original flop and turn decisions are retained. On an
eligible river, the policy constructs a public range game, solves it, checks a
numerical best-response certificate, and samples the actor's averaged strategy.
Unsupported inputs and rejected solves use the original range heuristic.

The adapter accepts two-player states with original stacks of 20,000 chips,
five distinct board cards, no public profiles, both players live and active,
and equal contributions before the river. It checks the public pot, stack,
commitment, pending-action and reopening state. It does not change `Poker`
layout, observations, dealing, stepping, reward calculation or policy IDs.

The public game offers fold when facing a bet, check/call, and one legal
two-thirds-pot raise. The tree follows both players symmetrically through
actual fold, check, call or all-in termination. Raises use total street
commitment, matching the engine API. Capped calls return unmatched chips;
payoffs include prior contributions and use player-zero net chips.

## Information boundary and ranges

The full board leaves 1,081 possible unordered private pairs for each seat.
The adapter retains all of them while solving. For physical seat `p`, holding
`h` and public state `s`, the factor is:

```text
r_p(h) = poker_range_weight(public_view(s), p, h, NULL)^alpha
alpha = 0.42089278763026228
P(h_0, h_1 | s) = r_0(h_0) r_1(h_1) I[disjoint(h_0, h_1)] / Z
```

The public view contains only street, board, button and the strongest recorded
public action kind for each seat on each street. Actual hole cards, deal RNG,
unused tables and observation history do not enter either factor. The two
factors use the existing range function and exponent; this mode performs no
parameter fitting.

`poker_cfr_make_inputs` creates this public-only input. Then
`poker_cfr_solve_public` solves the complete game and validates every root
strategy row. It has no `Poker` pointer, actual holding or RNG argument. Only
`poker_cfr_select_row`, after a successful solve, reads our actual holding to
choose its row. Actual opponent cards are never a policy input.

Using the same exponent for both physical seats is a modeling assumption.
Matching a conditional opponent distribution after fixing our holding does
not validate our own range factor: that factor cancels from the conditional
distribution. The joint factors affect the strategy solved for the opponent.

## Solver and numerical fallback

The generic solver uses alternating CFR+ traversals, nonnegative cumulative
regrets, and a linear, own-reach-weighted average strategy. Terminal evaluation
uses rank cumulative sums and card-removal inclusion-exclusion; it does not
allocate a dense 1,081 by 1,081 payoff matrix for runtime traversals.

For the averaged strategies, the certificate computes:

```text
V = u(average_0, average_1)
U = max_response_0 u(response_0, average_1)
L = min_response_1 u(average_0, response_1)
NashConv = U - L
mean exploitability = (U - L) / 2
```

Best-response choices depend on the responding player's own holding and the
public history. They are made after integrating over the opponent's holding.
`U` and `L` bound the value of the supplied abstract game subject to numerical
error; the implementation uses floating-point arithmetic.

The fixed cumulative iteration checkpoints are 100, 200, 400, 800 and 1,000.
The adapter accepts the first certificate with finite fields, positive chance
mass, ordered value bounds within `1e-8` chips, and NashConv at most `0.05` BB.
This threshold is the full two-player gap; the corresponding mean
exploitability is at most `0.025` BB per supplied conditional game. It is not
a BB/100 win-rate target or a per-holding guarantee.

A construction, iteration, certificate or probability-row failure rejects the
solve. A finite larger gap continues to the next checkpoint; reaching the cap
without acceptance also rejects it. All rejection paths return before
consuming any local simulation RNG draw. Accepted strategies are sampled
using one 32-bit local draw, rather than choosing the largest probability.
The outer policy retains its original seed and roll draws.

The generic solver allocates a temporary game tree and work arrays per
decision, releases them before returning, and does not allocate per iteration.
It retains no strategy or opponent state between calls. Unlike the default
native policy path, this optional mode performs heap allocation and substantial
river computation. It is intended for explicit evaluation.

## Limits of repeated decisions

At each later actual decision, the adapter constructs fresh heuristic factors
from the current public state. Those factors need not equal the previous
factors updated by the earlier solved action probabilities. Actual opponent
bet sizes may also fall outside the single modeled aggressive size. Thus a
small numerical gap at one solve does not establish safe continuation across
later solves or full-poker exploitability.

The implementation does not establish that its supplied ranges are accurate,
that the action abstraction is sufficient, or that the complete bot wins
against a particular opponent. Those questions require separate fixed playing
comparisons. Six- and nine-player policies do not invoke this solver.

## Source structure and checks

| File | Responsibility |
| --- | --- |
| `river_cfr.h` | Generic game, strategy and certificate API |
| `river_cfr_core.h` | Money tree, CFR+, terminal integration and best responses |
| `river_cfr_rank.h` | Adapter to the existing exact seven-card ranker |
| `river_cfr_adapter.h` | Public factors, eligibility, numerical gate and private row selection |
| `test_river_cfr.c` | Bounded synthetic numerical and adapter assertions |

The generic core is textually included in the existing native translation
unit. Direct builds should retain normal floating-point semantics, such as
`-ffp-contract=off`; do not add fast-math transformations to a frozen candidate
without treating the resulting binary as a new implementation to verify.

`bash ocean/poker/build.sh --native-test` includes a bounded CFR test alongside
the existing simulator and policy tests. This is an offline check and makes
no live Slumbot requests. The generic API also exposes read-only terminal
values and a test-only dense traversal switch for independent reference
comparisons. The policy does not enable that switch.

## Algorithm references

- Zinkevich, Johanson, Bowling and Piccione (2007),
  [Regret Minimization in Games with Incomplete Information](https://papers.nips.cc/paper_files/paper/2007/file/08d98638c6fcd194a4b1e6992063e944-Paper.pdf).
- Tammelin, Burch, Johanson and Bowling (2015),
  [Solving Heads-up Limit Texas Hold'em](https://www.ijcai.org/Proceedings/15/Papers/097.pdf),
  describing CFR+ and weighted averaging.
- Johanson, Waugh, Bowling and Zinkevich (2011),
  [Accelerating Best Response Calculation in Large Extensive Games](https://poker.cs.ualberta.ca/publications/ijcai2011_accelerated_best_response.pdf),
  describing ordered terminal evaluation with card-removal correction.
