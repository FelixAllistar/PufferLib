# Bomberman baseline 2

Fine-tuned from `saved/bomberman1/model.bin` with the believable L-pocket
corner-breakout curriculum added in commit `305366e2`.

- Training run: `corner_breakout_v1_20260727`
- Final checkpoint step: `39,976,960`
- Saved model: `model.bin`
- SHA-256: `66a68066ec303c770df4c7437650938d130ae35769918b5ce576b5ec3a7f5f9a`
- Architecture: unchanged (1200 observations, hidden 128, 2 MinGRU layers,
  6 actions)
- Ordinary-game match vs bomberman1: about 58.6% over 4096 games

Watch it from the repository root with:

```bash
./bomberman watch saved/bomberman2/model.bin
```

The associated `config.ini` contains the ten-stage curriculum and points to
this saved copy. `bomberman1` remains available as the pre-corner baseline.
