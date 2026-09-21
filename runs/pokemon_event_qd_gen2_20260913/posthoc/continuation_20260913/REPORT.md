# Continuation diagnosis: evaluation only

The population reset is confirmed. Every completed job logs `history=1
external=0`. Generation-2 control loads the generation-1 specialist's final
checkpoint, then both frozen banks initially use that checkpoint. No preceding
historical population is loaded. The saved learning rate is 0.0001, with LR
annealing and entropy annealing disabled in both parent and child. The native
path creates a fresh trainer/optimizer and loads network parameters, including
the value head. The child's event weights change from the parent's
[-0.3891507538, +0.1108492462] to [0, 0].

## Fresh checkpoint comparisons

Two new evaluation seeds, 15601 and 15602, were used for every policy. Each row
has 512 games across the same four frozen selection opponents, plus 256 games
against the parent. Seats alternate. Scores count draws as one half. The
evaluator's SHA-256 matches the completed search contract. These evaluations
do not train or change any search results.

| Checkpoint | Older panel | Against parent |
|---|---:|---:|
| Parent before continuation | 75.98% | — |
| Child, 5.24M steps | 71.68% | 67.19% |
| Child, 20.97M steps | 59.77% | 81.64% |
| Child, 52.43M steps | 56.45% | 80.47% |
| Child, 99.61M steps | 61.13% | 87.11% |

The parent-vs-itself sampling check scored 54.69% over 256 games; that finite
sample does not imply one identical policy is stronger than itself.

Both seeds exhibit the same main tradeoff. The final child scores 61.72% and
60.55% against the older panel (parent: 74.61% and 77.34%), but 87.50% and
86.72% against its parent. The team remains Tauros/Raticate/Rhydon/Electabuzz/
Lapras/Zapdos. The final child occasionally chooses a different Tauros set.
All checkpoints still score at least 98.4% against the weakest panel member;
the losses are against the stronger older opponents.

This supports narrow adaptation / loss of older matchup strength. It does not
establish that population reset, optimizer reset, objective change, or critic
readjustment caused the tradeoff. There is no observed early improvement on the
older panel among these four prespecified child checkpoints; the first saved
checkpoint is already 5.24M steps, so behavior before that remains unmeasured.

## Next experiment to consider (not launched)

Use two win-only continuations of the same parent: one begins with only that
parent in its rolling history; the other begins with its previous active
history. Keep actor/critic initialization, fresh optimizer, reward, learning
rate, training seed, rollout settings, history capacity, pruning and sampler
rules otherwise matched. Reset opponent sampling statistics consistently so
the membership change is the controlled difference. Evaluate saved checkpoints
on the old panel and against the parent.

About 21M steps per arm reaches the observed divergence. Two matched training
seeds would total roughly 84M steps. The common change from shaped rewards to
win-only remains part of both arms; this tests the history effect under that
change. A same-objective continuation would answer a separate question.

The parent's `payoffs.tsv` lists 20 encountered policies, but the active rolling
history has capacity 16 and removes the oldest member when full. Reconstruct
the live population from registration order instead of equating all saved
checkpoints with active opponents. Under the checked implementation, its final
16 members are the 20.97M through 99.61M parent checkpoints. Those weights are
present. Merely restoring these members may still leave the population narrow;
the original ancestor and earliest parent checkpoints have already aged out.

Raw per-game logs, manifest and machine-readable results are alongside this
file in `report.json`. No generations 3–4 or new training were launched.
