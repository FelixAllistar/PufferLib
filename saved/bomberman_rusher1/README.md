# Bomberman rusher master 1

First aggressive-master branch, fine-tuned from `saved/bomberman5/model.bin`
for 499,908,608 full-game steps.

- Training run: `rusher_v1`
- Final checkpoint: `0000000499908608.bin`
- SHA-256:
  `cb2a98c73343c2448e3f330d0aeaea95c8ca976b77f3e3f9db4494d8cd7b0c1b`
- Architecture: 1200 observations, hidden size 128, 2 MinGRU layers,
  6 actions

The branch used a deadline-scaled credited-kill bonus of `0.30`, reduced the
safe escape component from `0.30` to `0.15`, and faced an even mixture of its
rotating history and the pinned bomberman5 parent. It became qualitatively
stronger and safer, but not a clean rushing archetype.

Final mixed-opponent evaluation reported 5.8% slot-0 credited kills, mean
slot-0 kill tick 526, 7.9% slot-0 self-kills, 66.4% draws, and 5.8 pickups per
agent episode. This is retained as a conservative/partially aggressive master
and as the control for rusher v2.

```bash
./bomberman watch saved/bomberman_rusher1/model.bin
```

`config.ini` is the resolved run configuration.
