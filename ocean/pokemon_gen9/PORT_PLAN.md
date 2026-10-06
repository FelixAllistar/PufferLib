# Gen 9 Random Battle Bend port plan

Port `gen9randombattle` into stock CPU Bend 2, including the original team
generator. Compile the mechanics through Bend's C backend and connect them to
PufferLib's CPU environment interface and CUDA learner. Both training and policy
evaluation select actions without search.

This document maps the upstream implementation and defines the port order.
Native generator modules, integer math, RNG, catalog import, C transport,
pre-start construction, queue/listener ordering, callback lookup, both RNG
streams, numeric damage and persistent Pokémon primitives now exist.
Scalar/array event outcomes after collection, typed scope restoration,
actual listener metadata and forty-three executable callback bodies also pass
original-source comparisons; the manifest records the supported bodies.
Persistent effect objects pass helper, mutation, shallow-alias and event-scope
comparisons. Function runEvent holders bind into persistent state; singleEvent
enters supplied state without target rebinding. Selected constructor effect
states, condition maps, party identities and exact effect ID/display-name text
also pass source comparisons. Remaining constructor state, activation and
complete effect lifecycles remain pending.
Native holder discovery now passes original source comparisons, including
duration-only end continuations, scalar bubbling and indexed targets. Joined
runEvent calls include raw onEffect insertion and exact persistent state/RNG
comparisons. Residual countdown/end execution and remaining callback bodies
are still pending.
Live Pokemon.calculateStat/getStat/getActionSpeed/updateSpeed/getBestStat/getWeight now
pass original-source comparisons with native event discovery. Ordinary boost
objects preserve sparse/full copies, ordered writes and aliases for Unaware,
Foresight and Miracle Eye. getBestStat preserves the source's repeated reads.
Read-only field/Pokémon weather queries and six weather stat modifiers now pass
independent source comparisons, including Umbrella, suppression and effect-sensitive
Mega Sol. The three weight bodies and Unburden's stat condition are executable.
Scalar terrain queries and Surge Surfer, Hadron Engine and Grass Pelt stat bodies
now pass source comparisons, including nested depth faults and suppression.
Persistent abilityState.ending overrides stale flat flags between callbacks.
Persistent arrays, Roost/Arceus/Silvally Type functions and live getTypes/hasType
also pass original-source comparisons with retained aliases and exact allocation.
Species-array constructor sharing also passes original-source comparisons;
setType/addType are implemented with immutable apparent strings; Transform/forms,
other type-changing lifecycle callers and frozen-array mutation safety remain pending.
Sandstorm/Snow defense bodies now query live types first and apply priority-10
direct modifiers before item chains. Remaining stat callbacks, weather/terrain/effect lifecycles, live activation/
queue callers and speed maintenance across lifecycle changes remain pending.
Grounded queries now preserve original bool/null outcomes, condition presence,
live type mutations, item/ability suppression and early returns. Their immunity,
hazard and terrain callers remain to be integrated.
Implementation and validation status are recorded in
[README.md](README.md). Battle activation, effect execution and the PufferLib
environment remain to be implemented.

The JS compilation sidequest has a separate measured source-runtime path:
direct pinned Showdown games run without poke-env/server transport, and a Bun
executable matched 32 complete source-game logs. Packaging produced no speed
gain in that short trial (about 501 versus 509 actor decisions/sec on one CPU).
Actual native AOT via Static Hermes is untested and needs an adapter for dynamic
data loading and Node host functions; untyped AOT has no promised throughput
gain over V8/JSC. See README's runtime experiment for reproducible commands.
The user paused the native conversion goal. The source-engine route now has a
player-visible encoder, action masks/retries, terminal accounting and batched
binary worker transport connected to PufferLib. It passed 128 complete log
comparisons, 32 C-transport games and an actual 1,024-step/eight-update CUDA PPO
smoke with finite changing weights and four evaluation games. See README for
the guarded build/train commands. A lossless smaller encoder and a source
continuation backend are now implemented; stronger independent evaluation
remains separate work. Retain the
native port separately; source training does not complete that paused goal.

**Batched execution redesign requested after the scaling experiment.** The
initial trace-driven prototype has become an autonomous source continuation
backend. The compiler emits generator companions from original simulator/data
methods; ready operations execute in in-process C batches, with independent
source objects and event scopes per battle. C covers 43 ability handlers, nine
math helper specializations and four compound stat/damage expressions.
Non-admitted operations retain their original source behavior. The player-only
encoder packs its original 53,819 features losslessly into 1,775 float32 values;
the CUDA policy uses categorical lookups and dense expansion only for PPO
minibatches. The source backend is trainable, while most state and execution
remain JavaScript. README/VERIFICATION document exact qualification and rates.
This does not resume or complete the paused native conversion goal.

The user wants batching inside the simulator. Four exposed cores, 128 agents and four workers
measured 852 actor rows/sec through the client/transport; 256 agents did worse.
The 128-agent PPO run passed 16,384 steps but remains far below the target.

The live continuation scheduler is implemented. Further native conversion
would replace remaining complete-battle object execution with numeric state
arrays and lower the remaining operations. Advance each battle only to its next
operation, collect independent lanes ready for the same operation/handler, and
execute a compiled batched kernel. Preserve order within each battle while
allowing independent battles to advance in different groups. Precompute static
effect/event metadata; retain live suppression, duration, holder and ordering
checks. RNG state/consumption and nested event scopes remain per battle.

| Source point | Proposed change |
| --- | --- |
| `sim/battle.ts:2938` turnLoop / `:2665` runAction | Explicit per-battle program counters and suspended action/event frames; schedule ready lanes by operation. |
| `sim/battle.ts:758` runEvent / `:1035` findEventHandlers / `:1018` getCallback | Numeric effect/event/handler IDs, compact live listener lists, and ordered continuation frames; group only independently ready callbacks. |
| `sim/pokemon.ts:560` calculateStat / `:596` getStat | Batched stat/boost arrays and kernels, suspending at callbacks and resuming at the original arithmetic stage. |
| `sim/battle-actions.ts:1585` getDamage / `:1724` modifyDamage | Batched arithmetic phases with original fixed-point rounding, branches, callbacks and RNG draw order. |
| Base `data/moves.ts`, `abilities.ts`, `items.ts`, `conditions.ts` | Keep original definitions as input. Investigate TypeScript AST lowering to masked kernels, with explicit support for effect-state mutation and nested calls. Unsupported handlers must be inventoried. |
| Source worker encoder/transport | Compact categorical/public-state records and direct output buffers; remove dense one-hot transport and text roundtrips from the fast path. |

An unchanged JavaScript callback cannot be made vectorized simply by passing
arrays instead of Pokémon objects. Scalar operators, conditionals, dynamic
state and nested calls must be lowered or redesigned. Grouping calls to the
original scalar functions is an intermediate optimization, not the desired
batched kernel. A scalar fallback can support incremental validation, but its
cost limits total speedup: one percent of baseline runtime left scalar caps an
otherwise infinitely fast replacement at 100x. No 10,000x gain is established.

The first meaningful vertical experiment should cover mixed damaging moves,
relevant stat/ability/item events, independent RNG, public outputs and terminal
handling over complete original battle traces. Check intermediate state and
ordered public events against the pinned source before timing sustained runs
at thousands of simultaneous battles. An isolated arithmetic-loop benchmark
does not establish environment throughput. CPU C/SIMD and GPU execution are
candidate backends for the same grouped kernel representation; both require
measurement. Reuse the catalog, team generation and original oracle.

For an actual AOT pilot, keep mechanics functions intact and adapt the loader:
`sim/dex.ts:447` uses computed data-module requires, `:471` enumerates mod
directories, and `:590` loads aliases; `sim/teams.ts:628` selects generator
modules dynamically, and `data/random-battles/gen9/teams.ts:1749` loads set JSON.
Bundle these into a pinned module registry and provide the required host
functions, preserving module identity, property order, missing-module behavior
and seeded RNG. Compare complete source game logs before timing any native
candidate. No Static Hermes compiler or generated native simulator has been
validated in this sidequest. The direct-source CPU profile instead identifies
callback lookup/discovery as a concrete optimization target; do not cache live
effect properties on an unproved immutability assumption.

**Reference revision.** Pokémon Showdown
[`9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e`][revision], committed October 3, 2026.
The inspected checkout is `build/pokemon_gen9/showdown/`, relative to the
PufferLib root. Gen 9 aliases the base dex in `sim/dex.ts:773`, and
`data/scripts.ts` declares generation 9. Its mechanics live in `sim/` and base
`data/`; there is no separate `data/mods/gen9/` engine to port.

[source-map.json](source-map.json) records SHA256 hashes of 50 source/reference
files, method locations, every base move/ability/item/condition/ruleset entry,
handler locations, handler ordering metadata, nested conditions, matching test
filenames, and the direct random-set inventory. The map is a lexical index,
not a proof of behavioral reachability.

The pinned set data contains **509 species/forms, 879 templates, 348 distinct
listed moves, 203 listed abilities, and 10 set roles**. These counts refer to
this revision.

**Format contract.** The entry is [config/formats.ts:29][format]. It selects
`mod: gen9`, `team: random`, singles, six generated Pokémon, and no team preview.
Its rules resolve as follows:

| Rule | Implementation | Port treatment |
| --- | --- | --- |
| PotD | `data/rulesets.ts:483`; generator `randomTeam` | Carry an explicit optional Pokémon-of-the-Day input in the pinned configuration; the normal unset case performs no replacement. |
| Obtainable | `data/rulesets.ts:166`, dependent rules at 219–237; `sim/team-validator.ts` | Retain source validator checks in the oracle/tooling. The native environment receives teams from its ported generator; it needs no arbitrary-team legality service. |
| Species Clause | `data/rulesets.ts:788`; `randomTeam` base-form tracking | Port the generator's exact base-species uniqueness checks. |
| HP Percentage Mod | `data/rulesets.ts:1353`; `Pokemon.getHealth:2060` | Preserve exact own HP and the source's opponent HP rounding. |
| Cancel Mod | `data/rulesets.ts:1371`; `Side.choose`, `Battle.undoChoice` | Preserve simultaneous commitment. The joint RL step submits final choices; the online adapter handles the pre-commit protocol. |
| Sleep Clause Mod | `data/rulesets.ts:1379` | Port `onSetStatus`, including the source of an existing sleep; self-induced sleep is distinguished. |
| Illusion Level Mod | `data/rulesets.ts:2923`; `Pokemon.getUpdatedDetails:536`; `Abilities.illusion:2055` | Conceal the disguised Pokémon's true level until the source reveals it. |

These are the actual Random Battle rules. In particular, the OU sleep-move ban,
OU tier bans, and team preview do not belong in this environment. The source's
1000-turn cap is enforced in `Battle.maybeTriggerEndlessBattleClause:1803`
even when the separately named Endless Battle Clause is absent. Preserve that
cap and its timing; distinguish any shorter training truncation from a game tie.

**Team generation maps to `Generate.bend` and small supporting modules.**

The external path is `Teams.generate:651 → Teams.getGenerator:628 →
RandomTeams.getTeam:260 → RandomTeams.randomTeam:1752`. `Battle.getTeam:3165`
supplies a team seed and uses the same generator. See [sim/teams.ts][teams]
and [the complete Gen 9 generator][generator].

| Generator work | Source symbols in `data/random-battles/gen9/teams.ts` | Planned Bend module |
| --- | --- | --- |
| Constants, role lists, type-based move enforcement | Constants at 70–150; `constructor:178` | `GenerateRules.bend` |
| Sampling semantics | `random:283`, `randomChance:268`, `sample:272`, `sampleIfArray:276`, `fastPop:291`, `sampleNoReplace:311` | `Rng.bend`, `Generate.bend` |
| Count selected move properties | `queryMoves:385`, `getMoveType:696`, `MoveCounter:54` | `GenerateMoves.bend` |
| Exclude incompatible/redundant moves | `cullMovePool:463`, `incompatibleMoves:645` | `GenerateMoves.bend` |
| Select four moves in source order | `randomMoveset:738`, `addMove:675` | `GenerateMoves.bend` |
| Select ability | `shouldCullAbility:1070`, `getAbility:1109` | `GenerateSet.bend` |
| Select item | `getPriorityItem:1157`, `getItem:1347` | `GenerateSet.bend` |
| Level and initial form | `getLevel:1442`, `getForme:1470` | `GenerateSet.bend` |
| Assemble a complete set | `randomSet:1498` | `GenerateSet.bend` |
| Species pools and form weighting | `getPokemonPool:1641` | `Generate.bend` |
| Team compatibility | `getPokemonCompatibility:1679` | `Generate.bend` |
| Build the six-member team and select its lead | `randomTeam:1752` | `Generate.bend` |
| Curated templates, levels, move/ability/Tera pools | `sets.json` | Generated `data/RandomSets.bend` |

Port this singles path directly, preserving statement order and random draws.
The method call index in the JSON includes shared helpers and the unused
doubles branches inside them; specialize those branches to singles. The
separate Challenge Cup, Hackmons Cup, Factory, BSS, draft, 1v1, and doubles
generators are outside this format.

The source's species weights, lead insertion, team counters, conditional move
selection, required moves/items, EV/IV adjustments, form selection, move order,
gender and shiny draws are part of generation parity. Cosmetic draws can still
affect subsequent RNG. Preserve the otherwise-unused initial type selection
in `randomTeam` when comparing seeds. No pre-generated team bank is required.

**Static data maps to generated integer tables.**

| Source | Content to carry |
| --- | --- |
| `data/pokedex.ts` | Stats, types, weights, gender, base species, forms, required moves/items, ability names. |
| `data/moves.ts` | Move records, flags, target kinds, category, PP, power, accuracy, fixed effects, callbacks, nested conditions. |
| `data/abilities.ts` | Ability records, suppression/copy flags, callbacks, nested conditions. |
| `data/items.ts` | Item records, modifiers, removability/use flags, callbacks, nested conditions. |
| `data/conditions.ts` | Major statuses and shared battle conditions. |
| `data/typechart.ts`, `data/natures.ts` | Type effectiveness/immunities and stat nature modifiers/defaults. |
| `sim/dex-{data,moves,species,abilities,items}.ts` | Defaults and normalization applied to the raw records; export resolved records rather than guessing omitted values. |
| `data/aliases.ts`, `data/formats-data.ts` | Resolve source names and any metadata consumed during import/generation. |

Generate stable IDs and data arrays once from the pin. Retain upstream license
and provenance. Keep runtime mechanics in Bend and retain original order where
the generator samples a list. IDs may be densely remapped, but that must not
reorder a sampling pool or an effect's execution.

The item inventory comes from `getPriorityItem`, `getItem`, generated berry
names, and `species.requiredItems`. It cannot be read from `sets.json` alone.
Start the mechanics inventory from all generated sets, then include required
moves, Struggle, form changes, acquired abilities, item transfer, nested
conditions, and secondary/indirect move execution. Examples in this pin include
Sleep Talk, Transform/Imposter, Trace, Ogerpon and Terapagos transformations.
Track these dependencies by source entry; sampled teams alone do not establish
that an effect is unreachable.

**Battle engine source map.**

| Responsibility | Source symbols | Planned Bend module |
| --- | --- | --- |
| Stored battle, side, Pokémon and field data | Constructors/fields in `sim/battle.ts:191`, `sim/side.ts:229`, `sim/pokemon.ts:309`, `sim/field.ts:22` | `State.bend`, `Types.bend` |
| Initial switch-ins and battle start | `Battle.start:1905`; `BattleActions.switchIn:62`, `runSwitch:175` | `Battle.bend`, `Switch.bend` |
| Accept choices and preserve pending work | `Side.chooseMove:552`, `chooseSwitch:915`, `commitChoices:1142`; `Battle.commitChoices:2998` | `Choices.bend`, `Battle.bend` |
| Build and order action queue | `sim/battle-queue.ts` `resolveAction:166`, `insertChoice:369`, `sort:418`; `Battle.getActionSpeed:2619` | `Queue.bend` |
| Run until the next player request | `Battle.runAction:2665`, `turnLoop:2938`, `makeRequest:1377`, `getRequests:1418` | `Battle.bend`, `Requests.bend` |
| Collect/order/execute effect handlers | `Battle.singleEvent:571`, `runEvent:758`, `priorityEvent:943`, `resolvePriority:950`, `findEventHandlers:1035` and holder-specific helpers | `Events.bend`, `Handlers.bend` |
| End-of-turn effects and clocks | `Battle.fieldEvent:484`, `eachEvent:465`, `endTurn:1623` | `Residual.bend`, `Events.bend` |
| Move execution | `BattleActions.runMove:210`, `useMove:365`, `useMoveInner:377` | `Moves.bend` |
| Hit checks and multi-hit loop | `trySpreadMoveHit:550`, `hitStep*:621–857` | `Hit.bend` |
| Main effects and secondaries | `spreadMoveHit:1023`, `runMoveEffects:1186`, `selfDrops:1317`, `secondaries:1336` | `Hit.bend`, `Effects.bend` |
| Damage, critical hits and modifiers | `getDamage:1585`, `modifyDamage:1724`, `getConfusionDamage:1850`; `Battle.chain:2305`, `chainModify:2321`, `modify:2332`, `randomizer:2391` | `Damage.bend`, `Math.bend` |
| Switch, drag and pivot | `switchIn:62`, `dragIn:162`, `runSwitch:175`, `forceSwitch:1353`; pending queue in `Battle.commitChoices` | `Switch.bend` |
| Fainting, win/loss/tie and turn cap | `Battle.faint:1619`, `faintMessages:2535`, `checkWin:2606`, `win:1520`, `maybeTriggerEndlessBattleClause:1803` | `Battle.bend` |
| Pokémon stats, HP, PP, boosts | `Pokemon.calculateStat:560`, `getStat:596`, `getActionSpeed:641`, `getBestStat:656`, `deductPP:888`, `boostBy:1221`, `damage:1595`, `heal:1640`; `Battle.statModify:2354`, `calculatePP:2374` | `Pokemon.bend`, `Stats.bend`, `LiveStats.bend`, `BoostTable.bend` |
| Major status and volatile lifecycle | `Pokemon.setStatus:1684`, `addVolatile:1969`, `removeVolatile:2035`, `clearVolatile:1508` | `Status.bend`, `Effects.bend` |
| Item and ability lifecycle | `Pokemon.ignoringAbility:858`, `ignoringItem:879`, `eatItem:1768`, `useItem:1811`, `takeItem:1851`, `setItem:1868`, `setAbility:1908` | `Items.bend`, `Abilities.bend` |
| Weather, terrain, pseudo-weather | `sim/field.ts` `setWeather:39`, `setTerrain:130`, `addPseudoWeather:186` and effective/suppressed accessors | `Field.bend` |
| Side/slot conditions | `Side.addSideCondition:413`, `addSlotCondition:464` and removers | `Side.bend` |
| Transform and forms | `Pokemon.transformInto:1270`, `setSpecies:1387`, `formeChange:1427`, `updateMaxHp:1498` | `SpeciesChange.bend` implements default setSpecies; complete `Forms.bend` callers remain pending |
| Tera and its form/type/stat consequences | `BattleActions.canTerastallize:1924`, `terastallize:1931`; `modifyDamage:1724`; `Pokemon.getTypes:2138` | `Tera.bend`, `Forms.bend`, `Damage.bend` |
| Immunity, grounded state, contact, protection | `Pokemon.isGrounded:2148`, `runEffectiveness:2208`, `runImmunity:2236`, `runStatusImmunity:2269`; `Battle.checkMoveMakesContact:1289`, `checkMoveBypassesProtect:1300` | `Types.bend`, `Hit.bend` |

Most method names above live in [battle.ts][battle], [battle-actions.ts][actions],
[battle-queue.ts][queue], [pokemon.ts][pokemon], [side.ts][side], and
[field.ts][field]. The JSON provides exact links for registry entries and
file-level links plus line numbers for methods.

The source invokes methods named `trySpreadMoveHit` and `spreadMoveHit` even
for singles. Keep the singles semantics of those functions; their names do not
make them doubles-only. Conversely, Mega Evolution, Dynamax, Z-moves, other
generation branches, and multiplayer targeting do not belong in this port.

**Effect definitions and dependencies.** The engine is event-driven. The
data files contain executable TypeScript handlers; their callbacks must be
ported along with their scalar data.

| Effect family | Source entry locations | Where behavior is completed |
| --- | --- | --- |
| Burn, paralysis, sleep, freeze, poison/toxic | `conditions.ts:2,20,47,83,123,138` | Status lifecycle, before-move events, residual ordering, Sleep Clause handler. |
| Confusion, flinch, trapping, move/choice locks, recharge | `conditions.ts:162,198,208,222,253,287,324,364` | Requests, damage, switching and before-move processing. |
| Weather and future attacks | `conditions.ts:379,476,514,546,592,628,666,696,729` | Field lifecycle, delayed target selection, residual damage and suppression. |
| Protect, Substitute, hazards, Trick Room | `moves.ts:13961,18304,17814,17500,19749,17935,19940` | Nested `condition` handlers and side/field/volatile lifecycle. |
| Multi-hit attacks | `moves.ts` Population Bomb `13613`, Triple Axel `20005`; `items.ts` Loaded Dice `3454` | Accuracy per hit, per-hit effects, early termination and true damage rolls. |
| Future Sight, Sleep Talk, Transform | `moves.ts:6391,16866,19828`; `abilities.ts` Imposter `2115` | Saved future effects, secondary move invocation, copied state and PP. |
| U-turn, Shed Tail, Revival Blessing | `moves.ts:20268,16161,15110` | Pausing/resuming the action queue; switch selection; selecting a fainted target for revival. |
| Tera Blast and Tera Starstorm | `moves.ts:19204,19240` | Tera type/category changes, damage calculation and form-dependent targeting. |
| Protosynthesis/Quark Drive | `abilities.ts:3513,3650`; `items.ts` Booster Energy `622` | Weather/terrain activation, selected stat, volatile persistence and item use. |
| Illusion | `abilities.ts:2055`; `rulesets.ts:2923` | Disguise lifecycle, apparent identity/level, health reporting and public reveal events. |
| Ability copying/suppression | `abilities.ts` Trace `5119`, Neutralizing Gas `2905`; `Pokemon.setAbility/ignoringAbility` | Handler discovery, suppression exceptions and effect restoration. |
| Terapagos/Ogerpon/other forms | `abilities.ts` Tera Shift `4965`, Tera Shell `4958`, Teraform Zero `4944`, Embody Aspect `1198–1237`; `pokedex.ts` | Form transitions, max HP/stats, type handling, associated abilities. |

`data/conditions.ts` is only one condition store. [DexConditions.getByID:672][conditions-loader]
also resolves rules, item/ability prefixes, and `condition` objects nested in
moves, abilities and items. The source index includes those nested locations.
The full base registries indexed are 953 moves, 321 abilities, 583 items, 35
standalone conditions and 162 rulesets. These totals describe upstream code;
they are not the set of mechanics required by this one format.

**Port semantics to preserve.**

Type-query work must preserve array identity, rather than storing only two flat
type IDs. `Pokemon` initially aliases `baseSpecies.types`; `setType` allocates
an array for a string argument but retains an array argument. `getTypes` returns
the Type event's array, pushes Normal into an empty result, and makes a fresh
concatenation only when including `addedType`. Non-Stellar Tera returns a fresh
single-element array; Stellar follows ordinary type queries. Roost stores the
incoming array as `typeWas` before returning a filtered array. Arceus and Silvally
type callbacks read raw held-item Plate/Memory data, including when that item is
suppressed. `Pokemon.transformInto` first runs the target's pre-Tera type query
and then uses its retained Roost `typeWas` array if present (`pokemon.ts:1294–1295`);
enforced array-valued `setType` retains that reference. `setSpecies` also retains
the resolved species' array, while apparent type captures a separate joined
string (`pokemon.ts:1393–1398`). Verify shared constructor aliases, retained Roost aliases, fresh
results, empty results and added-type mutations independently. The new
`EventListener.bend` separates discovery data from executable callbacks so these
nested queries can use native collection below the stat callback layer.

`ValueArray.bend` supplies persistent indexed arrays, text filtering/membership,
in-place push, fresh one-value concatenation and sparse copies. The independent
JavaScript fixture checks retained graph identities rather than only list
contents. Wire op 49 exposes the kernel for diagnostics. `TypeBodies.bend`
ports Roost and the inherited Arceus/Silvally functions. `TypeEvent.bend` reuses
the lower `ListenerExecution.bend` order/gate machinery and native collection
below generic callback dispatch, so stat callbacks can ask for live types.
`PokemonTypes.bend` supplies getTypes/hasType through persistent Pokémon roots
96/97/98 (types reference, added-type text, terastallized-type text); wire op 50
exposes the helpers. The independent fixture inventories all resolved Type
functions and compares retained graph transitions with original discovery.
`TypeInit.bend` attaches shared constructor arrays following all 1,585 resolved
Dex species/display names and their 1,480 actual `.types` identities, including
cosmetics. Equal contents do not merge separate arrays. The independent
constructor/cache fixtures compare retained roots and original getTypes calls.
Transform/forms, lifecycle callers and remaining stat bodies
remain pending. `TypeChange.bend` implements setType/addType through op 54.
`TextStore.bend`/`TextMem.bend` keep immutable UTF-16 primitives separate from
effect-object identities and order; `TextJoin.bend` snapshots indexed strings
with slash separators, including holes/null/undefined. Constructor offsets
99/100/101 initialize knownType/apparentType/baseTypes. Source comparisons now
track original arrays and apparent strings independently across later writes.
The type-change fixture passes 8,505 original method calls in retained sequences;
array mode 8 supplies sorted indexed assignment, including sparse growth.
The current 131 checked laws and selected regression evidence are recorded in
`VERIFICATION.md`; full games/source equivalence remain unfinished.
The species-number guard uses current species field 51, including signed-number
serialization, rather than assuming a base-species ID. Other join coercions and
prototype overrides remain outside this admitted model. Resolved species are cached and
deep-frozen in `dex-species.ts:483,619`; arbitrary mutation of a frozen source
species array is outside the current mutable-array fixture. Native frozen-write
enforcement or a reachable mutation-safety proof remains necessary. Full reachable
bounds and the original-to-native proof relation remain obligations.

`StatTable.bend` retains storedStats/baseStoredStats as ordinary objects at roots
102/103. Live stat reads and collection's unboosted speed use storedStats;
rows 17–21 are mirrors, and older rootless diagnostic fixtures retain their flat
input. Fields 104/105 encode nature plus/minus and 106 retains set.level.
`Spread.bend` computes the six stats, allocates even during Transform mode,
initializes HP only when maxhp is zero, replaces baseStoredStats only for ordinary
changes, copies values into the existing storedStats object and updates speed.
`SpeciesChange.bend` joins this to original-order type/weight changes and native
ModifySpecies discovery. Its independent fixture calls unmodified setSpecies
for all 1,585 resolved records, 25 natures, retained/reordered stat objects,
HP/maxHP/level extremes, Tera/active contexts and original depth faults. Complete
transformInto/formeChange/updateMaxHp, public records and ability/volatile
lifecycles remain pending; diagnostic error codes do not reproduce source log
messages. Both single and team resets now attach distinct stat tables and pass
their independent constructor graph comparisons.

`MoveSlots.bend` now retains base/current arrays and ordinary slot records at
roots 108/107. PP, disabling, getMoveData and numerical move views read those
records rather than stale mirrors. Constructor arrays share records; the
Transform slot stage makes fresh virtual records with min(5, Dex base PP),
preserves raw target/name strings and uses the transformer's Hidden Power
type. The clearVolatile array stage restores another shallow base-array slice.
Neither stage completes its parent method. The admitted slot domain is dense
arrays of at most four records with nonnegative whole U32 PP values/amounts;
full reachable-domain closure and source correspondence remain obligations.

The next Transform dependencies are illusion,
timesAttacked, Hidden Power and Tera availability
fields; remove/addVolatile with Start/Restart/End events and crit-state fields;
setAbility with its events; and public transform messages. Preserve the exact
source sequence instead of treating copied stats/moves as a complete Transform.
In particular, transformInto first gets the target's pre-Tera types even when
Roost.typeWas will then replace the result. It removes all four crit volatiles
before adding any. setSpecies writes cached speed from the transformer's spread;
the subsequent target stored-stat copy does not write cached speed again in this
method. That stale cached value lasts until the source's later speed update.
Constructor/reset moveSlots is baseMoveSlots.slice(): the arrays are distinct
but their slot records are shared. Transform replaces only the current array
with fresh records, so the base records and their PP survive. Preserve these
aliases before extending the existing flat PP/disable/move-view helpers.

Persistent boosts must follow the ordinary-object semantics in
`pokemon.ts:1196–1267,1333–1336,1509–1520`. Constructor/clearVolatile creates a
fresh seven-field zero object; clearBoosts mutates the existing object's own
fields. setBoost and Transform iterate the supplied/target object's properties
and assign into the existing destination, retaining its identity and property
order. copyVolatileFrom assigns the target's actual boost object before the
target's clearVolatile creates a replacement; the recipient must retain the
old shared object. getStat's unmodified branch reads the actual boost object,
while its modified branch passes a fresh shallow object to ModifyBoost.
`BoostTable.bend` now provides authoritative root 109, constructor attachment,
root-based live reads/clones, existing-object mutation, fresh zero replacement,
actual table sharing and source-property copying into the retained destination.
The independent fixture compares 10,192 transitions in 52 retained sequences,
including original boost methods and projected parent boost stages, with
deliberately stale flat mirrors and retained aliases. The normal guarded build
checked 131 laws, compiled C and passed seven selected suites. There are now 41
registered suites; the previous full regression baseline passed 40, and no full
41-suite rerun is claimed. Stored boosts currently admit seven whole stages in
-6..6; prove reachable closure before treating this diagnostic domain as the
complete simulator domain. The ability/volatile/public stages of Transform and
Baton Pass remain pending.

Sandstorm.onModifySpD and Snowscape.onModifyDef (`conditions.ts:641,707`)
use a conditional direct return rather than chainModify. Native execution calls
hasType before checking effective weather; a failed predicate returns undefined
without reading the relay. Independent direct/singleEvent/runEvent/getStat
fixtures compare the nested Type event and retained mutations, including when
weather does not qualify, plus direct rounding before later item modifiers.
Depth faults preserve the original partial graph and RNG. These two bodies do
not implement weather activation, clocks, messages or residual damage.

`Grounded.bend` ports `Pokemon.isGrounded` (`pokemon.ts:2148`) through wire op
52. Preserve gravity/Ingrain/Smack Down presence before item suppression/Iron
Ball, then Flying and ??? type queries, effective Levitate/Eelevate and active
attacker suppression, Magnet Rise/Telekinesis presence and Air Balloon.
negateImmunity skips only the Flying branch; ability immunity returns null.
`EffectMem.has` tests field presence independently of value. The independent
fixture checks retained dictionary writes/deletes and original Type events,
including falsy states and skipped depth checks. Nonduration collection now
permits falsy condition states and initializes them only on function execution;
truthy primitive states and primitive duration reads remain outside the admitted
model. Full type-changing lifecycles and immunity/hazard/terrain callers remain
separate obligations.

The pinned scalar TryTerrain query has no listeners in the resolved Gen 9 Dex.
`TerrainQuery.bend` still checks nested event depth and discovers actual handlers;
an unexpected runnable listener is an explicit unsupported error. Its independent
fixtures assert the empty listener inventory across moves, abilities, items,
species, conditions (including nested/prefixed definitions) and the format.
Terrain clocks, activation, public records and remaining terrain-related bodies
are separate obligations.

- Replace string handler lookup with typed event and handler IDs. Gather only
  applicable holders, then preserve event-specific ordering, effect priority,
  speed, suborder, creation order and source tie shuffling. Some events use
  left-to-right or redirect ordering instead of the general speed ordering.
- Preserve handler context (`effect`, `effectState`, `event`, source, target,
  relay value) across nested events and ability/item/weather suppression.
- `singleEvent:634` selects the supplied state or initializes an empty one,
  without assigning its target. `runEvent:900` assigns `effectHolder` to the
  state target only in the function-callback branch. Literal runEvent handlers
  and handlers skipped by guards do not initialize or bind state. Restoring
  context restores references, while persistent object mutations remain.
- Use distinct typed outcomes for the source's `undefined`, `null`, `false`,
  empty-string `NOT_FAIL`, numeric zero `HIT_SUBSTITUTE`, successful values and
  ordinary success. Collapsing all falsy results changes move behavior.
- Preserve the 4096-based modifier arithmetic and distinct rounding in
  `chain`, `chainModify`, `modify` and damage. Represent required fractions
  exactly; avoid substituting approximate floating-point damage math.
- Source action commitment keeps the remaining old queue during a pivot.
  Insert the newly chosen switches before it without re-sorting saved actions.
- Use separate persistent party identity, current party/active position,
  transformed identity and public apparent identity. Showdown swaps party
  positions, and Illusion depends on that order.
- Use a typed execution state plus explicit continuation frames for nested
  effects/move calls. Bend's acyclic definition order prevents copying mutually
  recursive TS classes directly. Derive bounded work measures from queues,
  hits and source nesting limits; exhausted implementation capacity is an
  explicit engine error, not a silently skipped effect.
- Keep tables out of huge literal U32 pattern matches. Use arrays, typed
  small dispatchers and the checked stock Bend affine-array transport pattern.

**RNG and replay.** [sim/prng.ts][rng] defines integer range sampling, chance,
sample, shuffle and both `Gen5RNG` and the current default `SodiumRNG`.
`SodiumRNG.next:198` runs ChaCha20, takes a new seed from the first 32 output
bytes and a big-endian U32 from the following four. `Gen5RNG.nextFrame:285`
is the four-U16 representation of a 64-bit LCG. Both should have explicit
mode/version metadata.

Both Gen5 and the default Sodium mode now pass independent draw/whole-team
comparisons. Preserve the source's scaled U32 range mapping
and shuffle; `% n` is not an equivalent mapping. Team A, team B, battle and
policy-sampling RNG are separate. Fix seeds and record draw locations in oracle
fixtures so differences can be reduced to the first divergent draw/effect.

**Player observations and actions.** `Requests.bend` follows source requests;
`Knowledge.bend` records public revelations; `View.bend` produces actor input
from own-team knowledge and public events. The C adapter copies flat buffers.

The relevant sources are `Battle.getRequests:1418`, `Pokemon.getMoves:964`,
`getMoveRequestData:1083`, `getSwitchRequestData:1153`, `getHealth:2060`,
`getUpdatedDetails:536`, and the public/private split in
[SIM-PROTOCOL.md][protocol] and `Battle.addSplit:3083`.

Use phase-aware choices: requested move slot, requested move with Tera, living
switch target, revival target, and forced wait. Synthetic Struggle/recharge
requests must follow the source; do not freeze an action count before these
paths are enumerated. Both sides submit independently. Only the actors that
actually have choices contribute an action loss.

Public request masks must match Showdown's available choices. Hidden trapping
or a hidden move restriction may cause an unavailable-choice response and a
revised request without advancing battle time (`Side.chooseSwitch:915`,
`updateDisabledRequest:851`, `updateRequestForPokemon:906`). Do not reveal
private engine facts by pre-emptively filtering those choices. Preserve pending
opponent commitments while the affected player reselects, without exposing them.

Opponent bench identities start unknown. Keep exact own HP/PP and private set
data distinct from opponent HP percentages, revealed moves/items/abilities,
known Tera types and apparent forms. Generator role, future RNG, hidden
durations, exact opposing sets and party ordering are not actor features.

**Implementation sequence and completion checks.**

| Order | Concrete deliverable | Completion check |
| --- | --- | --- |
| 1 | Stable catalog export; `Types`, `Math`, `Rng`; small native Bend transport path | Every direct source ID resolves; RNG range/shuffle and arithmetic fixtures match; compile a small stock CPU increment. |
| 2 | Complete singles `GenerateRules`, `GenerateMoves`, `GenerateSet`, `Generate` | Exact full teams and post-generation RNG match the reference for recorded seeds and targeted role/lead/compatibility cases. Include the upstream Iron Bundle/Freeze-Dry check. |
| 3 | `State`, `Queue`, `Events`, `Choices`, `Requests`, `Battle` | Initial activation and simultaneous choices; exact ordering/ties; forced wait, faint switch, pivot continuation and revival selection. |
| 4 | `Pokemon`, `Stats`, `Damage`, `Hit`, `Switch`, `Status`, `Field`, `Side` | Reference fixtures for hit checks, 16 damage rolls, crits, multi-hit effects, status clocks, entry effects and residuals. |
| 5 | Port all reachable move/item/ability/nested-condition handlers, plus `Forms` and `Tera` | Each inventory entry has an implementation, source link and relevant interaction fixtures. Implement plain scalar effects through shared handlers and special effects through named functions. |
| 6 | `Knowledge`, `View`, structured public event output and online protocol adapter | Compare each player's requests and event-visible state independently; hidden-state variations preserve observations until source-authorized revelation. |
| 7 | `Wire.bend`, C `pokemon_gen9.h` adapter, standalone runner and CUDA learner configuration | Seeded games, deterministic reset/replay, valid requests, separate engine errors/truncations, consistent single/multiple-worker behavior and a real training smoke. |
| 8 | Bend scripted opponents, frozen-policy league, independent Showdown evaluation | Ported bot decisions match their originals on saved public observations; evaluation uses fresh seeds and independently resolved battles. |
| 9 | Complete-loop performance qualification and CPU/GPU overlap | Measure generator/reset, transitions, masks/views, transfers, inference and training separately and together; preserve identical fixed-seed behavior while optimizing. |

The generator is an early native module in this sequence. The simulator's
full completion target is the pinned Random Battle distribution, including
rare reachable mechanics; a temporary incomplete implementation reports its
unsupported mechanics rather than silently approximating them.

**Verification sources and meaningful laws.**

Use [test/random-battles/gen9.js][gen-tests], `test/random-battles/tools.js`,
`test/random-battles/all-gens.js`, and the corresponding `test/sim/moves/`,
`abilities/`, `items/`, `statuses/`, `rulesets/`, and `misc/` fixtures. The source
map lists 358 simulator test files. Existing dedicated fixtures include PRNG,
ordinary Tera, Stellar Tera, Terapagos, Illusion, Sleep Clause, individual
multi-hit moves and switching effects. Tests must be selected for Gen 9 singles;
upstream also tests other generations and formats.

Run a Node oracle against the pinned source to emit canonical full states for
mechanics checks and per-player views for visibility checks. Advance the two
engines independently, compare after each action/event/request boundary, and
reduce mismatches. Normalization drops protocol timestamps and internal object
identity; it must retain real move results, order and visibility. Reuse
`sim/state.ts` for understanding saved state and `sim/tools/runner.ts` /
`exhaustive-runner.ts` for upstream runner conventions.

Proposed Bend laws include HP/PP/stat-stage bounds, preservation of legal
generated-set domains, six distinct base species, terminal absorption,
conservation of unchanged state fields, exact integer damage subroutines,
once-per-side Tera use, preservation of queued actions across choice pauses,
and observation independence from unrevealed information. Generator laws must
allow source failure if its candidate pool is exhausted; a retry wrapper must
not be presented as the same algorithm.

Write source-derived laws with independently specified expected behavior.
The remaining all-state equivalence obligations and current evidence are
tracked in [VERIFICATION.md](VERIFICATION.md).
Fixtures establish correspondence to Showdown; proofs establish their stated
properties. Neither is a claim of universal Showdown equivalence or compiler
correctness. Keep semantics in project-local stock Bend 2.0.35 and use the existing guarded
compiler workflow when implementation begins.

**PufferLib and opponent integration.** Reuse the adapter conventions in
`ocean/pokemon/bridge.h`, `pokemon_core.h`, and `pokemon_base.h`, while assigning
Gen 9 its own ABI, observations and policy metadata. The existing Gen 1 engine,
catalog and checkpoints remain useful independently.

Use independent CPU battles with persistent state and no string parsing in the
step path. C owns worker/buffer transport; Bend owns generation, transitions,
phase changes and rewards. CUDA owns policy inference and PPO. Start with
synchronous reproducible batches, then enable the existing multiple-buffer /
async facilities in `src/pufferl.cu` with actor versioning and preserved
recurrent state. Match both seats of a battle to the same environment buffer.

Port the singles choices in `sim/tools/random-player-ai.ts` as an immediate
smoke opponent. The planned stronger scripted ports are poke-env's
`MaxBasePowerPlayer` and `SimpleHeuristicsPlayer` from
`src/poke_env/player/baselines.py`; pin that separate repository before porting.
Frozen neural policies continue to run through CUDA. Run external opponents
such as Foul Play in their original implementations against the independent
Showdown simulator for final playing-strength checks.

**Reproduce this source audit.** From the PufferLib repository root, after
checking out the reference revision under the path above:

```sh
python3 ocean/pokemon_gen9/audit_source.py --check ocean/pokemon_gen9/source-map.json
```

To regenerate the map from that clean pinned checkout:

```sh
python3 ocean/pokemon_gen9/audit_source.py --output ocean/pokemon_gen9/source-map.json
```

This checks the source inventory, not the planned engine. The audit refuses a
different revision or modified tracked reference files.

[revision]: https://github.com/smogon/pokemon-showdown/commit/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e
[format]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/config/formats.ts#L29
[teams]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/teams.ts#L628
[generator]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/data/random-battles/gen9/teams.ts
[battle]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/battle.ts
[actions]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/battle-actions.ts
[queue]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/battle-queue.ts
[pokemon]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/pokemon.ts
[side]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/side.ts
[field]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/field.ts
[conditions-loader]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/dex-conditions.ts#L672
[rng]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/prng.ts
[protocol]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/sim/SIM-PROTOCOL.md
[gen-tests]: https://github.com/smogon/pokemon-showdown/blob/9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e/test/random-battles/gen9.js
