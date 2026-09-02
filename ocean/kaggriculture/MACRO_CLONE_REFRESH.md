# Structured replay-clone refresh

`macro_mode=2` reuses the existing Kaggriculture policy ABI (1,280 observation
bytes, 47 heads, and a 1,058-bit packed mask), but the first three 44-way heads
mean:

1. macro intent;
2. quantity bin `1,2,4,8,12,20,32,64`; and
3. target `AUTO,NW,NE,SW,SE`.

An old elite clone trained with `import_elite_replays.py`'s default is a
primitive clone. Its first head must not be loaded as a mode-2 clone: command
IDs such as `PLANT` and `PICKUP` have different meanings from macro IDs.

## Incremental data

The public Kaggle index is current even on Vast images without a Kaggle CLI or
credentials. The restartable refresher preserves raw archives and skips any
non-empty archive already present:

```bash
python ocean/kaggriculture/refresh_daily_replays.py \
  --root /workspace/elite_replays --since 2026-08-22 --download \
  --report /workspace/elite_replays/refresh-2026-09-02.json
```

Each archive gets a `*.zip.metadata.json` sidecar with its exact index row,
download URL, size, and SHA-256. Use `--dry-run` to inspect the newest index
rows first. The `total_bytes` field in the index is the aggregate uncompressed
episode size, not the ZIP size.

## Exact identity audit

`scan_replay_identities.py` keeps `info.TeamNames` (display/team identity) and
`info.Agents[*].Name` (submission identity) as separate exact strings. It does
not lowercase, substring-match, or merge similar names. Prefix scanning is
cheap; full action/reward statistics are opt-in for selected identities:

```bash
python ocean/kaggriculture/scan_replay_identities.py \
  /workspace/elite_replays/raw/*/*.zip --agent-name Yuan800 --output yuan800.json
```

`--top N --full` parses full JSON only for the N most represented exact
display/agent pairs. Reports include episodes, player streams, days,
module-version counts, final-money quantiles, action/market counts, opening
turns `<=60`, and a turn-180 window (`168..192`).

## Macro labels

Use the importer with an exact simulator version and structured labels:

```bash
python ocean/kaggriculture/import_elite_replays.py \
  --macro-mode structured --exact-version 1.32.7 \
  --display-name Yuan800 --output yuan800_1.32.7_macro2.bc daily.zip
```

`macro_bc_labels.py` translates same-intent primitive evidence. Same-crop
`BUY_SEED+PLANT`, same-animal `BUY_ANIMAL+PLACE`, maintenance adjuncts, and
feed purchases are coherent; incompatible market/unit families are ambiguous.
Ambiguous rows are emitted with expert `-1` (the BC kernel's skip marker), and
the audit records each reason. HOLD is used for PASS/routes/carry-only rows
because the native executor owns those mechanics. Quantity values are floored
to the nearest native bin and marked `quantity_exact=false` when lossy. Plant
targets are inferred only from acting-worker positions and current public board
state; unknown/locked/saturated targets use AUTO. Legality is checked against
the current mode-2 public-state mask before a hard label is emitted.

The importer also fills the macro candidate-score tail in each observation and
records the previous mode-2 decision. It never reads opponent private state.
The `.audit.json` and `.players.tsv` sidecars include mode `2`, source episode
IDs, and exact agent names, so datasets with primitive and macro semantics
cannot be confused.

Opening labels should be weighted in training/evaluation (`turn <=60`) while
retaining turn-180 and late maintenance/liquidation slices. Compare models on
held-out whole episodes or later daily versions; training loss alone does not
establish a clone improvement.

`evaluate_macro_clone.py` also records the ten most common hard-label opening
signatures and prediction overlap with that expert top ten, making unusual
single-agent openings and shared top-agent openings visible in the report.

`split_macro_dataset.py` creates a clean train/holdout pair from an imported
KAGB file using complete manifest trajectories. It holds out the latest source
day by default (or an explicit `--holdout-day`), preserves section ordering and
the mode-2 header, and writes hashes/counts to its report:

```bash
python ocean/kaggriculture/split_macro_dataset.py full.bc \
  --train-output train.bc --holdout-output holdout.bc \
  --report split.json
```

No episode ID is split across the two outputs, so recurrent-state evaluation
cannot leak neighboring turns from a held-out replay.

When training from these pre-built split files, set
`KAG_MACRO_REUSE_EXISTING=1` on the factory invocation. Split outputs carry
their own split report rather than a full-import audit sidecar; this guard
requires the existing dataset/manifest and prevents an accidental raw replay
re-import from collapsing the train/holdout boundary.

## Factory

`build_macro_clone_factory.sh` builds exact-agent mode-2 datasets and names
the 128/256 (and data-supported 512) artifacts with identity, cutoff, replay
version, ABI, width, layer count, seed, and epoch count. It never deletes raw
archives. For a bounded fresh pass, set `KAG_MACRO_MAX_EPISODES` (the default
`KAG_MACRO_NEWEST_FIRST=1` makes that sample use the newest source days):

```bash
KAG_MACRO_CLONE_ROOT=/workspace/elite_replays/clone_factory_macro2 \
KAG_MACRO_TRAIN_UNTIL=2026-08-31 \
KAG_MACRO_MAX_EPISODES=160 \
KAG_MACRO_SKIP_TRAIN=1 \
./ocean/kaggriculture/build_macro_clone_factory.sh \
  "Crop Dusta" "Ryo Hasegawa" peikopon tetsuya
```

The factory TSV/JSON manifests include source paths, trajectory counts, and
SHA-256/size metadata. Set `KAG_MACRO_MAX_EPISODES=0` for a full import; a
full pass is preferred before attempting a 512-wide clone.

For a non-interrupting Vast run, the checked-in queue wrappers detect an
active `./puffer train kaggriculture` by command shape (so a restarted user
PID remains protected), wait for the GPU, and train/evaluate both widths:

```bash
tmux new-session -d -s kag_macro_train_20260903 \
  '/workspace/PufferLib/ocean/kaggriculture/run_macro_clone_train_queue.sh'
tmux new-session -d -s kag_macro_eval_128_20260903 \
  '/workspace/PufferLib/ocean/kaggriculture/run_macro_clone_eval_queue.sh 128'
tmux new-session -d -s kag_macro_eval_256_20260903 \
  '/workspace/PufferLib/ocean/kaggriculture/run_macro_clone_eval_queue.sh 256'
```

The 256 queue waits for the 128 marker so fixed-opponent GPU matchups do not
overlap. Both evaluation widths use all rows of the already episode/day-held
out, and write deterministic plus stochastic fixed `pass,rules,top` results.

For a matching historical window, `run_macro_import_batch.sh` accepts
space-separated exact patterns in `KAG_MACRO_RAW_GLOBS`; this avoids silently
including older archives merely because a cutoff date is earlier:

```bash
KAG_MACRO_RAW_GLOBS="/workspace/elite_replays/raw/kaggriculture-episodes-2026-08-16/*.zip /workspace/elite_replays/raw/kaggriculture-episodes-2026-08-17/*.zip" \
./ocean/kaggriculture/run_macro_import_batch.sh "Crop Dusta"
```
