# Native public-policy ports

The three selected notebooks are exposed as native headed sides and as live
bot-opponent profiles:

| Native side | Source | Runtime type | Main prior |
| --- | --- | --- | --- |
| `pulse` | `harvest-pulse-goose-dividend-v2.ipynb` | dynamic planner | goose/co-op dividend, wheat reserve, price-aware sales |
| `structured` / `economic` | `kaggriculture-structured-economic-policy.ipynb` | dynamic planner | COW/COW/COW/SHEEP herd, staged land, quadrant crop mix |
| `triad` / `adaptive` | `adaptive-farming-strategy-for-kaggriculture.ipynb` | dynamic planner | cow/sheep opening, melon/wheat feed lane, trace-inspired repairs |

The adaptive-farming notebook contains a large `TRACE_ACTIONS` list. Native
`triad` does not replay that list. It uses its opening strategy as a prior and
rebuilds jobs, routes, purchases, maintenance, and sales from the current
state every turn. This keeps the opponent responsive to a different seed,
seat, market, or opponent policy.

All three planners:

- observe only the public opponent state and their own private shed/seeds;
- use the complete native action vocabulary, including up to ten ordered
  market orders and all currently hired hands;
- reserve wheat for animal feed before selling;
- maintain water/feed/care/harvest/fertilizer duties before lower-priority
  planting and expansion;
- use fixed-size stack arrays and no allocation, string parsing, or Python in
  the per-turn path.

The live bot mixture is controlled by `env.bot_script_fraction` and
`env.bot_adaptive_fraction`. The dashboard exposes:
`adaptive_fraction`, `adaptive_pulse_fraction`,
`adaptive_structured_fraction`, and `adaptive_triad_fraction` alongside the
existing tape/rules fractions.

Quick checks:

```bash
./build.sh kaggriculture --fast
./kaggriculture bench 720 pulse pass
./kaggriculture bench 720 structured pass
./kaggriculture bench 720 triad pass
./kaggriculture watch pulse structured
```

## Tape ports from the 2026-08-12 harvest

Two more public agents were reconstructed from their shipped replay action
lists and added as native tape bots:

| Native side | Source notebook | Route |
| --- | --- | --- |
| `lugovoy` / `8c4s` | `v16-rc5-high-score-8c-4s-premium-market-lead.ipynb` and `3094-score-kaggriculture.ipynb` (identical tapes) | 8 COW / 4 SHEEP route reconstructed from Nikita Lugovoy submission 55440039 episodes 92165990/92185587/92223213; three-quadrant expansion, wheat/strawberry/melon crop mix, fertilizer farmed and sold |
| `thunder25` / `v25` | `kaggriculture-adaptive-replay-agent.ipynb` | THUNDER-derived production route; sheep-first day-0 basket, fertilizer race, herd expansion, heavy daily hires |

Both are byte-for-byte replays of the public action lists converted into the
packed KGT1 format (`KG_TAPE_LUGOVOY_B64`, `KG_TAPE_THUNDER25_B64`), with the
same generic survival repair as the other tape bots (dig weeds, water
unwatered plants, feed/care animals, harvest and collect fertilizer). The
notebooks' runtime-only layers (V16's one-turn premium market lead, v25's
sparse market ordering) are not ported yet; that is the remaining fidelity
gap.

They join the random scripted-bot lane automatically (`bot_script_fraction`),
and the dashboard adds `script_lugovoy_fraction` and `script_thunder_fraction`.

Also harvested but not yet ported:

- `kaggriculture-reproducible-multi-worker-policy.ipynb` is a readable
  observation-only Capacity-First rules planner (service-first task
  allocator, 9 COW / 4 SHEEP plan, price-floor batch selling at hour 1) -
  a candidate for a new adaptive profile.
- `reverse-engineering-rank-5-thunder-thunder.ipynb` is a full strategy spec
  (day-0 basket, fertilizer race, land days 6/10, day-20 wheat spike, price-
  sized herd expansion) - candidate for a price-adaptive rules bot.
- `kaggriculture-movements-in-the-top-xx` dataset (440 MB) holds per-step
  movement tables for top-1% vs median players; useful mainly for validating
  movement/opening priors, not for action tapes.

Decoded tapes and the converter live outside the repo in
`~/puffertank/kaggle_harvest/` (`*_decoded.json`, `convert_tapes.py`).

## THUNDER price-adaptive planner

`reverse-engineering-rank-5-thunder-thunder.ipynb` was promoted from a strategy
spec into a native adaptive profile `thunder` / `thunderbot`
(`KAG_ADAPTIVE_THUNDER`). It is a planner, not a replay: every turn it rebuilds
the job list, routes workers, and emits a market queue from the current native
state, so it does not drift when the opponent or market seed differs.

The port keeps the documented northstar:

- day-0 basket is sheep-first and leaves no cash reserve, but buys the wheat
  feed buffer **first** (`BUY_PRODUCT WHEAT 14`, then 4 hires, COW 1, SHEEP 4,
  MELON 5, WHEAT 5, one more hire). Reordering the wheat to slot 0 mirrors the
  shipped 8c/4s tape and is what keeps the opening herd from starving before
  crops/fertilizer income arrives;
- land on exactly days 6 and 10 (three quadrants, never the fourth);
- cow/sheep only (no goose, no carrot/tomato);
- fertilizer is sold first in the market queue (the shared pool is
  first-come-first-served) and wheat is held back until the day-23+ dump;
- herd size is priced: wool/milk health in the day-10 window expands the herd,
  depressed prices hold it lean;
- 14 hands/day from day 10;
- wheat + strawberry + melon crop mix with a day-20 wheat replant.

### Shared structure-layout fix

The public planners originally placed the opening herd at the far NW corner
`(0,0)..(1,1)`. The shipped replays build the first pasture at `(3,4)`, one
tile from the shed's NW access tile `(4,4)`. That corner placement starved the
opening animals because the only unlocked worker had to walk eight tiles to
feed them. `kag_public_structure_position` now places slots 0-3 at
`(3,4),(4,3),(3,3),(2,4)`; this is the single largest win for `structured`
(~27k to ~48k vs pass) and is required for `thunder` to hold a herd at all.

Quick checks:

```bash
./kaggriculture bench 720 thunder pass
./kaggriculture bench 720 thunder lugovoy
./kaggriculture watch thunder structured
```

Measured (50 games, seat P0):

| opponent | thunder money | opponent money |
| --- | --- | --- |
| pass | 48642 | 3000 |
| lugovoy | 29700 | 17744 |
| thunder25 | 30774 | 19148 |
| top | 48730 | 0 |
| structured | 38160 | 40573 |
| triad | 29494 | 35063 |
| rules | 25868 | 31260 |

It beats the two fixed tapes and `top`, and is close to `structured` but still
loses to `triad`/`rules`. The remaining gap is economic, not mechanical.

## Public-kernel harvest (2026-08-18)

The following public Kaggle kernels were pulled and replayed against
`kaggle-environments==1.32.7`.  Their raw 720-frame traces are archived under
`~/puffertank/kaggle_harvest/public_20260818/`, and the native packed tapes are
included by `kaggriculture_public_extra_tapes.h`.

| Native profile | Source family | What it contributes | Current status |
| --- | --- | --- | --- |
| `k320_10c4s` | Rank-your-agent / V20 multi-route | K320 10C4S route | Useful BC diversity; native parity still poor |
| `k320_yarn` | Rank-your-agent route ladder | 6C12S first/second-yarn route | Useful BC diversity; native parity still poor |
| `e279` | X544 / 3000-score family | Price-aware adaptive route trace | Useful BC diversity; native parity still poor |
| `v16` | V16 RC5 premium-market lead | High-care 8C/4S livestock specialist | Native smoke-tested and useful |
| `c166` | Breaking-the-tie 2883 | High-care livestock specialist with a distinct late trace | Native smoke-tested and useful |

The first rank-your-agent kernel embeds five route tapes, not five independent
trained neural policies.  Its full private ladder also depends on a reference
dataset that is not shipped in the pulled notebook.  We therefore forced and
harvested the five route variants separately, preserving real behavioral
diversity instead of pretending they were independent checkpoints.

The external Python traces are strong: K320/E279 finish with substantial herd,
care, feed, water, and harvest activity.  In the current native simulator those
same tapes finish near-zero money/GDP, which is a parity/layout problem rather
than evidence that the public policies are bad.  Keep them as BC teachers and
diversity sources until parity is repaired.  V16/C166 transfer cleanly enough
to serve as experimental native opponents: in a 10k native benchmark they
produce roughly 28k GDP and 5.7k milk while maintaining high care/feed rates.

### Clone artifacts

`saved/kaggriculture_public_20260818/` contains 96-step and full-episode
clones for all five profiles, plus DAgger round 1 for all five and a second
round for `v16` and `c166`.  The first DAgger round is the primary robust
candidate; the second round is retained as an exploratory off-distribution
candidate because its held-out validation did not improve.

Examples:

```bash
./ocean/kaggriculture/clone_bots.sh v16 c166
./ocean/kaggriculture/clone_bots_full.sh v16 c166
./ocean/kaggriculture/dagger_bot.sh v16 1 \
  saved/kaggriculture_public_20260818/v16_full_clone.bin
```

Compatible BC binaries can be combined without decoding them:

```bash
python3 ocean/kaggriculture/merge_bc_datasets.py \
  saved/kaggriculture_public_20260818/public_livestock_full_data.bin \
  saved/kaggriculture_public_20260818/v16_full_data.bin \
  saved/kaggriculture_public_20260818/c166_full_data.bin
```

The checked-in merger preserves whole-game boundaries and validates the KAGB
header/dimensions before writing.  The prebuilt livestock and route mixes are
in the same saved directory.  Treat the route mix as a data source first: its
public routes share a very strong opening and a naive mixed clone can learn a
high-confidence no-op continuation even while individual route traces remain
useful.

The five new profiles are also available to `clone_bots.sh` and
`dagger_bot.sh`; use K320/E279 for supervised diversity first, and V16/C166
when a native opponent is required.
