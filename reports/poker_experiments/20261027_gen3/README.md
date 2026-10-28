# Poker gen3 sweep (started 2026-10-27, poker box Vast 58025673, RTX 5060 Ti 16 GB, 32 cores)

Copy kept in Git (runtime files live under leagues/poker/gen3/, which is not tracked).

Cash tables, 256x3 PufferNet from scratch (config/poker.ini; previous config/poker.ini.gen2_sweep).
- Env (ocean/poker/poker.h, new): stacks carry over 200-hand sessions; each seat's depth is uniform 20-250 BB at session
  start, after a bust (rebuy) and above 500 BB (leaves, a new player sits). Reward per hand in BB. The policy already
  sees everything in big blinds (chips / 100, the big blind is 100 chips), so only depth needed randomizing.
- Opponents: all five bot styles (rules, range, maniac, station, tight), ramping (league_ramp 40) to the trial's own
  snapshots (league_pool_dir = auto: the trainer now publishes PUF_CHECKPOINT_DIR, see reports/core_changes_audit.md).
  20% panel tables. Score: panel_worst (BB/hand vs the hardest bot style).
- Sweep: 500 trials; training length swept 100M-1B; agents 4096-32768, horizon 8-128, minibatch 1024-32768, LR, entropy,
  magnet, gamma, lambda, clipping; width/depth pinned at 256x3; replay pinned to 1. Trial 0 = gen1_selfplay2's recipe.
- Smoke: ~60K steps/s (CPU-bound), 2 GB of GPU memory.
- tmux poker_gen3 (sweep + dashboard), poker_scores (scores.py), poker_clean (clean.sh keeps each finished trial's
  final checkpoint only).
- Not a baseline: gen1 is archived as a style policy (leagues/poker/style_maniac_gen1).
