# Gen 3 random battles: first run, fresh, 1B steps (2026-11-06)

Run (Vast 59392729, RTX 5060 Ti): `ocean/pokemon3`, 1024x2 policy trained from scratch, pure mirror self-play on Gen 3
random-battle positions (100K Showdown-generated team pairs, turn 1). No team picking, forced combinations, comps or
Gen 1 starting point. 4096 agents (8192 does not fit the 16 GB GPU), ~210K steps/s, 80 minutes, 0 engine errors.
Kept: `checkpoints/pokemon_final/20261106_gen3_randbat_1B.bin` (md5 194ad0eac00b3280329ab634a33f636f).

## In the env (evals.txt, watch.sh)
| Steps | vs random (sampled) | vs 52M checkpoint (4K games) |
|---|---|---|
| 105M | 92.4% | 49.9% |
| 315M | 95.2% | 59.4% |
| 524M | 95.8% | 66.2% |
| 734M | 96.4% | 69.9% |
| 944M | 96.7% | 68.8% |
| 1B final | 96.6% (argmax 95.8%) | 70.9% (argmax 69.0%) |
Argmax vs sampled, same final checkpoint: 51.6% (8K games), a tie.

## On Showdown (ocean/pokemon3/showdown bridge, argmax)
| Opponent | Format | Result |
|---|---|---|
| random moves | gen3randombattle | 25-5 before, **55-5 (91.7%)** after the bridge fixes below |
| max damage (maxdmg.mjs) | gen3randombattle | 15-25 before, 8-31-1 after (40 games each) |
| Metamon Kakuna | gen3ou, both sides from Metamon's 20 competitive teams | 1-119 before, **2-118** after |

Bridge fixes found here: `[of]` ability reveals went to the wrong Pokémon; the env shows our network a "wait" step
while the opponent replaces a fainted Pokémon, the bridge did not (recurrent state out of step).

Kakuna on this box: no flash-attn build for torch 2.11 / Blackwell, so `metamon/rl/configs/models/superkazam.gin` uses
amago's SlidingWindowFlexAttention with the same causal 96-step window (same computation; the .orig is kept), and
amago's `random_traj_length` casts to int (Python 3.12).

## What it means
- The randbat model is strong against random play, but weak against a greedy max-damage bot on Showdown and nearly
  helpless against Kakuna in OU (level 100 OU sets it never saw, and Kakuna is a top-ladder model).
- Remaining bridge gap (91.7% vs the env's 96.6% against random): the env shows the opponent's exact stats, the
  bridge estimates them (85 EVs; ~22% of Gen 3 randbat sets have 0 Attack EVs, and OU spreads are 252/252). The model
  can read a set's role from those stats in training and not on Showdown. Fix for the next run: the env shows the same
  estimated stats a player has.
- Not measured yet: the same model vs a max-damage bot inside the env (would separate model weakness from bridge loss).

## Next
Gen 3 OU model: Smogon teams and sets (`ocean/pokemon3/data`: analysis sets for 106 species, usage stats with
teammates and spreads), the Gen 1 combination/comp curriculum adapted to Gen 3, estimated opponent stats in the
observation.
