# Pokémon experiment TL;DR — 2026-09-13

- **Goal:** learn diverse, useful team compositions; distinguish forced choices,
  learned species choices, and moveset-only changes.
- **Old potential-weight CMA:** changing potential shaping was not a dependable
  way to request different final behavior. Discounted, terminal-zero potentials
  telescope; they can change learning dynamics without changing the ideal objective.
- **Manual non-cancelling event rewards:** paralysis-up and sleep-down strongly
  changed behavior; sleep-up had little room to move. Behavior steering worked,
  but did not by itself establish broad team-composition exploration. The saved
  manual summary has 12 completed jobs, not a completed 15-job grid.
- **Two-generation event-space CMA:** retained three archive entries but only
  **two six-species compositions**. One variant kept Slowbro/Exeggutor/Rhydon/
  Electabuzz/Lapras/Zapdos; the novel composition used Raticate/Tauros/Rhydon/
  Electabuzz/Lapras/Zapdos. Moveset novelty is not species-team novelty. The
  compute-matched uninterrupted baseline retained the original composition.
- **Continuation:** the child adapted toward its parent matchup while losing
  older matchup coverage; its species composition stayed the same. Continuing
  weights did **not** continue the old optimizer or rolling opponent history.
  The separate causes of the tradeoff were not isolated.
- **DAGS-style bank:** 45,938 snapshots from 8,192 fresh legal games played by
  six archived policies; 1/8 exploratory games. Only 2,051 species teams and
  39,532/540,274 possible three-species subsets (7.32%). Snorlax appeared in
  670 snapshots. Existing policies' composition bias carried into the bank.
- **Bank pilot outcome:** neither final reset policy changed its species team.
  It supplied varied battle positions, not comprehensive team exploration.
- **Working hypothesis / user takeaway:** “DAGS only works if you have an expert
  oracle; otherwise RNG state sets are better.” Treat this as the hypothesis
  motivating the next experiments, **not an established general result**:
  we have not run a matched expert-oracle versus exhaustive-RNG comparison.
  What we did establish is that this self-generated bank inherited narrow teams.
- **Next 1:** seeded, without-replacement coverage of every three-species core;
  learn movesets, order, other three members, and battle play. Fresh drafts only.
- **Next 2:** identical core coverage, with the existing two- or three-specialist
  frozen expert roster re-enabled. These are trained specialists, not perfect
  oracles. Start both experiments from scratch for a clean comparison.
- **Credit-assignment caveat:** snapshots skip draft learning; 100% forced cores
  train conditional team completion, not choosing which core to assign. The
  remaining three slots still learn. The next mixture test retains unrestricted
  drafts and independently mixes current self-play/expert opponents; measure
  normal-draft compositions separately from forced-core extras. No mixture
  experiment has been run by the assistant yet.

Sources: `runs/pokemon_behavior_manual_20260913/summary.json`,
`runs/pokemon_event_qd_gen2_20260913/review.json`, its
`posthoc/continuation_20260913/REPORT.md`, and
`runs/pokemon_states_20260913/ANALYSIS.md`. Commands/options: [CORES.md](CORES.md).
