# BC, replay data, and critic audit — 2026-09-20

Historical audit below. Subsequent user-authorized LOCAL fixes and the new
entity-BC/multi-intent data pilot are documented in [BC_ENTITY_PILOT.md](BC_ENTITY_PILOT.md).
In particular, the cow/shed bug is now fixed locally and all 16 pilot games pass;
the remote live training checkout remains untouched.

## Bottom line

Single-teacher BC followed by PPO is a sensible next experiment. Optional value
pretraining is a separate ablation, not something the existing BC trainer does.
The old BC path is **not compatible with the current entity encoder yet**. No
training job, reward setting, checkpoint, remote binary, or simulator code was
changed during this audit. Preparation ran locally on CPU; no GPU jobs launched.

## What the current value head actually learns

- `kaggriculture_encoder.cu` has three decoder branches: task logits, market
  logits, and one scalar value. The value branch has its own nonlinear MLP but
  shares the entity encoder and MinGRU with the actor. Local and remote encoder
  source hashes matched during this audit.
- It approximates `V_pi(observation, recurrent history) = E_pi[sum gamma^k r]`:
  future **configured reward**, under the current policy/opponent distribution.
  It is not necessarily final cash and does not output one value per action.
- `src/pufferl.cu::select_copy` constructs return targets as old value plus
  estimated advantage. `src/algo.cu` uses clipped, return-variance-normalized
  squared error, multiplied by `train.vf_coef`. This path was also inspected on
  the remote, not assumed identical from local source.
- `kaggriculture.cu::kag_cuda_reset_episode` copies a sampled native game state.
  The learner then generates new actions/rewards from there. Those transitions
  train both actor and critic normally. The expert's unplayed continuation is
  not a supervised target. Recurrent memory resets at terminal boundaries;
  learned network parameters do not. `kag_reward_reset` resets cash/quality/
  growth baselines at the sampled state, so inherited assets aren't new gains.
- The latest start snapshot inspected, `1789895106981.start.ini`, was mode 2,
  executor 1, observation v3, H512/L3, gamma 0.999338269, vf_coef 2.00000024,
  reset_state_prob 0. The user is changing runs; these are snapshot facts, not
  a claim about every subsequent run. Reset learning applies when enabled.
- This confirms the training path is connected; it is not a measurement that
  the current critic is well calibrated. That needs value-vs-return diagnostics
  separated by root/reset starts.

`Q_pi(s,a)` instead asks about the return after a particular action, followed by
policy pi. DQN learns an action-value function using bootstrapped targets and a
max over next actions. Our 47 conditional action heads represent a combinatorial
joint action, not 1,058 mutually exclusive complete actions; DQN is not a direct
replacement for the scalar PPO critic. See the primary
[value-function definitions](https://spinningup.openai.com/en/latest/spinningup/rl_intro.html)
and [DQN paper](https://arxiv.org/abs/1312.5602).

## Existing tooling and gaps

| Existing component | What it does / current limitation |
| --- | --- |
| `refresh_daily_replays.py`, `refresh_reset_archives.py` | Public official daily ZIP refresh, resumable downloads, hashes, byte/disk caps. Reused successfully. |
| `scan_replay_identities.py` | Cheap prefix inventory; distinguishes display and agent names. Names alone do not identify a fixed submission revision. |
| `import_elite_replays.py` | Streaming primitive/macro-2/task-3 demo conversion; sparse labels for ambiguous decisions. Still imports the 1,280-byte Python submission encoder. |
| `macro_bc_labels.py`, `task_bc_labels.py` | Heuristic primitive-to-macro/task projection. Mode-2 runtime defaults to executor 0; importer does not explicitly select executor 1. Relabeling needs a measured behavior-equivalence/coverage gate. |
| `kag_bc.cu` | Recurrent cross-entropy BC and bot/DAgger generation. Value gradient explicitly zeroed; no value targets in BCv2. Still has byte-sized observation allocations/copies/I/O although `obs_t` is now float. Unsafe to use as the new float pipeline without a port. |
| BC checkpoint output | Writes raw weights without the fresh policy/controller sidecars required by `src/kag_observation_contract.h`. Size alone is not a valid compatibility test. |
| `build_macro_clone_factory.sh`, `build_task_bc_dataset.sh`, `train_elite_bc.sh` | Older data/model factories, dimensions/controller assumptions, and old validation logic. Do not launch blindly against today's encoder. |
| `bc_pipeline.sh`, `train_bc_ppo.sh` | Older launchers also override retired reward knobs, bot mixes, and league choices. Not used. |
| `index_replay_states.py`, `build_replay_state_bank.py` | Existing selected-state indexing, native replay reconstruction and parity-checked reset-bank tooling. |
| `counterfactual_dataset.py`, `fit_macro_value.py`, `macro_value_model.py` | Separate candidate-vs-baseline cash scorer (ridge/optional LightGBM), not the PPO critic or DQN. An existing research route for action-conditioned evaluation, not a ready critic warm-start. |

Current observation contract: **1,424 float32 features / observation v3**, not
1,280 uint8 bytes. Entity observations include stateful episode accounting and
land-fill timers (`entity_observation.h`); a stateless JSON encoder replacement
is insufficient. The `kag_policy_view.c` entity bridge also currently requires
a full Env and gates on mode 3. Mode 2/1 needs explicit support and parity tests.

Old archived factory results exist for Crop Dusta, Ryo Hasegawa, peikopon and
tetsuya, with widths 128/256 and two recurrent layers. The September 3,
deterministic H256 comparison tables show roughly 1.6k–3k cash, not strong
standalone clones. Those are old, small-scope evaluations under old code, not a
fair score comparison to today's PPO. They do establish that high cloning
accuracy or a `READY` filename is insufficient; closed-loop games are required.

## Data located and prepared

The remote already has 45 compressed daily archives, July 30–September 12.
Its prefix inventory reports 1,835 Crop Dusta player streams over 21 days, all
module v1.32.7; SpaTaro 792, peikopon 744, Ryo Hasegawa 729 and ymg_aq 653 are
other substantial pools. These are prefix counts, not full compatibility audits.

New official archives for September 17–19 were downloaded **locally**, with a
3 GiB cap and 40 GiB disk reserve. Verified ZIP member counts and saved SHA-256
sidecars: **1,945 unique games; 1,905,960,173 compressed bytes (~1.78 GiB)**.
Source: [official replay index](https://www.kaggle.com/datasets/kaggle/kaggriculture-episodes-index).

Fresh batch inventory (not a controlled ranking):

| Exact replay name | Games | Mean final cash | Train / holdout |
| --- | ---: | ---: | ---: |
| Majkel1337 | 518 | 107,267 | 442 / 76 |
| DSM | 459 | 105,205 | 388 / 71 |
| SpaTaro | 438 | 97,938 | 363 / 75 |
| Unknown Mother-Goose | 425 | 108,330 | 355 / 70 |
| ymg_aq | 385 | 101,956 | 319 / 66 |

Current leaderboard rank could not be verified: the public leaderboard API
returned HTTP 401 and the page did not expose ranked entries to the reader.
Do not relabel these as confirmed current top-100 agents. Daily archives are
also selection-biased. Replay metadata observed has names but no submission ID;
do not assume a month of one name is one unchanged policy or merge team aliases.

Local artifacts under `/home/felix/puffertank/elite_replays/`:

- `bc_refresh_2026-09-20.json`: download/hash report.
- `bc_prep_2026-09-20/inventory/`: fresh game/player catalog and identity summary.
- `bc_prep_2026-09-20/older_local_inventory/`: the six existing August archives;
  4,185 version-compatible games indexed without full JSON parsing.
- `bc_prep_2026-09-20/majkel_pilot/`: bounded 16-game pilot summary, accepted tape
  manifest, and explicit rejection reasons. **12 passed all 720 frame checks;
  four failed and were excluded.** Accepted split: 9 train / 3 holdout.
- `bc_prep_2026-09-20/tapes/`: 12 verified primitive action tapes, 438,528 bytes
  total. Both players' actions and seed/config are preserved to reconstruct
  dynamics; BC labels must still select the teacher seat, not both players.
- `bc_prep_2026-09-20/native/libkaggriculture_core.so`: isolated CPU-only core
  used to verify the pilot, SHA-256 beginning `fb588d59c9991d84`.

The deterministic split hashes episode ID (seed 20260920, 15% holdout). Both
seats and every future controller projection share that split. For final model
selection, add an untouched later-day/opponent holdout too. Full source ZIPs
are retained. The prefix inventory does not prove every game can be cloned.

### Replay mismatch found, not patched

All four rejected pilot games first diverged on `PLACE COW` beside the shed
while standing in a locked quadrant. For example, episode **110659520**, turn
148, seat 1, hand 3 at (5,5): official replay moves one COW from inventory into
the shed; native core keeps it carried. At the preceding frame the tile is
LOCKED. `kg_apply_unit_action` returns for locked tiles before reaching its
PLACE-to-shed fallback (DROP/PICKUP are handled before that guard). This points
to operation-order semantics, not an encoder or reward change. The other
failures are 110241660:244, 110579969:148, and 110607329:219.

The full raw replays and rejection records remain available. No simulator fix
was applied and this sample does not establish its impact on PPO performance.
Exclude these games until a separately tested parity repair; do not fabricate
expert labels/returns from divergent native states.

### Reusable preparation command

`prepare_bc_replays.py` is an additive, CPU-only preparation utility. It reads
8 KiB prefixes, deduplicates episode/player keys, preserves exact identities,
and optionally fully verifies a bounded sample before publishing compact tapes.
No policy observations, macro labels, reward labels, or trained weights are
claimed ready. Cached tapes are keyed by source CRC and native core SHA-256;
they include the full source SHA-256 and explicit t-to-t+1 action alignment.

```bash
python3 ocean/kaggriculture/prepare_bc_replays.py \
  '/home/felix/puffertank/elite_replays/raw/kaggriculture-episodes-2026-09-1[789]/*.zip' \
  --output /home/felix/puffertank/elite_replays/bc_prep_2026-09-20/next_pilot \
  --teacher Majkel1337 --agent-name Majkel1337 --cache-limit 16 \
  --cache-dir /home/felix/puffertank/elite_replays/bc_prep_2026-09-20/tapes \
  --lib /home/felix/puffertank/elite_replays/bc_prep_2026-09-20/native/libkaggriculture_core.so \
  --skip-incompatible
```

Use a new output directory for each inventory. Existing tapes are reused;
incompatible games are explicitly reported, never published as verified tapes.
Without `--skip-incompatible`, parity failures stop the run. `--cache-limit`
bounds attempted unique games, including failures. Omit it for prefix-only work.

Single-game local measurement: 33.2 MB official JSON took 1.97 seconds to unzip
and parse; loading its 34.6 KB compressed tape and replaying all 719 transitions
took 0.080 seconds (median of three). This is an illustrative ~25x preprocessing
comparison, not an end-to-end BC speed guarantee. First-pass all-frame parity
validation is additional work; native observation/label extraction still costs
time. Changing controller/rewards need not redo the full raw JSON parse when
game-core semantics are unchanged.

## Recommended experiment sequence

1. Keep the user's PPO/reward experiments independent. Preserve checkpoints and
   explicit controller metadata; don't revive old launchers or mutate rewards.
2. Port the standalone BC path to float observations with explicit dataset dtype,
   observation/policy version, controller mode/executor/source hash, mask/label
   version, and checkpoint sidecars. Use the production Env observation path,
   carrying episode accounting and land-delay state. Check no opponent-private
   information enters policy observations. Old BCv2 must fail closed.
3. Start with one exact teacher and a small parity-compatible set. Mode 2/1 is
   first because it is the user's best controller. Measure how much of the
   teacher can actually be represented by that executor; ambiguous/impossible
   actions must be masked or rejected, not assigned convenient wrong labels.
   Branch mode 3 labels from the same tapes/splits. Changing a controller
   implementation needs a new semantic fingerprint even if its numeric mode
   stays the same. The current 12-game pilot is validation material, not the
   intended full training set.
4. Gate a small BC run on held-out action coverage plus actual game cash/win
   results. Then expand data from the chosen teacher, retaining losses/recovery
   states as appropriate rather than selecting only easy wins. Historical
   Crop Dusta maximizes observed long-lived volume; fresh Majkel is a useful
   recent high-volume alternative. Mixing top agents is a later controlled
   ablation, not an assumption that their conditional strategies agree.
5. Compare matched-budget, matched-reward experiments: scratch PPO; BC→PPO;
   BC+critic warm-start→PPO. Keep seed/opponent/reset mixture and evaluation
   starts controlled; report learning curves and wall time, not only final score.

For offline critic pretraining, derive discounted return targets using the
**same reward code/config, gamma, termination and reset-accounting convention**
as fine-tuning. Preserve primitive tapes and regenerate reward sidecars when
rewards change. Fitting expert returns estimates the expert's continuation
value, not the value of a randomly initialized policy. It is more defensible
after BC, with conservative fine-tuning or an on-policy critic warm-up under
the cloned actor. Critic-only transfer into raw PPO is a separate risky ablation;
it does not by itself teach the actor which actions realize the expert return.

Do not treat offline DQN on demonstrations as a drop-in shortcut: greedy
bootstrapping can exploit errors on actions not supported by the dataset.
[Implicit Q-Learning](https://arxiv.org/abs/2110.06169) is a relevant later option
because it avoids querying out-of-dataset actions during value fitting and
extracts a policy with advantage-weighted BC. It still needs new Q/V losses,
action conditioning and validation here; it is not currently implemented.

## Verification and boundary

Seven new unit tests passed for deduplication, episode-level splits, exact
identity separation, malformed outcomes, next-frame action alignment, parity
failure exclusion, cache reuse and core-hash invalidation. Twelve real games
passed 8,640 frame comparisons; cached rollouts were replayed again. Scope-only
diff checks passed. Existing unrelated Retro/WebNav edits were preserved.

This delivers audited findings, new raw data, catalogs/splits, a reusable
primitive-tape pilot, and a tested preparation tool. **It does not deliver a
ported float BC trainer, a trained BC checkpoint, or offline critic training.**
Those remain the next implementation stage; no claim of improved model score
is made.
