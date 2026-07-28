# Bomberman baseline 1

Checkpoint selected after the believable reverse-curriculum v7 density run on
2026-07-27. This is the first visually validated baseline: it can play and kill
competently, with a remaining early-corner self-kill failure mode.

- Original checkpoint: `believable_reverse_v7_density_20260727/0000000034996224.bin`
- Saved model: `model.bin`
- SHA-256: `9377916b65dfe32b8d7b8dbb6c2abac6915e841cef21e2df6574ef18ac517b78`
- Architecture: 1200 observations, hidden size 128, 2 MinGRU layers, 6 actions

Watch it from the repository root with:

```bash
./bomberman watch saved/bomberman1/model.bin
```

`config.ini` is the exact Bomberman configuration snapshot associated with the
model. The standalone watcher runs ordinary games and applies the same legal
action masking used by training and evaluation.
