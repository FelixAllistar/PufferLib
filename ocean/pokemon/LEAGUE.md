# One fixed named roster

**Master + one specialist per named team = the league.** The default has exactly
eleven members: one unrestricted master and ten specialists. Checkpoints are
versions, not extra members. No selector adds historical versions or removes
named members. Descriptors are diagnostics, not roster-admission criteria.

## Normal master training (usual workflow)

```sh
./puffer train pokemon selfplay.enabled=0 base.load_model_path=None
```

This starts a **fresh unrestricted master** against the exported current league.
It does not train or replace any league member. Checkpoints go to the normal
`checkpoints/pokemon/RUN/` directory; `./pokemon watch latest` watches this learner.
To resume instead, explicitly pass `base.load_model_path=latest` or an explicit
checkpoint path. Plain `./puffer train pokemon` uses your current config values;
it does not force a fresh start or enable selfplay.

`env.native_league` independently selects frozen league opponents; `None`
disables the league. `selfplay.enabled=0` does not disable league opponents.
In native league mode all opponents come from the manifest: learner/history
snapshots are neither registered nor sampled, even if selfplay is enabled.
Without a league manifest, the ordinary selfplay switch retains its usual behavior.
The current config selects `leagues/pokemon/named_roster/native_two_20260910.ini`
(Alakazam–Zapdos and Jynx–Starmie equally). Select
`leagues/pokemon/named_roster/native.ini` for the full exported roster.
Edit `config/pokemon.ini` to persist the selection; command-line overrides
affect only that run. The full roster itself is unchanged.
`league.sh export` publishes the current roster manually, and league evaluation
publishes it automatically. Training reads this file **once at startup**; edits
or specialist updates do not change a live run's opponents. No evaluation matrix
or Python subprocess is launched by normal training.

Each of the 32 native frozen bank slots binds both weights and team constraints.
Bank counts approximate exported opponent probabilities; every member receives
at least one slot. Small probabilities and category budgets are consequently
quantized (check the printed slot counts for the actual distribution). With
32 slots the resolution is about 3.125%, and banks may differ by one environment
when the rollout count is not divisible. All opponents currently need the same
network architecture. `native_bank_slots` can be edited between roster size and
32; fewer slots reduce per-bank inference overhead at coarser mixture resolution.
Opponents remain fixed throughout a run; no mid-battle bank rotation occurs.
The existing master is a frozen opponent, while the newly trained master lives
outside the roster until explicitly promoted. Saved native run configs record
the resolved layout settings, but keep the manifest path; preserve `native.ini`
separately if exact opponent-layout reproduction is needed after a later export.

To improve specialists occasionally, use `league.sh run --rounds N`. That explicit
workflow still trains the whole named roster, then exports its latest versions
for the **next** normal master-training run. Those commands disable native-league
mode internally and bind their own selected opponent. To opt out of the league
for an ordinary run, pass `env.native_league=None`.

Shared native code contains optional bank-path/bank-loaded hooks and an
environment-overridable bank limit (other environments retain eight). Pokemon's
hook binds team constraints and republishes initial observations/masks before
inference. No optimizer, sampler or loss changes are involved.

## Fresh start

Edit [league.json](league.json) before initialization. From the repository root:

```sh
./build.sh pokemon
make -C ocean/pokemon league-eval
./ocean/pokemon/league.sh init --fresh
./ocean/pokemon/league.sh run --rounds 20
```

Fresh initialization gives each member independent native random weights,
loading no previous checkpoint or magnet. To export initialization without
changing the shared trainer, it runs one small native rollout at learning rate
zero per member and binds the resulting weights to that member's team.
Optimizer state is not carried into subsequent training.

The new default directory is `leagues/pokemon/named_roster`. The previous
`leagues/pokemon/main` run is untouched. Version-1 checkpoint-population states
are rejected rather than silently interpreted as fixed rosters. Initialization
refuses to overwrite existing state. For another fresh run, select a new
`directory` in a copied config and use `league.sh --config PATH init --fresh`.
`init --checkpoint PATH.bin` remains an explicit warm-start option.

## Teams and settings

### Expanded roster

| Specialist | Constraint |
|---|---|
| starmie_rhydon | Existing six sets, fixed order/lead preserved |
| alakazam_zapdos | Existing six sets, fixed order/lead preserved |
| starmie_alakazam | Six documented sets; policy chooses order/lead |
| jynx_starmie | Six documented sets; policy chooses order/lead |
| jolteon_cloyster | Six documented sets; policy chooses order/lead |
| jynx_cloyster | Six documented sets; policy chooses order/lead |
| lapras_core | Tauros + Snorlax + Chansey + Lapras; two free slots/order |
| articuno_core | Tauros + Snorlax + Chansey + Articuno; two free slots/order |
| victreebel_core | Tauros + Snorlax + Chansey + Victreebel; two free slots/order |
| dragonite_core | Tauros + Snorlax + Chansey + Dragonite; two free slots/order |

The six complete teams use the guide's coordinated movesets (Teams 1, 2, 3,
5, 6, 7). Policy-selected leads on the four new complete teams are an explicit
departure from the guide's prescribed opening order. The four niche entries
are **experimental partial cores**, not copied tournament teams. Their niche
sets come from the pinned OU analyses linked in `niche_sources`. There is no
claim that a single set is universally best. All play the same RBY OU rules;
lower-tier species are not being evaluated under lower-tier rules.

`sets` in the config holds reusable, human-readable moveset definitions. A team
can be a six-entry list (fixed order, as before), or an object:

```json
{"required": ["tauros", "lax_boom", "chansey_boltbeam", "articuno"]}
```

This requires those exact four sets, allows any legal remaining species/catalog
variants, and lets the policy choose the whole order (first pick is lead).
Six required sets fix composition/moves but still allow order selection.
The action mask reserves enough remaining slots to guarantee required picks;
required-species variant masks enforce the assigned moveset. No actions are
replaced after sampling. The native compact representation is
`required:445,512,392,522`, supported in training, watch and evaluation.

`opponent_group_mass` allocates **80% OU-group / 20% niche-group** probability.
The unrestricted master belongs to the OU group. Metagame weights and uniform
exploration determine relative weights *within* groups. Budgets are reapplied
after excluding the learner itself. These are expected chunk probabilities,
not guaranteed percentages in four draws. All members train the same number
of chunks; the 80/20 split weights opponents, not training compute.

To add configured members to an existing named roster:

```sh
./ocean/pokemon/league.sh sync
./ocean/pokemon/league.sh run --rounds 20
```

Sync preserves existing teams/checkpoints, initializes only new members with
fresh native weights, and invalidates the old evaluation. It refuses removals
or redefinitions. Training defaults are 44 chunks per round. Evaluation now
uses 55 unique pairs, not 121 directed/self matchups; see the schedule below.

The original two specialists use complete teams from Shellnuts' Smogon guide,
[An Introduction to Teambuilding in RBY OU](https://www.smogon.com/forums/threads/an-introduction-to-teambuilding-in-rby-ou.3667061/):

- `starmie_rhydon`: Team 1, Starmie + Mega Drain Exeggutor + Rhydon + three Normals.
- `alakazam_zapdos`: Team 3, Alakazam + Zapdos + Big Four.

The first Pokemon is the fixed lead. Battle switching remains free.
Catalog-canonical move-slot order is used; all sets are validated against the
pinned sourced catalog. Find supported sets with `league.sh sets Snorlax`.

| Setting | Meaning |
|---|---|
| `members` | Permanent named roster; `team: null` is the unrestricted master |
| `teams` | Six species with four moves each; first is the lead |
| `chunk_steps` | Native agent steps per chunk, including frozen-agent rows |
| `chunks_per_member` | Opponent draws/training chunks per member per round |
| `games` | Full-evaluation games per unique pair (128) |
| `quick_games` | Ordinary-round games per unique pair (32) |
| `full_eval_every` | Full refresh cadence in absolute league rounds (5) |
| `baseline_members` | Members tracked against fixed references (default: master) |
| `eval_binary` | Separate league evaluator; leaves a running old viewer alone |
| `uniform_mix` | Exploration added to the empirical metagame weights |
| `train_overrides`, `vec_overrides` | PPO/EMAG settings and rollout sizing |
| `descriptor_weights` | Diagnostic metadata, never roster admission |

Team definitions cannot silently change for existing checkpoints. Edit before
fresh initialization; old runs retain their original teams.

## Each round

### Evaluation schedule

`run` uses a current compatible evaluation at startup, or performs a full one
if needed. It then does quick evaluations after ordinary rounds, full ones at
multiples of `full_eval_every`, and a full one at the requested run's end. Each
evaluation refreshes training weights. A final fifth round gets one full check,
not both a quick and full check. Calling `run --rounds 1` repeatedly requests a
full check each time because each call is an end boundary.

For 11 members:

- Quick: 55 unique pairs × 32 games = **1,760 games**.
- Full: 55 unique pairs × 128 games = **7,040 games**.
- Old routine: 121 matchups × 128 = 15,488 games.

One seat-balanced batch supplies both scores and both real behavioral profiles.
Reverse score is exactly `1 - score`; it is not a second independent experiment.
The diagonal is analytically zero payoff, with no self-games or fabricated
sample counts. Profiles now average each member's games against the *other*
members. `profiles.json` is version 2 and is not a common-panel descriptor
archive bundle; self exclusion gives members slightly different panels.

At the start and end of `run`, a separate check measures `baseline_members`
against an immutable copy of each member's **initial checkpoint metadata**.
Default: master vs 11 initial references, 128 games each = 1,408 extra games per
boundary. These snapshots are evaluation references only, never added to the
training roster. The panel is saved once in `baseline_panel.json`; later roster
expansions do not silently change it. Reports in `baselines/` include panel,
rules, seed and evaluator hashes; compare only reports with matching panel IDs.
If upgrading an existing run, the first recorded baseline measures the current
master against its initial references—it is not a reconstructed round-zero score.
Initial random opponents may eventually be too weak; saturated results cannot
certify general strength. Configure more tracked members if desired, at linear
additional evaluation cost.

The new binary is `build/pokemon/pokemon-league-eval`, built by
`make -C ocean/pokemon league-eval` (or
`OUTPUT_NAME=build/pokemon/pokemon-league-eval ./build.sh pokemon --fast`).
An already-running Python league process retains its old code/config and
continues the old schedule. These changes apply on its **next launch**, not
mid-run; the previous `./pokemon` binary is left alone.

1. Evaluate only the current checkpoint of every named member under the same
   rules, alternating seats. Output uses member names rather than checkpoint IDs.
2. Compute empirical metagame weights for this fixed roster. No members removed.
3. Freeze those current checkpoints for the entire training round.
4. Train each member against the **other** members. Exclude its own frozen copy
   and renormalize the weights. If those weights are all zero, use uniform sampling.
5. Save updates and advance each member's current checkpoint pointer.
6. Reevaluate the same named roster. Previous versions remain backups only.

The printed mixture is global; each learner's actual opponent distribution
excludes itself. Opponents change between chunks, not mid-game. Resumes restore
weights, not optimizer state; schedules and optimizer state restart per chunk.
The solver is approximate and exploitability concerns only the empirical matrix,
not the full game. More chunks improve sampling coverage of the target mixture.

## Commands and artifacts

```sh
./ocean/pokemon/league.sh status
./ocean/pokemon/league.sh eval
./ocean/pokemon/league.sh eval --quick
./ocean/pokemon/league.sh baseline
./ocean/pokemon/league.sh train                 # dry-run commands only
./ocean/pokemon/league.sh train --execute       # one training round
./ocean/pokemon/league.sh watch starmie_rhydon --opponent master
```

Watch/status can run during training, using the last committed versions.
`state.json` maps members to current checkpoints. Evaluation `mixture.json` and
`profiles.json` record exact versions for reproducibility but display fixed names.
Failed training leaves `pending.json`; inspect it, then `league.sh recover`
archives the journal and restarts from the last committed round. No old weights
are deleted. Storage history is not opponent-pool membership.

Team JSON logging remains disabled by default, with an 8 MiB cap if enabled.
Dashboard statistics do not require it; old oversized logs are left untouched.

Tests: `make -C ocean/pokemon league-test policy-test profile-test`.
Optional native fresh-init and tiny full-round GPU test:

```sh
POKEMON_LEAGUE_SMOKE_CHECKPOINT=1 python3 ocean/pokemon/tests/test_league.py
```

The legacy variable name now only enables the GPU test; no seed checkpoint is used.
