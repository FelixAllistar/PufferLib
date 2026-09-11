# Version-aware PSRO analysis

`psro.sh analyze` and `eval_population.sh` now resolve each checkpoint's own
observation version. They use `MODEL.obs_version` when supplied, otherwise the
source run's logged `[env] observation_version`; unversioned legacy models use
0. The active training config is never used to infer a model's layout.

`eval_observation_versions.py` partitions native GPU evaluations by layout.
Same-layout cohorts use the persistent matrix evaluator; cross-layout cohorts
use resident screen banks with explicit learner/frozen observation versions.
Both seats are handled by the existing native seat-balancing path. Output IDs,
money columns, and score orientation are remapped to the input manifest, and
missing, duplicate, nonfinite, or undersampled results cause failure.

The four-new/eight-legacy shortlist needs three GPU processes, instead of
falling back to individual pair processes. Fixed-bot evaluations also receive
the checkpoint's layout. Payoff cache keys include layout and runner code.
Analysis caches are kept beside the analysis outputs, not in the active league.
The existing CPU JSD probe evaluates all models as physical player 0, where
layouts 0 and 1 are identical; it remains a player-0 behavior diagnostic.

Homogeneous version-1 leagues now support `iterate`: all candidate and active
checkpoint layouts are checked, copied policies retain observation sidecars,
and the next learner/frozen-bank config is set to the shared version. EMAG
reference sidecars are preserved when available. Mixed-layout training leagues
remain rejected; their version-aware `analyze` path does not change the league
or training INI. Native macro submission packaging separately carries the
observation version in `policy_metadata.json` and supports both layouts.

Validation: CPU tests cover every requested pairing exactly once for homogeneous
and interleaved layouts, cached-tail filtering, bank limits, score/cash reversal,
and version metadata precedence. A small remote GPU smoke run checks actual
mixed-layout execution before starting the full checkpoint analysis.
