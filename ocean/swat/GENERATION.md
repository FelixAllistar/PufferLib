# Generated houses and player preferences

The playable generator chooses the parameters of a bounded building grammar.
Its shipped neural network has been trained on synthetic, validated examples
ranked by authored layout scores. It has received **zero human preference
pairs**. The local comparison UI and offline preference-training path are
implemented so real player judgments can replace those initial assumptions.

## Playing and reproducing a house

Open **P > Houses**, choose Compact/Standard/Complex, a seed and Learned/Random,
then build. All layouts use the same physical wall faces, timber frames,
floor finishes, furniture, doors, occupants, tools and snipers as Cedar House.
Generated missions place 1/2/3 suspects and three civilians. Secure everyone
and return to staging. Choosing a house starts a fresh mission.

```sh
./swat play --mission generated --layout-seed 42 --difficulty 1
./swat host --mission generated --layout-seed 42 --difficulty 1
./swat server --mission generated --layout-seed 42 --difficulty 1
```

Only the session leader can change the shared mission. The host sends accepted
tokens, seed, model ID, room metadata and all physical objects; peers use that
authoritative result without running a local model. Restart preserves the
layout seed. NPC weapon randomness is separately controlled by the ordinary
simulation seed. A seed plus the exact model/build reproduces generation;
the accepted tokens are the portable layout description.

The current grammar includes three arrangements: a three-room T partition, a
four-room grid, and four rooms around a central hall. Other decisions choose
10/12/14 m width, 8/10/12 m depth, partition offsets, entry/exit placement,
windows, doorway offsets, furniture, floor palette and occupant distribution.
This is a single-storey prototype, not a general architectural design model.

Every candidate passes a room/door graph check, opening bounds, minimum room
dimensions and conservative 30 cm navigation-grid clearance around walls and
furniture. Occupants must be reachable from staging. The actual wall builder
also runs in counting mode before allocation; oversized plans are rejected
against the 1,536-object world limit with eight spare slots. Fixed 2.7 m ceilings
and 2.1 m door openings fit the standing controller. The test suite walks the
real controller through every door in both directions across 24 houses.

Sampling retries up to 128 candidates. If an externally supplied neural model
cannot produce an accepted layout, the game falls back to the uniform sampler
and records policy ID zero. No unchecked neural output enters the physics world.

## The trained policy

`generation.c` performs autoregressive inference on twelve categorical tokens.
The 49 inputs contain 34 prefix one-hot slots, twelve decision-step slots and
three difficulty slots. A 64-unit tanh hidden layer produces three logits;
invalid categories are masked and sampling uses temperature 0.9. There are
3,395 parameters. Runtime inference is ordinary C with no Python, Torch or GPU
dependency. The committed header embeds the bootstrap weights, and
`generated/layout_policy.txt` contains the same optional loadable weights.

Bootstrap training draws 12,000 valid uniform examples, deduplicates layout
fingerprints, and imitates the best 40% within each difficulty. The heuristic
rewards appropriate floor area/room count, reasonable room proportions, windows,
door connections and usable cover. These choices are initial design hypotheses.
Train/validation splits keep identical houses together.

The committed [training manifest](generated/layout_training.json) records the
seed, corpus hash, weight hash, model ID, architecture and measurements:

| Unseen-seed check | Uniform | Learned |
| --- | ---: | ---: |
| Layouts sampled | 1,500 | 1,500 |
| Unique layouts | 1,499 | 1,494 |
| Mean authored score | 1.4174 | 1.7692 |
| Size combinations represented | 9 | 9 |

All three arrangements remain represented. Raw learned samples passed
validation 99% of the time before rejection sampling. C and PyTorch logits
agreed within `2.4e-7` in the recorded parity check. These results establish
that training changes the distribution toward the specified score; they do
not establish that the houses are more fun.

To train an experiment, use a Python environment with NumPy and PyTorch
(the manifest records the tested version). Training uses two CPU threads:

```sh
make -C ocean/swat layout-tool
python ocean/swat/train_layout.py bootstrap --out build/swat/layout-experiment
./swat play --mission generated --layout-model build/swat/layout-experiment/policy.txt
build/swat/layout_tool check 512 neural --model build/swat/layout-experiment/policy.txt
```

The output directory contains `policy.npz`, portable `policy.txt`, and
`manifest.json`. `--header PATH --manifest PATH` explicitly exports a new built-in
header and its provenance for review. Train once and load the text file to try
a model without rebuilding the game. The file header is
`SWAT_LAYOUT_NET 1 49 64 3 MODEL_ID`, followed by exactly 3,395 finite float32
values in input weights/bias, output weights/bias order. Loading is transactional.
The short model ID is provenance, not a cryptographic authenticity mechanism;
the manifest also records SHA-256.

## Collecting player comparisons

After playing two different generated houses for at least five simulation
seconds each, **P > Houses** enables **Previous / This one / Tie**. It appends
one JSON line only when a player chooses a button. There is no background upload,
automatic rating, player identifier or automatic retraining. The file lives
beside the selected settings profile:

- Windows: `%LOCALAPPDATA%\SWAT Gold Element\settings.ini.layouts.jsonl`
- Linux: `~/.config/swat-gold-element/settings.ini.layouts.jsonl`, or the
  corresponding `$XDG_CONFIG_HOME` location.
- `--settings my-profile.ini` uses `my-profile.ini.layouts.jsonl`.

Each row contains version/source/time, the two accepted token sequences,
difficulty, seeds, model IDs, fingerprints, played ticks, current mission outcome,
civilian damage and the explicit comparison. `a` means the previous house,
`b` means this house; `choice` is `a`, `b` or `tie`. The UI remembers comparisons
within the current play session and accepts one vote per current pair. Training
deduplicates a repeated pair regardless of ordering. The file can be inspected,
copied deliberately for training, or deleted independently of saved controls.

## Preference learning

```sh
python ocean/swat/train_layout.py finetune \
  --feedback /path/to/settings.ini.layouts.jsonl \
  --base ocean/swat/generated/layout_policy.txt \
  --out build/swat/layout-player-model
./swat play --mission generated --layout-model build/swat/layout-player-model/policy.txt
```

A 37-input, 64-hidden-unit reward network learns Bradley–Terry pairwise
preferences, including ties. Validation houses are entirely excluded from
training; comparisons crossing the split are excluded. Training requires at
least 64 distinct comparisons, including 32 usable training and eight validation
pairs. Insufficient data produces an explanation rather than inventing labels.
Imported layouts also pass the current C validator.

The generator then receives REINFORCE updates from the learned reward with a KL
penalty to the base policy and a small entropy bonus. Invalid sampled layouts
receive a penalty, and the runtime validator still applies. The output includes
reward weights, policy weights, held-out preference loss/accuracy, raw validity,
predicted reward, KL and source hashes. Predicted reward improvement is an
internal optimization result; use new blind player comparisons and mission
completion/civilian-harm metrics before promoting a model.

An executable pipeline smoke test uses clearly marked synthetic comparisons:

```sh
python ocean/swat/train_layout.py smoke \
  --base ocean/swat/generated/layout_policy.txt \
  --out build/swat/layout-preference-smoke
```

The recorded smoke run trained on 571 pairs, validated on 62 disjoint-house
pairs and reached about 91.5% decisive preference accuracy. The policy's mean
normalized predicted reward increased from 0.798 to 1.178; raw validity was
99.0% before and 99.3% after. These labels came from the authored heuristic,
and the resulting smoke policy is **not** the shipped policy. Ordinary finetuning
refuses synthetic labels unless `--allow-synthetic-test` is explicit.

## Research context and next steps

The representation and bootstrap approach are a small implementation choice
in the space surveyed by [Procedural Content Generation via Machine Learning](https://arxiv.org/abs/1702.00539).
It uses generated examples from our grammar; it does not claim a real-house
dataset, a general floor-plan network or an implementation of another published
architecture. The learned reward plus policy optimization approach follows the
general pattern of [Deep reinforcement learning from human preferences](https://arxiv.org/abs/1706.03741),
applied here to complete layout tokens rather than behavior clips.

Next useful evidence is real play: compare different arrangements at the same
difficulty, capture why a layout was preferred, and track success, time,
approach options, sight lines and spawn fairness. Expand the grammar only with
physical/navigation tests, then collect comparisons covering that new space.
Room identity, richer furniture, multi-storey construction and architectural
datasets need separate representation/data work. A trained house generator is
not a trained suspect, civilian or squad policy; the annex RL contract stays v1.
