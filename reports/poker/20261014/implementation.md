# Poker CPU implementation — 2026-10-14

Implemented in [ocean/poker](../../../ocean/poker/README.md), with the default
six-seat profile in [config/poker.ini](../../../config/poker.ini). No CFR,
blueprint imports, datasets, behavioral cloning or reward shaping.

## Working scope

- One SoA C kernel specialized at compile time for 2, 6 and 9 seats. Default
  200 BB independent hands; integer chips, unequal stacks, full/short raises,
  cumulative reopening, side pots, ties, uncalled returns and automatic runouts.
- Exact bit-mask hand evaluation, no large rank tables. No hot-path allocations.
- Private observations with a bounded public event window and legal action masks.
  Fixed input scaling; recurrent state belongs to each table/seat.
- Dense acting-seat request/response API, including terminal payoffs for folded
  and all-in players. A working PPO adapter learns one fixed seat per table.
- Our own rules bot, styles 0/1/2. Style 1 is the fixed initial baseline.
- Small frozen PufferNet opponent banks, sampled per seat/hand, shared immutable
  weights, inference only for acting opponents. Sequential training stints build
  a bounded snapshot league; the optimizer restarts between stints.
- Stock Bend 2.0.6 model/laws/proofs, independent native fixtures, PokerKit
  comparisons and sanitizer checks. A thin live Slumbot evaluation client.

The simulator is CPU-only. The existing learner uses GPU inference/training;
frozen opponents currently use CPU inference. No shared trainer files were
changed for this integration.

## Measured performance

Intel i5-7400, WSL2, Clang 19.1.1, `-O3 -march=native -DNDEBUG`, one thread pinned
to CPU 3. Three runs of one million 200 BB hands per seat count, style-1 rules
bots, 64 tables. Median and observed range; this shared host was noisy.

| Seats | State/table | Decisions/s median | Observed range | Hands/s median |
|---|---:|---:|---:|---:|
| 2 | 84 bytes | 6.65 M | 5.75–7.85 M | 1.84 M |
| 6 | 188 bytes | 6.24 M | 4.63–8.72 M | 0.740 M |
| 9 | 266 bytes | 6.91 M | 6.20–7.58 M | 0.556 M |

These include dealing, rules-bot choices, stepping and settlement. They exclude
observation writing, neural inference, collection and optimization. Showdown
fractions were 12.5%, 15.1% and 19.5%. The separate random-action workload reached
4.40–7.30 M decisions/s. All-call and repeated minimum-raise workloads, both with
100% showdowns, reached 13.2–19.8 M decisions/s; they have different decision
counts and policy costs, so do not compare their rates as equivalent work.

Six-seat adapter timing, 128,000 random legal learner decisions, one CPU thread:

| Field | Learner decisions/s | Includes |
|---|---:|---|
| Rules only | 1.21 M | Observations, masks, complete opponent play and resets |
| 25% rules + two H32/L2 frozen banks | 35.1 K | The above plus CPU opponent inference |

The adapter timings are single runs, not stable production estimates. Frozen
neural inference is the principal remaining throughput cost. A collector that
batches active opponents for GPU inference is the next substantial optimization;
the current implementation does not claim kernel rates for neural self-play.

Changing starting-stack/commitment/bet/high/increment fields from 32 to 16 bits
reduced state from 100/228/324 to 84/188/266 bytes. Stacks after payout, pots and
reopening thresholds remain 32-bit. The before/after throughput ranges overlap;
the nine-seat compact median was about 8% lower in these samples. The measured
memory saving is clear; a CPU speedup from this change is not established.

State figures exclude observations, adapter structs, RNN state and rollouts.
Default six-seat H64/L2 opponent weights are 127,744 bytes/model; eight banks
occupy 1,021,952 bytes, shared across tables. Opponent RNN state adds 2,560 bytes
per table. Native libraries are approximately 24–28 KiB.

Raw timings, compiler, affinity and source hashes: [benchmarks.json](benchmarks.json).

## Verification

[verification.txt](verification.txt) records the final complete test run:

- All 16 Bend laws type-check; independent fixtures compile under a serialized
  2 GiB/no-swap CPU guard. Universal properties include natural-number chip
  transfer conservation, terminal absorption, hidden-card independence and
  full/short raise updates. Reopening, side-pot and odd-chip fixtures also run
  against the C engine.
- ASan/UBSan: 20,032 hands each for 2/6/9 seats, starting stacks through 65,535,
  including maximum stacks, random/rules play, chip conservation, card uniqueness,
  legal actor membership, private-card independence and terminal absorption.
- PokerKit 0.7.7: 20,000 random 5/6/7-card rank/order/suit-invariance samples;
  1,000 complete shared-action hands per seat count, including arbitrary raises,
  unequal stacks and independently calculated payouts.
- Acting-seat compaction preserves table/seat identity and zero-sum terminal
  payoffs across 64 tables. Folded/all-in seats receive their final return.
- Puffer adapter: 128,000 learner decisions against rules and another 128,000
  against mixed rules/frozen opponents, including hand resets and bank cleanup.
- Frozen H32/L2 policy logits and recurrent states match the existing PufferNet
  CPU implementation within 1e-4 across 500 interleaved requests/resets.
- Both CPU evaluation and CUDA learner/CPU environment builds pass. A two-stint
  six-seat PPO smoke run completes: 4,096 learner steps against rules, then 4,096
  against a field containing that saved snapshot. H32/L1, 64 tables, one worker.
  Commands and checkpoint hashes: [league-smoke.jsonl](league-smoke.jsonl).
- Injected Slumbot action timeout preserves completed results, marks the run
  incomplete and makes no retry; tokens are not written to result files.

Bend proves the stated properties of its model. It does not prove the C engine,
RNG, card evaluator or trainer equivalent to that model. Differential/property
testing covers that boundary; this is not a claim of a bug-free implementation.

### Explicit PokerKit differences

The oracle is not silently treated as infallible. There were **0/7/21 raise-option
disagreements** in HU/six/nine-seat comparisons. PokerKit 0.7.7 can reopen callers
after a first short preflop raise because its recorded full increment starts at
zero. Our engine retains the nominal BB/full-raise threshold. Tests report these
cases, require that precise condition, and continue with actions legal in both
games; separate fixtures verify the intended reopening behavior. See the
[TDA raising rules](https://www.pokertda.com/view-poker-tda-rules/), particularly
cumulative short all-ins. Our declared cash-game conventions also include:

- Short posted BB: nominal opening price while multiple players have chips.
  Oracle comparisons avoid a short posted BB; separate fixtures exercise it.
- Odd chips: one chip per tied winner clockwise from the button. PokerKit gives
  the entire remainder to the first winner. Tests use its independent pots and
  ranks before hand-mucking/pot-pushing, then apply the declared distribution.
- An automatic runout skips PokerKit's forced lone-player check when no call is due.

These qualifications matter: the result is not unqualified full PokerKit parity.

## Rules bot / Slumbot results

The three styles were piloted, then style 1 was retained as a fixed starting
baseline. It was not selected as a statistically demonstrated winner.

| Run | Completed hands | Outcome |
|---|---:|---|
| Style 0 pilot | 62/100 | Interrupted by read timeout |
| Style 1 pilot | 100/100 | −41.4 BB/100; approximate 95% half-width 62.8 BB/100 |
| Style 2 pilot | 83/100 | Interrupted by read timeout |
| Frozen style 1, new seed | 119/250 | Interrupted by HTTP 504 |

Raw completed-hand logs are the `rules_*_slumbot.jsonl` files here. The older
interrupted logs have no terminal summary; their absence must not be read as
successful completion. The updated runner emits an explicit incomplete summary
and never retries an ambiguous action. Incomplete runs are not promoted as
winning evaluations, and the tiny completed pilot establishes no strength claim.

## Next work

Measure larger batches and batch active frozen-policy inference if it justifies
the collector complexity. Improve the rules baseline on completed development
matches, then qualify a frozen version on independent, adequately sized Slumbot
matches. Populate the snapshot league with trained policies; add fixed-field
cross-play and dedicated unilateral exploiters. No multiway exploitability or
tournament-transfer claim follows from these implementation/smoke results.
