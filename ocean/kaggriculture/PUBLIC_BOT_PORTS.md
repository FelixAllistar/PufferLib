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
