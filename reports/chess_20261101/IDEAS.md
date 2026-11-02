# Chess ideas and open questions (Felix, 2026-11-02)

Kept as a running list; each becomes an experiment node (inputs, change, result) as we go, like the earlier
experiment graphs.

## Now (v3)
- One action per move: from-square x to-square (4096 + pass), illegal moves masked. Done since v1.
- Zero-sum paired critic for self-play (as in kaggriculture critic_mode 2 and the Dota-style shared critic):
  V = tanh(0.5 * (f(own view) - f(opponent view))), so V(me) = -V(opponent) exactly. Done (chess_decoder.cu,
  CHESS_PAIRED); mate-in-1 test learns faster than with the plain value head (0.69 vs 0.57 at 6M steps).
- EMAgnet (train.magnet_coef) + state sets.
- State sets of ONE simple, aggressive repertoire, played against many bot replies (Maia 1100/1500/1900 with some
  random replies): White Italian / Fried Liver, Black 1...e5 and Queen's Gambit Declined. Done (tools/gen_openings.py,
  60K positions), plus mate-in-1, KQK/KRK and Maia self-play positions.
- Config sanity: replay_ratio 1, high gamma, GAE lambda 0.95, small minibatch (big ones smooth out good play),
  learning rate high enough for Muon with a 4097-way head (0.003 works for the 512 MLP; probe for 1024).

## Next
- Encode rule knowledge in the observation without misleading the net: binary planes, not ordinal numbers
  (0-10 values imply a line that does not exist). Candidates: squares attacked by each side (thermometer: attacked
  by >= 1, >= 2, >= 3), pinned pieces, hanging pieces (attacked and not defended), checking squares, legal
  destinations per piece type. These are facts computed from the rules, so they cannot mislead future planning;
  they only save the net from deriving them.
- Reward for following a fixed opening sequence (hard-coded, "lame, but I bet it works"), as a test of whether the
  repertoire can be taught directly.
- SE-ResNet encoder (chess_encoder.cu, CHESS_SENET) at a properly high learning rate.
- Behavior cloning on human games (Lichess, CC0) with the magnet anchored to the BC policy, then RL.
- Longer term: an ensemble / learned search, where the model is taught to use search and decides how much to search
  (Thinker, PonderNet-style adaptive computation) instead of fixed AlphaZero-scale search.
