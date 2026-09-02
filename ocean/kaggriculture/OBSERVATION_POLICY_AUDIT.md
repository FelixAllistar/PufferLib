# Kaggriculture encoder/decoder audit

This audit covers the current native training path used by `./puffer train
kaggriculture`, including structured macro mode 2.

## Result

No action-head masking bug or private-information leak was found. The decoder
does not train on ignored market-tail choices, and structured macro parameters
are legal and active only where intended. The most credible remaining model
limitations are information compression and temporal context, not a silently
broken decoder.

## What the policy actually receives

The 1,280-byte public observation contains:

- own cash, opponent public cash, cash difference, turn/day/hour/time left;
- both land masks, public worker counts, shops, and season phase;
- both farms summarized by quadrant, crop/animal kind, age, yield, maintenance
  state, harvestability, care, and fertilizer;
- own shed and seed inventory;
- own worker/cohort positions, carried inventory, and route features;
- opponent public worker positions/counts, but no opponent private inventory;
- current market inventory deviations and prices;
- reset-source identity; and
- in macro mode, 44 legal/economic candidate bytes plus active intent,
  quantity, target, and sticky ticks.

Bytes are normalized once to `[0, 1]` before any learner or frozen bank sees
them. CPU/GPU adapter tests and submission parity tests cover this ABI.

## Encoder

`kaggriculture_encoder.cu` currently has an environment-specific vtable but
uses the same operation as the generic encoder: one bias-free dense matrix
from 1,280 inputs to `hidden_size`. At 128 hidden units this is a 10:1 linear
bottleneck before the MinGRU. It has no entity sharing, product embedding, or
separate farm/market/candidate branches. Consequently the model must learn
tomato, carrot, crop, animal, market, and routing interactions independently.

The MinGRU does carry state across rollout chunks at inference. Training
gradients, however, are truncated to `train.horizon`; horizon 64 is only 2.67
game days. GAE/gamma can assign a long return to an action without teaching a
long recurrent computation across all 720 turns. This distinction is a likely
source of “knows the current farm, poorly times long plans.”

Two intentional quantizations deserve attention:

- cash uses one byte over `$0..$100k` (about `$392` per step and saturation
  above `$100k`);
- native macro estimates have seven score bits, with the high bit indicating
  legality. They are coarse guidance, not a learned value function.

The observation contains current price/inventory but no explicit recent price,
sales, or supply delta. Trend must be reconstructed in recurrent state. That
makes short horizons and small hidden sizes especially relevant to hold/sell
timing.

Changing observation semantics in the unused tail could add cash bands and
market deltas without changing the file size, but it would not be safe for old
checkpoints: their weights on formerly-zero columns were never trained. Treat
such a change as a new observation ABI/league rather than silently applying it
to current champions.

## Decoder and masks

The decoder is one linear projection from the recurrent hidden state to all
1,058 logits plus a value row. In structured macro mode:

- head 0 chooses one of 44 macro intents;
- head 1 chooses one of eight quantity bins;
- head 2 chooses a target bin;
- unused unit heads are masked to one canonical action; and
- every market slot is masked to STOP because deterministic execution packs
  the primitive orders.

Heads with only one legal action have exactly zero entropy/policy gradient.
Conditional market heads beyond STOP are excluded from the sampled log
probability and PPO loss. Therefore the 47-head legacy ABI does not dilute the
macro policy with 44 independently noisy decisions. The shared linear decoder
is plain, but not currently wrong.

## Recommended order of experiments

1. Use the quality-diversity archive to separate crop, animal, mixed,
   expansion, and cash-out behaviors while ranking each by real final cash.
2. Compare horizon 64 versus 128 only on matched 128x2 settings; do not combine
   this with simultaneous hidden-size/minibatch/LR changes.
3. Promote diverse finalists into an architecture-compatible league and run a
   longer continuation with reward scales reduced.
4. If contextual selling is still weak, add learned candidate values or a
   versioned semantic encoder with explicit market-delta/cash-band inputs.
   Do not keep increasing handcrafted crop/animal score coefficients first.

The existing Ridge counterfactual model is useful as a pipeline check, not yet
accurate enough to replace the native estimate. A LightGBM-to-small-MLP model
should be trained on much more counterfactual state/action data, validated by
held-out episodes and actual rollouts, and then supplied as an observation
feature. PPO should retain the final choice so it can deviate from the model
against new opponents.
