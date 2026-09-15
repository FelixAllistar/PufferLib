# Semantic free-pick Pokémon and leagues (ABI 3)

Run these commands from the repository directory containing `build.sh`.
This is a new checkpoint format: **old preset-draft policies and state banks
cannot be loaded**. They are retained, but need fresh training/collection.
Defaults start a fresh unrestricted policy, with no expert bank or core resets.

```sh
bash build.sh pokemon
./puffer train pokemon base.load_model_path=None
```

## What the policy learns

Each player privately chooses six distinct legal species (first pick is the
lead), then 1–4 individual legal moves per Pokémon. The 30 simultaneous draft
steps are six species choices plus 24 move slots; finished movesets use masked
padding. The owner sees its entire team before choosing moves; the opponent
does not see the draft. There are 149 species, an action width of 168, and 640
observation bytes. The preset catalog is not the learned action space.

Pinned Showdown Gen1 OU validation covers all 2,931,585 candidate 1–4 move sets,
including source incompatibilities, and all incremental prefixes. OHKO,
evasion, and the format's Fly/Dig exclusions apply. Struggle is automatic.
`make -C ocean/pokemon freepick-check` verifies generated data and the retained
full audit; `node ocean/pokemon/generate_freepick.cjs --check` reruns the audit.

Shared move/Pokémon entity MLPs encode mechanics, species stats and movepools,
chosen/revealed moves, HP, PP, status, boosts, and public battle state. Entity
embeddings feed the recurrent policy. Candidate-aware scoring shares species
and move representations across draft/battle; battle scoring also includes
PP, STAB and nominal Gen1 type effectiveness. Stop, pass and Struggle have
explicit handling. CPU evaluation mirrors CUDA training. There is no HRL layer.

## Create and train a league

The first unrestricted member is named `master`. `--new-policies` counts
unrestricted members, including that master; specialists are additional.

```sh
# Four independently initialized unrestricted members.
ocean/pokemon/league.sh create leagues/pokemon/free4 --master None --new-policies 4
ocean/pokemon/league.sh train leagues/pokemon/free4 --rounds 10 --steps 1048576 --selfplay 0.5

# A fresh master plus all 13 sample compositions, with their specified leads.
ocean/pokemon/league.sh create leagues/pokemon/samples --master None --teams smogon

# Same three compositions/leads as the previous expert experiment, retrained
# in the new format. An ABI-3 checkpoint can replace None to seed every member.
ocean/pokemon/league.sh create leagues/pokemon/three --master None --teams experts-three
ocean/pokemon/league.sh train leagues/pokemon/three --rounds 10 --steps 1048576 --selfplay 0.5
ocean/pokemon/league.sh status leagues/pokemon/three
```

Creation initializes checkpoint files but runs no league training rounds. A
64-transition zero-learning-rate initialization pass is used for fresh weights.
Use `create ... --defer-init` to save the league without any GPU work; its first
`train` command will initialize fresh weights before training.
`--master PATH` seeds every new trainable member; `--anchor PATH` (repeatable)
adds frozen opponents. All members must share hidden size/layer count and rules.
The native roster supports at most 32 members. Default is two unrestricted
members, or one unrestricted master when `--teams` is given.

Custom `--teams FILE` JSON accepts:

```json
{"members":[{"name":"jynx","team":["Jynx","Starmie","Tauros","Snorlax","Chansey","Exeggutor"],"lead":"Jynx"}]}
```

Members may also set `checkpoint`, `trainable`, and dotted-key `overrides`.
Species/lead constraints do not fix moves: specialists still learn movesets
and battle play. Up to six required species can also specify a partial team.
Common config overrides go after `--` on **create**, for example
`-- policy.hidden_size=64 policy.num_layers=2`.

Each round trains members sequentially in roster order. A completed member is
published immediately: B faces the newly trained A, C faces the newly trained
A and B, and the next round's A faces their latest versions. Each stint trains
against every other member uniformly in resident opponent banks, mixed with
current-policy self-play. Opponent versions stay fixed only during that stint,
including retries. `--selfplay 0.5` allocates half the environments to
current self-play, not half the learner transitions. Self-play has two learning
seats; expert matches have one. Plans record the actual environment/step split.
`--steps` is total agent transitions per member and must be divisible by
`vec.total_agents * train.horizon` (262144 with default config).

```sh
ocean/pokemon/league.sh resume leagues/pokemon/three --rounds 1
ocean/pokemon/league.sh eval leagues/pokemon/three --games 256
```

`--rounds` means additional rounds, including completion of an interrupted one.
Completed members are not repeated. Interrupted members restart from their
round-start checkpoint; optimizer, live simulator and recurrent state restart.
Completed members remain published even if a later member fails. Resume keeps
the interrupted stint's saved opponents. Resuming an old unfinished round also
publishes its completed members before continuing under the sequential schedule.
Status/export/evaluation use the current roster, even partway through a round;
the completed-round counter advances only when all members finish. Checkpoints,
attempts, plans and per-game species/moves are retained. Evaluation uses balanced
seats, requires an even game count per pair, and resumes completed pair results.
Use this evaluator (or `./pokemon eval`/`watch`) to preserve specialist bindings.

### Live terminal dashboard

In an interactive terminal, create/train/resume now display the native PufferLib
dashboard automatically. `--live` forces live output; `--quiet` retains the old
log-only behavior. Live mode uses the Linux `script` command (util-linux) to give
the trainer a real pseudo-terminal, preserving its in-place updates and terminal
size detection. Output is also retained in the member's log, including ANSI
redraw codes; keyboard input is not recorded. Ctrl-C interrupts training; resume
keeps completed members and retries the interrupted stint as described above.

## Core resets plus selected masters/experts

Export any selected members into a native bank, excluding `master` if desired:

```sh
ocean/pokemon/league.sh export leagues/pokemon/three \
  --members alakazam_zapdos,jynx_starmie,jynx_cloyster \
  --output leagues/pokemon/three/experts.ini
./puffer train pokemon base.load_model_path=None \
  env.native_league=leagues/pokemon/three/experts.ini env.expert_fraction=0.5 \
  env.force_core_combos=1 env.force_core_prob=0.5 env.core_pool=all
```

For two experts, export only two member names to a different file. Exports bind
the exact checkpoint, composition and lead, and reject existing files without
`--force`. Do not reuse the historical `ocean/pokemon/experts_three.ini`:
its old checkpoints are incompatible. To warm-start the learner, replace None
with an explicit compatible checkpoint. This does not change exported experts.

For the same experiment inside round-robin training, configure the league:

```sh
ocean/pokemon/league.sh create leagues/pokemon/core-three --teams experts-three \
  -- env.force_core_combos=1 env.force_core_prob=0.5 env.core_pool=all
ocean/pokemon/league.sh train leagues/pokemon/core-three --rounds 10 --steps 1048576
```

Core constraints apply to unrestricted learners; prescribed-team/lead members
keep their constraints and automatically disable core/snapshot resets. Frozen
experts retain their own composition and lead in either seat. `core_pool=all`
contains all 149 legal species (540274 unordered triples). Normal drafts do
not consume deck cards. Counts distinguish **assigned** from **completed** cores.
League rounds carry the core cursor when the pool and seed match. Unfinished
games may be dropped at a round boundary; this is not exact simulator resume
and does not imply every assigned core completed. Ordinary native warm starts
must manually pass the previous checkpoint's `core_next_assigned` and
`core_next_drafted` as `env.core_start_assigned`/`env.core_start_drafted` to
continue the deck; loading weights alone starts the configured cursor.

Snapshot resets are separate and mutually exclusive with forced cores. Collect
new version-2 banks with `make -C ocean/pokemon states state-audit`; the collector
accepts `OUTPUT GAMES SEED CHECKPOINT_A CHECKPOINT_B ...`. Collector policies
must have unrestricted teams/leads. Old banks and old flag-migration scripts
are incompatible. ABI 3 encodes the public reset-source flag directly.
Audit with `build/pokemon/audit_states BANK SEED GAMES POLICY_COUNT`; pass the
same seed, game count and number of input policies used for collection.

## Verification and practical limits

`make -C ocean/pokemon policy-test freepick-test core-test native-league-test
state-test behavior-test league-test` covers CPU/CUDA forward and BPTT finite
differences, legality/privacy, deterministic deck continuation, expert bindings,
state resets, and interrupted-league recovery. `make -C ocean/pokemon audit`
runs randomized full-game contract checks under ASan/UBSan.

Short real BF16/CUDA-graph/EMAG training, seeded/fresh leagues, round-robin
evaluation and native bank loading were tested. These are correctness smokes,
not evidence of strategic improvement. No long experiment was launched.
The default H128/L2, 4096-agent run used nearly all of a 3 GB GPU; use
`vec.total_agents=2048 train.minibatch_size=4096` for more headroom and choose
step budgets accordingly. `base.eval_epoch_mult=0` disables post-training eval
for short diagnostics. Use `--dry-run` on league create/train to inspect plans.
