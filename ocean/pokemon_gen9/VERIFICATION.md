# What completion and proof mean

The target is default singles `gen9randombattle` at Showdown revision
`9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e`. The native simulator and its
verification are both incomplete. Passing the current concrete Bend laws is
not a proof of equivalence to Showdown, state-space coverage or training readiness.

## Current evidence

Latest checked increment: the guarded normal persistent-boost build checked
131 laws (84 concrete, forty-seven universal), compiled C and passed all seven
selected suites: boost_state, init, stats, pokemon, sodium, species_change and
move_slots. The new fixture passed 10,192 retained boost transitions in 52
sequences, including 1,196 original mutation calls, 104 Transform boost stages,
104 clearVolatile boost stages, 52 shared-table assignments and eight rejected
diagnostic inputs. Stale flat mirrors, original object identity/property order,
retained aliases, private state, queue and RNG are checked. These are boost-stage
projections, not completed Transform/clearVolatile/Baton Pass parents. There are
41 registered default suites; a full 41-suite rerun is not claimed.

Previous full helper baseline: the guarded normal move-slot build checked 128 laws
(82 concrete, forty-six universal), compiled C and passed all seven selected
suites: init, sodium, pokemon, move_view, request, species_change and identity.
The new move-slot fixture separately passed 4,988 original method/projection
transitions in 259 retained sequences, including all 938 resolved move IDs and
15 source target strings. It compares the move-array stages of original
transformInto/clearVolatile; their other stages are not implemented by this
increment. The subsequent guarded `--test-existing --test` passed all 40
registered default suites with unchanged native inputs/library, including the
expanded slot fixture and its 11 rejected diagnostic inputs.

Previous complete helper baseline: the guarded normal species/stat build checked 125 laws
(82 concrete, forty-three universal), compiled C and passed eleven selected
suites. Its subsequent guarded `--test-existing --test` passed all 39 registered
default suites with unchanged native inputs/library. Single-Pokemon reset,
team construction, selected persistent constructor graphs and all existing
helper regressions passed after fixing the missing single-reset stat attachment.
The new fixture passed 1,735 original setSpecies calls, including 575 Transform-mode
updates and 50 original depth faults. These checks do not constitute a complete
transformInto implementation, playable battles or a source-equivalence proof.
Complete native Bend/C games and native-backend training runs remain zero.
The user paused that goal; its in-flight full helper regression scope
`run-r8360f7276ca8449baa97ddd84085f6fb` was stopped (process exit 143) before the
41-suite run finished. The source-worker backend below has separately completed
a real PufferLib/CUDA training smoke; that does not complete the native port.

| Area | Evidence | Remaining obligation |
| --- | --- | --- |
| Default team generation, both RNGs | Independent exact sets/teams and post-generation seeds; targeted generator branches | Universal source-to-model argument, including reachable numeric bounds and sampling order |
| Pre-start Pokémon construction | 509 individual constructors and 16 two-team assemblies; independent rows/stats/HP/PP, selected persistent constructor graphs, exact fields/aliases/creation order, party maps, newlySwitched, cached speed, shared type arrays, baseTypes/known/apparent snapshots, distinct stored/base stat objects and retained set level. Independent cache fixture covers 1,585 resolved species/display names, 1,480 source type-array identities, 409 retained sequences and 4,908 original getTypes calls | Remaining constructor state, frozen-array mutation safety, activation, start callbacks and initial requests |
| HP, PP, boosts, disabling, numerical damage | Independently compared primitives and concrete laws | Their complete callers, lifecycles, immunity and hit/event integration |
| Persistent Pokémon boosts | 10,192 retained transitions in 52 sequences; original mutations and projected Transform/clearVolatile/copyVolatileFrom boost stages, authoritative root 109, stale mirrors, source own-property order, shared/copied/replaced identities and eight rejected diagnostic inputs | Full parent methods, dense seven-stage reachable-domain closure, other lifecycle mutations and source correspondence |
| Callback catalog, aliases, ordering, suppression | Resolved Dex comparisons, 5,009 exact ID/display-name pairs and targeted interaction cases; Magic Room read from persistent field state and ending from persistent ability state | Complete lifecycles and all state changes between callbacks |
| Native handler discovery | 44,112 original find*EventHandlers cases with 46,649 listeners; exact state/holder/end identities, duration-only records, insertion order, bubbling/prefix exclusions, custom holders and indexed targets | Universal source correspondence, reachable bounds, activation/speed maintenance and residual end execution; dynamic registrations outside the default configuration |
| Scalar singleEvent/runEvent and array-target runEvent | Original source bodies with independent results, ordering, scopes and RNG; 768 complete native-collected runEvent calls, including onEffect allocation and faults | Complete effect lifecycles and all executable bodies; constructor/callback integration of persistent arrays |
| Executable function callbacks | Forty-three named bodies checked against original functions and singleEvent, including persistent Slow Start reads, boost mutations, Heavy/Light Metal and Float Stone, six weather stat bodies, Sandstorm/Snow defenses, three terrain stat bodies, Unburden and three Type functions; inventory in `PORTED_CALLBACKS.json` | All remaining reachable functions and interactions |
| Live Pokémon stats | 4,944 original calculateStat/getStat/getActionSpeed/updateSpeed/getBestStat/getWeight calls with original event discovery; 1,536 direct/singleEvent boost-body calls and 768 object/clamp fixtures; exact object order/aliases, private boost isolation, scopes, allocation and RNG | Remaining stat callback bodies (including type paths), reachable numeric bounds, activation/queue integration and automatic speed maintenance |
| Weather queries and modifiers | 48,384 original field/Pokémon weather queries and 6,144 full stat/speed/weight calls; all eight weather values, suppression and Umbrella/Magic Room, effect-sensitive Mega Sol, scopes, persistent state and exact RNG; 9,216 ending mutations with stale/conflicting flat flags, retained aliases and exact property order. 16,410 Sandstorm/Snow defense transitions in 1,236 retained sequences, including 6,284 original depth faults; direct/singleEvent/native-collected runEvent/getStat, nested Type mutations/aliases and direct rounding before item chains | Weather lifecycle/messages, remaining interactions and synchronization of other cached suppression facts with lifecycle mutations |
| Terrain queries and modifiers | 143,040 original scalar effectiveTerrain/isTerrain queries and 1,440 full stat/speed calls, including 95 nested depth faults; all five terrain values, explicit/inherited/falsy targets, suppression, scopes, persistent heap and RNG; complete empty TryTerrain inventory across 4,959 resolved effects | Terrain lifecycle/messages and remaining terrain interactions; indexed query targets outside the current scalar diagnostic |
| Persistent effect objects | 2,583 original helper/field transitions, 1,560 event/mutation transitions in 78 persistent sequences, selected constructor roots/maps, retained native aliases and listener-state snapshots; universal clear/property-order/initialization/raw-allocation laws | Remaining constructor/lifecycle integration, reachable bounds, discovery across lifecycle mutations and worker isolation |
| Indexed persistent arrays | 4,093 independent JavaScript operations/snapshots across 907 retained objects, retaining the previous corpus before adding index assignment and the real JS last-index case; sparse/dense cells, holes versus present undefined, fresh filter/concat/copy results, cyclic/shallow aliases, exact property order, allocation, counter and RNG; ten array laws, including five universal properties | Remaining constructor/callback integration, full reachable bounds and source correspondence; named properties, accessors and prototype overrides outside the current admitted model |
| Type events and live types | 3,591 original transitions: direct/singleEvent bodies, native-collected Type events, getTypes/hasType and intervening shared-array writes; includes 276 original depth faults. Three original functions across 39 resolved owners in a 5,010-effect inventory; raw item typing, transformed/shared aliases, Roost typeWas, fallback, added type, Tera/Stellar, no-handler paths, first-call empty/string/array membership, allocations, exact retained graph, scopes and RNG. Constructor fixtures separately check source sharing/frozen/nonempty inventory and native cache construction | Frozen-array mutation enforcement or reachable mutation-safety proof, Transform/forms and remaining lifecycle/stat callers, full reachable bounds and source equivalence |
| Immutable primitive texts | 32,889 independent JS transitions across 8,864 static and 1,389 dynamic texts; UTF-16 contents including surrogates/NUL, full collision checks, canonical IDs, retained snapshots, exact object/counter/private/RNG preservation and reset | Universal interning/source relation, bounds, all remaining coercions and worker ownership |
| Type changes | 10,204 transitions in 266 retained sequences, including 8,505 original setType/addType calls, 1,565 original empty-string faults, six writes/pushes and 55 memberships; every resolved species number, guard order, retained array/baseTypes aliases, fresh string arrays, known/added/apparent snapshots and exact heap/private/queue/RNG | Transform/forms/Tera/other lifecycle callers, full reachable-state invariants, frozen-write safety and universal source correspondence |
| Species changes and persistent stats | 1,735 original setSpecies calls across all 1,585 resolved species/display records and 25 natures; 575 Transform-mode updates, 50 original depth faults; original empty ModifySpecies discovery, default/explicit source, retained set level, HP initialization/maxHP, shared types, ordered in-place stored stats, fresh/retained base tables, cached speed and retained graphs/scopes/private/queue/RNG. Existing stat helpers use persistent roots despite deliberately stale flat mirrors | Complete Transform/forms/updateMaxHp and ability/volatile/public lifecycles, error log messages, full numeric/object/reachability invariants and universal source correspondence |
| Persistent move slots | 4,988 original method/projection transitions in 259 retained sequences; 283 transformInto slot-copy stages and 331 clearVolatile array-restoration stages; all 938 resolved move IDs and 15 raw target strings, constructor base/current shallow aliases, PP exhaustion/used, duplicate matches, true/hidden disabling, retained/copied/cross-holder records, stale-mirror getters, self-target order and exact heap/counter/private/queue/RNG. Three universal field-presence laws | Remaining Transform/clearVolatile stages, complete textual requests and lock/Type discovery, other move replacements, reachable dense-four-slot/U32-number closure and universal source correspondence |
| Battle loop and player observations | Source contract and partial resolved request primitives | Complete independent games, visibility, choices, turn/switch/faint/residual processing |
| Grounded state | 8,082 original isGrounded calls in 1,242 retained sequences; 321 imported abilities, 509 generated species/forms, 315 dictionary mutations, 1,152 original depth faults; bool/null distinctions, early returns, condition presence despite falsy values, live types/aliases, repeated Arceus/Silvally Type events, suppression/actor/item paths and exact retained graphs/scopes/RNG | Immunity, hazard and terrain callers/lifecycles; reachable numeric/collection/state invariants and universal source correspondence |
| Training | Architecture/integration contract in `TRAINING.md` | Native worker ABI, bots, independent evaluation and real training smoke |

The older isolated runEvent tests replace **only outer-event handler discovery** in the source
to exercise its original post-collection execution; nested events use original discovery. New discovery and joined-event
tests call the original find*EventHandlers methods without replacing discovery.
Neither establishes universal collection completeness. The shared native diagnostic bridge is serialized;
it does not establish multiple-worker correctness or battle throughput.

On October 5, 2026, `node ocean/pokemon_gen9/build.cjs --test` checked 92 laws:
66 concrete checks and twenty-six universal scope/relay/object/stat/weight/weather/
suppression/terrain properties, and compiled the native library. Corrected
isolated fixtures connect foe sides, stub only the outer event's discovery,
and leave nested discovery original. Subsequent guarded `--test-existing --test`
and selected regression runs passed all 31 default suites against the compiled
library under the unchanged 1,200-second guard. Native inputs stayed unchanged
through compilation and the subsequent regression runs. Source stack faults
in the scalar/vector/terrain fixtures require the original explicit stack-overflow
error; nested failures also compare persistent partial mutations and RNG.
The added laws clear only cached ending state, preserve unrelated flags, respect
truthy object endings and preserve explicit/inherited terrain targets.
Fixtures preserve pre-action source inputs independently, including mutable relay
arrays and sparse/full boost objects. Universal laws quantify over arbitrary
typed values/references, rather than only fixtures.
Neither category establishes the source-to-model relation described below.

The later guarded command
`node ocean/pokemon_gen9/build.cjs --test --tests=value_array,effect_store,effect_scopes,stats,weather,terrain`
checked all 99 laws (69 concrete, thirty universal), emitted C, compiled the
native library and passed all six selected suites. The array fixture adds a
32nd default suite; this increment does not claim a full 32-suite rerun. Its
source comparison uses actual JavaScript builtins; it does not establish
complete Pokémon type handling.
U32 length-boundary reads are supported without allocating all cells. Growth
beyond that limit returns a native domain error and is explicitly outside the
comparison contract: JavaScript push can write a named property before its
overflow exception. No reachable-game proof presently excludes that boundary.

The Type increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=types,value_array,callbacks,run_event,vector_event,listener_priority,identity,registry,collection,stats`.
All 104 laws checked (72 concrete, thirty-two universal) and the library
compiled. Six selected suites passed before the new Type fixture's syntax and
transport-encoding errors. After correcting only that fixture, guarded
`--test-existing --test --tests=types,identity,collection,stats` passed the
remaining four selected suites; native inputs/library were unchanged during
those rechecks. This adds a 33rd default suite without claiming a full rerun
of all 33. The Type fixture observes original initEffectState/runEvent/getTypes
allocation outputs, while retaining original discovery and callback functions.
It compares all selected persistent graph nodes after each independent action.
Initial shared mutable type arrays are prepared fixture inputs, not evidence
of completed native constructor species-array ownership or frozen-source
mutation behavior. Native construction left type roots unpopulated at that increment;
the subsequent constructor increment described below populates them.
A subsequent guarded `--test-existing --test --tests=types` passed the expanded
3,591-transition fixture, including no-handler shared-array fallback, first-call
empty membership side effects and the original string/array hasType branches.

The constructor-array increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=init,type_init,types,identity,registry,generator,teams,sodium`.
All 105 laws checked (73 concrete, thirty-two universal) and the library
compiled. Five selected suites passed before the Sodium reset fixture's old
empty-cache expectation failed. Correcting only that fixture to check the
exact populated cache root and clear deliberately dirty other cache entries,
guarded `--test-existing --test --tests=sodium,types,identity` passed the three
remaining suites. Native inputs/library were unchanged during that recheck;
the authorized 1,200-second guard remained unchanged. This adds a 34th default
suite without claiming a full rerun of all 34. Native construction now attaches
type roots and preserves the actual source sharing groups. Full constructors
and independent cache finalization compare exact retained graphs and original
getTypes calls. The resolved source inventory checks frozen/nonempty arrays;
native arrays do not enforce frozen writes, and no complete reachable-state
mutation-safety proof has been established.

The weather-defense increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=weather_defense,stats,weather,types,callbacks,run_event,vector_event`.
All 106 laws checked (74 concrete, thirty-two universal), the native library
compiled and all seven selected suites passed under the unchanged 1,200-second
guard. The new suite adds a 35th default suite without claiming a full rerun of
all 35. Its 16,410 transitions compare original functions and discovery,
retained graph mutations/aliases/allocations, private rows, preserved action
queues, scope restoration on success, RNG and matching nested depth faults.
All imported types/eight weather values and targeted Roost, shared empty
arrays, added types, Tera/Stellar, raw Plate/Memory, suppression, Mega Sol caller
effects and item-chain paths are included. This is targeted correspondence
evidence, not exhaustive reachable-state coverage or universal source equivalence.

The grounded-state increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=grounded,types,weather_defense,collection,effect_store,effect_scopes,suppression`.
All 110 laws checked (76 concrete, thirty-four universal), the native library
compiled and all seven selected suites passed under the unchanged 1,200-second
guard. The new suite adds a 36th default suite without claiming a full rerun of
all 36. It compares original isGrounded and Type operations, with retained
dictionary writes/deletes rather than importing post-action state; all selected
objects, allocation/counter, bool/null results, scope restoration on success,
private rows, action queues, RNG and 1,112 matching source faults are checked.
Nonduration collection now permits falsy state values, including temporary
initialized Roost callback states. Truthy primitive condition states, primitive
duration reads and arbitrary prototype changes remain outside the admitted
model; reachable-state invariants and complete source equivalence remain unproved.
A subsequent guarded `--test-existing --test --tests=grounded` passed additional
Arceus/Silvally cases without Roost, checking fresh allocations for both the
Flying and ??? Type queries. Native inputs/library remained unchanged. The
expanded suite totals 8,082 queries, 1,242 sequences and 1,152 original faults;
the 315 independent dictionary mutations remain included separately.

The text/type-change increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=text,type_change,init,type_init,types,grounded,weather_defense,effect_store,collection,sodium,value_array,identity`.
All 120 laws checked (81 concrete, thirty-nine universal), C compiled and all
twelve selected suites passed under the unchanged 1,200-second guard. Earlier
checks stopped on a constructor fixture's inspection-before-reset-check ordering
and on sparse index writes sent through the generic object writer. The former
was corrected without weakening the clear-state check; the latter required a
native indexed-assignment operation. A boundary law now checks U32 length
arithmetic rather than expanding MAX into a literal Nat heap; original JS/native
fixtures additionally check actual sparse last-index fields. No limits changed.

The new text fixture passed 32,838 JS transitions over 8,847 static and 1,389
dynamic strings. The type-change fixture passed 10,204 retained transitions,
including 8,505 original method calls and 1,565 matching empty-string faults.
Source methods are original. Explicit species/Tera/known edits only prepare
fixture inputs and do not demonstrate native form/Tera lifecycle transitions.
Array aliases/baseTypes and immutable apparent strings are compared separately
after later writes/pushes. Constructor fixtures also check all three new fields.
A subsequent guarded `--test-existing --test --tests=value_array` passed 4,093
operations/snapshots across 907 retained objects, preserving the entire older
corpus before adding assignments and the real JS boundary case. Native
inputs/library remained unchanged during the recheck. Two new suites bring the
registered default count to 38; a full 38-suite rerun is not claimed.
The initial text foundation also passed all 113 laws and eight selected suites,
including native/events/queue/damage with buffers sized from catalog metadata.
Join admits string cells and holes/null/undefined; other element coercions,
prototype overrides and frozen writes remain separate obligations. String IDs
are a serialization contract, not JavaScript object identities. Full state/source
equivalence, compiled-C correctness and multiple-worker isolation remain unproved.

The move-slot increment ran
`node ocean/pokemon_gen9/build.cjs --test --tests=init,sodium,pokemon,move_view,request,species_change,identity`.
All 128 laws checked, C compiled and all seven selected suites passed. The
first new slot fixture assumed every move request carried a target; correcting
the projection to preserve the original omitted-field cases fixed that fixture
without changing native mechanics. Guarded selected rechecks passed first the
retained alias cases and then the expanded inventory of every resolved move.
The subsequent full `node ocean/pokemon_gen9/build.cjs --test-existing --test`
passed all 40 suites (scope `run-rfe6a0f36007c4536a1e45e46b9cf8744`, exit 0,
inactive/success) under the unchanged 1,200-second guard. Native inputs/library
remained unchanged throughout the full run.

The slot fixture calls original PP/disable/getMoveData/getMoves methods and
original transformInto/clearVolatile. For the latter two, only their retained
move records/arrays are projected and compared; the native operations implement
only those stages. Their other mutations, callbacks, allocations and messages
are not evidence of completed native parent methods. Move-view comparisons use
the existing numerical request projection; complete textual requests and live
lock/Type discovery remain obligations. Source input records are imported only
before actions, and existing references cannot be overwritten by that importer.
Dense arrays of at most four slots and nonnegative whole U32 PP inputs remain
an admitted domain whose reachable closure is unproved. Invalid diagnostic
array installation can assign its root before later field validation fails;
the oversized-length rejection checked here occurs before assignment.
Catalog schema 12 occupies 401,274 words and preserves all previous static text
IDs while appending exact target/slot strings. Its current binary SHA256 is
`f7d2255b22e80e800a9994f3ead682adb602671d28d8dfca5c69c527c163b50c`.

The subsequent persistent-boost build ran
`node ocean/pokemon_gen9/build.cjs --test --tests=boost_state,init,stats,pokemon,sodium,species_change,move_slots`.
All 131 laws checked, C compiled and all seven suites passed (scope
`run-rec0615f9ce764cf58f9344d849560919`, exit 0, inactive/success). An earlier
invalid-input fixture exposed sharing an uninitialized target table; native
share now rejects its zero root before mutation, and the unchanged check passes.
Stored-stage readers admit seven whole stages in -6..6; temporary ModifyBoost
relays retain their broader clamp contract. Reachable closure of that stored
domain remains unproved. Sharing/copying malformed nonzero diagnostic roots is
not a general atomic-validation guarantee.

## Original-source runtime sidequest

The guarded `--js-pack-only` trial passed exact summaries and complete-log SHA256
comparisons for 32 original Showdown games under Node 24.18.0 and a Bun 1.4.2
executable, with identical seeds and diagnostic clock inputs. It covered 1,070
turns and 2,456 actor decisions, including three hidden-trap choice retries and
six Terastallizations. Scope `run-r778f24a9ba714321a214732d67536da8` ended with
exit 0, inactive/success. Node measured 509.20 decisions/sec and Bun 500.73 in
short single-worker one-CPU shared-machine trials. This is sampled runtime
agreement, not universal equivalence or a reliable performance comparison.
The Bun executable embeds its JS runtime and loads an external copied source
tree; it is not native AOT. Static Hermes AOT has not been tested here.

These games execute all original rules encountered by the source bots; they
do not exercise native Bend/C transitions. They include original generation,
requests/logging, bot decisions and hash construction, while excluding the
PufferLib encoder/transport, GPU learner, worker scaling and training. They
therefore establish feasibility of direct source-engine execution only.

The guarded `--js-profile-only` run passed 128 measured complete source games
and exact baseline-prefix comparison (scope
`run-rb917929784294cb39c033e35df688019`, exit 0, inactive/success): 4,435 turns,
10,121 actor decisions and 21 Terastallizations. The sampling profile includes
startup, data loading and warmup as well as the measured games; its timings
are not a runtime speed comparison. getCallback's 32.48% sampled self time
motivates investigating dispatch without assuming immutable runtime callbacks.

## Source-worker training evidence

`--worker-test-only` passed 128 original-source games, 4,435 turns and 10,121
decisions, with three original hidden-trap retries. Every complete log matched
the prior independently run source benchmark. Per-player views receive only
their filtered log channel and their own request; tests check finite features,
unknown opponent bench, private-state independence, Illusion species/level
disguise, forced wait, Struggle and revival choices. A Recharge request lacking
ordinary move power/accuracy exposed nonfinite features; the encoder now marks
Recharge explicitly and preserves absent numeric data as zero. Scope
`run-r47c3b5a2b10f48a2b290541ecb23b21e` exited 0, inactive/success.

`--worker-transport-only` compiled the real PufferLib C environment interface
and completed 32 games/3,129 decisions in 25.295 seconds under the one-CPU guard.
It checks masks, every float observation, zero-sum terminal rewards and automatic
reset. Scope `run-r813001e73b224b539f2591162d2196b4` exited 0, inactive/success.

The actual PufferLib trainer build succeeded in scope
`run-r42f01a500ae8417c8cfaf031d823b48f`. The first successful training smoke in
`run-r55e800feab5b4b14b85ca39ad4ed3ee1` completed 1,024 agent steps, eight PPO
updates and four final evaluation games. All 3,457,728 final weights and saved
metrics were finite; checkpoint weights changed after the first update.
`verify_worker_smoke.py` checks these artifacts. The initial final checkpoint
SHA256 was `bbe9dd8aec29d6adca3c2eecd6eebbb30bdceca4344d7c9bb9afb9845b69c5ad`.
The reproducible smoke command writes its transcript and runs that verifier;
`build/pokemon_gen9/worker/training-smoke.json` identifies the latest checked run.
The complete command was rerun in `run-r8d3f7b404ded41ad8914271880a2e2a9`,
which exited 0, inactive/success. Run `1791233371994` reproduced the same final
checkpoint hash and passed the automatic artifact verifier. The two short runs
reported final rates of 75.29 and 67.05 agent steps/sec under the one-CPU guard;
these are integration smoke measurements, not sustained throughput benchmarks.

This establishes source-engine/PufferLib training integration, not trained
playing strength, exhaustive public-observation completeness, native mechanics
conversion, universal runtime equivalence, efficient scaling or a strong bot
ladder. The first wide one-hot encoder and current-policy self-play are baseline
choices for starting training; the CPU environment uses the original rules.

Cancellation was subsequently repaired with a named resource scope and an
asynchronous launcher. The launcher handles SIGINT/SIGTERM by stopping the scope;
its detached flock process holds the resource lock until the scope exits. A real
terminal Ctrl+C during the live C transport run exited 130 and left scope
`pokemon-gen9-433066-a3cb16bb-f685-45ec-8a4b-d9e31b6790c0.scope` inactive/success,
with the launcher, harness and Node worker all gone. A separate
`--worker-stop-only` invocation stopped the next live transport scope and its
command exited 143. The subsequent guarded profile completed successfully,
also confirming the resource lock was available again. Resource limits and
the 1,200-second deadline were preserved.

The CPU-only worker profile completed the same 32 games, 3,129 decisions and
433 steps as the prior transport fixture, with finite observations and valid
terminal/reset/mask checks. Its packet is 215,292 bytes per actor. Sampling
attributes 21.75% to `writeBuffer`, 11.87% to `getCallback`, and 7.52% to the
profiling `cpuUsage` calls themselves. Worker phase timings report 11.40 CPU
seconds in rules/public-client stepping, 2.97 in writes, 1.04 in observation
encoding and 0.88 in resets. These measurements include instrumentation and
startup effects and do not establish uninstrumented throughput. The full
report is `build/pokemon_gen9/worker/worker-profile-summary.json`.

Following the user's request for substantially more agents and CPU use, source
runtime commands now receive `100 * availableParallelism()` percent CPU quota;
build/check commands retain 100%. The same lock, memory cap, no-swap limit,
task limit and 1,200-second timeout remain. On this host the runtime quota is
400%, matching its four exposed cores. Native conversion remains paused.

The CPU-only scaling scope `pokemon-gen9-433641-b00b2087-ccc3-4eea-ba0a-d81733e6fd3b.scope`
completed five cases from eight to 256 agents. Four workers/128 agents reached
852.32 rows/sec, with 3.90 average busy CPU cores including startup. Eight
workers/256 agents reached 703.09 rows/sec and 3.94 average busy cores. Every
boundary checks masks and zero-sum terminals; finite observations are sampled
at the first and every sixteenth boundary in this performance harness. This
does not replace the earlier full-observation/source-parity tests. Workload
seeds, episode counts, startup and process CPU accounting are recorded in
`build/pokemon_gen9/worker/scaling.json`; rates include source rules, public
client projection and dense transport, and are not a pure simulator ceiling.

The real 128-agent/four-worker PPO run in
`pokemon-gen9-434084-a375cc51-cd8c-466e-9cc2-7ea4f3a89b15.scope` exited 0 and
passed `verify_worker_batch.py`: 16,384 steps, 32 updates, four finite and changing
checkpoints, all saved metrics finite. The final checkpoint is in run
`1791240807662`. Its final reported rate was 580.35 agent steps/sec; 16,384 steps
over 39.82 reported seconds average about 411 steps/sec including early warmup.
This throughput run used horizon=4 and no final evaluation; it does not replace
the original horizon=16/evaluation smoke. Results are stored separately in
`build/pokemon_gen9/worker/batch-training.json`. No shared learner source was
changed. In inspection, its CPU worker/main handshake contains busy-wait loops;
blocking waits remain an untested optimization candidate.

## Earlier trace-driven batching prototype

`batch_compile.cjs` reads methods/handler bodies from the clean pinned
TypeScript source with TypeScript 5.9.3 (exact dependency and integrity lock).
It lowers a restricted numeric/primitive-field subset into C with explicit
JavaScript unsigned truncation and signed right-shift intrinsics. The manifest
records original body hashes, input fields, static helper signatures and all
rejections. It generated 43 ability handlers and nine helper specializations;
448 other ability callback methods were rejected or outside this experiment's
numeric event selection. These counts are not mechanics coverage estimates.

The guarded `--batch-test-only` run in scope
`pokemon-gen9-438902-54a63bd5-e9d6-4abf-8ac0-b44bbec96bf8.scope` exited 0.
Instrumented source execution preserved full original log hashes and decision
counts for all 128 games: 4,435 turns and 10,121 decisions. Recorded primitive
and compiled-handler calls plus synthetic edge/conditional/persistent-chain
fixtures produced 320,418 operations over 256 lanes. Both native scheduling
orders matched source return tags/numeric values and event modifier mutations.
All 52 kernels received explicit synthetic comparison inputs. Numeric tests
include signed zero, nonfinite values, unsigned overflow and signed shifts;
finite outputs are not assumed when the original helper legitimately returns
nonfinite values on synthetic inputs.

Largest-ready-group selection used 64,742 batches (mean 4.949 lanes); rotating
operation selection used 9,762 (mean 32.823). Both reached 256 lanes in a batch
and matched all results. Replay keeps a separate per-lane continuation position
and frame modifier table. It explicitly reports 160,567 initial reference frame
imports and 127 unconverted frame resets. Inputs and unsupported transitions
are still supplied by the source, so this is **trace-driven differential
validation**, not autonomous whole-battle equivalence or training readiness.
No throughput speedup is inferred from these batch statistics, and there is
no formal proof that the frontend preserves every JavaScript behavior.

Artifacts: `build/pokemon_gen9/batch/manifest.json`, `kernels.c`, `kernels.so`,
`trace.bin`, `replay` and `validation.json`. The report pins source IR hash
`621e6b4df6d13328a56c7732b926373f9539c1871f3b2328e3bfeed75bbba94a`;
the compiled library exports that hash and replay rejects a mismatched trace.
The native Bend/C full-conversion goal remains paused. The autonomous source
continuation backend below now drives live battles without those captured
inputs. The older hash/counts above describe the initial replay milestone;
the latest numeric compiler report uses 56 kernels and 320,930 operations.

## Autonomous source continuations and compact CUDA training

The supported format is the pinned default `gen9randombattle`. Source
constructors, getters, generators, resets and non-admitted semantics remain
JavaScript. A separate compiler creates resumable counterparts for simulator
and data methods, preserving the original synchronous functions. Nested
action/event calls suspend at admitted numeric operations; each batch contains
independent live battles. The runtime uses per-lane event objects and copies
modifier state back only when the C operation actually writes it. There are
56 admitted kernels: 43 ability handlers, nine math helper specializations and
four source-derived compound stat/damage expressions. Manifests include source,
frontend, runtime and bridge hashes. No captured operation inputs or imported
frame transitions drive autonomous games.

The default Gen 9 dependency analysis excludes other generations/mods, leaves
unregistered imported free functions scalar, and specializes constant boolean
arguments and known pure comparators. It retains continuations for reassigned,
shadowed, destructured, spread/rest or otherwise unknown arguments. This avoids
suspending lookup/sort functions that cannot reach a native operation. Tests
cover those distinctions, call/argument evaluation order, optional short
circuiting, sparse callbacks, apply/bind, exceptions and concise-method
constructibility/property shape, including an own `__proto__` method. This is finite compiler
qualification, not a formal frontend proof or support for arbitrary JavaScript.

The game comparison checks both private requests, masks, commitments, PRNG
state, full ordered logs and both public actor observations after every
decision. Original terminal logs are compared to independent prior game hashes
under rotating and largest-ready-batch scheduling. Targeted cases additionally
exercise Illusion, hidden-state independence, Transform, Terapagos Stellar,
multi-hit moves, Revival Blessing, weather/nested events and Trick Room.

ABI 2 packs the existing encoder's 53,819 features into 1,775 float32 values.
Numeric values remain exact; one-hot categories use IDs and sets use complete
16-bit words. Every compared packed observation decodes byte-exactly to the
original public encoder. The C wire checks the ABI and schema hash. The CUDA
encoder selects the original dense weight columns and expands only a PPO
minibatch. The independent encoder test passed all 269,095 expansion values,
80 sparse-forward values (maximum error 9.09e-7), dense forward (1.42e-7),
861,104 weight-gradient values (5.96e-8), and CUDA graph replay. Floating-point
summation order can differ; policy output is checked with numeric tolerance.

Training qualification checks all saved weights and metrics for finiteness,
checkpoint changes, step counts and the logged ABI/schema identity. The smoke
also completes four final evaluation games. The larger throughput run disables
final evaluation explicitly. Neither establishes playing strength. The separate
environment/config/checkpoint namespace preserves the original source backend.

Artifacts under `build/pokemon_gen9/continuation/`: `manifest.json`,
`validation.json`, `mechanics.json`, `benchmark.json`, `scaling.json`,
`smoke/validation.json` and `train-bench/validation.json`. Read each report's
source hashes and workload before comparing rates. The matched action-tape
benchmark includes rules, public clients and both observations, excluding
startup, generation, IPC and PPO; scaling includes C/worker transport; PPO
reports actual training rates. Payload reduction is not a CPU speedup claim.
Complete native state/execution and the requested throughput target remain
unestablished, as does an all-state correspondence proof.

Most native battle mechanics and the complete native training environment remain to be
implemented. A completion percentage cannot be inferred from imported catalog
entries, test counts or law counts. The executable callback manifest currently
contains only 43 named function bodies, mostly stat modifiers; importing a
callback descriptor does not implement its body. No complete native game has
been established. Formal equivalence is a separate, larger obligation than
finishing and independently testing the simulator.

## The all-state proof obligation

An equivalence proof requires an explicit relation `R(source, native)` between
the original state and the native state. For every state admitted by the
reachable-state invariant, every legal choice and every corresponding RNG
transition, the original transition and native transition must preserve `R`
and yield the same observable results. The obligations include:

1. **Initialization:** every team/battle seed produces related starting states.
2. **Source coverage:** inventory every reachable engine method, function
   callback, data branch and state field. Close over called/borrowed moves,
   acquired abilities/items, species/form changes and nested conditions.
   Lexical inventories and sampled teams do not prove reachability closure.
3. **State invariants:** prove native numeric/collection bounds and persistent
   identity rules from initialization and preservation. Each diagnostic bound
   needs a reachability proof before being treated as a simulator bound.
4. **Transition equivalence:** quantify over related states, action arguments
   and RNG draws, including null/undefined/false/zero distinctions, ordering,
   intermediate mutations, suspended choices and rare branches.
5. **Composition:** prove that the event scheduler, callback continuations,
   switch/faint processing and requests preserve the relation together.
6. **Public projection:** prove that related states produce corresponding
   per-player requests and revelations. Private-state changes cannot change an
   actor view before source-authorized revelation.
7. **Termination and outcomes:** account for errors, win/loss/tie, the source
   turn cap and truncations; preserve rewards and action/loss accounting.
8. **Transport:** separately establish that C serialization, ownership, worker
   isolation and learner buffers implement the verified Bend interface.

Bend's kernel can check laws about Bend definitions. A claim about the original
TypeScript additionally needs a checked translation or a formal semantics of
the relevant source, including JavaScript numeric behavior. Hand-written laws
about the native model alone cannot supply that connection. Current bounded
rational/integer emulation also needs proofs that all reachable source values
fit its domains; arbitrary JavaScript numbers are not automatically represented.

Exhaustively enumerating every battle is infeasible. Universal transition laws
can cover entire state classes without enumerating them, but only after those
classes, their invariants and source correspondence have been established.
Concrete laws remain useful regression checks, not substitutes for universal laws.

## Independent evidence alongside proofs

Keep original Showdown execution as an independent oracle. Compare after
actions and observable event/request boundaries without copying oracle state
back into native state. Use upstream fixtures, targeted interaction matrices,
seeded complete games, randomized state/action generation constrained by
reachability, and mismatch reduction. Track uncovered handlers/branches
explicitly and fail on unsupported native execution.

Coverage measurements tell us which code or outcomes were exercised; they
cannot alone demonstrate no missed states. A complete simulator validated by
these methods is a distinct milestone from a formally verified equivalent
simulator. Neither milestone has been reached yet.
