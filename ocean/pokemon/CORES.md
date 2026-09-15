# Exhaustive three-species cores

> For current ABI-3 commands, defaults and cursor continuation, see
> [FREEPICK.md](FREEPICK.md). The experiment description below is historical:
> cores and native experts are now disabled by default, and the old three-expert
> checkpoint export must be replaced by newly trained free-pick league members.

Run from the repository root:

```sh
./puffer train pokemon
```

No launcher, Python command, snapshot collector, or pretrained learner is needed.
The normal build is `bash build.sh pokemon`. Defaults are in `config/pokemon.ini`.

## Current default: independently mix drafts and opponents

```ini
[base]
load_model_path = None

[env]
force_core_combos = 1
force_core_prob = 0.5
core_pool = all
core_seed = 76102
reset_state_prob = 0
native_league = ocean/pokemon/experts_three.ini
expert_fraction = 0.5
```

`force_core_prob` is the probability that a new game uses forced cores. The
other games have completely unrestricted six-member drafts. Both current-policy
players use the same draft mode in a self-play game; each gets its own core when
forced. A frozen expert always keeps its own team.

`expert_fraction` is the fraction of concurrent environments assigned to the
expert roster. Remaining environments run the current learner against itself,
with **both sides learning**. It is not a historical-opponent mixture.
Within each opponent group, the seeded per-episode core coin uses the same
`force_core_prob`. Thus the two knobs enable all four combinations:

| Draft mode | Current-policy self-play | Learner versus expert |
|---|---|---|
| Normal | Both policies choose all six | Learner chooses all six; expert keeps its team |
| Forced core | Both get independently dealt cores | Only learner gets a core; expert keeps its team |

These are **not guaranteed 25% shares of completed games or learner samples**.
Longer games occupy environments longer. Self-play supplies two learned seats,
whereas expert matches supply one; a 50% expert-environment split supplies about
one third of learner transitions from expert matches. Episode/step telemetry
reports the realized four-way mix instead of inferring it from the knobs.

The game is still six versus six. In forced-core games, every learned team must
contain its assigned three-species core. The policy chooses all six movesets, draft/lead order, the
other three distinct species, and every battle action. It can pick the core
members at any point; masks reserve enough remaining slots to finish the core.
No teams, HP, status, or battle histories are copied from old policies.
In normal games it chooses everything, and the three core-conditioning bytes
are zero. Ordinary games do **not** consume deck cards. This retains direct
training of unconstrained composition selection, not just play around assigned
cores. The experiment tests whether mixing improves the observed composition
trend; that benefit has not yet been measured.

`core_pool = all` means all 149 supported species: **540,274 unordered triples**.
Alternatively specify distinct national species IDs, e.g.
`core_pool = 65,94,103,113,128,131,143,145`. This restricts the core pool only;
the three free members may still be chosen from the complete catalog.

## Reproduce the earlier experiments or choose another mixture

| Experiment | `force_core_prob` | `expert_fraction` |
|---|---:|---:|
| All cores, current self-play | 1 | 0 |
| All cores, experts only | 1 | 1 |
| All normal, mixed opponents | 0 | 0.5 |
| Mixed drafts, mixed opponents (default) | 0.5 | 0.5 |

Keep `force_core_combos = 1` for the probability knob to apply. Setting the
master switch to 0 disables forcing entirely. Expert fractions above zero need
a roster; `native_league = None` always disables experts.

## Select expert roster

Change just this `[env]` line for two experts:

```ini
native_league = leagues/pokemon/named_roster/native_two_20260910.ini
```

Or use three:

```ini
native_league = ocean/pokemon/experts_three.ini
```

Then run the same `./puffer train pokemon` command. Keep
`[base] load_model_path = None` to start a fresh learner, and keep the same seeds
and other training settings for comparison. `expert_fraction = 1` reproduces
the earlier all-expert setup; `0` disables loading the roster entirely.
The roster loader supplies the
correct resident bank count and checkpoint/team bindings automatically;
`selfplay.enabled = 0` is intentional (no rolling historical population), and
is enforced for the expert/current-policy mixture. Do not edit
`vec.frozen_bank_pct` to set this mixture: the loader sets it from
`env.expert_fraction`. A positive fraction must allocate at least one environment
per expert per buffer; tiny fractions that round below that are rejected.
The teams are:

- Two-expert bank 0: Alakazam / Tauros / Snorlax / Chansey / Exeggutor / Zapdos.
- Two-expert bank 1: Jynx / Starmie / Tauros / Snorlax / Chansey / Exeggutor.
- Optional third: Jynx / Cloyster / Alakazam / Tauros / Snorlax / Chansey.

Only learned seats in selected forced-core games receive exhaustive cores; frozen
specialists retain their own bound teams and see no core-conditioning bytes.

To turn the expert roster off again, set `native_league = None`.
To turn core forcing off, set `force_core_combos = 0`.
Core forcing and snapshot resets cannot be enabled together.

## Coverage and determinism

- Enumerate every triple once into an approximately **2.06 MiB** deck, then use
  seeded, unbiased Fisher–Yates shuffling. Deal without replacement; after the
  final card, regenerate and shuffle with the next deterministic cycle seed.
- A single process-wide deck covers learner assignments across **all** envs,
  not a separate 540,274-card task repeated by each environment.
- Workers step in parallel; a serial post-step hook assigns pending resets in
  environment-ID then player-seat order before observations go to the GPU.
  The next cycle starts after all cards are **assigned**, not after every
  outstanding game finishes. No card is skipped by initial expert-bank binding.
- Requires `base.async = 0`, `vec.num_buffers = 1`, and
  `base.reset_every_horizon = 0`; incompatible settings fail explicitly.
- Same config/seeds and action sequence reproduce per-episode mode choices,
  deck assignments and engine
  trajectories independently of worker scheduling. CPU tests compare one and
  four workers. This does not promise bitwise-deterministic CUDA training across
  hardware/library versions. Different learned actions may change episode
  lengths and therefore which environment receives later cards.
- Normal checkpoint loading saves/restores weights, not live environment/deck
  state. A new process restarts cycle 0. Use the same seed for reproducibility,
  a different `core_seed` for a new reproducible order. Exact mid-run deck resume
  is not implemented; a sufficiently long uninterrupted run is needed for a pass.
- The assigned core is encoded in own observation bytes 477–479 during drafting
  only. Opponent assignments remain private. Ordinary games and battle-phase
  observations keep these bytes zero. Observation width remains 640.

## What to inspect

Startup prints `PK_CORE_DECK ... combinations=540274`.
The dashboard and native metrics show:

- `core_cycle` (zero-based) and `core_cycle_assigned` (out of 540,274);
- `core_total_assigned` and `core_drafts_completed` (not interchangeable);
- Completed-game percentages for `normal_self`, `normal_expert`, `core_self`,
  and `core_expert`;
- **Normal drafts**: species the policy chooses when all six slots are free;
- **Learned extras**: species chosen for the other three slots in forced-core
  games. Assigned species are excluded, and the two rankings use separate
  per-team denominators. With 100% core games the first ranking shows free extras;
  with 0% core games it shows normal drafts. Leads remain in full metrics.

Full logs retain `team/*` for all members, `forced_team/*` for assigned members,
`free_team/*` for learned additions, and `normal_team/*` for unrestricted teams,
including moveset variants. Each group
is a per-team inclusion fraction, not normalized to sum to one. A complete deck
forces each species into exactly C(148,2) = 10,878 assigned cores.
Passing through a core once is coverage, not proof of mastering that composition,
every moveset, every full six-member team, or every opponent matchup.

Full metrics also include `{normal,core}_{self,expert}_{games,steps}` and the
corresponding `_game_fraction` / `_step_fraction`. Steps here are joint game
decisions, not learner-only samples. Optional sampled team JSONL
(`team_log_interval > 0`) labels `draft_mode`, `opponent_kind`, and `env_tag` so
complete compositions can be separated by treatment. Normal draft metrics are
unrestricted choices; core extras are conditional choices. Ordinary match
evaluation disables forcing.

Verification without launching training:

```sh
make -C ocean/pokemon core-test
```

Tests cover exhaustive two-cycle uniqueness/completeness, seed reproducibility,
invalid pools, variant freedom, reserved slots, private observations, terminal
reset semantics, multithreaded trajectory parity, and expert-team compatibility.
Mixed tests cover probabilities 0, 0.5, and 1; no deck consumption by ordinary
games; unchanged expert teams in both draft modes; all four episode/step counters;
separate normal/forced/free species statistics; and exact one/four-worker replay.
