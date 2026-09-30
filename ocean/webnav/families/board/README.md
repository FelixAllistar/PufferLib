# Tic-tac-toe board

Local ID 0 is `tic-tac-toe`. Stock CPU Bend owns board transitions, opponent
policy, instance generation, the 10-second clock, and all rewards. X is always
the player. O opens when `randi(0,100)>50` (49% of draws). On every O turn,
the source first draws `randi(0,100)` and chooses a random free cell when the
result is below 55, or whenever all nine cells are free. Otherwise it tries a
smart move: O's immediate win before an X block, the first candidate within
each directional helper, then the **last** available of seven helpers in source
order. When no smart candidate exists, it samples a random free cell. The
model preserves this draw-consumption order. A click on an occupied cell is
inert; after a legal X click, X win is checked before O moves, and each side
can cause a draw. Rewards are +1 time-scaled for X win, -0.5 unscaled for a
draw, -0.75 unscaled for O win, and -1 for timeout.

The C ABI-v2 layer only validates/encodes cell clicks and projects nine public
`WF_CELL` values (`X`, `O`, or empty). The model's random state at row word 32
is never included in `WFView`. The public controller reads those nine values
alone and searches for a non-losing move, favoring branches that admit more
wins against randomly chosen O replies. It cannot guarantee a win in every
game: some positions force a draw against competent O play.

Native regressions include an independent source-order C opponent oracle over
complete games, forced smart-win and block positions, marked-cell no-ops,
draw/win/loss/timeout rewards, fresh-versus-dirty resets, initial-board
variation, and public-view independence from private RNG. The browser fixture
runs the pinned original handlers. It substitutes the same small deterministic
random stream in both browser and Bend, then compares board, consumed RNG,
terminal status, raw reward and timed reward after every action. This tests
the original policy and branch timing without copying post-action browser
state into Bend. It does not claim bitwise parity with the page's default
`seedrandom` implementation; the sampled board positions and random draws
remain source-valid.

Root qualified eight Bend laws, 256 independent native games, 20 original-page
episodes, and 1,000 public scripted games (808 wins, 192 draws, zero losses).
The native public controller test separately recorded 209 wins and 47 draws
over 256 games. Draws are reported separately; they are not full-credit wins.
See [RESULTS.json](RESULTS.json) for commands, source hashes and scope. These
are model/conformance checks, with no learned RL policy for this family yet.
