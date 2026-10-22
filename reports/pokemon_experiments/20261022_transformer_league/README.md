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

**350M: SyntheticRLV2 20.1, TaurosV0 18.8.** This is far below B1's 50.7 / 52.1 at 350M.
- By panel team, it wins about 56–58% vs the gar_egg_zap mirror and 0–2% vs zam_egg_mie and
  jynx_mie_don.

**CPU match vs each of the 13 league banks** (`./pokemon eval`, 128 argmax games each):
- It wins only vs the three Gengar-led banks: gar_egg_mie 33%, gar_egg_zap 29%, gar_egg_mie_zap 28%.
- It has 0 wins vs all ten others, with losses and draws only.

**GPU check.** A learning-rate-0 resume of the checkpoint vs a one-bank zam_egg_mie league scored about
0.3%, which agrees with CPU. So this is not an inference bug: training against the league produced a
narrow policy that only handles the Gengar mirror, while PufferNet with the same league learned broadly.
The in-training score (about 0.47) hid this, which motivated the per-opponent win-rate logging.
