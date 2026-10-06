# Gen 9 Random Battle native port

The native Bend/C conversion goal is paused at the user's request. There are
two source-derived training backends: the original persistent Showdown workers
and autonomous source continuations that group admitted numeric operations into
in-process C batches. Both connect to PufferLib CUDA PPO. The continuation
backend uses lossless compact public observations and categorical CUDA lookups.
Most engine state and nonnumeric execution still use JavaScript objects; this
does not complete the paused native conversion or establish the throughput target.

## Run autonomous source continuations

Use the pinned checkout, prepared oracle and dependency catalogs described below.
From the repository root:

```sh
node ocean/pokemon_gen9/build.cjs --worker-deps-only
node ocean/pokemon_gen9/build.cjs --batch-deps-only
node ocean/pokemon_gen9/build.cjs --compact-build-only
node ocean/pokemon_gen9/build.cjs --continuation-build-only
node ocean/pokemon_gen9/build.cjs --batch-test-only
node ocean/pokemon_gen9/build.cjs --continuation-test-only
node ocean/pokemon_gen9/build.cjs --continuation-transport-only
node ocean/pokemon_gen9/build.cjs --continuation-encoder-only
node ocean/pokemon_gen9/build.cjs --continuation-trainer-build-only
node ocean/pokemon_gen9/build.cjs --continuation-smoke-only
node ocean/pokemon_gen9/build.cjs --continuation-train-only --train.total_timesteps=10000
```

`config/pokemon_gen9_batch.ini` selects 256 actors across four workers, 32
battles per worker, a 64-wide one-layer policy and both-seat current-policy
self-play. Each worker can own up to 512 battles. Build/check commands use
one CPU; training and performance commands use all exposed CPUs. The shared
resource lock, memory/no-swap cap, task cap and 1,200-second timeout apply to
both backends. Ctrl+C and `--worker-stop-only` stop the entire Pokémon scope.
The separate binary is `build/pokemon_gen9/puffer_batch`; the shared `./puffer`
is preserved. The default format is pinned singles `gen9randombattle`.

The source compiler retains the original synchronous methods and creates
generator companions for simulator/data methods that can reach admitted native
operations. Nested action/event control flow runs independently per battle;
the scheduler groups ready operations without captured source inputs or frame
transitions. C implements 43 original ability handlers, nine numerical helper
specializations and four compound damage/stat expressions. Unsupported native
lowerings retain their original source behavior. Pure lookups, imported free
functions and statically nonsuspending boolean/comparator branches remain scalar.
Constructors, team generation, resets, getters, most state mutation and public
client processing also remain scalar. See the generated manifests for the
exact admitted kernels, source hashes and excluded lowerings.

ABI 2 preserves all 53,819 features in the existing public encoder using
1,775 float32 values (30.32 times smaller). Single categories use IDs and full
category sets use exact 16-bit words; unknown and absent categories remain
distinct. IDs select the same categorical weight columns as the dense encoder.
CUDA rollout uses sparse weight lookups; PPO expands only an update minibatch,
so dense observations are not stored in the full rollout or sent over IPC.
This preserves the earlier encoder, including its limits on public history and
counters; it does not add new public features. ABI/schema hashes are checked
when the worker connects and saved in training logs. Build with `--float`;
float16 cannot represent every category word. The guarded trainer build sets
that option automatically. This environment has its own checkpoint namespace.

Qualification compares requests, masks, pending commitments, PRNG seeds,
ordered logs and both public observations after every decision. Compact
observations decode byte-exactly to the original encoder. The independent
128-game log hashes and both scheduler orders are checked. Targeted scenarios
cover Illusion visibility, hidden-state independence, Transform, Terapagos
Stellar, multi-hit moves, Revival Blessing, weather/events and Trick Room.
Compiler fixtures cover call order, optional chains, sparse callbacks,
apply/bind, exceptions, boolean mutation/shadowing, spread/rest arguments and
method names/constructibility (including an own `__proto__` method).
These finite tests do not establish an all-state equivalence proof.

The CUDA encoder test checks dense expansion, sparse and dense forward results,
weight gradients and CUDA graph replay against the existing dense encoder.
Training qualification checks finite metrics and all saved weights, changing
checkpoints and the ABI/schema identity. It does not measure playing strength.
Current artifacts live under `build/pokemon_gen9/continuation/`, with the
compact schema/encoder under `build/pokemon_gen9/compact/` and C kernels under
`build/pokemon_gen9/batch/`.

For matched complete-game timings, C/worker scaling and actual PPO throughput:

```sh
node ocean/pokemon_gen9/build.cjs --continuation-benchmark-only
node ocean/pokemon_gen9/build.cjs --continuation-scale-build-only
node ocean/pokemon_gen9/build.cjs --continuation-scale-only
node ocean/pokemon_gen9/build.cjs --continuation-train-bench-only
```

The matched benchmark uses identical source-generated action tapes and checks
all terminal logs. It includes rules, public clients and both observation
encoders, excluding startup, team generation, IPC and PPO. Worker scaling
includes compact IPC and auto-reset; the training benchmark includes CUDA
policy execution and PPO. Rates from these three workloads are not interchangeable.
Compact transport reduces memory/payload size; it does not guarantee faster
CPU encoding or simulation. No 10,000-times or 100,000-SPS gain is established.

## Run the source CPU training backend

From the repository root, using the existing prepared oracle/catalog:

```sh
node ocean/pokemon_gen9/build.cjs --worker-deps-only
node ocean/pokemon_gen9/build.cjs --worker-test-only
node ocean/pokemon_gen9/build.cjs --worker-transport-only
node ocean/pokemon_gen9/build.cjs --worker-build-only
node ocean/pokemon_gen9/build.cjs --worker-smoke-only
node ocean/pokemon_gen9/build.cjs --worker-train-only --train.total_timesteps=10000
```

Every command keeps the same resource lock and 1,200-second guard. Build/check
commands retain the one-CPU quota; `--worker-train-only`, `--worker-batch-only`
and `--worker-scale-only` use all CPUs exposed to this process, following the
user's request to scale runtime workers. Memory/no-swap/task limits remain.
The build
produces `build/pokemon_gen9/puffer_showdown`, preserving the shared `./puffer`.
Ctrl+C stops the complete resource scope, including the trainer and all workers.
To stop Pokémon commands from another terminal, without waiting for the lock:

```sh
node ocean/pokemon_gen9/build.cjs --worker-stop-only
```

Stopping preserves checkpoints already written; it does not save an additional
checkpoint. The default final evaluation runs 32 games after the requested
training steps, so reaching the last training checkpoint does not end the run.
The default `config/pokemon_gen9.ini` uses one worker with four battles/eight
agents, a 64-wide one-layer MinGRU policy, terminal +/-1 rewards and gamma=1.
Both seats train the current policy; `selfplay.enabled=0` disables the historical
opponent pool, not current-policy self-play. Longer jobs must still respect the
guard; the trainer saves checkpoints periodically. No long run is left running.

`worker_core.cjs` invokes original rules and generates both teams inside each
worker. The policy encoder accepts only its own request and a player-specific
`@pkmn/client` view built from channel-filtered public logs. The client uses the
pinned source Dex, not a second battle dataset. Dependencies are exact-version
and integrity pinned in `worker-package-lock.json`. No battle server, websocket,
Python per step, search or transformer is involved.

The initial ABI has 15 actions: wait, four moves, four move+Tera choices, and
six switch slots. Hidden trapping/disable rejections refresh the source request;
the other player's committed choice remains pending. Own requests govern masks,
including forced switches, Revival Blessing, Struggle and Recharge. The C wire
uses batched float observations and binary masks/actions, and both players
receive terminal rewards before the next battle's observations are published.

The first encoder is intentionally wide: 53,819 floats with categorical one-hot
features, own/revealed rosters, public field/active effects, and move candidates.
It is a baseline projection of the client state, not an exhaustive encoder of
all public history/counters or the final shared entity architecture. Improving
its representation, worker scaling and stronger independent evaluation bots
remain work; a smoke run does not establish playing strength.

Validation: 128 complete source games/10,121 decisions matched the earlier
original log hashes exactly, with finite observations and checks for private
state independence, unknown opponent bench and Illusion disguise. The compiled
C transport then completed 32 games/3,129 decisions, including auto-reset and
zero-sum terminals, at 123.70 decisions/sec under the one-CPU quota. The actual
PPO smoke completed 1,024 agent steps, eight optimizer updates and four final
evaluation games; all final weights/metrics were finite and checkpoint weights
changed between updates. Two smoke runs reported final rates of about 67–75
agent steps/sec under the same quota, with identical final checkpoint hashes.
These rates count different work and are not scaling or
playing-strength results. Current artifacts:

- `build/pokemon_gen9/worker/validation.json`
- `build/pokemon_gen9/worker/training-smoke.json`
- `build/pokemon_gen9/worker/smoke.log`

For a CPU-only profile of 32 complete games through the C transport, use
`node ocean/pokemon_gen9/build.cjs --worker-profile-only` after building the
transport harness. It writes `build/pokemon_gen9/worker/worker-profile-summary.json`.
The initial sample attributes 21.75% of time to observation writes and 11.87%
to callback lookup; phase timings include scheduling/IPC waits, and the sampler
and CPU timing calls add overhead. Each actor currently sends 215,292 bytes per
step. Compact transport/representation and event-dispatch optimization are
measurable next experiments before a complete mechanics rewrite. Thousands of
of steps/sec have not been demonstrated. The one-CPU quota was used for this
initial profile; subsequent multicore results follow below.

### Measured worker scaling

```sh
node ocean/pokemon_gen9/build.cjs --worker-scale-build-only
node ocean/pokemon_gen9/build.cjs --worker-scale-only
node ocean/pokemon_gen9/build.cjs --worker-batch-only
```

The CPU scaling run used all four exposed cores. Its best measured case was
four workers/128 agents at 852 agent rows/sec (773 non-wait decisions/sec),
averaging 3.90 busy cores including startup. Eight workers/256 agents averaged
3.94 busy cores and reached 703 rows/sec. More agents therefore did not provide
unbounded throughput on this CPU. These random-policy runs include dense
transport and public-client overhead, and establish neither a raw-JavaScript
speed ceiling nor training strength. `worker/scaling.json` stores each case.

The actual 128-agent/four-worker PPO run completed 16,384 steps and 32 updates
with finite saved weights/metrics and changing checkpoints. It used horizon=4,
minibatch=128, synchronous learning, and disabled final evaluation specifically
for the throughput run. Its final reported rate was 580 agent steps/sec; total
reported training uptime was 39.82 seconds (about 411 steps/sec including early
warmup). The original eight-agent smoke and default training configuration are
preserved separately. `worker/batch-training.json` stores the result and
`worker/batch.log` stores the complete transcript. These paths are under
`build/pokemon_gen9/`.

## Earlier trace-driven batching prototype

Before autonomous execution, the separate batching experiment used a restricted TypeScript-to-C compiler,
explicit primitive-field schemas, numeric helper specializations, and a C
per-lane operation scheduler. It generates 43 ability handlers from their
original bodies plus nine fixed-point helper specializations (52 kernels).
The original source, data and reference runtime remain unchanged. Numeric
helper lowering retains JavaScript unsigned truncation/signed shift behavior;
event modifier updates use separate state per lane and frame. Unsupported
handlers are listed with reasons in the manifest and are not converted into
successful no-ops or silently counted as complete.

```sh
node ocean/pokemon_gen9/build.cjs --batch-deps-only
node ocean/pokemon_gen9/build.cjs --batch-test-only
```

The TypeScript compiler dependency is version/integrity pinned in
`batch-package-lock.json`. Generation and checks retain the one-CPU build guard,
memory/no-swap cap and 1,200-second timeout. Generated code, library, source
manifest and differential-test report are in `build/pokemon_gen9/batch/`.

Validation matched all 128 complete instrumented source games to the prior
full-log hashes and decisions. C replay checked 320,418 captured/synthetic
operations over 256 lanes under two scheduling orders, including all generated
kernels, nonfinite/wrapping numeric inputs, conditional field inputs and mixed
handlers sharing persistent modifier frames. Largest-ready-group scheduling
averaged 4.949 lanes/batch; rotating operation selection averaged 32.823 with
the same results, and both reached batch size 256.

This is **trace-driven kernel and scheduler validation**, not an autonomous
batched simulator. The source still supplies unconverted operations, input
state and frame transitions; the replay reports these imports/resets explicitly.
This does not establish training readiness or any full-environment speedup.
That report qualified the original leaf kernels and replay scheduler. The
autonomous continuation backend described above supersedes it for live battle
execution and training. A complete native state layout and native execution
of the remaining source operations are still pending. The original
native-conversion goal stays paused.

## Preserved native conversion

The reference is Pokémon Showdown commit
`9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e`, October 3, 2026. The native target
is the default singles `gen9randombattle` format, with generated teams and
search-free policies. [PORT_PLAN.md](PORT_PLAN.md) maps the engine and port order;
[source-map.json](source-map.json) inventories pinned source handlers.

The code implements default singles team generation with both Showdown RNGs,
pre-start Pokémon construction, queue/listener ordering, callback descriptors,
numerical damage stages and persistent PP/boost/disable/HP/faint primitives.
Persistent effect objects now pass original helper comparisons, including
shallow aliases, field order, creation order and clearing. Selected constructor
effect states/maps, party identities and exact effect ID/display-name text also
pass independent source comparisons.
The persistent indexed-array kernel passes independent JavaScript comparisons
for holes, mutations, fresh results and retained graph aliases. Original Type
callbacks and live type-query helpers also pass retained-state comparisons;
species-array constructor sharing also passes original-source comparisons.
Native setType/addType, setSpecies, persistent stat tables and immutable
apparent-type strings are implemented. Move arrays retain shared ordinary
records through PP/disable/view operations; Transform's slot-copy stage and
clearVolatile's array-restoration stage pass retained-source comparisons.
Complete Transform/forms, Tera and other lifecycle callers remain pending.
Grounded-state queries also pass retained-state comparisons, including distinct
true/false/null outcomes, early returns and condition presence with falsy values.
Scalar event values, typed context frames and forty-three executable callbacks
also have independent source fixtures; [PORTED_CALLBACKS.json](PORTED_CALLBACKS.json)
lists the bodies. Native holder discovery and joined scalar/array runEvent calls
now have independent original-source comparisons, including onEffect insertion.
Live Pokémon stat helpers now pass independent comparisons with original event
discovery, including boost-copy isolation, Unaware, room behavior, cached speed
and getBestStat's repeated reads. Remaining stat callback bodies stay unsupported.
Battle activation, complete event execution and handler
lifecycles, remaining move/ability/item handlers, complete player
requests/observations, PufferLib bindings, bots and learner integration remain outstanding.
No native battle throughput or playing-strength result is claimed. The separate
direct-Showdown runtime experiment below measures original-source games.
The full training completion contract is in [TRAINING.md](TRAINING.md):
shared semantic MLPs and MinGRU, with no transformer or search.
The current evidence and remaining proof obligations are in
[VERIFICATION.md](VERIFICATION.md).

## Build and validate

From the PufferLib repository root:

```sh
node ocean/pokemon_gen9/build.cjs --bootstrap-only
node ocean/pokemon_gen9/build.cjs --test
```

Bootstrap pins stock Bend **2.0.35**, Lean **4.34.0**, the oracle revision and
Node dependencies with checksums/lockfiles. Downloads, compilers, generated
catalogs and transpiled oracle files live under ignored `build/pokemon_gen9/`.
It currently selects Linux x86_64 toolchains and requires Python 3.12+, Node 22+,
npm, git and clang-19. It preserves the existing global Bend installation.
Archive checksums are verified before fresh extraction. Existing compiler
versions and an installed dependency-lock stamp are reused on later setup runs.

The CLI exposes `bend update` and `bend version`. Update fetches the latest
installer; this project instead uses a pinned compiler at
`build/pokemon_gen9/toolchain/bend/bin/bend`.

The build uses the shared WebNav resource lock and resource guard: up to 6 GiB,
no swap, one CPU, 128 tasks and a 1,200-second timeout for the whole process tree.
`--emit-only` emits C without invoking clang.
With `--test`, `--tests=native,event_value,event_context,callbacks` selects
specific suites after a normal guarded proof/compile. Omitting it runs all suites.
After building and validating, `node ocean/pokemon_gen9/build.cjs --bench-only`
measures the existing native library against the original generator under the
same resource guard. It records reset-generation results in
`build/pokemon_gen9/native/benchmark.json`. Rebuild after changing Bend or C
sources before invoking this benchmark.

A file at `build/pokemon_gen9/native/defer-benchmark` defers timing runs on a
busy shared machine. Remove that file when ready to benchmark. The first timing
run occurred during other workloads and is not a clean speed comparison;
the counter representation has since changed. Correctness results are independent
of those wall-time measurements.

## Direct Showdown runtime experiment

The JS sidequest uses the same pinned original battle engine directly, without
poke-env, a battle server, websockets, Python per step or action search. Run it
under the unchanged build guard:

```sh
node ocean/pokemon_gen9/build.cjs --js-probe-only
node ocean/pokemon_gen9/build.cjs --js-pack-only
node ocean/pokemon_gen9/build.cjs --js-profile-only
```

`js_probe.cjs` runs four warmup games and 32 measured complete Gen9 Random
Battles. Actor bots receive only their own source requests. Games include team
generation, original rules, request construction, logging, bot decisions and
complete-log hashing. Both runtimes receive identical seeds and a fixed
diagnostic Date.now clock because source logs include timestamps; timings use
monotonic clocks. GPU inference, PufferLib encoding/transport and worker scaling
are outside this benchmark.

The successful guarded packaged-runtime trial (scope
`run-r778f24a9ba714321a214732d67536da8`, exit 0, inactive/success) compared all 32
complete game summaries and SHA256 log hashes exactly: 1,070 turns, 2,456 actor
decisions, three revealed-trap retries and six Terastallizations. Node 24.18.0
measured 509.20 actor decisions/sec; the Bun 1.4.2 executable measured 500.73.
These are short, single-worker shared-machine trials with a one-CPU quota,
not a robust speed or scaling result. Packaging showed no gain in this trial.

`js_pack.cjs` downloads a checksum-pinned experiment-local Bun binary and
embeds JavaScriptCore into an executable. Pinned Showdown files remain in an
external, private copied runtime tree; its PRNG dependency is bundled there
without replacing RNG functions. This is runtime packaging, not JS-to-native
AOT, and neither the oracle nor upstream checkout is edited. Evidence lives in
`build/pokemon_gen9/js-probe/{node-baseline,bun-compiled,pack-comparison}.json`.
The profile command records 128 measured source games separately and checks
their first 32 against the baseline; sampling overhead prevents using its
timing as a speed comparison.

The guarded 128-game Node CPU profile also passed (scope
`run-rb917929784294cb39c033e35df688019`, exit 0, inactive/success), covering
4,435 turns, 10,121 actor decisions and 21 Terastallizations. Its first 32
game summaries/log hashes matched the baseline. Across startup, data loading,
warmup and measured games, getCallback accounted for 32.48% of sampled self
time, findEventHandlers 6.63%, deepClone 5.13% and runEvent 4.55%; GC was 2.19%.
This points to callback lookup/discovery as optimization candidates, rather
than establishing compiler gains. Preserve live callback semantics before
attempting lookup caches. The raw profile and summary are
`build/pokemon_gen9/js-probe/{showdown-node.cpuprofile,node-profile-summary.json}`.

[Static Hermes](https://github.com/facebook/hermes/blob/static_h/doc/blog/2024-12-23-compiling-javascript-to-wasm.md)
provides actual JS-to-C/native AOT. It has not been built or tested here.
Showdown's dynamic data-module loading and Node host functions need a static
module/host adapter before a standalone compiler can consume it. Its
[maintainer explains](https://github.com/facebook/hermes/discussions/1685)
that native AOT of untyped JS does not generally improve on V8/JSC throughput;
even typed compilation has no guaranteed speed advantage.

The direct-source worker backend at the top of this document now supplies
player-visible observations, masks, retry handling, terminal accounting and
binary transport to the existing learner, with a completed training smoke.
Stronger evaluation bots, a more efficient encoder and scaling qualification
remain. The paused Bend/C port is preserved and incomplete; source-runtime
training does not complete the native conversion or its proof obligations.

`PROOF.bend --verdict` asks Bend's Lean-verified kernel to recheck imported
domain definitions and the laws in `LAWS.bend`. Those laws cover
Q12 rounding, full-word scaling, a seed transition, ordered list edits, bounded
HP, stat boosts, action order, damage truncation, listener metadata, ChaCha
quarter-round/byte order, numeric conversion, suppression precedence and
request visibility, scalar event outcomes, scope defaults and callback dispatch.
They do not prove universal correspondence with Showdown. Independent tests
run the original TypeScript and native Bend separately, comparing outputs and
post-operation RNG state without replacing native results with oracle results.

## Source layout

| File | Responsibility |
| --- | --- |
| `Math.bend`, `Rng.bend`, `Sodium.bend`, `SodiumMem.bend` | Integer math and both Showdown RNG streams |
| `GenRng.bend`, `Lists.bend` | Stateful sampling, shuffle, ordered filters and swap-pop |
| `GenData.bend`, `Catalog.bend`, `data/` | Typed generator state and resolved integer catalog |
| `GenCounter.bend` | Move typing and counters |
| `GenCull.bend` | Ordered incompatible-move and team-detail culls |
| `GenMoves.bend` | Required moves, STAB/coverage/support enforcement and remaining moves |
| `GenAbility.bend`, `GenItem.bend` | Ability and held-item selection |
| `GenSet.bend` | Full set, form, level, EV/IV, gender, shiny and move order |
| `GenTeam.bend` | Weighted species sampling, rejection checks, team counters and lead insertion |
| `State.bend`, `Init.bend`, `Stats.bend` | Private state, pre-start Pokémon/team construction and stat/HP/PP arithmetic |
| `Queue.bend` | Source selection-sort swaps/tie RNG and stable redirect/left-to-right ordering |
| `Events.bend` | Listener sub-order, creation order and fractional switch-in speed |
| `Damage.bend` | Resolved numerical damage stages, critical RNG and confusion arithmetic |
| `Pokemon.bend`, `Boosts.bend` | Persistent PP, hidden disable and ordered stat-stage changes |
| `Number.bend`, `HP.bend`, `Faint.bend` | Bounded rational/non-finite conversion, HP mutations and deferred faint records |
| `Registry.bend` | Resolved callback/constants, SwitchIn aliases and requested-event ordering metadata |
| `Suppress.bend` | Ability/item/Fling, Neutralizing Gas, weather and active-attacker suppression |
| `MoveView.bend` | Numeric move-choice projection after source lock/type events are resolved |
| `EventGate.bend` | Separate singleEvent/runEvent pre-callback gates, stale-status checks and depth/line limits |
| `EventValue.bend` | Distinct scalar return kinds, defaults, continuation/fast exit and final modifiers |
| `EventContext.bend` | Typed single/run/handler scope frames, modifier context and exact parent restoration |
| `EffectStore.bend` | Persistent effect objects, U32 reference lookup, ordered field updates, shallow clone/init and source clear semantics |
| `EffectMem.bend` | Persistent-state reads/writes, missing-state initialization and runEvent holder binding |
| `ValueArray.bend` | Indexed persistent arrays with holes, length, in-place push/index assignment, fresh text filters/concatenation/copies, shallow aliases and text membership; full lifecycle integration remains pending |
| `TypeBodies.bend`, `TypeEvent.bend`, `PokemonTypes.bend` | Roost/Arceus/Silvally Type functions, a closed Type executor with native discovery, and live getTypes/hasType; type-change lifecycle integration remains pending |
| `TypeInit.bend` | Shared constructor type arrays following actual resolved Dex identities, including cosmetic aliases; equal contents retain distinct ownership |
| `TextStore.bend`, `TextMem.bend`, `TextJoin.bend` | Immutable UTF-16 primitive strings, content interning with collision checks, and indexed string-array join; primitive storage is separate from effect objects |
| `TypeChange.bend` | setType/addType guards, retained array references, fresh string-input arrays and independent apparent-type snapshots; lifecycle callers remain pending |
| `StatTable.bend`, `Spread.bend` | Persistent stored/base stat objects, ordered in-place stat copying, retained set level/nature and species spread calculation; HP initializes only when maxhp is zero |
| `SpeciesChange.bend` | setSpecies through native ModifySpecies discovery, shared types, apparent snapshots, weight, stats and cached speed; Transform/forms/public lifecycle callers remain pending |
| `MoveSlots.bend` | Persistent base/current move arrays and ordinary records, authoritative PP/disable/view reads, and the slot-copy/array-restore stages of Transform/clearVolatile |
| `BoostTable.bend` | Ordinary relay and persistent boost objects, root-authoritative reads/clones, ordered mutations and separate shared/copied/fresh replacement stages |
| `Grounded.bend` | Original isGrounded ordering, presence checks, live types, Levitate/Eelevate, suppression, item effects and distinct bool/null results; immunity/hazard/terrain callers remain pending |
| `ListenerExecution.bend` | Shared event ordering and gates below generic callback execution; reused by Type events |
| `EffectInit.bend` | Selected pre-start battle/Pokémon/side effect states and condition dictionaries, with exact identities and initialization order |
| `Callbacks.bend`, `PORTED_CALLBACKS.json` | Executable stat, boost, weight and Type bodies and all imported scalar constants; unsupported function bodies fail explicitly |
| `SingleEvent.bend` | Raw/custom callback selection, gates, scalar execution and restoration; full lifecycle/public records remain pending |
| `RunEvent.bend`, `VectorEvent.bend` | Scalar/indexed post-collection listener execution, relay rules and parent restoration |
| `ListenerPriority.bend` | Ordering keys from actual effect descriptors, holder facts and requested callback names |
| `EventListener.bend` | Shared listener data below callback execution, allowing native discovery inside nested queries |
| `Collect.bend` | Pokémon/side/field/default-format discovery, scalar bubbling and indexed targets; duration-only listeners retain end continuations |
| `CollectedEvent.bend` | Original runEvent front half joined to scalar/array execution; raw onEffect lookup, pre-sort state allocation and original invalid-onEffect faults |
| `LiveStats.bend` | Pokémon calculateStat/getStat/getActionSpeed/updateSpeed/getBestStat/getWeight through native event discovery; remaining event bodies stay explicitly unsupported |
| `Weight.bend` | Heavy Metal's signed magnitude and Light Metal/Float Stone truncation before U32 conversion; minimum effective weight and explicit diagnostic domain checks |
| `WeatherQuery.bend` | Field/Pokémon effective weather, suppression, Utility Umbrella, effect-sensitive Mega Sol and weather membership queries; weather lifecycle/messages remain pending |
| `TerrainQuery.bend` | Scalar effectiveTerrain/isTerrain with inherited targets, nested depth checks and native collection; pinned TryTerrain has no listeners, and unexpected runnable handlers fail explicitly |
| `Request.bend` | Singles move request, hidden restriction/trapping flags, Struggle/recharge and Tera projection after resolved lock events |
| `Mem.bend`, `Wire.bend`, `Main.bend` | Affine memory state and diagnostic operations |
| `read.c`, `write.c`, `bridge.c` | Native runtime and array ownership transport |
| `read_effects.c`, `write_effects.c`, `effects_transport.h` | Ownership transport for an opaque Bend effect-store root retained across calls |
| `export_catalog.cjs` | Build-time resolved Dex export, stable IDs and sampling pools |
| `emit_item_rules.py` | Authoring-time item-rule emission; produces ordinary Bend functions |
| `reference.cjs`, `prepare_reference.cjs` | Independent pinned Showdown oracle, used only by tooling |

The native library links no Python, TypeScript, Node or Showdown simulator.
Counters skip zero increments and are updated as moves are added. A cached
counter survives pool culls; replacing the selected moves invalidates it.
The completed counter is reused by ability and item selection.
Catalog schema 12 records callback descriptors, literal handlers, metadata-only
rows, species callbacks, callback-name relationships, condition identities,
exact effect ID/display-name text, type immunities and move target IDs. Species
row offset 30 identifies a shared type-array group; `speciesTypeArrays` exports
each group's full ordered type list. Species field 52 stores exact addedType
text; export checks the bounded base-stat domain used by spread arithmetic. The
export asserts the default format's constructor rule hooks at the pinned source.
Exact raw target strings and reverse primitive-text-to-move/target indexes
support persistent move records without normalizing their stored strings.
Executable
callbacks still require a native port. It retains `SHOWDOWN_LICENSE` and
`CHACHA_NOTICE`.

## Diagnostic wire

`pg9_execute(uint32_t *words)` accepts 524,288 words. Words 0–16383 are mutable
transport/state; the remaining words are a read-only imported catalog. Set
word 2 to 1 to upload the catalog once; the native transport clears it as an
acknowledgment. Normal calls copy only the mutable prefix and retain the owned
native catalog array. The current bridge serializes calls through one Bend
runtime; it is a validation interface, not the final vectorized environment ABI.

Word 0 selects an operation, word 1 reports status (0 success, 1 unknown op,
2 generator failure, 3 invalid diagnostic input/capacity, 4 unsupported executable
body, 5 event depth, 6 singleEvent pending-line limit, 7 invalid onEffect), and words 8–11 hold
the four 16-bit Gen5 RNG limbs. Word 4 selects Gen5 (0) or Sodium (1); the
Sodium key is eight little-endian words at 560–567.

| Op | Operation | Inputs | Outputs |
| --- | --- | --- | --- |
| 1 | RNG draw | bound 12 | next limbs 16–19, draw 20 |
| 2 | Arithmetic | operands 8–9 | upper product 16, chain 17, modify 18 |
| 3 | Species import | species 12 | fields 16–31, encoded weaknesses 64–83 |
| 4 | Move counter | species 12, template 13, Tera 14, moves 32– | counts 64–108, lists 128/192 |
| 5 | Ability | counter inputs, details 48–60 | ability 16, updated RNG 8–11 |
| 6 | Empty sample | none | expected failure without RNG consumption |
| 7 | Item | ability inputs, lead 15, ability 30 | item 16, updated RNG |
| 8 | Cull pool | counter inputs and details | length 16, pool 64– |
| 9 | Moveset | species/template/Tera/lead/details | length 16, ordered moves 64– |
| 10 | Full set | species 12, lead 15, details 48–60 | set at 64 |
| 11 | Full team | RNG limbs | six sets at 64, stride 32; size 16 |
| 12 | Construct one Pokémon | set at 64 | private Pokémon row at 576 |
| 13 | Stats/HP kernels | fields 8–14 and 22–24 | results 16–21 |
| 14 | Construct two Gen5 teams | first seed 8–11, second seed 12–15 | private state 512–16383, final second-team RNG 8–11 |
| 15 | Sort resolved action/effect keys | count 12, mode 13, eight-word rows at 8192 | sorted rows at 8192, size 16, updated RNG |
| 16 | Resolved damage/critical arithmetic | fields 32–43 | damage 16, unmodified base 17, critical flag 18, updated RNG |
| 17 | Confusion arithmetic | level/power/attack/defense 32–35 | damage 16, updated RNG |
| 18 | Listener metadata | fields 32–40 | encoded sub-order 16, creation order 17, encoded speed 18 |
| 19 | Deduct persistent PP | side/holder/move/amount 32–35 | paid 16, live move slots |
| 20 | Ordered boosts, clear or set | side/holder/mode/count 32–35, stat/value pairs 64– | signed actual delta +32768 at 16, positive stages 17 |
| 21 | Disable a live move | side/holder/move/hidden/source kind/ID 32–37 | persistent disabled flag/source |
| 22–23 | Raw callback / getCallback alias | family/ID/callback/target-Pokémon 32–35 | presence 16, descriptor 64–69 |
| 24 | Select Sodium seed | eight LE key words 8–15 | mode 4, persistent key 560–567 |
| 25–26 | Selected bounded RNG / raw Sodium | bound 12 for op 25 | draw 16, updated persistent seed |
| 27 | HP damage/heal/sethp/faint | fields 32–41 | result tag/sign/magnitude 16–18, holder state and deferred queue |
| 28 | Resolve suppression | side/holder/active-attacker/ignoreAbility 32–35, private initial facts | five booleans 16–20 |
| 29 | Project move choices | side/holder/lock mode/move/restrictData 32–36 | count 16, eight-word rows 64– |
| 31 | Pre-callback gate | family/effect/event/side/holder/mode/scope/depth/pending lines 32–40 | allowed 16 or source fault 5/6 |
| 30 | Resolved singles move request | side/holder/hard lock kind+ID/semi lock kind+ID/canSwitch 32–38 | count 16, visibility flags 17, Tera type 18, rows 64– |
| 32 | Scalar event values | mode/fastExit/Q12/literal tag+payload 32–36, old/returned values 64/68 | value 64–67, stopped 16, truthy 17 |
| 33 | Typed event context trace | command count 32, parent context 64–82, 24-word commands 8192– | count 16, initial context 10216, 24-word boundary snapshots 10240– |
| 34 | Executable callback body | family/ID/callback/side/holder 32–36, context 64–82, relay 88–91 | value 96–99, truthy 17, resulting context 10216– |
| 35 | Scalar singleEvent | family/ID/requested callback/state/override callback/pending lines 32–37, parent context 64–82, relay 88–91, child event 104–116 | value 96–99, parent context 10216– or explicit error |
| 36 | Sort resolved listener keys separately | count/mode 32–33, eight-word input rows 8192– | sorted rows 8192–, count 16, RNG; preserves action rows 2176–2687 |
| 37 | Scalar runEvent after collection | count/fastExit 32–33, parent context 64–82, relay 88–91, event 104–116, 16-word listener rows 8192– | value 96–99, parent context 10216–, visited count 16, IDs 13824– |
| 38 | Array-target runEvent after collection | count/fastExit/target count/hasRelay 32–35, context/event as op 37, target addresses 144–, initial four-word relays 160–, listeners 8192– | four-word values 96–, restored context 10216–, visited count/IDs 16/13824–, target count 18 |
| 39 | Resolve an actual listener's ordering | family/ID/callback/state/scope/creation/holder/isPokemon/cached speed/unboosted speed/rank/ability order/target index 32–44 | presence 16, eight-word sort key 64–71, callback descriptor 72–77 |
| 40 | Persistent effect-state helpers | mode/reference/key-or-field-count 32–34, target-isPokemon/target-active/explicit-order flags 35–37, order value 38–41, write value 64–67, five-word key/value rows 8192– | reference 16, next reference 17, creation counter 18, field count 19, return value 64–67, ordered fields 8192– |
| 41 | Exact effect identity text | family/ID 32–33 | ID text 16, display-name text 17; invalid families/IDs fail explicitly |
| 42 | Native handler discovery | mode/target family/target/callback/duration/custom Pokémon/source Pokémon/array count 32–39; array targets 160– | count 16, 32-word listener records 8192– |
| 43 | Scalar runEvent with native discovery | fastExit/onEffect/source-effect family+ID 32–35, parent context 64–82, relay 88–91, event 104–116 | value 96–99, restored context 10216–, visited count/IDs 16/13824– |
| 44 | Array runEvent with native discovery | op 43 inputs plus target count/hasRelay 36–37, targets 144–, four-word values 160– | four-word values 96–, restored context 10216–, visited count/IDs 16/13824–, target count 18 |
| 45 | Ordinary boost-object helpers | mode/Pokémon address/stat index 32–34, value 88–91; modes 0 clone, 1 sparse, 2 decode/clamp stage | object reference or encoded stage 16; objects inspectable through op 40 |
| 46 | Live Pokémon stat helpers | mode/Pokémon address/stat index/unboosted/unmodified/Q12 modifier/statUser 32–38, parent context 64–82, boost value 88–91 | stat/speed/best-stat index/weight 16, restored context 10216–; modes 0 getStat, 1 calculateStat, 2 getActionSpeed, 3 updateSpeed, 4 getBestStat, 5 getWeight |
| 47 | Read-only weather queries | mode/Pokémon address/condition count 32–34, parent context 64–82, condition IDs 160–; modes 0 field effectiveWeather, 1 Pokémon effectiveWeather, 2 field isWeather | condition ID or boolean 16; count at most eight is diagnostic transport only |
| 48 | Scalar terrain queries | mode/count 32–33, parent context 64–82, requested target value 88–91, condition IDs 160–; modes 0 effectiveTerrain, 1 isTerrain | condition ID or boolean 16, or nested depth fault; count at most eight is diagnostic transport only |
| 49 | Persistent indexed arrays | mode/reference/argument 32–34, push/concat value 88–91, initial four-word values 160–; modes 0 dense create, 1 length, 2 index read, 3 push, 4 text filter, 5 concat one value, 6 text membership, 7 sparse copy, 8 index assignment | value 64–67, object metadata 16–19 and ordered fields 8192–; argument is create count, index or exact string ID according to mode |
| 50 | Live Pokémon type queries | mode/Pokémon address/excludeAdded/preTera/query count 32–36, parent context 64–82; mode 0 getTypes, mode 1 hasType with exact string IDs 160– | value 64–67, restored context 10216–; getTypes also returns array metadata/fields as op 49; flags apply to getTypes, hasType always queries ordinary current types |
| 51 | Constructor type-array finalization | existing private party pointers and display species | populated type roots and per-battle array cache; repeating attachment reuses existing arrays |
| 52 | Live grounded-state query | Pokémon address/negateImmunity 32–33, parent context 64–82 | value 64–67 (bool or null), restored context 10216–; nested Type mutations remain persistent |
| 53 | Immutable UTF-16 text | mode/ID-or-length 32–33; mode 0 read, mode 1 intern units 160– | ID/next-object/order/length/dynamic-count/hash 16–21, text value 64–67, UTF-16 units 8192–; 1,024 input/4,096 output units are diagnostic bounds |
| 54 | Pokémon type changes | mode/Pokémon address/enforce/text-ID-or-array-reference 32–35; modes 0 setType string, 1 setType array, 2 addType | boolean value 64–67; persistent roots/arrays/text snapshots remain inspectable |
| 55 | Persistent stat tables | mode/Pokémon address/non-HP stat index 32–34; mode 0 attach diagnostic flat tables, mode 1 read current stored stat | mode 1 integer 16; roots 102/103 and ordinary objects inspectable via op 40 |
| 56 | Pokémon setSpecies | Pokémon address/resolved species ID/isTransform/use-default-source 32–35; parent context 64–82, explicit source value 88–91 | returned Species value 96–99, restored parent context 10216–; mutations remain in persistent rows/objects |
| 57 | Persistent move slots | mode/Pokémon address/argument/Hidden Power type text 32–35; modes 0 attach initial flat slots, 1 peek index, 2 getMoveData by move ID, 3 restore base-array slice, 4 install existing current array, 5 copy Transform slots from target Pokémon | reference 16 (0 for missing peek/lookup); roots 107/108 and ordinary objects inspectable via ops 49/40; modes 3/5 implement only their move-array stages |
| 58 | Persistent Pokémon boosts | mode/Pokémon address/argument 32–34; modes 0 attach flat initial table, 1 read encoded stage by stat index, 2 fresh zero replacement, 3 share target Pokémon's table, 4 copy target fields into existing table | stage/reference 16; root 109 and ordered fields inspectable via op 40; modes 2/3/4 implement only their boost stages |

A set stores species ID, display form ID, role, Tera, ability, item, level,
gender, shiny and move count in offsets 0–9; moves start at 10, six EVs at 16
and six IVs at 22. Gender IDs are 1 M, 2 F, 3 N. Type weakness entries encode
the source's signed effectiveness exponent plus 8.

Team details are rain, sun, sand, snow, status cure, Spikes count, Toxic Spikes,
Stealth Rock, Sticky Web, Defog, Rapid Spin, screens and Tera Blast usage.

Private state starts at 512. Constructed teams have phase 1 and no active
Pokémon; switch-in events and a first request have not run. Party rows start at
576 with stride 128. Queue storage starts at 2176. Reset clears the complete
private region through 16383 and preserves the catalog and a selected Sodium
stream. The constructor
diagnostic receives explicit team seeds; battle RNG remains a separate future
input. This interface must not supply private state to policy observations.

Private effect roots occupy 524–527 (format, weather, terrain and current
battle effect state) and 529 (pseudo-weather dictionary). Pokémon row offsets
89–93 hold species/status/ability/item state and the volatile dictionary;
94–95 hold constructor-final newlySwitched and cached speed. Party pointers
occupy 4160–4171. Side-map blocks begin at 4192 and 4200; offsets 0/1 hold
side conditions and the sole initial singles slot-condition dictionary.
Pokémon offsets 96–98 hold types-array reference, added-type text and
terastallized-type text. Constructor type-array cache roots occupy 4480–8191;
only encountered source identity groups are allocated, after the selected
constructor effect states/maps. Reset clears this cache with other private state.
These selected roots are initialized before activation; they do not establish
that all constructor fields or lifecycle transitions are implemented.
Offsets 102/103 hold ordinary storedStats/baseStoredStats object references;
104/105 hold nature plus/minus indices (hp=0, atk=1 through spe=5, neutral=0),
and 106 retains set.level independently from the live Pokemon.level at offset
3. Stored stats stay in the same object across setSpecies; the six-stat base
table is fresh for ordinary changes and retained during Transform-mode changes.
Row 16 records the latest spread HP; rows 17–21 mirror current non-HP stats.
Persistent roots are authoritative for live stat and collection speed reads.
Rootless flat states remain admitted only by the existing diagnostic fixtures.
Offsets 107/108 hold current/base move-array references. Constructor arrays
are distinct shallow copies sharing the same ordinary slot records. Record
properties use exact source order and primitives: move=40, id=1, pp=41,
maxpp=42, target=2, disabled=43, disabledSource=44, used=45, virtual=46.
Initial records omit virtual; transformed records omit disabledSource and set
virtual=true. Transform slots receive min(5, Dex base PP), fresh records and
the transformer's Hidden Power type in the move name. Base records survive.
The slot-copy stage assigns an empty current array before reading the target,
so a self target yields no slots. Array restoration makes another shallow
slice of the base array. These operations do not implement the other
transformInto/clearVolatile stages. Slot roots govern PP, disabling, move views
and getMoveData; flat slot rows are diagnostic mirrors and may be stale in
another holder sharing the same records. PP's admitted domain is nonnegative
whole U32 amounts and values; arbitrary JavaScript numbers remain unproved.
Offset 109 holds the ordinary boost table. Roots govern boostBy, setBoost,
clearBoosts, positiveBoosts and both live-stat boost paths; rows 24–30 are
diagnostic stage mirrors. clearBoosts mutates the existing object in its own
key order; clearVolatile's boost stage allocates a fresh seven-field zero
object. The Baton Pass assignment stage shares the actual target table.
Transform's boost stage copies target properties into the existing destination,
retaining its identity and key order. Mutations remain visible through shared
references even when another holder's flat mirrors are stale. Modified getStat
passes a fresh shallow copy with the actual object's field order to ModifyBoost.
The stored mutation domain admits full seven-field tables with integral stages
from -6 to 6; existing temporary relay readers retain their wider integer
clamping domain. Reachable closure, sparse/malformed stored tables and complete
parent lifecycles remain unproved.
The switch-in speed order uses count 530 and field-position values 531–532;
weather/terrain condition IDs occupy 533–534. Collection reads party pointers,
current positions and cached speed from private state. Once the pseudo-weather
dictionary exists, suppression derives Magic Room from it; the legacy resolved
flag at 544 applies only to standalone diagnostics without that dictionary.

Queue rows contain ID, order, priority, speed, sub-order, effect creation order
and two untouched payload words. Priority uses signed tenths +32768, speed
uses signed halves +32768, and sub-order uses signed integers +32768.
Order zero means unspecified and sorts last. Op 15 is a standalone fixture
operation: its input/output scratch area begins at 8192, away from private
header and RNG state even at the full 64-row capacity.
Modes 0/1/2 select priority speedSort, stable redirect ordering and stable
left-to-right ordering. Only mode 0 consumes tie-shuffle RNG. Modes 1/2 use the
last two fields as holder ability effectOrder +1 (zero absent) and target index;
mode 0 preserves those fields as uninterpreted payload. Stable redirect
fixtures use consistent comparator keys; mixed missing/present ability keys
with contradictory ties are not covered.

Op 16 fields are level, resolved base power, resolved attack, resolved defense,
flags, weather Q12 modifier, STAB Q12 modifier, encoded effectiveness exponent
(signed +6), final Q12 modifier, willCrit mode, resolved critical ratio and
whether a critical is allowed. Flags 1/2/4/8 mean Parental Bond's second hit,
unknown move type (skip STAB), applicable burn penalty and bypass Protect.
willCrit modes 0/1/2 mean undefined/false/true. These diagnostics cover integer
stages after upstream decisions have been resolved. They do not yet resolve
immunity, variable base power, Tera/STAB state or native effect callbacks.

Op 18 fields are effect-kind code, ability ID, target scope, encoded explicit
sub-order, callback/holder flags, creation order, cached speed, unboosted speed
and switch-in speed rank. Effect kinds 0–7 are condition, ability, item, format,
weather, rule, ruleset and other. Scopes 0–4 are Pokémon, side, slot, field and
other. Flags 1/2/4/8 mean SwitchIn suffix, RedirectTarget suffix,
onAllyTryHitSide and Pokémon holder. Rank is source speedOrder index +1.

The generator targets default singles without custom rules or a configured
Pokémon of the Day. The configured PotD variant remains separate follow-up
work. Type IDs, cosmetic forms and required items
retain source semantics; integer IDs do not determine sampling order.

HP op 27 fields are side, persistent holder index, mode (damage/heal/sethp/faint),
numeric tag (positive finite/negative finite/NaN/positive infinity/negative
infinity), numerator high/low, denominator, optional source address and effect
kind/ID. A finite numerator fits 48 bits and a denominator is nonzero.
Result tag 0 is a signed numeric delta; tag 1 is source boolean false.
Row offsets 80/81/82 hold faintQueued/fainted/switchFlag. Deferred records
start at 4096 with target/source/kind/ID; count is 528. Resolving those records
still requires BeforeFaint/Faint/End/AfterFaint events.

Suppression reads row 23 flags: active 1, transformed 2, Gastro Acid 4, Embargo 8,
Commanding 16, ability ending 32, side.active membership 64, Heal Block 128.
It keeps membership distinct from isActive and excludes fainted gas holders.
When ability-state root offset 91 is supplied, its `ending` field (key 16)
overrides cached bit 32, including an absent or undefined field. Only older
diagnostics with no root use the cached bit. Queries do not write the cache;
retained object mutations take effect on the next read.
Magic Room is 544; active attacker address and move ignoreAbility are 548/549.
Flag 256 denotes the Dynamax volatile for direct Choice-item body fixtures;
the default Gen 9 format does not allow Dynamax.
The diagnostic accepts attacker index +1, zero absent.

Move-choice rows contain move ID, live source slot +1 (zero synthetic/fallback),
PP, max PP, target ID, disabled, field-presence bits and synthetic kind.
Presence bits 1/2/4/8 represent PP/maxPP/target/disabled. Locked requests omit
those fields; Recharge uses synthetic kind 1 and move ID zero. Live slot offset
7 stores its target; holder offset 83 stores an added third type. Integrating
live type/lock events and the surrounding request/choice protocol remains pending.
Op 30 uses lock kinds 0/1/2 for unlocked/move/recharge. Visibility bits
1/2/4/8 mean trapped/maybeTrapped/maybeDisabled/maybeLocked. Holder fields
84–88 retain trapped (0 false, 1 true, 2 hidden), maybeDisabled, maybeLocked,
maybeTrapped and available Tera type. An active singles holder is last-active;
inactive holders take the source's unrestricted-data branch. Hard locks set
trapped and clear the three uncertainty flags. Struggle is
synthetic kind 2. The complete phase protocol, lock/type event resolution and
living switch enumeration remain separate engine work.

Op 31 modes 0/1 are singleEvent/runEvent; scope 1 is a Pokémon holder.
Header 536/537 holds eventDepth/pending lines. Status 5 means the depth limit
was exceeded; 6 means singleEvent's pending-line limit was exceeded.
The gate checks requested event identity, stale major status, item/ability/
weather suppression and active-attacker exceptions. These source faults remain
engine errors, distinct from game results and training truncations. Handler
collection and public fault records remain separate engine work. Ops 32–35
exercise scalar execution and typed scopes; they do not assert full games.

Scalar values occupy four words: tag/high/low/denominator. Tags 0–10 are
undefined, null, false, true, nonnegative finite number, negative finite number,
NaN, positive infinity, negative infinity, text ID and object reference.
Finite magnitudes fit 48 bits; text ID zero represents empty text. Objects use
high/low for family/ID. Modifier finalization preserves the source's double
rounding before integer truncation for large integer products.

Context rows hold effect family/ID, persistent effect-state ID, requested event,
three four-word values (target/source/source effect), modifier presence/Q12,
depth, frame count and last numeric result, followed by three reserved words.
Effect family 4 denotes no effect; 5 denotes species. Scope restoration preserves effect-state
references rather than undoing mutations to their objects. The diagnostic
trace admits 64 commands and rejects mismatched or empty returns. It is not a
general instruction-budget limit for full games.

Stat callbacks receive resolved integer relays. Raw bool/number/text callbacks
are supported. Function bodies outside the named manifest return error 4.
Porting a stat callback does not finish the ability/item/condition's remaining
callbacks, clocks, activation, public events or lifecycle.

Live stat helpers invoke native-collected ModifyBoost and stat-specific events.
Stat indices 0–4 denote atk/def/spa/spd/spe. Ordinary boost objects have keys
32–38 for those five stats plus accuracy/evasion, with signed numeric values;
private row stages remain encoded as stage + 6. Raw allocations preserve field
order and aliases without adding effect-state id/order fields. Foresight and
Miracle Eye clear only positive evasion. Unaware reads its persistent holder,
activePokemon (548) and activeTarget (535), preserving the source's ordered
writes, self exception and event suppression.

`calculateStat` builds a sparse boost object and swaps base defenses under
Wonder Room. `getStat` uses a full boost copy when modified, while unmodified
Wonder Room queries swap the boost selection instead of the base value. These
helpers preserve source flooring, default a zero calculateStat modifier to one,
cap speed at 10000 and apply Trick Room before 13-bit action-speed truncation.
`updateSpeed` writes cached speed at row offset 95. `getBestStat` retains
the first stat on ties and repeats the original getStat call on each new maximum,
including its allocations and RNG draws. These are helper implementations;
queue integration, live speed maintenance and remaining stat callback bodies
remain pending.

`getWeight` reads private row offset 22 and invokes original-collected ModifyWeight.
Heavy Metal doubles a signed integer magnitude before event finalization;
Light Metal and Float Stone truncate half the original magnitude before U32
conversion. This preserves odd/negative inputs and the source's final modifier
rounding. The result has a minimum of one hectogram. Ordinary reachable weight
relays are integers; Heavy Metal rejects fractional or overflowing diagnostic
magnitudes with error 3 before multiplication. Proving all reachable numerical
bounds is still a separate obligation.

Weather queries use field condition ID 533 and the persistent Magic Room map.
Chlorophyll, Swift Swim, Solar Power and Orichalcum Pulse query the Pokémon's
effective weather, respecting Utility Umbrella and item suppression. Sand Rush
and Slush Rush query field weather. The helper preserves source Mega Sol's
effect-category check and its override before Umbrella; default calls emit no
activation message. Unburden's stat condition checks an empty held item and
the Pokémon's current ability suppression, without assuming it still has
Unburden. Sandstorm's Rock SpD and Snow's Ice Defense bodies query live types
before effective weather and directly modify their relays at priority 10. They
preserve Type-event mutations even when no bonus applies, and round before
later item chains. Weather setting/clearing, effect messages and remaining
weather/terrain lifecycle bodies remain pending.

Effect-store op 40 modes 0–7 reset, initialize, read, write, delete, clear,
shallow-clone/initialize and inspect. Mode 8 imports a new raw object at the
requested reference, without initialization; it rejects existing IDs, zero
and reference overflow. It supplies independent initial diagnostic state,
never replaces native post-action state. Reset's reference argument initializes
the creation counter. Reserved field keys 1/2/3/4 are `id`/`target`/`effectOrder`/`counter`;
other keys identify source properties. Fields use the existing scalar/reference
value codec. Object-reference family 7 names another effect object, so a clone
keeps nested aliases. Creation order follows original `initEffectState` at
`sim/battle.ts:3322`; clear follows `clearEffectState:3334`, retaining target,
emptying id, resetting an existing order field and deleting other fields.

The Bend store persists independently of the flat words through an owned opaque
root transported by C. The U32 radix tree reads/writes an object in 32 steps;
there is no fixed object-count or field-count bound in the model. Op 40 admits
128 fields for diagnostic transport. Reference/counter overflow reports an
explicit domain error; their full reachable-game bounds still need proof.
Reset clears both flat private state and this root. SingleEvent/runEvent callback
scopes now use persistent references. A supplied singleEvent state is used
without initialization or implicit target rebinding; a missing state receives
`initEffectState({})` only if a callback is
selected and allowed. Function runEvent handlers ensure a state and write their
holder into its target; literal and skipped runEvent handlers neither allocate nor bind.
Restoring the parent scope retains object mutations. Slow Start's two stat
callbacks read this persistent counter; its start/residual/end lifecycle remains
pending. Listener transport uses row words 13/14 for non-Pokémon holder family/ID
(2 Side, 3 Field, 4 Battle); zeros retain the existing Battle-holder diagnostic.
The named constructor states and dictionaries are attached to private roots.
Remaining constructor/lifecycle fields, garbage collection, full array-valued state integration
and per-worker isolation remain integration work.

Op 49's arrays use Value family 8 and share the persistent object heap. Own
indices are ordered U32 keys below 4294967295; that reserved key stores length
and cannot be read as an index. A hole reads undefined but is absent from
filter iteration. Text filtering allocates a fresh dense array; concatenation
and sparse copying allocate fresh arrays while preserving holes. Push updates
the existing reference. Nested object/array values retain shallow aliases.
Neither allocation nor mutation advances the effect creation-order counter.
The 64-value dense input limit belongs to this diagnostic transport, not the
core model. Named properties, accessors, prototype overrides and generic
concat spreadability are outside the admitted indexed-array model. Growth
beyond JavaScript's U32 length limit returns domain error 3; it does not emulate
the named-property mutation preceding a JavaScript push overflow fault. Full
reachable-game array and allocation bounds remain proof obligations.

Op 50 reads private Pokémon offsets 96/97/98 as a types-array reference,
added-type text ID and terastallized-type text ID. Native construction populates
these roots; isolated query fixtures also import independent pre-action roots.
Non-Stellar Tera returns a fresh
one-element array before entering Type events. Otherwise native discovery,
ordering, gates and holder binding run Roost and inherited Arceus/Silvally
bodies. Roost saves the incoming reference as field key 18 (`typeWas`) and
returns a fresh filter excluding Flying. Species typing reads raw ability and
Plate/Memory data, ignoring held-item suppression, and retains the incoming
array when transformed or lacking the required raw ability. Empty results
receive Normal in place; added types use fresh concatenation. Empty hasType
query lists still run getTypes before returning false. Exact type text IDs in
`data/TypeTexts.bend` are appended without moving existing IDs. Shared species
constructor arrays preserve their source aliases. Frozen-array mutation
enforcement or a reachable mutation-safety proof and Transform/form
changes remain pending. Native setType/addType are described below. Grounded-state queries and Sandstorm/Snow defense callbacks
now use these live type queries through native event discovery.

Op 53 preserves JavaScript primitive strings as immutable UTF-16 code units,
including NUL and unpaired surrogates. Static IDs retain their previous values;
new contents receive per-battle IDs after the static catalog. Hash buckets only
select candidates: full code-unit equality decides identity. Dynamic interning
does not consume an object reference or advance effectOrder. Object mutations
preserve text snapshots, and resetting the heap forgets dynamic strings. Catalog
schema 10 adds complete static text units/hash chains and signed species numbers
encoded as U32 words in species field 51. The catalog/state transport now has 524,288
words (array depth 19); test buffers derive their size from catalog metadata.

Op 54 follows the original setType/addType guard order. Ordinary setType rejects
Stellar, current species number 493/773, then active Tera before checking the
empty-string error. Enforce bypasses those rejection predicates. A string input
allocates a fresh array; an array input retains its reference. Accepted setType
clears addedType, sets knownType and saves a separate joined apparentType string.
Constructor roots 99/100/101 hold knownType/apparentType/baseTypes; baseTypes
aliases the original shared species array and is unchanged by setType. addType
only updates addedType when Tera is absent. Join supports string cells and empty
fragments for holes/null/undefined; other coercions/prototype overrides remain
unsupported. Native arrays still lack frozen-write enforcement or a complete
reachable mutation-safety proof. Forms/Transform/Tera activation and revelation
callers remain separate obligations. Array mode 8 preserves numeric own-key order
while filling holes/growing length; the named MAX property remains outside the
admitted own-indexed array model.

Op 52 preserves original isGrounded order: gravity, Ingrain and Smack Down
presence precede item suppression and Iron Ball, then live Flying/??? queries,
effective Levitate/Eelevate and attacker suppression, Magnet Rise/Telekinesis
presence, and Air Balloon. negateImmunity skips only the Flying query branch.
Ability immunity returns null rather than false. Early returns skip later Type
queries and their depth checks/allocations. Presence reads use own condition
dictionary fields even when their values are undefined, null, false, zero,
NaN or empty text; arbitrary prototype changes remain outside this model.

Op 42 modes 0–5 collect Pokémon, side, field, battle, scalar-target and
array-target handlers respectively. Its duration flag applies to holder-specific
collection; scalar/array routing follows ordinary findEventHandlers. Each record
contains effect family/ID, callback presence, requested/actual callback IDs,
tag/value, persistent state, holder family/ID, end kind/arguments, indexed target,
scope and the existing eight ordering keys. Format family 6 appears only in
collected default-format records; the export asserts that this format has no
runtime callback properties. Custom onEvent registrations are outside the
default configuration. The 256-record transport limit is diagnostic, not a
proved reachable-game bound.

Without duration reads, collection accepts falsy condition states and uses an
absent state reference for optional priority reads. Function execution initializes
that state only if needed, without replacing the original dictionary entry.
Truthy primitive condition states and primitive duration reads remain outside
the admitted effect-object model.

Duration-only records keep a missing callback and the correct end continuation;
their countdown and end execution are future residual-loop work. onEffect uses
raw callback lookup, allocates an empty initialized state before sorting, and
only binds its target when its function actually executes. Literal callbacks
still allocate this insertion state. Missing source effects and array targets
with a defined insertion callback reproduce the source fault as error 7.

To rerun fixtures against the existing library under the same guard, use
`node ocean/pokemon_gen9/build.cjs --test-existing --test [--tests=collection]`.
This refuses missing libraries or native source/generated C files newer than
the library. It skips compilation and proof checking; a normal build remains
required after native source changes.

## Validation status

The guarded native build and regression runs validate these areas:

On October 5, 2026, `node ocean/pokemon_gen9/build.cjs --test` checked 92 laws
and compiled the native library. After correcting the isolated fixtures for
nested discovery, guarded `--test-existing --test` and selected regression runs
passed all 31 default suites against that library. All runs retained the
1,200-second resource guard. Native inputs were unchanged during compilation
and the subsequent regression runs.

The subsequent array increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=value_array,effect_store,effect_scopes,stats,weather,terrain`.
It checked all 99 laws, compiled the native library and passed all six selected
suites. The array fixture adds a 32nd default suite; a full 32-suite rerun is
not claimed for this increment.

The Type increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=types,value_array,callbacks,run_event,vector_event,listener_priority,identity,registry,collection,stats`.
All 104 laws checked and the native library compiled; six selected suites passed
before the new Type fixture encountered syntax and transport-encoding errors.
After correcting only that fixture, guarded
`--test-existing --test --tests=types,identity,collection,stats` passed the
remaining four selected suites against the unchanged native inputs/library.
The Type fixture adds a 33rd default suite; this increment does not claim a
full 33-suite rerun.
An expanded guarded `--test-existing --test --tests=types` also passed the
plain no-handler path, first-call empty membership side effects and original
string versus array membership branches, for 3,591 total Type transitions.

The constructor-array increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=init,type_init,types,identity,registry,generator,teams,sodium`.
All 105 laws checked and the library compiled. Generator, teams, initialization,
type initialization and registry suites passed. The Sodium reset fixture still
expected an empty constructor type cache; it was corrected to check the exact
new array/cache root and clear deliberately dirty unrelated cache cells.
Guarded `--test-existing --test --tests=sodium,types,identity` then passed all
three remaining suites with unchanged native inputs/library. This adds a 34th
default suite without claiming a full 34-suite rerun. All runs retained the
authorized 1,200-second guard.

The weather-defense increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=weather_defense,stats,weather,types,callbacks,run_event,vector_event`.
All 106 laws checked and the library compiled; all seven selected suites passed
under the unchanged 1,200-second guard. The new suite compares 16,410 original
transitions in 1,236 retained sequences, including 6,284 matching source depth
faults. This adds a 35th default suite without claiming a full 35-suite rerun.

The grounded-state increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=grounded,types,weather_defense,collection,effect_store,effect_scopes,suppression`.
All 110 laws checked, the native library compiled and all seven selected suites
passed under the unchanged 1,200-second guard. The new suite compares 7,806
original isGrounded calls in 1,196 retained sequences, 315 dictionary mutations
and 1,112 matching source depth faults. This adds a 36th default suite without
claiming a full 36-suite rerun.
A subsequent guarded `--test-existing --test --tests=grounded` passed added
Arceus/Silvally cases without Roost, checking repeated Type-event allocations
for the Flying and ??? queries. Native inputs/library stayed unchanged; the
expanded suite totals 8,082 queries, 1,242 sequences and 1,152 source faults.

- 1,024 Showdown RNG draws, 516 full-word products/modifiers, 320 modifier chains
  including the source's signed shift.
- Imported fields and type weaknesses of all 509 directly generated species/forms.
- Move counters, dynamic move typing, ability/item choice and final RNG state
  across all 879 templates.
- 3,516 ordered move-pool culls and 3,516 seeded complete movesets.
- 2,036 full-set comparisons, including two matching source failures.
- 256 complete six-member teams with exact final RNG state.
- 2,048 raw/scaled Sodium draws with full key transitions and 64 complete
  Sodium-seeded teams; reset preserves the stream.
- 509 pre-start Pokémon constructors, 16 independently seeded two-team
  constructions and 325 nature/boost cases, including dirty private-state reset,
  selected persistent constructor graphs/maps, exact fields/aliases/creation
  order, party pointers, newlySwitched, cached speed and shared type arrays.
- 1,585 resolved species/display names and 1,480 actual source type-array
  identities; 27 shared groups and 193 equal-content groups with distinct
  ownership. 409 independent constructor cache sequences attach twice each
  and compare 4,908 original getTypes calls, exact roots/cache/retained graphs,
  scopes and RNG. Source arrays are frozen/nonempty; native cache reuse
  allocates nothing. This does not prove frozen-mutation behavior or reachability.
- 1,219 priority/redirect/left-to-right sorts in each of two buffers (2,438
  transitions), including negative fractional speeds, stable ties, preserved
  payloads, exact RNG and preservation of action rows during listener sorting.
- 960 resolved damage/critical cases covering all 16 damage rolls and 512
  confusion cases, with exact final RNG and rejected zero defense.
- Listener metadata comparisons across all 321 imported abilities and
  side/slot/field scopes, explicit sub-orders and switch-in ranks.
- 4,096 PP deductions, 1,024 ordered boost transitions and 512 hidden disables.
- 3,489 callback descriptors/constants, 30,054 SwitchIn aliases including species
  records, and 361 type immunities.
- 5,009 exact resolved effect ID/display-name pairs, including cosmetic species;
  original literal-text IDs remain stable and identity lookup preserves RNG/actions.
- 4,640 independent HP/faint transitions, including fractions, non-finite and
  wrapping inputs, false/zero distinctions and duplicate faint prevention.
- 16,520 suppression combinations covering all imported abilities/items,
  Fling, weather, gas lifecycle and active-attacker exceptions.
- 16,416 numeric move-choice projections across all 509 set species, with
  hidden disables, PP exhaustion, locks/recharge and target overrides.
- 12,288 resolved singles move requests across active/inactive holders, hard
  and semi locks, hidden restrictions, trapping, Struggle, recharge and Tera.
- 58,468 original singleEvent/runEvent pre-callback gates, with 14 matching
  source depth/line faults.
- 2,913 scalar event-value cases, including undefined continuation, fast exit,
  signed zero, non-finite values and large-product source rounding.
- 206 original nested event traces and 3,048 independently observed scope/
  modifier boundaries, including empty/mismatched frame and source-fault checks.
- 10,978 direct executable/literal callback cases for thirty-five numeric bodies,
  prefixed effects, conditional status/HP/species/item checks and paralysis with
  suppressed Quick Feet.
- 11,049 integrated original singleEvent cases covering raw/custom selection,
  falsy custom fallback, missing callbacks, suppression, source faults and exact
  parent restoration for the supported stat/literal bodies.
- 396 original scalar runEvent cases and 336 array-target cases, including
  indexed relays, early exits, DamagingHit's numeric-zero exception, all sorting
  modes, exact RNG, target binding and parent restoration after collection.
- 26,911 ordering cases using actual Dex callbacks, aliases, species metadata,
  scope defaults, switch-in bias and Magic Bounce.
- 2,583 original effect-state/helper transitions over retained native calls,
  including target/activity/explicit-order cases, shallow-copy aliases,
  field insertion/deletion order and cleared-state identity; invalid references
  and counter overflow are rejected without wrapping.
- 1,560 original persistent effect-scope transitions in 78 independent sequences,
  including supplied/absent states, single/run/array holder binding, aliases,
  counter mutations/clears, literal/skipped handlers, fallback allocation,
  exact field order, parent restoration and RNG. Existing scalar/array fixtures
  also compare every listener's post-event object by persistent identity.
- 44,112 original handler-discovery cases with 46,649 listeners: exact
  state/holder/end identities, duration-only records, dictionary insertion
  order, cached-speed ordering, base species, scalar bubbling, prefix exclusions,
  inactive/source cases, custom holders and repeated indexed targets.
- 768 complete original runEvent calls with native discovery, including stat
  combinations, authoritative Magic Room state, scalar/array/fast exits,
  raw onEffect insertion, literal allocation, original invalid-onEffect/depth
  faults, retained field snapshots, exact RNG and restored parent scope.
- 4,944 original Pokemon.calculateStat/getStat/getActionSpeed/updateSpeed/getBestStat/getWeight
  calls with unmodified/unboosted combinations, sparse/full boost objects, large
  boost inputs, Wonder/Trick/Magic Room, Unaware/suppression, speed caps/wrapping,
  best-stat ties and repeated event calls. Compare persistent field snapshots,
  retained temporary aliases, allocation order, cached speed, parent scope and RNG.
- 1,536 direct/original singleEvent cases for the three boost-mutation bodies,
  including ability-prefixed Unaware, positive/negative/absent evasion and ordered
  sparse writes; 768 ordinary boost-object/clone/stage-clamp fixtures. Remaining
  weather lifecycle bodies fail explicitly rather than being approximated.
- 48,384 original field/Pokémon weather queries and 6,144 complete stat, speed
  and weight calls across eight weather values and suppression/item/ability
  combinations. Source discovery and callbacks remain original. Cover the six
  weather stat bodies, Unburden and the three weight bodies, Utility Umbrella,
  Magic Room, Air Lock/Cloud Nine/Gas/Gastro Acid/Shield/ending/inactive facts,
  Mega Sol's effect-sensitive override, persistent field snapshots, allocation
  order, parent scope and exact RNG. Invalid weather transport and out-of-domain
  weight magnitudes are rejected explicitly before multiplication.
- 9,216 successive abilityState.ending writes/deletions with deliberately stale
  flat flags. Compare original weather and all five suppression results after
  each mutation, including false/null/zero/empty text, nonzero numbers, objects,
  undefined, deletion and re-insertion, retained aliases and property order.
- 143,040 original scalar terrain queries and 1,440 full stat/action-speed/
  updateSpeed calls, including 95 original nested depth faults. Cover all five
  terrain values, explicit/inherited/falsy targets, suppression and the three
  terrain stat bodies. Independently assert the empty TryTerrain callback
  inventory across 4,959 resolved effects. Compare retained persistent state,
  allocation, cached speed, parent scopes on success and exact RNG; reject
  invalid diagnostic targets, modes, counts and condition IDs.
- 2,630 independent JavaScript array operations/snapshots across 906 retained
  objects. Cover sparse/dense cells, explicit undefined versus holes, fresh
  filters/concatenation/copies, cyclic and shallow references, original-array
  isolation, numeric property order, exact length and unchanged RNG. Initial
  sparse inputs are imported once; no post-action source state replaces native
  results. Invalid shapes/references and diagnostic/domain limits are rejected.
- 3,591 original Type callback/event/helper transitions: 477 direct bodies,
  954 singleEvent calls, 72 native-collected Type events, 1,008 getTypes calls,
  1,008 hasType calls and 72 intervening shared-array writes. The 276 original
  depth faults are included in those counts. Inventory three original Type
  functions across 39 resolved owners and 5,010 effects including cosmetic
  aliases. Compare raw Plate/Memory values/defaults under suppression,
  transformed and shared aliases, retained Roost typeWas, Normal fallback,
  added types, Tera/Stellar, no-handler paths, first-call empty membership and
  string/array membership branches, exact allocation/graphs, scope and RNG.
- 16,410 original Sandstorm/Snow defense transitions in 1,236 retained sequences:
  5,286 direct calls, 3,708 singleEvent calls, 3,708 native-collected runEvent
  calls and 3,708 getStat calls. The 6,284 original depth faults are included.
  Compare live Type mutations/aliases, empty fallback, added types, ordinary
  and Stellar Tera, Plate/Memory typing under suppression, Mega Sol caller
  effects, direct rounding before item chains, exact retained allocations,
  private rows, action queues, restored scopes and RNG. False weather predicates
  leave null relays untouched; native integer conversion occurs only if active.
- 8,082 original isGrounded calls across all 321 imported abilities and 509
  generated species/forms, targeted type/item/ability matrices and retained
  condition mutations. Include 1,152 original depth faults and 315 independent
  writes/deletes; compare bool/null outcomes, gravity/Ingrain/Smack Down presence
  with falsy values, early query skipping, Flying/???/Roost, added/Tera/Stellar
  and Plate/Memory types, item/Gas/Gastro Acid/breaker/Shield/actor suppression,
  Magnet Rise/Telekinesis/Air Balloon, exact graphs/allocations/scopes/RNG and
  unchanged private rows and action queues. Falsy Roost states test temporary
  initialized callback states without changing their dictionary entries.
- 4,988 original move-slot method/projection transitions in 259 retained
  sequences, including all 938 resolved move IDs and 15 raw target strings.
  Compare shared constructor records, retained PP/disable mutations, duplicate
  and cross-holder aliases, fresh virtual Transform slots with base PP caps,
  Hidden Power names, shallow base-array restoration and self-target ordering.
  Include 11 rejected diagnostic inputs. These comparisons cover only the
  move-slot stages of transformInto/clearVolatile.
- 10,192 retained persistent boost transitions in 52 sequences, including
  1,196 original boostBy/clearBoosts/setBoost calls, 104 Transform boost stages,
  104 clearVolatile boost stages and 52 shared-table assignments. Root-authoritative
  reads tolerate deliberately stale flat mirrors; all stages, ordered changes,
  clamping/deltas, retained cross-holder aliases, replacement/copy identity,
  property order, private state, queue and RNG are compared. Eight invalid
  diagnostic inputs are rejected. Parent methods' other stages remain pending.
- 131 stated laws (84 concrete, forty-seven universal) and imported domain definitions
  under `--verdict`.

These comparisons exercise varied seeds, Tera types, lead flags and accumulated
team details. They do not enumerate every possible team or random stream.
Damage fixtures use original Showdown event hooks to provide resolved numeric
inputs; listener fixtures use original `Battle.resolvePriority`. Discovery and
joined-event fixtures call original find*EventHandlers without replacing discovery;
the older isolated execution fixtures still substitute only supplied listeners.
Remaining executable callbacks and lifecycle processing are pending. Initialization fixtures
compare pre-start constructor state; they do not assert playable battle reset.
Both RNG modes have full-capacity queue fixtures. Handler discovery comparisons
do not prove reachable-state coverage, dynamic registration support or complete
effect lifecycles. Complete effect-state lifecycle integration and independent
playable battles remain unfinished.

The setup script's end-to-end check passes under the authorized 1,200-second
timeout, including project-local dependencies, pinned-source audit, oracle
transpilation and catalog/rule generation. Performance comparisons remain
deferred while the shared machine is busy.

The text/type-change increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=text,type_change,init,type_init,types,grounded,weather_defense,effect_store,collection,sodium,value_array,identity`.
All 120 laws checked (81 concrete, thirty-nine universal), C compiled, and all
twelve selected suites passed under the unchanged 1,200-second guard. The prior
constructor fixture was corrected to check cleared reset memory before its new
read-only text inspection writes diagnostic output. Retained sparse mutations
also exposed the need for dedicated index assignment instead of generic object
field writes; array mode 8 now preserves numeric key order and sparse length.
A literal expected MAX-length heap overwhelmed proof normalization, so the
boundary law states U32 length arithmetic; independent JS/native fixtures
additionally compare the full sparse boundary fields. Limits were unchanged.

The text fixture passed 32,838 independent JS transitions over 8,847 static and
1,389 dynamic strings. The type-change fixture passed 10,204 transitions in
266 retained sequences: 8,505 original setType/addType calls, 1,565 original
empty-string faults, six independent writes/pushes and 55 memberships. It
compares all 1,585 resolved species numbers, guard order, fresh string-input
arrays, retained array/baseTypes aliases, known/added/apparent snapshots,
sparse/null/undefined cells, private rows, queues, object/counter and RNG.
Constructor fixtures additionally check knownType/apparentType/baseTypes.

Op 56 implements the pinned default setSpecies path. Original ModifySpecies
discovery runs before any Pokemon mutation, including its depth check. The
resolved species supplies the shared type array, addedType and weight; the raw
species supplies the independent apparent-type string. Spread calculation uses
the retained set level, EVs, IVs and nature. Existing nonzero maxhp leaves HP and
baseMaxhp unchanged, even when the new species has a fixed maxHP. A zero maxhp
initializes all three HP fields. Ordinary changes replace baseStoredStats with
a fresh six-field spread, while Transform mode retains its old reference.
Both modes allocate the spread and mutate the existing storedStats object in
its own property order, then set speed from its spe property.

Resolved Species values use object namespace 5; other resolved effect values
use 16/17/18/19/20 for move/ability/item/condition/format and 21 for the empty
effect. These are immutable Dex identities, separate from heap objects (7) and
arrays (8). Unexpected executable ModifySpecies handlers fail through ordinary
native dispatch. Nondefault rules that produce modified/custom Species objects
are outside this default-format path. The fixture compares original discovery,
not a supplied empty handler list. Sparse or nonnumeric stored-stat tables,
arbitrary set mutation and all-state reachability remain proof obligations.
The source's stack-error log messages are not implemented by this diagnostic.
Transform mode here is setSpecies's flag; complete transformInto, formeChange,
updateMaxHp, ability/volatile lifecycles and public revelation remain pending.

A subsequent guarded `--test-existing --test --tests=value_array` passed the
expanded 4,093-operation/snapshot corpus with 907 retained objects, preserving
the previous 2,630-transition corpus before adding index writes and the real JS
last-index case. Native inputs/library stayed unchanged during that recheck.
That increment registered 38 default suites; it did not claim a full 38-suite
rerun. The initial text foundation separately passed all 113 laws and eight
selected suites, including the four formerly fixed-size transport buffers.
Full games, remaining lifecycles, workers, bots and training remain unfinished.

The species/stat increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=species_change,init,type_init,stats,sodium,collection,type_change,identity,weather_defense,weather,terrain`.
All 125 laws checked (82 concrete, forty-three universal), C compiled and all
eleven selected suites passed under the unchanged 1,200-second guard. The first
constructor run caught an omitted stat attachment in the single-Pokemon reset;
the fixed normal build passed both single and team constructor graphs, including
the distinct stored/base table identities and retained set level. Source stat
fixtures deliberately supply stale flat mirrors, proving that live stat and
collection speed reads use persistent objects. The expanded corpus preserves
all 4,944 original stat-helper calls, 1,536 boost callbacks and 768 boost fixtures.

The new 39th default suite passed 1,735 original setSpecies calls across every
resolved species/display record and 25 natures. These include 575 Transform-mode
updates and 50 original depth faults, retained/reordered stat objects, all old
spreads/types, fixed maxHP, zero-maxhp initialization, unchanged existing HP,
separate set/live levels, active/Tera contexts, default/explicit sources, exact
scopes/private rows/heap/queues and RNG. Observing wrappers only register arrays
and spreads at original call boundaries; discovery and methods remain original.
Transform/forms and complete games remain unfinished.

The subsequent guarded `node ocean/pokemon_gen9/build.cjs --test-existing --test`
passed all 39 registered default suites against the compiled native library.
Native inputs stayed unchanged through this full recheck. This established that
increment's helper regression baseline, alongside its normal build's 125 checked
laws; no complete independent battle or training smoke has passed yet.

The move-slot increment checked all 128 laws (82 concrete, forty-six universal),
compiled C and passed all seven selected constructor/PP/view/request/species/
identity suites. Its new fixture passed 4,988 original method/projection
transitions in 259 retained sequences: 283 Transform slot stages, 331
clearVolatile array stages, all 938 resolved move IDs, 15 raw target strings and
11 invalid diagnostic inputs. Original parent methods provide the expected
slot graphs; their other stages remain pending. Native input records are
supplied before actions, with no replacement of native post-action objects.

The subsequent guarded `node ocean/pokemon_gen9/build.cjs --test-existing --test`
passed all 40 registered default suites with unchanged native inputs/library
and the unchanged 1,200-second guard. This is the previous full helper regression
baseline.

The persistent-boost increment checked all 131 laws (84 concrete, forty-seven
universal), compiled C and passed all seven selected suites: boost_state, init,
stats, pokemon, sodium, species_change and move_slots (scope
`run-rec0615f9ce764cf58f9344d849560919`, exit 0, inactive/success). The new suite
brings the registered default count to 41; a full 41-suite rerun is not claimed.
Complete native games, worker integration, bots and training remain unfinished.
