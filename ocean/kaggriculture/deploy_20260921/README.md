# Active remote 2/2 setup

Launch from `/workspace/PufferLib`. The new binary/config are installed there;
the sibling `PufferLib-multi-intent` folder is retained staging, not required
as a launch directory. Work is visible in `ssh_tmux:3` (`kag-bc`), with data
preparation in `ssh_tmux:4` (`kag-data`).

**Pilot warning:** the first 100-epoch actor-only and joint-value clones both
scored zero cash/production in 128 reset-free games each. They are not selected
as default PPO weights, but are retained for BC -> eMAG/PPO tests: standalone
scores do not establish whether they improve PPO over a fresh start. The user
reports that this downstream fine-tuning was necessary for previous BC wins.
Full-size PPO works, but runs at only about 9.3k
steps/s, substantially below the prior sweep's aggregate throughput. The GPU
is idle after these bounded checks; do not interpret installation success as
approval for a long training run. Results are in
`qualification/production_20260921/pilot.5SAQq3/summary.json`.

The old runtime, config and sources are archived at
`/workspace/PufferLib-backups/pre-multi-intent.vh45lF/old-runtime.tar.gz`.
Old checkpoints, logs and reset-state bank remain at their original paths.
Do not extract that archive over a running job. It contains the matching old
binary/config/source for a deliberate rollback; changing only executor=1
does not make a policy-v3 checkpoint compatible with the new policy-v5 binary.

Active setup: macro 2 / executor 2, entity observation v3, policy ABI 5,
H512/L3, 1,978 logits. Rewards, discount, optimizer, 2,048-agent geometry and
reset-state probability 0.8 are retained. Land-buy delay is now 0 as requested.
No old checkpoint is silently loaded, no reward reversion was performed, and
no new sweep was launched.

## Commands

Check the active configuration without allocating a GPU:

```bash
cd /workspace/PufferLib
./puffer check kaggriculture
```

Train replay BC into a **new** output file. This uses the configured 64-game
dataset, fixed 57/7 episode split, H512/L3 and baseline reward/discount profile:

```bash
mkdir -p saved
ocean/kaggriculture/build/kag_bc bc.output=saved/my_new_2_2_bc.bin
```

Add `bc.value_coef=0.1` to test joint expert-return critic training; default 0
is actor-only. Learning-rate/epoch overrides are `bc.learning_rate=...` and
`bc.epochs=...`. `bc.validation_games=0` means **use the split in the dataset**,
not disable validation. An existing output checkpoint is never overwritten.
This is replay BC, not DAgger: no callable expert is being queried.

Warm-start a new PPO run from a compatible BC checkpoint:

```bash
./puffer train kaggriculture base.load_model_path=saved/my_new_2_2_bc.bin
```

That loads weights, not optimizer state. Training resets remain enabled; use
separate reset-free, fixed-opponent full-game evaluation to measure strength.
The existing pilot checkpoints are
`qualification/production_20260921/pilot.5SAQq3/actor_100.bin` and
`qualification/production_20260921/pilot.5SAQq3/joint_100.bin`.
Neither has had downstream PPO/eMAG strength testing yet. The active config
still has `train.emag_kl_coef=0`, so the command above is BC -> PPO, not
BC -> eMAG + PPO. Preserve the user's known-good eMAG settings for a separate
bounded check rather than inventing new coefficients. When enabled without
an explicit magnet or a saved `.emag` companion, the reference initializes
from the loaded BC weights; `train.emag_tau=0` freezes that reference, whereas
a positive tau enables EMA updates. Check memory before a long eMAG run:
the 2,048-agent PPO check already used about 15.4 GiB of the 16 GiB GPU, and
eMAG allocates an additional reference network and training buffers.

The bounded pilot runner does both small/full geometry checks and paired
actor/joint BC experiments, with all results in a new
`qualification/production_20260921/pilot.*` directory.

## Data and limits

`data/entity_2_2_policy5_baseline_64g_v1.bc` contains 64 parity-verified Majkel
games, with 40,829 training and 5,015 held-out actor-labeled states. Parsing
reused the original 16 cached games and added 48; no new archive downloads.
Primitive tapes in `data/majkel64_tapes` can be reused when deriving labels or
return targets for a changed controller/reward profile. Never reuse derived
returns after changing rewards or gamma; source/semantics fingerprints guard
the trainer against that mismatch.

On these 64 games, native previews match 31,097/31,371 useful strategic
operation/crop/region signatures (99.13%), and fully label 25,740/26,737
nonempty market queues (96.27%). This is representation coverage, not learned
accuracy or identical routing. The old submission exporter is not compatible
with policy ABI 5; submission export still requires separate implementation
and parity testing.
