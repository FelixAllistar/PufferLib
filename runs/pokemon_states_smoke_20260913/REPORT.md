# Pokémon state-bank pilot

Parent held-out score: 87.50%.

| Training seed | Arm | Steps | Held-out score | Training seconds to checkpoint |
|---|---|---:|---:|---:|
| 6100 | root | 1,048,576 | 87.50% | 12.2 |
| 6100 | states | 1,048,576 | 87.50% | 6.7 |

Bank: [29, 512, 384] opening/midgame/endgame states; collection 4.1s.

All scores use normal drafts, both seats, fresh evaluation seeds and four held-out opponents.
The training population is the same six fixed self-generated policies in both arms.
Bank episodes use zero recurrent memory and a persistent auxiliary-task flag.
Reset improvements must transfer to root games; reset-game win rates are not the result.

Matched final reset-minus-root score differences: seed 6100: +0.00%.
