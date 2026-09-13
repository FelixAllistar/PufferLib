# Pokémon state-bank pilot

Parent held-out score: 81.45%.

| Training seed | Arm | Steps | Held-out score | Training seconds to checkpoint |
|---|---|---:|---:|---:|
| 6101 | root | 5,242,880 | 80.47% | 34.8 |
| 6101 | root | 10,485,760 | 80.86% | 75.5 |
| 6101 | root | 15,728,640 | 78.12% | 105.2 |
| 6101 | root | 19,922,944 | 78.52% | 129.4 |
| 6101 | states | 5,242,880 | 78.91% | 55.0 |
| 6101 | states | 10,485,760 | 79.10% | 95.8 |
| 6101 | states | 15,728,640 | 77.54% | 135.4 |
| 6101 | states | 19,922,944 | 79.49% | 163.9 |
| 6102 | states | 5,242,880 | 81.05% | 41.0 |
| 6102 | states | 10,485,760 | 79.49% | 78.7 |
| 6102 | states | 15,728,640 | 80.27% | 116.1 |
| 6102 | states | 19,922,944 | 80.27% | 140.3 |
| 6102 | root | 5,242,880 | 80.47% | 56.4 |
| 6102 | root | 10,485,760 | 77.54% | 101.2 |
| 6102 | root | 15,728,640 | 79.30% | 146.9 |
| 6102 | root | 19,922,944 | 79.49% | 183.2 |

Bank: [1079, 20574, 24285] opening/midgame/endgame states; collection 305.2s.

All scores use normal drafts, both seats, fresh evaluation seeds and four held-out opponents.
The training population is the same six fixed self-generated policies in both arms.
Bank episodes use zero recurrent memory and a persistent auxiliary-task flag.
Reset improvements must transfer to root games; reset-game win rates are not the result.

Matched final reset-minus-root score differences: seed 6101: +0.98%, seed 6102: +0.78%.
