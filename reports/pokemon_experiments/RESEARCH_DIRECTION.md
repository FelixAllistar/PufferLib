# Research direction: OMNI training and the Kirby loop (2026-10-24)

## Core ideas (keep these prominent)

1. **OMNI training.** Train one policy on *all* strategies of a strategy space at once, by resetting episodes into
   states that commit it to a strategy (here: a fixed team drawn per game from a pool). OMNI_MASTER, one policy
   for all 13 league teams, beats every single-team specialist on every team, and single-team policies do not
   transfer (B0 gar scores 62.5 / 57.6 on its own team and 2–51 on the other 12; OMNI scores 55–77 on all).
2. **EMAgnet** keeps several strategies alive in a non-transitive game instead of collapsing to one (magnet off
   cost about 10 points).
3. **The Kirby loop** (working name; "consume and copy"). Sun Tzu: to defeat your enemy you must become your
   enemy. Naruto's shadow clones: send clones out, harvest what they learned.
   - Clones explore: free-pick policies started from OMNI draft their own teams (thousands tried, then converge).
   - Harvest: a discovered strategy (team + sets) becomes a new reset state in OMNI's team pool.
   - Keep a forced league of scaled, diverse opponents (frozen OMNI piloting every team, free-pick policies).
   - Accept a new strategy only within acceptable parameters (it should not cost OMNI on the others);
     otherwise discard it.
   - Repeat.

## Research question (long term)

Can one OMNI policy be grown from rules bots / policies / simple forcing of states with a specific strategy:
consume them into one main policy, keep a forced league of scaled diverse opponents, iterate exploration plus OMNI
piloting the league, and consume new strategies into its state sets when they meet acceptance criteria? Then
apply the same loop to a harder environment. First target: beat Metamon clearly (done on fixed teams: OMNI 1024
77.1 / 69.4) and keep pushing.

## Policies (names)

| Name | What | Best (SyntheticRLV2 / TaurosV0) |
|---|---|---|
| OMNI-256 | OMNI_MASTER 256x2, 13 league teams, 2B | 71.5 / 59.7 (zam_egg_zap) |
| **OMNI-1024** | OMNI_MASTER 1024x2, 13 league teams, 2B, 38 MB | **77.1 / 69.4** (zam_egg_cloy_don) |
| Lapras-A (OldFreepick) | free-pick from OMNI-256: Alakazam / Chansey / Lapras / Rhydon / Starmie / Tauros | 62.8 / 56.6 |
| Lapras-B (new free-pick) | free-pick from Smogon OMNI, no forcing: Alakazam / Chansey / Exeggutor / Lapras / Tauros / Zapdos | 72.6 / 55.2 |

Every free-pick run that started from an OMNI policy converged on a Lapras team. Lapras is in none of the 13
league teams; the league is Exeggutor / Snorlax heavy, and Lapras (Ice, Sing, Body Slam) answers both.

## Plan

1. **LeagueV2 = OMNI-1024 alone.** A frozen league made of OMNI-1024 piloting all 13 teams. Short sweeps of
   OMNI-1024 continued against it (warm start from its 2B checkpoint): learning rate with annealing, entropy
   down to 0, magnet, replay ratio; scored on Metamon. Needs a small env change: a league bank may reuse an OMNI
   checkpoint on any team (today a bank's team must equal its checkpoint's learner_team).
2. **Round robin of OMNI-1024, Lapras-A and Lapras-B** (plus a fresh free-pick from the swept OMNI) and an eval
   matrix of every policy on every team, compared with their Metamon scores. Keep at least 2 policies running.
3. **Consume:** add the Lapras teams (and later free-pick discoveries) to OMNI's team pool if OMNI keeps its
   scores on the old teams; repeat with new free-pick clones.
4. Later: a harder environment.
