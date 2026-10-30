# Video 1 plan: Weedle vs Agatha, with an AI that learns (draft, 2026-10-30)

Draft for Felix to rewrite in his own voice. Nothing here is final.

## What Gym Leader Matt did (his two videos)

- Video 1 ("I Simulated 100,000,000 Battles to See If Weedle Could Beat Agatha"): Gen1Recomp (a PC port of Red) with a
  headless battle mod, 1,000-1,500 battles/s on one PC. Level 100 Weedle, perfect DVs, max stat exp, all badges, vs a
  clone of Agatha with her real team and AI. Weedle only ever uses Poison Sting. One win at 133.5M attempts (Weedle
  left at 7 HP). Level-cap test: wins get easy from level ~115 (1 in 8,500), trivial from ~195.
- Video 2 ("I Simulated 1.5 Billion MORE Weedle Battles Because of a Bug"): he had missed the Gen 1 rule that the AI
  misses stat-lowering moves 25% of the time (Screech 85% -> 64%, Acid's defense drop 33% -> 25%). Fixed: 1.52B
  battles over 6 days on 27 instances / 2 PCs (about 250M/day), zero wins. Bound: below ~1 in 500M. Also: Bruno 1 in
  5.5, Lance 1 in 31, Giovanni 1 in 66, Lorelei 1 in 2,633, Champion 1 in 4,770. Debunks Creep Fan's run from his
  footage (HP 273 pins the HP DV; 43 confusion self-damage pins attack/defense; his position before Arbok never
  happened in 1.5B battles; from that position the win chance is 1 in 1,468).
- He plans a "Gen 1 AI tournament" series, inspired by PimpNite Rules' tournaments. His simulator stays private.

## Our angle

His Weedle never decides anything: it spams Poison Sting. Ours learns.

1. A reinforcement-learning agent pilots the Weedle (Poison Sting vs String Shot every turn, with the whole battle
   state in view) and is trained over billions of battles against Agatha's exact team and cartridge AI. The question
   his simulator cannot ask: is spamming Poison Sting even the best way to play it? String Shot lowers speed; against
   Haunter, Golbat and Arbok, moving first changes who gets hit before the next Hypnosis or Wing Attack. If the agent
   finds a better line, Weedle's odds change (headline). If it doesn't, we prove Poison Sting spam is optimal, which
   no one has shown either.
2. Scale: the trainer runs hundreds of thousands of battles per second on one GPU box, so "1.5 billion in 6 days"
   becomes 1.5 billion in roughly an hour. We show the counter.
3. The level threshold, redone with a smart pilot: the lowest level at which the AI beats Agatha 1 in 10,000 / 1 in 100
   / most of the time, against his spam-only numbers.
4. Teach RL along the way (the actual goal): reward, exploration, why the agent tries "dumb" moves early, what it
   learns, a "brain" visual of its move probabilities per turn.

Honest risk: Weedle has two moves, so the agent's edge may be small. Point 3 still gives a clean chart either way, and
the series can grow into his tournament idea with an agent that actually plays (our Gen 1 OU agent peaked at 1595 on
the Showdown ladder).

## What has to be built first

- Gen 1 engine in cartridge mode: pkmn/engine with showdown = false (ours is built with showdown = true). Cartridge
  mode has the 25% AI stat-drop miss, the badge boost glitch, Gen 1 crits, etc.
- Agatha's trainer AI from the pret/pokered disassembly (engine/battle/trainer_ai.asm and data): move-choice
  modifiers for her class, her switching rule, Super Potion use. Unit-test against known behaviour.
- A Weedle-vs-Agatha env (or mode of ocean/pokemon): the learner is Weedle, the opponent is the scripted AI; reward
  +1 win, 0 loss; seeded RNG for exact replays.
- Validation against his published numbers with Poison Sting spam: Bruno ~1 in 5.5, Lance ~1 in 31, Lorelei ~1 in
  2,633, Champion ~1 in 4,770, Agatha below 1 in 500M. Matching these is our proof of accuracy (and a segment).
- Replays: battle logs to Showdown's replay viewer (Gen 1 sprites) recorded headlessly; Gen1Recomp + voxel mod for
  B-roll from Felix's own ROM dump.

## Title and thumbnail ideas

- "His Weedle Spammed Poison Sting 1.5 Billion Times. Mine Learned."
- "I Taught an AI to Play Weedle (It Found Something)"  (only if it does)
- "Can an AI Make Weedle Beat Agatha?"
Thumbnail (GIMP, real sprites/screens only): Weedle sprite large and center, Agatha's Gengar behind it, a big counter
("1,000,000,000+") or a win-rate number, one short text line. No generated images.

## Production

Script outline below; Felix records the voiceover. Placeholder narration: Kokoro (Hugging Face) on the box CPU.
Edit: ffmpeg (or Remotion for animated charts). Assets: Showdown sprites and replays, Gen1Recomp footage, charts made
from our logs.

## Script outline (about 15 minutes)

1. Cold open: the 1.5 billion zero-win result; "but his Weedle never made a single decision."
2. Credit and recap of Matt's videos (short, respectful, link them).
3. What an AI that learns is: the reward, the trial and error, why it needs billions of battles.
4. Building it: cartridge-accurate engine, Agatha's real AI, matching his numbers for every Elite Four member.
5. Training: the counter, the agent's early nonsense, what it starts to do, its move probabilities by situation.
6. Results: Agatha odds with a smart pilot vs spam; the level chart.
7. What's next: every Gen 1 boss, every solo Pokémon, an AI tournament with agents that play.
