# Free-pick Pokémon implementation

## Objective

Fresh policy and observation formats are allowed. Existing checkpoints, leagues,
and experiment artifacts are retained, not overwritten. No long experiment is
launched automatically. Hierarchical draft-only RL is a future experiment, not
part of this implementation.

Deliver genuine legal individual-move drafting, semantic Pokémon/move features,
a shared entity encoder and candidate-aware decoder, configurable core resets
and expert/current-self-play mixtures, and resumable league creation, training,
evaluation, and export. A league may contain a checkpoint-seeded or fresh master,
species-and-lead specialists, arbitrary fresh unrestricted members, and frozen
anchors. Include the 13 current RBY OU sample compositions with their leads.

## Work sequence

1. Generate pinned Gen1 OU legality and mechanic tables; independently verify
   complete movesets and incremental masks against the validator.
2. Replace preset-only construction with private species/lead selection followed
   by legal individual move selection. Preserve forced three-species coverage,
   deterministic resets, mixed modes, and accurate draft/battle telemetry.
3. Implement shared semantic entity encoding and candidate-aware action scoring
   for native GPU training, frozen opponents, and CPU evaluation/viewing.
4. Implement a simple league CLI with explicit budgets, reproducible round
   snapshots, optional self-play, safe resume, per-member constraints, and export
   suitable for ordinary expert-mixture training.
5. Validate CPU/GPU policy and gradient contracts, legality, privacy, deterministic
   replay, core coverage, opponent binding, league interruption recovery, and
   short fresh/resumed training/evaluation smoke workflows. Build normal binaries.
6. Document exact usable commands and configuration, format breaks, limitations,
   and the distinction between learner steps, environment shares, and game counts.

## Required boundaries

- Capabilities and format legality constrain choices; recommended movesets do not.
- Partial moveset masks must admit only legal continuations, including source
  compatibility. Support Pokémon with fewer than four learnable moves.
- The entire chosen team is visible to its owner before moveset construction;
  opposing draft choices, unknown moves, hidden state, and RNG remain private.
- Expose move mechanics in draft and battle, not just numerical move identities.
- Keep species constraints, lead constraints, learned moves, and opponent layout
  independent. Specialists learn moves and battle play; unrestricted members can
  use mixed forced-core and normal drafts.
- Train in roster order and publish each member immediately, so later members
  face updated earlier members. Freeze opponents only within each member's stint
  and its retries. Do not require QD/CMA admission or metagame solvers.
- Retain exact learned movesets in evaluation output for later discovery/library
  experiments. Do not introduce a preferred-set library as the primary action space.

## Status

Implemented and validated. See [FREEPICK.md](FREEPICK.md) for the current ABI-3
policy contract, league commands, core/expert experiments, tests and limitations.
Historical policies require fresh training; smoke results are not strength claims.
