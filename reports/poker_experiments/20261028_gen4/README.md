# Poker gen4 (2026-10-28): state sets A/B

Goal: break the recurring loss-minimization failure (shove-or-fold; pots rarely reach turn/river, so the policy never
learns deep play) by starting a share of training hands from stored mid-hand spots.

- Recipe: gen3 trial 0 (the gen3 sweep's best was its own center point = config/poker.ini defaults), 256x3 from
  scratch, 300M steps, cash sessions, live pool of the run's own snapshots, 20% panel tables.
- New opponent: trapper bot (id 11, ocean/poker/archetypes.h): slow-plays strong hands (flat-calls premiums, check-calls
  flop, check-raises turn, bets/raises big on the river). Exploit probe at 100 BB: shove -7.3, raise -8.5, call -4.6
  BB/hand for the script, so it punishes shoving. In the training mask (3970) and the panel (panel_trapper).
- State set: states_2m.bin, 2,000,000 states (200 B each, 400 MB) from build/poker/gen_states (ocean/poker/gen_states.c),
  32 processes x 62,500, 31 s. Cash sessions, 20-250 BB, seats: 10% our policies (gen1.bin 128x3 maniac, gen3.bin
  256x3 trial 0), else uniform over rules, Fullhouse, range, maniac, station, tight, trapper. Capture biased to deep
  spots: preflop 0.4%, flop 5%, turn 15%, river 20% of decisions, x2 facing a bet, x2 pot >= 20 BB, x0.25 when less than
  half the pot is behind. Per 62,500: pre 4,076 / flop 8,989 / turn 21,949 / river 27,486; mean pot 31.8 BB.
  Deciding seat creators: rules 9.6K, Fullhouse 10.2K, range 10.2K, maniac 2.0K, station 11.4K, tight 3.8K, trapper
  10.7K, gen1 1.4K, gen3 3.3K.
- Restore (ocean/poker/states.h): the deciding seat is rotated to seat 0 (the learner), unseen board cards are redrawn
  per use, hole cards stay. Reward counts from the restore point. Logged as state_bb / state_share, kept out of score
  and panel. test_states: 74,113 states round-trip with identical observations, legal actions and chip totals.
- Runs (tmux): poker_gen4_states (state_fraction 0.3, log states.log) and poker_gen4_control (state_fraction 0,
  control.log), 16 threads each, side by side.
- Judge: panel_worst and per-panel BB/hand, the exploit probe, all-in frequency (the user wants ~25% less than gen3),
  and play on the web table.

## Results (28 Oct, both runs 300M steps)
- Offline panel (panel.c, 200 BB, 20K hands; panel_*.txt): states, control and gen3 are equally strong. Worst opponent rules
  -1.02 / -0.90 / -0.93 BB/hand; range +1.5 / +1.5 / +1.7; tight +9.9 / +9.8 / +10.0. Exploit probe the same (exploit_*.txt).
- All-in rate (ocean/poker/allin.c, 100 BB; allin_*.txt), share of hands all in vs rules / range / tight / trapper:
  gen3 34 / 32 / 31 / 40%, states 27 / 26 / 25 / 31%, control 24 / 23 / 22 / 29%. About the 25% drop the user asked for;
  shared by both arms, so likely from the trapper and recipe, not the state sets (no measurable gain this round).
- gen4 control is on the web game as "Puffer gen4" (user: still a bit too happy to all in, but a lot better).
- Final checkpoints: leagues/poker/gen4/gen4_{states,control}.bin (checkpoints/poker/1793207441621 and 1793207461631).
  The 2M-state set was not kept; regenerate it in ~30 s on 32 cores (build line in ocean/poker/gen_states.c):
  seq 0 31 | xargs -P 32 -I{} build/poker/gen_states parts/s{}.bin 62500 --seed {} --policy gen1.bin 128 3 --policy gen3.bin 256 3
  then cat parts/s*.bin > states_2m.bin.
- Poker box 58025673 released after this run.
