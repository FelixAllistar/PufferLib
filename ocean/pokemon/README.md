# Gen 1 Pokémon self-play

Two-player level-100 RBY singles using native Zig `pkmn/engine`, with an Ocean
C adapter. No Python/JavaScript/network calls occur during rollouts. Engine
revision: `9b88fd6c5467f703c38951d5b2e8a660314d410b`; Zig 0.16.0.

## Build and run

From the repository directory containing `build.sh`:

```sh
make -C ocean/pokemon validate
make -C ocean/pokemon test
make -C ocean/pokemon sanitize
./build.sh pokemon
./puffer train pokemon
./pokemon watch latest
./pokemon eval latest random --games=1024
./pokemon matrix path/to/A.bin path/to/B.bin path/to/C.bin --games=1024
```

The first build downloads the pinned engine checkout (including its MIT license)
and checksum-verified Linux x86-64 compiler into ignored `build/pokemon/`.
Other platforms must set `ZIG` to a Zig 0.16.0 executable. Current Makefile
test/viewer dependencies assume the repository's Linux raylib installation.
`--gpu` and `--web` builds are not supported. Training uses GPU neural inference
with CPU environments. `bash build.sh pokemon --fast` builds only the CPU
viewer/evaluator. `NATIVE_OUTPUT_NAME=build/pokemon/puffer` optionally preserves
another environment's `./puffer` binary when building the native trainer.

Watch/eval must cast observation bytes directly to floats (0–255), matching
native Pokémon training. Older viewer builds incorrectly divided by 255; their
checkpoint scores, team profiles, and population evaluations must be rerun.
Rebuild with `./build.sh pokemon --fast`; existing checkpoints need no conversion
or retraining for this fix. `make -C ocean/pokemon policy-test` checks the viewer's
input contract. CPU float and GPU BF16 inference are not bitwise identical.

Pokémon uses `train.epoch_sampling=1` with `prio_alpha=0`: learner trajectories
are shuffled without replacement per training epoch, with unit weights. This
bypasses a reproduced replay-CDF rounding bug that selected zero-probability
frozen rows and produced NaN importance weights, even with prioritization off.
Keep this enabled; it uses existing trainer support, not a custom Pokémon kernel.
See [the numerical audit](ENV_CONTRACT_AUDIT.md) for the reproduction.

## Experimental realized-strategy archive

For the fixed named roster (master + one specialist per team), see
[league setup and commands](LEAGUE.md). It evaluates current members only;
historical checkpoints are backups, not additional members. The experimental
archive below is a separate offline tool and does not select the league roster.

The offline profiler plays the candidate's own games against a common panel.
It does not use shared-bot-state action JSD. It measures selected species/type/
lead distributions, mean team HP before battle decisions, terminal surviving
fraction, update-cap-normalized duration, and timeout frequency. Type counts
include each distinct type once per Pokémon (dual types both contribute).
These last metrics describe outcomes/occupancy, not certified aggression or
defensive intent. No action counters are relabeled as successful events.

```sh
./build.sh pokemon --fast
./pokemon eval random random --games=32 --profile
python3 ocean/pokemon/profile_population.py \
  --candidate baseline=random --candidate candidate=checkpoints/pokemon/RUN/STEP.bin \
  --opponent random=random --opponent reference=checkpoints/pokemon/REFERENCE/STEP.bin \
  --games 256 --output evaluations.json
python3 -m pufferlib.population evaluations.json --output selection.json
make -C ocean/pokemon profile-test
```

`--profile` prints one compact summary with top-six species/leads and battle
statistics. `--profile-json` is the raw per-game format used by the archive script.
Paths in this example are placeholders. The native `PK_PROFILE` lines are
per-game JSON for candidate A, seat-balanced. Python runs only outside the
simulator, records raw episodes, aggregates equal-weight matches, fingerprints
checkpoints/configs/evaluator, and refuses changed inputs or existing outputs.
Use immutable checkpoint paths, not `latest`. Every candidate must be measured
against the same panel; supply old league members as candidates too.

The generic selector retains quality, descriptor novelty and statistically
supported matchup coverage. It does not itself train or change frozen banks;
the separate league driver handles that. Protein search is not implemented.
Normal training now loads the exported Pokémon league using optional native
bank hooks; Kaggriculture's league is unchanged. See [league usage](LEAGUE.md) and
[population contract and limitations](../../pufferlib/POPULATION.md).

## Sourced sets and team construction

**All 149 non-Uber Gen 1 species are selectable**, including unevolved Pokémon.
The catalog contains **556 distinct moveset variants**, adapted from the pinned
Smogon full-generation data at https://data.pkmn.cc/sets/gen1.json. These are
recommended sets, not measured usage frequencies or a claim of balanced teams.
See [data/NOTICE.md](data/NOTICE.md) and [data/source.json](data/source.json) for
attribution, retrieval time, hash and source limitations. Smogon set data is not
covered by the pkmn/smogon API code's MIT license.

The importer considers standard RBY tier analyses in this preference order:
OU, UU, NU, PU, ZU, LC Level 100, LC, Middle Cup, Ubers. It excludes Mew and
Mewtwo, expands slash alternatives, removes duplicate moves/sets, normalizes
to level 100 and maximum Gen 1 DVs/stat experience/PP, then validates every
full moveset in a six-species team against `pokemon-showdown@0.11.11 gen1ou`.
Stadium, tradebacks, rentals and altered-movepool formats are not imported.
Every retained record includes its original format/template and actual moves
in [data/catalog.json](data/catalog.json). Lower-tier recommendations are not
relabeled as OU recommendations.

The snapshot has no Metapod/Kakuna analyses. Their clearly marked `inherited`
entries use Caterpie/Weedle's sourced movesets, which both pass OU validation.
They are not invented or represented as original evolved-species analyses.
Some Pokémon have fewer than four legal moves in their set, e.g. Ditto.

`env.team_selection=1` (default) makes **12 private simultaneous decisions**:
choose species, choose one of that species' sourced variants, repeat six times.
The same policy then plays the battle. The first completed pick is the lead.
There is no exclusive draft: both sides may choose the same species, but each
team has six distinct species. No opponent picks are revealed during selection.
No species has more than 17 variants in this snapshot. Separate species/variant
steps keep the action head at 160 instead of one action per global catalog entry.

`env.team_selection=0` samples distinct species uniformly, then samples one of
each species' variants uniformly. It does not sample uniformly over global set
IDs, which would overweight species with more published variants. Sampled teams
are legal combinations, not Showdown Random Battle teams or guaranteed coherent
competitive teams.

Regeneration is offline and deterministic from the checked-in snapshot:

```sh
node ocean/pokemon/import_sets.cjs --check  # validate + verify generated outputs
node ocean/pokemon/import_sets.cjs          # regenerate from pinned snapshot
# Deliberate dataset update only; review changed rules/IDs before training:
node ocean/pokemon/import_sets.cjs --refresh
```

`catalog.zig`, `catalog_meta.h` and `data/catalog.json` are generated artifacts.
The importer explicitly checks banned species, Double Team, Minimize, Horn Drill,
Fissure, Guillotine, Dig and Fly, then the full validator checks learnability and
move-combination legality. The runtime bridge accepts only catalog indices and
checks Species Clause again; arbitrary unvalidated movesets cannot reach it.

## Rules and limitations

Only `env.generation=1`, `env.format=0` are supported. The engine uses Showdown
compatibility, including Sleep/Freeze/Endless Battle clauses. The double KO of
both last Pokémon is a draw, not a custom Self-KO-loss clause. Compatibility
targets *patched* Showdown; exact parity with every unmodified Showdown edge
case is not claimed. See upstream `docs/TESTING.md`.

This is RBY OU mechanics with a sourced restricted-moveset builder, not full
unrestricted OU team construction. All legal species are available but agents
cannot invent movesets outside the catalog. Level/stat configuration is fixed.
`env.max_updates=512` counts battle decisions including forced replacements;
the cap is an adjudicated terminal draw, not a tournament rule or bootstrapped
truncation. It is logged separately in `timeout_rate`. Engine errors abort loudly.

## Policy ABI 2 and checkpoints

**Old 20-set/32-action checkpoints are incompatible. Start a new training run.**
Old files are not deleted. Saved configs contain `env.abi_version=2` and a hash
of the actual normalized catalog. The viewer rejects missing/different catalog
metadata rather than interpreting old set IDs under new semantics. Checkpoint
weight sizes also change. New runs save `config.ini` beside checkpoints so the
viewer works before training finishes, including with custom output directories.

One categorical head of width **160**, **640-byte** observations, cast directly
to floats on the 0–255 scale for inference. During species selection action `a` means Pokédex
species `a+1` (0–148); during variant selection action `a` means the zero-based
variant within that species' contiguous catalog range. Unused actions are masked.
During battle the existing action meanings are unchanged:

| Action | Meaning |
| --- | --- |
| 0–3 | Current active Pokémon's moves |
| 4–9 | Switch to original own party slot 0–5 |
| 10 | Pass when requested |
| 11 | Struggle when returned by the engine |
| 12–159 | Unavailable in battle |

Invalid actions are counted and replaced by the first legal action. The bridge
itself rejects invalid battle choices without mutating state.

| Observation bytes | Contents |
| --- | --- |
| 0–11 | Draft/battle phase, completed picks, turn, active/request/last-move data |
| 12–13 | During draft: species/variant subphase and own pending species |
| 16–207 | Six own records, 32 bytes each |
| 208–399 | Six opposing records in first-revealed order; unseen entries zero |
| 400–463 | Two active-state records, own then opponent |
| 464–475 | Six own global set IDs + 1, little-endian uint16 |
| 476–479 | Reserved, zero |
| 480–639 | Current legal-action mask |

Pokémon record offsets: species at 0, HP scaled to 0–255 at 1, public status at
2, fainted flag at 3, level at 4, types at 5–6, moves at 8–11. Own records expose
PP at 12–15 and base HP/Atk/Def/Spe/Spc divided by four at 16–20. During drafting
completed own picks expose species and selected moves. Opponent moves are
recorded only from executed events; unrevealed moves, PP and stats stay hidden.
Opponent slot IDs reflect order of appearance, never hidden party order.
Active records expose species/types, boosts, public volatile flags and own
modified stats. No RNG, future choices or hidden status durations are exposed.
The catalog is public, so inference about possible opposing sets is legitimate.

## Watching and training telemetry

```sh
tail -f logs/pokemon/teams.jsonl
./pokemon watch latest
./pokemon watch latest random
./pokemon watch latest --emag
```

Exact completed teams are sampled on the first episode and every
`env.team_log_interval=100` episodes per environment thereafter; 1 logs every
battle and 0 disables file logging. `env.team_log_path` selects the append-only
file (parent must exist). Records contain both variant-name teams, both leads,
each set's moves, policy banks (0 learner, positive frozen), process/environment
IDs, seed, catalog hash, ABI and result (1=P1, 2=P2, 3=draw, 5=cap).

The terminal dashboard shows only the top six **species** by learner team
inclusion and top six **leads**, sorted independently with readable names and
percentages. All variants of a species are combined. An 80% team rate means the
species appeared on 80% of completed learner teams; an 80% lead rate means it led
80% of those teams. Rankings describe usage, not strength or win rate. Zero-use
entries are omitted; before any completions the display says it is waiting.

Species labels are generated static strings: `Starmie_OU`, `Porygon_PU-SS`,
`Meowth_LC`. The suffix before `-SS` is the actual species tier from pinned
Showdown Gen 1 data, not the chosen moveset's source format. `SS` is our local
marker for a non-OU species with a published OU strategy, not an official tier
or a guarantee of strength. Tier metadata includes OU, UU, NU, PU, ZU, ZUBL,
LC and NFE (Uber species are excluded from play). LC means Little Cup; NFE
means not fully evolved; ZUBL is the ZU banlist rather than a separately played
tier. See `data/species_labels.json` for the exact provenance. Labels are purely
display metadata and do not affect observations, rewards, catalog hashes or
checkpoint compatibility. No runtime dataset parsing or network access is added.

Full stable `env/team/<variant>` and `env/lead/<variant>` metrics remain in run
logs, including unsampled episodes. Team frequencies sum to 6, lead frequencies
to 1. Exact-team JSONL remains available for full moveset inspection. None of
this spectator information enters policy observations. Rebuild/restart to enable
the new display; this display-only change does not invalidate ABI-2 checkpoints.

The viewer runs fresh games with the saved model, not live training instances.
It displays full teams, lead/active markers, HP, active moves/PP, and prints all
selected movesets to stdout. Both players default to the same model but have
independent recurrent state. A second checkpoint or `random` changes the opponent.
Space pauses, N steps, Up/Down adjust speed, Enter skips the result pause.
`latest` resolves once at launch; restart to load a newer checkpoint. `--emag`
loads the `.bin.emag` sidecar, which exists only for EMAG-enabled runs.

## Rewards and evaluation

Default rewards are +1/-1 for win/loss, zero for draw. `env.reward_win` scales
them. Optional `reward_hp_scale` and `reward_ko_scale` default to zero and weight
a normalized HP/faint-advantage potential. Reward is terminal reward plus
`gamma * next_potential - previous_potential`, with zero initial/terminal
potential and opposite rewards for the two players. Native configuration aligns
`reward_gamma` to `train.gamma`; shaped rewards require `train.reward_clip=0`.
Score/win metrics remain unshaped. Auto-reset preserves completed rewards/dones.

The config exposes EMAG (`emag_kl_coef=0.05`, `emag_tau=0.001`, `emag_cutoff=1`),
entropy, LR/entropy annealing, PPO/value/gradient clipping, and replay settings.
These are provisional starting values, not tuned results. Historical opponents
are saved at `base.checkpoint_interval` training epochs; two frozen banks are
enabled. Post-training panel evaluation is configured in `[selfplay]`.

Headless `eval A.bin B.bin --games=1024` alternates seats, resets recurrent state
each game, and reports W/D/L, draw-adjusted score, timeouts and a conservative
95% Hoeffding interval. `matrix` evaluates every pair. Game counts must be even.
`--seed=N` sets a reproducible seed block; sampling is stochastic unless
`--deterministic` is supplied. Explicit `env.KEY=VALUE` overrides select shared
rules for an experiment. Against random is a baseline, not a strength certificate.

Neither real Pokémon rules nor legal sets establish non-transitivity. Look for
statistically supported A>B, B>C, C>A cycles in held-out matchup evaluations.
Symmetric self-play average reward alone cannot establish improvement or cycles.

## Validation and performance

Tests cover all 149 species / 556 variants, banned-move exclusion, bridge startup
for every set, duplicate-species rejection, private drafting, 16-bit IDs, hidden
state/party order, stable switching, move revelation, 200 reproducible rollouts,
adapter episode/reset behavior, reward telescoping and team logging. Zig tests
also compare the event recorder against stock engine transitions over eight
seeded rollouts. This is not an independent validation of every Pokémon mechanic.

Run `make -C ocean/pokemon benchmark` for current performance. Earlier 20-set
prototype benchmarks do not describe this expanded ABI. Benchmarks include
teams/reset/masks/observations/random policy, but exclude neural inference,
GPU transfers and learning. The graphical window requires separate visual QA.

The expanded catalog passed C AddressSanitizer/UndefinedBehaviorSanitizer tests,
Zig ReleaseSafe tests, a short native training run with shaping/EMAG, EMAG CPU
evaluation, and native opponent-panel evaluation. A 10,000-game private-draft
random-policy run completed with zero invalid actions or engine errors, at
1.55M agent steps/s on one CPU thread (9,526 games/s). This broader species pool
and machine load differ from the old catalog benchmark; it is not a controlled
speed comparison. Exact team logs were checked against catalog moves and leads.
