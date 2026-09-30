# Abyss tiers: one environment

`abyss` is the single environment, with one `abyss.h`, one training config and
one current observation contract for T0 and T1. The former separate `abyss_t1`
wrapper/config/binaries were removed. Tier controls encounter generation; combat
mechanics are shared.

```bash
./abyss_puffer train                         # configured tier (currently T0)
./abyss_puffer train --env.filament_tier=1     # T1
./abyss_puffer sweep --env.filament_tier=1     # same sweep, T1 encounters
```

Alternatively change `filament_tier` in `config/abyss.ini`. Both tiers use the
same Punisher/Xray fit and save under `checkpoints/abyss`. T0 samples the 28
recorded episode templates; T1 samples the 19 Calm encounter families from QSNA.
Only short training smoke checks were run; no long training job or sweep was started.

## Shared mechanics and bot

Both tiers have NPC repairs, ranged/cycling neuts with battery resistance,
tracking/optimal/falloff disruption, webs, painters, damps, scrams, Triglavian
buildup, damaged initial HP, Vila auxiliaries and special missile/Vorton
application. Afterburners remain usable under scrams; MWDs do not. The catalog
contains 107 types, including all 53 sampled T1 types.

Both tiers now use **1234 floats**, six unchanged action heads and a 394-entry
mask. The ten added inputs are estimates of optimal/falloff/tracking, speed,
signature, lock range, scan resolution, scramble presence and neut pressure,
plus an incomplete-estimate flag. Sim and bot use matching identity/distance
estimators; actual simulated EWAR strength/timing is hidden and randomized.
Unknown NPCs get an explicit conservative estimate. The original 18 NPC indices
are preserved. Unlocked NPC health uses initial-state estimates, matching the
bot; target locks expose current health. Unavailable cloud occupancy is zeroed.

The bot's `latest` lookup uses `checkpoints/abyss` for both tiers. The default T0
profile and optional T1 profile both select the same observation version. You
can also supply `config/abyss.ini` as the bot's `--ship-profile`; it reads the
configured fit and filament tier. From the Sanderling repository root, the
bundled T1 fit preset is:

```text
--ship-profile recorder/src/Sanderling.Controller/Profiles/punisher_t1_electrical_xray.ini --ppo-model checkpoints/abyss/RUN/MODEL.bin
```

That is a fit/tier preset, not another simulator. Exact-item weapon dogma reads
are logged for calibration; the bot does not yet treat them as verified live
EWAR measurements. Unbound or other-module attribute maps are rejected.

## Reusing existing T0 weights

Old weights have 1224 inputs. Convert a copy before loading them into the current
trainer; this preserves all learned weights and adds zeros for new inputs:

```bash
python3 ocean/abyss/tools/upgrade_checkpoint.py checkpoints/abyss/RUN/MODEL.bin
./abyss_puffer train --env.filament_tier=1 \
  --base.load_model_path=checkpoints/abyss/RUN/MODEL.abi2.bin
```

The original file is never overwritten. Select the original model's hidden size
and layer count if they differ from `config/abyss.ini`. This preserves the old
network's function initially, not its old simulated clearance rate: corrected
NPC mechanics still change the problem. Legacy bot profile version 1 remains
available for old checkpoints; tier does not select the ABI version.

## Evaluation

```bash
python3 ocean/abyss/tools/evaluate_tiers.py checkpoints/abyss/RUN/MODEL.bin \
  --hidden-size 128 --num-layers 2 --episodes 100
```

This runs 19 T1 families at both Electrical penalties and saves per-episode
telemetry plus `summary.json` under `logs/abyss/tier_coverage`. Add `--cpu` to use
`abyss_eval` without GPU contention. Each case repeats its selected family across
three rooms: a stress test, not a claim about real three-room sequences.
Reports include completion, mean/worst successful clear time and worst observed
capacitor/armor/hull. Normal mixed-family evaluation uses
`./abyss_puffer eval --headless --env.filament_tier=1 ...`.

## Calibration limits

- QSNA supplies marginal counts, not joint compositions or T1 layouts. Synthetic
  sampling is constrained to observed total-count bounds. Placements reuse T0
  anchors with an 8 km spread; movement retains T0 calibration where available.
- NPC EWAR activation/strength and sensor uncertainty are randomized assumptions.
  Both sides see estimates, not perfect hidden effect state.
- Repairs use continuous HP/s and heuristic remote target selection.
- Vila active count and total stock come from dogma. Launch/replacement delay is
  assumed 5 seconds; Swarmer orbit speed is assumed 1000 m/s. Suppressors damage
  the auxiliaries, and killing their tender deactivates them. The bot ignores
  orphan swarmers only with authoritative, fully identified tender absence.
- Guidance disruption has no effect on the current turret fit. Player drone
  control/aggro, ammo swapping and multiplayer Vorton chains remain future work.
- Only tiers 0 and 1 are enabled. Higher tiers need encounter/mechanics coverage
  checks. T1 has offline validation, not a demonstrated live-fit clearance rate.

## Build and data

```bash
source ../venv/bin/activate
CUDA_HOME=/usr/local/cuda bash build.sh abyss abyss_puffer
bash build.sh abyss abyss_eval --cpu
make -C ocean/abyss verify
python3 ocean/abyss/tools/build_combat_catalog.py
```

The combat generator produces `generated_combat_catalog.h`,
`generated_mechanics.h`, and `data/{combat_catalog,bot_catalog,tier_coverage}.json`.
The older `build_npc_catalog.py` normalizes the CSV; the combat generator enriches
it with pinned dogma. After regenerating, copy `data/bot_catalog.json` to the
controller's `Profiles/abyss_catalog.json` and rebuild the controller. Source
hashes are in `data/tier_coverage.json`. The generated C/C# parity fixture covers
428 combinations. `tests/test_tiers.c` covers both tiers in the same executable.

Sources: pinned CSV/QSNA snapshots; [QSNA NPC data](https://www.qsna.eu/api/eve/universe/items/npcs/1982;1997),
[QSNA spawn data](https://www.qsna.eu/api/eve/custom/abyss/stats),
[CCP stacking guidance](https://support.eveonline.com/hc/en-us/articles/203280381-Bonuses-and-Stacking-Penalties).
