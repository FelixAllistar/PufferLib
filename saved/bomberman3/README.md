# Bomberman baseline 3

Continued `saved/bomberman2/model.bin` in ordinary full-game self-play with the
reverse curriculum disabled.

- Training run: `fullgame_model2_100m`
- Final checkpoint step: `99,942,400`
- Known minimum cumulative training: `337,313,792` steps
- Saved model: `model.bin`
- SHA-256: `a59c2c742a25e4233b197d21ae8c680dba51085239cd1118534310e0a94e6209`
- Architecture: unchanged (1200 observations, hidden 128, 2 MinGRU layers,
  6 actions)

Qualitative evaluation found substantially better midgame positioning and bomb
allocation than bomberman2, including useful simultaneous bombs in separate
areas. The major remaining weakness is conservative late-game play with too few
attempts to force a credited kill.

Watch it from the repository root with:

```bash
./bomberman watch saved/bomberman3/model.bin
```

The associated `config.ini` is the exact completed run log. `bomberman2`
remains the pre-full-game-continuation baseline.
