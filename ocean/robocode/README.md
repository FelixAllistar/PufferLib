# Robocode play/watch tools

The simulator, scripted bots and training config remain the finalized upstream
5.0 versions. This fork restores the old interactive play/watch workflow and
the optional radar-wedge toggle without changing the trainer.

```bash
make -C ocean/robocode viewer test
./ocean/robocode/build/viewer play MODEL.bin
./ocean/robocode/build/viewer watch MODEL.bin --deterministic
./ocean/robocode/build/viewer watch MODEL.bin --vs-bot --bot-policy 3
```

Run from the repository root. Omit the model path (or use `latest`) to select
the newest `.bin` below `checkpoints/robocode`. With no arguments the viewer
starts human-versus-latest play. Watch defaults to two agents sharing the
checkpoint; `--vs-bot` uses one agent and one scripted bot instead.

Controls: W/S acceleration, A/D turning, Q/E gun, left/right arrows radar,
Space fire, R restart, H hide/show radar. `--seed N` selects the environment
seed. The default sampler is stochastic; `--deterministic` selects argmax.

As in the legacy viewer, settings come from `config/robocode.ini`, not from
checkpoint metadata. Supply matching overrides for older checkpoints, e.g.
`--policy.hidden_size=128 --policy.num_layers=2`. The loader checks checkpoint
size before constructing the network. Gameplay and reward settings may differ
from the legacy run because this viewer intentionally uses upstream 5.0.

Tests exercise upstream CPU inference with synthetic weights, stochastic and
argmax actions, mirror and scripted-bot episodes, and terminal carry resets.
The interactive executable builds; keyboard/window/radar rendering still
requires visual qualification on a machine with a display.
