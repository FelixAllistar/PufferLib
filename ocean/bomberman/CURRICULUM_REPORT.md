# Bomberman curriculum and self-play report

## Research question

Can reverse curriculum and self-play bootstrap competent Bomberman play from
sparse terminal rewards, without encoding strategy in action masks or scripted
rules?

The current result supports a qualified **yes**. Reverse curriculum supplied
successful tactical trajectories that raw self-play rarely discovered, and
subsequent full-game self-play connected those skills into substantially better
midgame play. It has not solved every part of the game: rare early/midgame
suicides remain, and the latest policy becomes excessively conservative in
sparse late-game states.

Only physically impossible actions are masked. We deliberately do not mask
strategically bad actions such as unsafe bombs or waiting near danger; those
behaviors must be learned.

## Current training system

Each environment advances through the curriculum independently. The original
ten-stage ladder accumulated useful but non-monotonic corner/escape insertions
and eventually demanded too much stochastic mastery from every worker. The
current five-stage ladder is again an explicit reverse combat chain. Among
tactical resets, 75% use the frontier and 25% rehearse an earlier stage.
Ordinary games are mixed in with probability `0.5 * progress^2`. Only frontier
episodes count toward graduation.

| Stage | Reset | Intended skill |
|---:|---|---|
| 0 | Cleared board, stationary target two cells away | Move once, bomb at the correct range, retreat, and kill |
| 1 | Cleared board, stationary target four cells away | Find and execute a stationary-target kill |
| 2 | Same farther setup with a moving target | Transfer the kill sequence to target motion |
| 3 | Plausible partially cleared midgame | Connect navigation, bombing, escape, and pursuit |
| 4 | Untouched opening with movement-only opponent | Execute a credited kill from turn zero |

Graduation is evaluated in windows of 64 frontier episodes. Every stage
requires a win containing a credited kill and no self-kill. Stages 0--1 require
45% success (29/64), stage 2 requires 5% (4/64), and stages 3--4 require 2%
(2/64). Passing stage 4 exits directly into ordinary self-play; ordinary games
are not redundantly gated as a final curriculum stage. The time-based fallback
is effectively disabled at one billion per-environment steps.

The optional `reward_closer` is signed nearest-opponent Manhattan-distance
progress: approaching pays `+x` per cell and retreating pays `-x`, so an
approach/retreat cycle has zero undiscounted shaping return. It defaults to
`0.0` at the current experimental pivot.

Training also uses a rotating pool of up to 32 historical learner checkpoints.
This reduces immediate overfitting to the newest opponent, but it is not a full
league: it cannot currently pin external specialist opponents or sample them by
measured weakness.

## What did not work

1. **Raw full-game self-play from weak policies.** Credited kills were too rare
   to bootstrap the sequence of bombing, preserving an escape, predicting the
   blast, trapping an opponent, and surviving.
2. **Reward magnitude changes alone.** A larger kill reward did not create
   successful trajectories. It only changed the value of trajectories the
   learner already encountered.
3. **Synthetic reverse states.** Fabricated bombs, cooldowns, inventory,
   invulnerability, or sealed layouts produced lesson-specific competence with
   poor ordinary-game transfer. Curriculum states were revised to be plausible
   states reachable under the real rules.
4. **Time-based forced graduation.** Advancing workers after a fixed number of
   steps caused ordinary-game performance to collapse when prerequisite skills
   had not been mastered.
5. **Rewarding threat or escape by itself.** The agent could farm reward with
   harmless bombs. Those bonuses are now tied to a credited kill in normal
   games; tactical bridge rewards exist only inside curriculum lessons.
6. **Interpreting aggregate score as aggression.** Score mixes reward sources,
   wins include opponent suicides, and draws or mutual deaths can hide the
   mechanism of an outcome. `slot_0_kills` is the principal aggression metric,
   alongside self-kills, opponent suicides, and draws.

## What worked

The successful strategy was not a perfectly designed backward chain. We began
near a terminal kill, audited ordinary play, identified where transfer failed,
and inserted believable missing-skill states. Rehearsal and the growing
full-game mixture allowed the lessons to overlap and grow into one another.

- The first believable curriculum produced `saved/bomberman1`.
- A recurring real corner-spawn failure motivated the L-pocket stages.
- Fine-tuning produced `saved/bomberman2`, which scored about 58.6% against
  bomberman1 over 4,096 ordinary matches.
- A further 99,942,400 full-game-only steps markedly improved qualitative
  midgame behavior and bomb allocation. The policy stopped routinely doubling
  bombs in one location and began maintaining useful bombs in separate areas.
- That continuation remains too passive late in the game: when rewards become
  sparse, it often preserves safety, places few bombs, and waits rather than
  forcing a credited kill.

At 60M steps of the full-game continuation, the training dashboard reported
68.9% draws, 0.054 slot-0 credited kills, and a mean episode length of 1,191
ticks. Those intermediate aggregate metrics suggested a conservative
equilibrium. Watching the final policy revealed genuine midgame improvement,
so the correct conclusion is not that the run simply failed: it improved useful
skills while still failing the late-game objective. Video inspection and
mechanism-specific evaluation are both necessary.

## Why the remaining problem is difficult

Bomberman has no single scalar "distance from victory." Difficulty changes
along partially independent axes: map topology, bomb timing and chains,
inventory, escape geometry, opponent aggression, destructible-block density,
powerup asymmetry, and the nonstationary self-play population. A fixed
one-dimensional reverse ladder therefore tends to expose new gaps as old ones
are repaired.

The late-game curriculum is also incomplete. Its cleared-board stages retain
turn-zero inventory, whereas real late games often contain asymmetric bomb
capacity, blast range, speed, and spatial control. The policy has observations
for all three pickup types and for both players' inventory statistics; the
problem is experience and credit assignment, not missing state information.

With `max_ticks = 1000` and `gamma = 0.99`, a terminal outcome 1,000 decisions
away has a direct discount of roughly `0.99^1000`, effectively zero. Thus a
timeout penalty alone gives very weak early pressure against a late draw. The
64-step rollout horizon covers the 18-decision bomb fuse, but not necessarily
long-term resource conservation or endgame initiative.

Rare suicides also need to be separated into two causes: genuinely incorrect
state values versus low-probability bad actions retained by stochastic policy
sampling and entropy regularization. Comparing sampled and greedy evaluation is
the clean diagnostic. Strategic safety masks would conceal this distinction and
invalidate the learning experiment.

## Powerups and the next controlled experiment

The simulator has three powerups:

- **B:** simultaneous bomb capacity
- **R:** blast range
- **S:** movement speed

The policy observes the pickup type on the board plus available bombs, maximum
capacity, deployed bombs, blast range, and speed for both players. Previously
the viewer rendered all pickups identically and omitted inventory; the revised
HUD displays distinct B/R/S pickups and both players' statistics.

`play/watch` loads the checkpoint's board, timer, movement, density, item, and
architecture settings, but deliberately extends visual sessions to 30,000
ticks so observation can continue until a death. Passing `1000` as the final
CLI argument restores exact training-deadline timing for controlled evaluation.
The viewer prints both the trained and visual deadlines at startup.

The bomberman3 baseline had no immediate pickup reward. The next experiment
adds `reward_pickup = 0.5` only when a pickup actually increases a statistic,
which we initially believed was small relative to the raw credited-kill reward.
Because the kill was clipped to `+1`, a pickup was actually half of the entire
learner-visible kill signal. Total and per-type pickups were logged separately;
the experiment demonstrated that dense pickup credit changes acquisition, but
not that `0.5` was an appropriately scaled value. No late-game penalty, bomb
cost, horizon change, or specialist opponent was introduced in that run.

After evaluating that isolated change, reasonable separate ablations are:

1. Shorter full-game time limit or stronger effective time preference.
2. Higher `gamma` and/or a longer rollout horizon.
3. Real-state failure replay: save authentic states preceding suicide, wasted
   bombs, missed kills, and timeouts, then restart progressively farther before
   each failure while retaining many full games.
4. A frozen aggressive specialist in a league if ordinary self-play still
   never produces rushing pressure. This requires trainer support for pinned
   external opponents and should not be approximated by scripted tactical
   masks.

## Reproduction and model lineage

`base.run_id` only names the checkpoint directory and log. Omitting it generates
a timestamp; it does not change learning. Loading a `.bin` restores weights but
not optimizer state or cumulative step metadata.

From random weights, the current five-stage experiment is:

```bash
./puffer train bomberman \
  base.load_model_path=None \
  env.reverse_curriculum=1 \
  train.total_timesteps=1000000000 \
  base.run_id=bomberman_from_zero
```

Full-game-only continuation is:

```bash
./puffer train bomberman \
  base.load_model_path=saved/bomberman2/model.bin \
  env.reverse_curriculum=0 \
  train.total_timesteps=100000000 \
  base.run_id=fullgame_model2_100m
```

The checkpoint ancestry reconstructs a known minimum of 237,371,392 steps for
bomberman2. Adding the completed 99,942,400-step full-game run gives the latest
policy a known minimum of **337,313,792 training steps**. The true total may be
higher because the oldest retained ancestor has no identifiable parent.

The historical route was iterative rather than a single clean run: believable
reverse curricula produced bomberman1, the L-pocket extension produced
bomberman2, and ordinary full-game continuation supplied the missing experience.
A fresh run through the current integrated curriculum is supported, but has not
yet been validated across multiple random seeds.

## Evaluation standard

Claims should be based on ordinary games with fixed seeds and frozen opponents,
not mixed-curriculum returns alone. Report at minimum:

- safe credited kills;
- early, midgame, and late self-kills;
- opponent suicides and mutual deaths;
- draw/timeout rate and episode length;
- pickups by type;
- bombs and empty bombs per credited kill;
- performance by topology and powerup asymmetry;
- sampled versus greedy action selection;
- worst scenario cluster, not only the mean.

The core result so far is that believable reverse curriculum solved the initial
sparse-exploration barrier, while extended full-game self-play improved
integration. The remaining scientific question is how best to supply realistic
late-game credit and coverage without hand-coding the desired strategy.

## Pickup experiment result

The isolated 100M-step pickup-reward continuation produced `saved/bomberman4`.
Training telemetry increased from almost no acquisition to more than 5.5 useful
pickups per agent episode, accompanied by an increase in raw kills. Qualitative
inspection did not show excessive pickup chasing. This is preliminary evidence
that a small dense reward improved access to strategically useful capabilities
and thereby helped the sparse combat objective; it is not yet a multi-seed
causal result. Bomberman3 is retained as the zero-reward control.

## Reward-clipping correction

Puffer hard-clamps every learner-visible per-step reward to `[-1, 1]`. Earlier
Bomberman configurations were written and interpreted as if their raw values
were preserved. They were not: safe kills above `+70` became `+1`, while timeout
`-2` and self-kill near `-101` both became `-1`. Pickup `+0.5` and soft-block
`+0.3` remained unsaturated, making each dense event enormous relative to the
actual kill signal. Environment logs retained raw returns, further obscuring
the discrepancy.

`saved/bomberman5` preserves the final policy trained under that clipped reward
system. The next configuration keeps all complete two-player event sums within
the learner's range: safe credited kill `+0.849`, timeout `-0.301`, ordinary
death `-0.401`, mutual credited kill `-0.441`, and unproductive self-kill
`-0.991`, including the per-step time cost. Pickup and soft-block rewards are
reduced to `+0.02` and `+0.01`. This restores meaningful reward ordering without
depending on hidden clipping.
