# Two-generation event-space search

Launch: `bash ocean/pokemon/run_event_qd_local.sh`. A smaller, separate end-to-end
check uses `--smoke`. The old `qd.py` potential-weight sweep is **not** used.
`event_qd.py` reuses its process/evaluation helpers, the native binaries from the
validated event experiment, and the existing CMA/Archive implementation with an
explicit event/team distance. Existing callers retain their original defaults.

## Fixed budget and controls

- Two generations, four CMA candidates plus one zero-weight control each.
- 100M requested steps per job; 99,614,720 actual after rollout rounding.
- Every job uses the validated PPO settings: 1,024 agents, horizon 512,
  GAE lambda .995, learning rate .0001, entropy .0005, no EMAg, no reward clipping,
  no HP/KO potentials, all five `personality_*` weights zero.
- Native learner/history self-play only; no external training opponent pool.
- All five jobs in a generation share a parent and training seed. Each job has
  its own optimizer/history. Generation 1 starts from the original competent
  win-only checkpoint. Generation 2 starts from the retained region farthest
  from the previous parent's measured behavior; quality breaks distance ties.
- The baseline starts from the same original checkpoint and trains in **one**
  invocation for **996,147,200** steps, exactly matching all ten search jobs'
  actual steps. Its optimizer and opponent history are not reset midway.
  Baseline training runs after search to avoid overlapping GPU jobs.
- New training totals 1,992,294,400 actual steps. Evaluation is additional.
  No generations 3–4 are queued; the CLI currently permits at most two.

This is an incremental training-step comparison, not a wall-time or historical
compute comparison. The archive is warm-started with all five completed seed-101
manual conditions, independently of their scores. That prior compute is sunk,
not hidden inside the new budget. The baseline does not benefit from an archive
of starting policies. A single search/baseline seed cannot establish reliability.

## Search and archive

Only `behavior_sleep` and `behavior_paralysis` vary. Search coordinates map to
`0.5*tanh(x)/max(1, sum(abs(tanh(x))))`: both signs are available, every weight is
within ±0.5, and total magnitude stays within the validated 0.5 bound. The
win-only control is exactly zero. CMA uses two dimensions, population four,
initial sigma 1, seed 4601 in the launch script, and mirrored proposals. Admission ranks (new region,
then local quality improvement, then rejection) update the CMA state after each
generation. The policy parent can change, so this is an archive-ranked search
with a changing training landscape, not optimization of a fixed objective or a
claim to reproduce CMA-ME/PPGA.

Archive distance uses:

- 50% squared distance in mean capped early sleep / normalized distinct paralysis;
- 25% squared histogram distance between complete unordered six-species teams;
- 25% squared histogram distance between complete unordered six-set teams
  (which distinguish movesets).

The square root gives the final distance. Radius .12, capacity 32, and split-half
noise allowance are heuristic design choices. Noise halves are balanced within
each opponent. Species marginals, pairs, leads, and legacy potential descriptors
are diagnostic only. Raw event counts, capped means, full team distributions,
and per-opponent battle scores are saved. Novel lower-scoring specialists are
retained; a higher score replaces an incumbent only within a nearby region.
The strongest common-panel policy is kept independently of archive admission.
Warmstarts and all completed checkpoints remain on disk even if replaced.
The overall champion can still be an imported warmstart; its existing strength
must not be attributed to the new search. Generation summaries identify all new
candidates and their matched controls separately from the imported references.

## Selection versus review

The common selection panel is four frozen self-generated policies from the
earlier self-play run, with seed 5601 and 64 games per opponent. It stays fixed
across both generations; all initial/warmstart policies are freshly rescored.
This panel is intentionally reused for selection and can be overfit.

The baseline's score never updates search. After it finishes, finalist identities
(archive, common-panel champion, baseline, original parent) are frozen. They are
then evaluated against four separate earlier generation-1 policies, with seeds
6601 and 7601, 64 games per opponent per seed. These opponents/seeds are held out
from **this search**, not claimed to be globally unseen. Those results never
update CMA, the archive, or champion selection.
The smoke run uses seed 601 (evaluation seeds 1601/2601/3601), separate from the
full run, so its plumbing checks do not consume the full run's held-out seeds.

Read `state.json` for current phase, `generations/g01/summary.json` and
`generations/g02/summary.json` for generation reviews, and `review.json` after
both search and baseline finish. Pairwise review fields distinguish event,
whole-species-team, whole-set-team, and matchup differences. Do not interpret
new moveset/lead usage alone as resolving species-team selection.

Inputs, snapshots, binaries and source are fingerprinted. Training commands,
reward handshakes, saved configuration, and exact checkpoint step counts are
validated. Completed jobs resume from cache; interrupted jobs refuse silent
retraining. Re-running the same command resumes completed work. Input changes
require a new output directory. No old archive/results are overwritten.

## Checks

```sh
python -m unittest discover -s ocean/pokemon/tests
bash ocean/pokemon/run_event_qd_local.sh --smoke
```

Tests cover event bounds, mirrored deterministic CMA, preservation of weaker
novel regions, local quality replacement, joint-team versus marginal diversity,
both generations, same-parent controls, one compute-matched baseline invocation,
held-out isolation, resume and input rejection. The real smoke run also checks
native arbitrary-weight configuration and the final reporting path.
