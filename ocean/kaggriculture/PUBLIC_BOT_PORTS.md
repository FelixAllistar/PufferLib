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
