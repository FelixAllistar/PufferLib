# Transformer + frozen league on gar_egg_zap (2026-10-22)

**Question.** With the same league setup as B1, does the entity Transformer beat PufferNet?

- **Team:** gar_egg_zap. Gengar (lead), Exeggutor, Zapdos, Chansey, Snorlax, Tauros. Species are fixed;
  the policy drafts its own moves.
- **Comparison:** B1 PufferNet on the same team at 700M scored 61.8 / 58.0 (SyntheticRLV2 / TaurosV0).
  The old league member scored 62.5 / 57.6.

**Recipe.** `run_league.sh` is B1's exact gar_egg_zap command. That is the trial-0 recipe for 700M,
magnet KL 0.1, 75% current-policy self-play and 25% of envs against the 13 frozen r26 PufferNet banks.
It changes only three things:
- the learner uses the entity Transformer (2 layers, width 128, 4 heads);
- checkpoints and logs go to this folder;
- `reset_state_bank` is `None`, which has no effect because `reset_state_prob` is 0.

**Core change.** The frozen banks are PufferNet checkpoints. They keep the MLP encoder through the new
`PUF_POLICY_ARCH` trainer hook, which is logged in `reports/core_changes_audit.md`; the box patch is in
`box_pufferl_policy_arch.diff`.

Local checks:
- the bank files are 3,616,896 bytes, exactly the MLP parameter count;
- the learner checkpoint has the Transformer size;
- the trainer logs "13 frozen bank slots, expert_env_fraction=0.25".

## Launched

- **Where:** Vast 57154204 (`ssh -p 28489 root@ssh2.vast.ai`), tmux `league`.
- **Early read:** tmux `league350` scores the 350M checkpoint as soon as it is saved.
- **Speed:** about 36K steps/s, so roughly 5.5 h to 700M. GPU memory is about 8.9 GB.
- **Results:** `evals.log` in this folder on the box.

## Results

(pending)
