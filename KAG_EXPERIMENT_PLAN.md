# Kaggriculture experiment plan (DAG)

Goal: beat the current leaderboard meta (top-replay opening + animal herd) and
produce a robust exploiter. Everything is native C/CUDA; Python is only for
offline replay analysis and submission.

## Current state (2026-08-09)

- Strong league policies: `178616@104.86M` (~$22.4k vs rules),
  `kag_diver_a@15.73M`, `178616@68.16M`.
- Native `top` bot (tape opening + rules economy after t26): ~$35k vs pass,
  ~$27.5k vs rules (beats rules). This is a good exploiter-opponent candidate.
- Failed: hard action-mask opening (`opening_turns=10`, 500M) peaked ~$10.4k
  vs rules and overtrained. Hard masks give zero gradient on forced turns, so
  the model never learns why the opening works and struggles after handoff.

## Key mechanisms (choose per experiment)

### A. Hard mask (DONE, failed)
Forces one action per head; zero policy gradient on those turns. Bad prior.
Use only for illegal-action filtering, not strategy.

### B. Post-opening reset curriculum (RECOMMENDED NEXT)
Instead of masking, sample a reset state after executing the canonical opening
for k ~ [10,26] turns, then start PPO from there. PPO sees mid-game states as
legitimate init and learns the handoff naturally.
Mix: 60% post-opening states, 20% perturbed opening, 20% normal turn-0.

### C. Opening imitation KL (BC)
`L = L_PPO + lambda * KL(policy || opening_script)` for t < N, lambda annealed
toward 0. The policy learns why the opening actions map to observations,
instead of having them injected externally. This is the arXiv 2606.19370
"pinch of human data" idea.

### D. Scripted control during opening, then PPO
Reset executes canonical opening; after t=N, hand control to PPO. Similar to B
but simpler; no gradient on opening turns.

## Exploiter training

Do NOT train against a single frozen clone. Build a small opponent distribution:
- 40% top-bot + strong frozen continuation (e.g., `178616@104.86M`)
- 25% top-bot + older checkpoint
- 15% perturbed top opening + current selfplay
- 15% current self-play population
- 5% random/weird opponent

Initialize the exploiter from the strongest general checkpoint
(`178616@104.86M`), not from scratch, then specialize against the above mix.

## DAG / order

1. Confirm strong baseline continues (current ini: `178616@104.86M`, LR 2e-4,
   horizon 16, mask off). Run `./puffer train kaggriculture` for 100M.
2. Implement B (post-opening reset) as an optional reset-state knob.
3. Train B variant (60/20/20 reset mix) for 100M from the same base.
4. Compare B vs baseline vs `top` bot. If B wins, that's the new meta.
5. Implement C (opening KL) as the "pinch of human data" auxiliary loss.
6. Train C variant; anneal lambda. Compare with B.
7. Build exploiter: init from best of (baseline/B/C), train against the
   opponent distribution above, with `top` bot heavily weighted.
8. PSRO iterate on each run; admit winners; update the four-policy weighted
   league. The persistent evaluator coarse-screens 12 checkpoints and fully
   evaluates only four local peaks.

## Status

- [x] Hard mask (failed; keep off)
- [ ] Baseline continuation (running)
- [x] Post-opening reset (B; `reset_opening_turns`, default off)
- [ ] Opening KL (C)
- [ ] Exploiter population
- [x] Compact weighted league + persistent/incremental PSRO integration

## Tagged mechanics curriculum (2026-08-26)

The replay bank alone is reset augmentation: it supplies valid states but does
not explain which mechanic should be learned or shorten the credit path. The
new native curriculum retains the 1280-byte observation and 47-head action ABI
and constructs short, valid lessons inside the ordinary rule core:

0. sell scarce, held product;
1. harvest, move product into the shed, and sell;
2. water/feed, harvest, and sell;
3. initiate crop/animal production while choosing the high-price option over a
   mechanically comparable low-price distractor;
4. ordinary root games.

Each GPU environment graduates independently after a configurable window and
success threshold. Half of lesson resets rehearse an earlier mastered stage by
default. Ordinary root probability rises quadratically with the frontier, so
transfer is tested throughout rather than at one abrupt handoff. Curriculum
bonuses exist only in the short lessons; root games use the configured normal
reward. Launch the initial cold-start 512x2 proof with
`ocean/kaggriculture/run_tagged_curriculum.sh`.
