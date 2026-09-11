# Pokémon NaN investigation — 2026-09-09

## Stage-3 cause reproduced; Pokémon configuration workaround

The subsequent full run `1788936176437` stopped with
`POKEMON_NUMERIC_FAILURE stage=3 index=16672 value=nan`: finite learner/magnet
forward outputs, but a non-finite action-logit gradient before backpropagation.

The actual replay sampler kernels reproduce a zero-probability frozen-row draw.
For 4096 rows, of which 3072 are learners, the learner-end CDF is
`0.999999762`; the final CDF is `1`. Floating-point prefix-sum rounding leaves
an artificial tail over frozen rows. A draw of 1 selects frozen row 3968 with
probability 0. CUDA `__powf(0, -0.0f)` gives NaN, so disabling prioritization
(`prio_alpha=prio_beta0=0`) does **not** prevent this failure.

Replaying 4,000,000 actual cuRAND draws with seed 73 finds the first frozen row
at draw **2,131,072**, row 3072. With 64 trajectories/minibatch and 48
minibatches/epoch, this is **epoch 694 / 363,855,872 steps**, matching the
314M-finite / 367M-corrupt window. Resuming resets this training RNG counter,
explaining why short checkpoint resumes did not reproduce the original fault.

Forcing the valid RNG endpoint in the complete diagnostic training pipeline
produced:

```
POKEMON_PPO_FAILURE ... adv=0 mean=0 var=0 norm_adv=0 weight=nan
new_lp=-3.1988 ratio=1.00433 d_new_lp=nan ...
```

The logits, entropy and value terms were finite; the importance weight poisoned
the gradient. This run exited 86 before checkpoint save. The same forced-
endpoint binary with `train.epoch_sampling=1` completed 1,048,576 steps cleanly.
An additional 10,485,760-step run with both environment and numerical stage
checks enabled completed as `epoch_sampling_fixed_audit`; the saved main and
EMAG arrays each contained 200,832 finite entries and no non-finite entries.

`config/pokemon.ini` now sets **epoch_sampling=1**. This existing trainer mode
shuffles only learner rows without replacement with weight 1, bypassing the
CDF and fast-power path altogether. This is a sampling-policy change from
with-replacement replay, not a repair of the shared replay sampler. It requires
`prio_alpha=0` and a learner batch divisible by the minibatch size; the current
Pokémon configuration satisfies both. No additional shared trainer/kernel or
precision changes were made for this fix.

Reproduction and learner-only regression (one/two buffers):

```sh
node ocean/pokemon/tests/make_replay_probe.cjs
/usr/local/cuda/bin/nvcc -O3 -arch=native -std=c++17 -I/usr/local/cuda/include/cccl build/pokemon/replay_probe.cu -o build/pokemon/replay_probe
./build/pokemon/replay_probe
```

The numeric probe now prints the individual PPO terms for a bad gradient.
`--force-replay-endpoint` forces a draw of 1 in its isolated replay sampler;
use a separate output binary when building that diagnostic variant. To replay
the failing full-pipeline configuration after the workaround, explicitly set
`train.epoch_sampling=0` in that test only.

The remainder records the earlier investigation chronologically.

## Recurrence after the sampler correction

Run `1788934079962` again has finite main/EMAG weights at 314,572,800 steps
and all 200,832 entries non-finite in each file at 367,001,600 steps. Its
Articuno/Zapdos/Moltres/Dratini/Dragonair/Dragonite team and Dragonite lead
match the corrected last-legal fallback. This is numerical collapse, not
evidence of a learned team preference. The sampler fix did **not** solve it.

A further 52,428,800-step BF16 resume from this run's finite 314M checkpoint
completed as `second_collapse_debug`, with environment audit and existing PPO
debug enabled. Both saved main/EMAG arrays are finite. CLI-only test overrides
disabled periodic league evaluation and team JSON logging, capped steps, and
set `train.min_lr_ratio=0.95281408` to keep LR near its starting value during
the short run. This was not an exact replay: optimizer state and opponent
history restart, and the cosine learning-rate schedule differs.

### Stage-specific diagnostic build

`tests/make_numeric_probe.cjs` generates an isolated copy of the trainer with
finite checks around each minibatch. Production sources and `./puffer` are not
modified. Build with:

```sh
node ocean/pokemon/tests/make_numeric_probe.cjs
NATIVE_OUTPUT_NAME=build/pokemon/puffer-numeric-audit bash build/pokemon/build_numeric_probe.sh pokemon
```

`POKEMON_NUMERIC_FAILURE stage=N index=I value=...` identifies the first failing
checked boundary: 1 magnet forward, 2 learner forward, 3 logit gradients,
4 value gradients, 5 backpropagated weight gradients, 6 updated weights,
7 optimizer momentum. The diagnostic synchronizes each minibatch and exits
86 on a device error before saving another checkpoint. It is intentionally
slower than production, and cannot identify an operation within a boundary.

Validation: deliberately loading the known-corrupt 367M main/EMAG checkpoint
printed `stage=1 ... value=nan`, exited 86, and saved no model checkpoint.
The same binary completed 1,048,576 audited steps from the finite 314M pair
with finite displayed losses and exit 0. This tests the detector, not the cause.

To capture a clean full run (do not load the NaN `latest` checkpoint):

```sh
PUFFER_POKEMON_AUDIT=1 ./build/pokemon/puffer-numeric-audit train pokemon \
  base.load_model_path=None
```

At this stage of the investigation the cause was unresolved; the stage-3
reproduction and configuration workaround above supersede that status.

Initial scope: Pokémon adapter only. Following reproduction and explicit user
approval, the shared discrete sampler's rounding-tail fallback was corrected.
No precision, optimizer or reward changes were made. Later, the Pokémon
epoch-sampling configuration workaround above addressed the replay failure.

## Confirmed

In run `1788930392106`, the 314,572,800-step checkpoint has finite weights and
completed a two-game CPU evaluation. At 367,001,600 steps, all 200,832 main
weights and all EMAG weights are non-finite. This bounds the failure between
these saves; finite weights alone do not certify optimizer/recurrent state.

The first-six-species pattern is the adapter's first-legal-action fallback,
not evidence that the learner prefers those teams. The adapter converts invalid
actions to legal ones without changing the action stored by the caller; this
can conceal an upstream or integration failure. The probe below identifies one
such upstream failure before any optimizer update.

## Numeric sampler probe

An isolated generated translation unit (not an edit to shared sources) captured
the exact action-159 event with all 160 logits finite:

```
row=2572 rand=0.999999464 cdf=0.999999464
max=10.625 lse=10.7803164 fallback=159 legal=0 bad_logits=0
```

The probability sum rounded below 1 and equaled the draw. The strict `<`
comparison never selected an action, so the sampler returned its default last
index despite that index being masked out. This establishes the cause of the
reproduced invalid action; it is not merely a hypothesis about non-finite logits.

`tests/make_sampler_probe.cjs` generates the isolated diagnostic source/build
script under `build/pokemon/`. The original isolated candidate initialized the
rounding-tail fallback to the final **legal** action instead. This preserves a legal sampled
action and computes/stores that action's corresponding log-probability, unlike
changing the action after it reaches the environment. The approved production
fix now scans backward to the last legal action only when the CDF selects
nothing. The probe uses the current production fix (`--candidate` is an alias).

`node ocean/pokemon/tests/test_sampler_tail.cjs` compiles the actual production
selection block with host helpers and forces exact CDF equality, a rounded CDF
below 1, and draws of 1. It covers sparse/single/full masks, nonzero row/head
offsets, cached/uncached reads, deterministic selection and ordinary draws.

The isolated candidate completed 10,485,760 additional training steps from the
314M checkpoint with the adapter audit enabled, finite displayed losses, and
zero boundary violations. Its saved main and EMAG weights were checked for
non-finite entries. This regression establishes that the correction removes the
immediately reproducible invalid-action failure; a fresh optimizer on resume
and the shorter test do not certify that every possible long-run NaN is solved.

The rebuilt production `./puffer` (default BF16, no `--float`) subsequently
completed the same 10,485,760-step audited resume as
`sampler_production_resume`, with no boundary violations and finite losses.
Both saved main and EMAG files contained 200,832 finite weights and zero
non-finite entries. Outputs are isolated under
`build/pokemon/audit_checkpoints/pokemon/sampler_production_resume/` and
`build/pokemon/audit_logs/pokemon/`; the original run was not overwritten.

## Contract checks

`tests/test_contract.c` runs 1,200,000 joint steps across sampled/drafted teams,
both seat orders, noncontiguous player buffers, learner/learner and learner/frozen
layouts. It uses guarded buffers and sanitizers, poisons scalar outputs before
every step, checks exact observation/mask publication, binary nonempty masks,
finite zero-sum rewards in [-1,1], terminal flags, explicit resets and terminal
autoresets, and boundary notifications. No violation was reproduced.

Byte observations cannot contain floating NaNs. With shaping off and win reward
1, the adapter outputs only -1, 0 or +1. Current mask width is 160; observation
width is 640; the mask occupies bytes 480–639. Agent outputs follow the physical
pointers assigned by the vectorizer rather than assuming adjacent seats.

These tests do not reproduce GPU sampling, learning, or a 300M-step training
history and therefore cannot rule out an integration fault.

## Reproduced native boundary violation

Two isolated audited resumes from the finite 314M checkpoint stopped at the
same transition, in the first rollout before any optimizer update:

```
side=0 env=3077835397 tag=1 role=0 episode=0
picks=6 selecting_set=0 updates=9
battle_seed=16740812420936011371 rng=17573891410916755280
action=159 reward=0 terminal=0
legal=0,1,2,3,4,5,8,9
obs_match=1 mask_match=1
```

The published observation and byte mask matched the game's authoritative
buffers. Action 159 was not legal in either mask. Checks stopped before fallback
or simulator mutation. The replay command used the original 4096 agents,
128-step horizon and saved model/EMAG with isolated output directories, capped
at 1,048,576 training steps; neither run reached its first training update.

This demonstrates invalid action input before weight corruption. It does not
by itself prove the subsequent optimizer NaN's complete causal chain. Read-only
inspection showed the discrete sampler initialized its fallback to `A-1` (159)
when the cumulative-probability comparison selects nothing; numerical rounding
or non-finite logits were possible paths to that fallback. The numeric probe
established finite rounding for this event. The approved fix removes this
illegal fallback; it does not establish that every long-run NaN is solved.

## Opt-in environment-boundary checks

`PUFFER_POKEMON_AUDIT=1` enables environment-local checks before actions are
consumed and after outputs are published. On the first bad boundary it prints
the reason, side, environment/bank identifiers, phase, episode/update, RNG/seed,
action, reward/terminal and legal action IDs, then aborts before fallback.
There is no sanitization of bad values or change to the trainer. Default gameplay
and rewards are unchanged; audit checks are disabled unless explicitly enabled.

```sh
PUFFER_POKEMON_AUDIT=1 make -C ocean/pokemon audit
NATIVE_OUTPUT_NAME=build/pokemon/puffer-audit ./build.sh pokemon
PUFFER_POKEMON_AUDIT=1 ./build/pokemon/puffer-audit train pokemon \
  base.load_model_path=checkpoints/pokemon/1788930392106/0000000314572800.bin
```

Use the explicit finite checkpoint, not `latest`. Rebuilding the isolated binary
preserves `./puffer`. A diagnostic abort is evidence about the first boundary
violation, not automatically proof of the original numerical failure's cause.
