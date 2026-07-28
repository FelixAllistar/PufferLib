# Bomberman baseline 4

Continued `saved/bomberman3/model.bin` for 100M requested ordinary full-game
self-play steps with a small useful-pickup reward.

- Training run: `pickup_model3_100m`
- Final checkpoint step: `99,942,400`
- Known minimum cumulative training: `437,256,192` steps
- Saved model: `model.bin`
- SHA-256: `5a53a8eb82d94d4cdff4ba6d6669b0420bb60f0e3b4d5649cb600f76259c931b`
- Architecture: unchanged (1200 observations, hidden 128, 2 MinGRU layers,
  6 actions)
- Parent: `saved/bomberman3/model.bin`

## Intervention

This was an isolated reward ablation:

- `reward_pickup = 0.5`
- reward is paid only when a pickup actually increases bomb capacity, blast
  range, or movement speed;
- capped-out pickups are consumed but receive no reward;
- reverse curriculum remained disabled;
- all other rewards, model settings, horizon, discount, masks, game rules, and
  the 1,600-tick episode limit were unchanged from the parent run.

During training, pickups increased from almost zero to more than 5.5 per agent
episode, while raw kills also increased. Visual inspection did not show obvious
pickup-reward farming or excessive rushing. This suggests that the denser reward
made upgrades instrumentally useful for combat rather than replacing combat as
the objective.

Watch it from the repository root with:

```bash
./bomberman watch saved/bomberman4/model.bin
```

The associated `config.ini` is the exact completed run log. `bomberman3`
remains the zero-pickup-reward control.
