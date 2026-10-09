# One policy across MiniWoB++

Current direction (2026-10-03): extend the same learner to applications composed
from [reusable simulator primitives](primitives/README.md). MiniWoB is a
compatibility fixture suite; its family boundaries do not define the engine's
components. Application state/generation stays in CPU Bend. Exploration methods
and training distributions are user research choices. The browser, record and
draft cores now have native checks; their application bindings remain in
progress and have not changed this learner's ABI or task mixture.

The required product is one WebNav environment, one trainer, one policy and
one checkpoint evaluated across all 125 registered task names. Family-specific
training adapters were diagnostic scaffolding, not this deliverable. Stop adding
standalone learner environments or treating their task counts as unified coverage.

The native consolidation now exists in
[webnav_unified](../webnav_unified/README.md): all 125 names use one observation
format, action catalog, optimizer and checkpoint. The initial 31,616-step run
and matched random comparison are recorded in
[RESULTS.json](../webnav_unified/RESULTS.json). Browser integration and the
learned shared node architecture below remain open.

## What exists and what went wrong

There are 125 task names implemented in 23 stock CPU Bend family modules.
Six separate learner adapters (click, forms, navigation, numeric, catalog,
email) currently cover 51 names. The original `webnav` directory also contains
an earlier prototype. These counts are not interchangeable.

The six adapters independently defined their observation features, dimensions,
action vocabularies and histories. `build.sh` compiles `src/pufferl.cu` with one
selected environment header, including its compile-time `OBS_SIZE` and
`ACT_SIZES`. Those choices determine the network input and output shapes.
Running the resulting executable trains a policy; building it does not train
anything. Current family-specific checkpoints are incompatible diagnostic
baselines, not parts of a single trained policy.

Bend does not impose this separation. All family libraries already export the
same descriptor and public `WFView`/`WFAction` types. Separate modules are useful
for local state, generators and laws; separate policy schemas are the mistake.
This finding does not prove every Bend model or public projection is perfect.
Their documented conformance limits still apply.

## Consolidation contract

- Preserve checked Bend task transitions, generators, rewards and laws. Do not
  replace them with a C simulator or duplicate them into a giant new Bend file.
- Load selected family libraries into one suite runtime. Keep simulation lanes
  grouped by family to preserve each Bend batch kernel's lane count. Dispatch
  each dirty family once, and never accidentally replay another lane's action.
- Feed all lanes to the same policy with `num_policies=1`. Family/task identity
  is scheduler and reporting metadata, not a private shortcut in policy inputs.
- Use one public observation encoder and one action meaning across families.
  Instructions, text, roles, state, hierarchy, geometry and action history must
  use the same representation. Do not merely pad six incompatible feature
  arrays or dispatch six networks behind a launcher.
- Make public capabilities explicit: editable targets, supported key actions,
  pointer coordinate frames, selections, scrolling and compound gestures.
  Existing `WFAction` structs alone do not make these contracts uniform.
  A common action builder must not inspect private goals to construct masks.
- Use public text copy spans and literal editing consistently. Do not add
  per-family instruction parsers that calculate answers or select target nodes.
  Share text/node encoding and action scoring across positions rather than
  extending a family-specific flat feature layout for each new task.
- Sample task mixtures/curricula at runtime without changing network shape.
  Use one optimizer state during training and one policy checkpoint; report per-task success and aggregate
  success without allowing easy or frequent tasks to hide unsolved tasks.
- Run the same encoder/action contract against original-browser observations.
  One checkpoint must be evaluated across families without model replacement.

## Implementation gates

1. Suite loader and heterogeneous-lane dispatcher, with reset/step isolation
   tests across all available checked family libraries. This is transport only,
   not a claim that the unified learner is finished.
2. Audit family action contracts; add versioned public capabilities where
   needed. Preserve old conformance fixtures until the new contract is checked.
3. Implement one policy-facing encoder/action catalog and one native training
   entrypoint. Demonstrate a mixed-family batch updates a single parameter set.
4. Evaluate the same checkpoint on multiple families, then all supported tasks;
   retain honest unsupported-action and learning-failure reports.
5. Retire redundant family-specific production entrypoints after the unified
   replacement passes these gates. Preserve baseline results and checkpoints
   for comparison, not as the product architecture.

All implementation/build work remains subject to the existing root-only,
serialized 6 GiB/no-swap build rule. Workers are source-only. Initial architecture
audits are read-only; no new standalone family learner should be created.


## Audit findings

The shared native learner now exists at `ocean/webnav_unified`, with one fixed
public encoder/action catalog and `num_policies=1`. A 31,616-step mixed run
completed and saved a single 1,848,896-parameter checkpoint. Native evaluation
reuses that checkpoint across all 125 task names. This qualifies the shared
training/dispatch path; original-browser/headed evaluation and stronger learning
results remain required. The first flat policy is a speed/sample-efficiency
baseline, not the final shared-node architecture. See its README and RESULTS.json.

The initial shared dispatcher passed native validation on 2026-09-29 with all
23 existing family libraries loaded together: 104 simulation lanes and 125
global task names. Checks cover task/lane mapping, every task's reset and
deadline transition, public view validation, dirty-reset clearing and isolation
of untouched lanes/families. This establishes transport integration, not shared
policy training or full browser parity. Reproduce against existing libraries:

```
bash ocean/webnav/families/test_suite.sh
```

The runner serializes compilation/testing under the existing resource lock and
verified memory/no-swap limits. It does not regenerate the Bend libraries.

The current trainer already mixes fixed-shape agent rows through one primary
policy when `num_policies=1`. Additional policies are historical/frozen policy
slots, not the mechanism for adding task families. Keep whole four/eight-lane
family batches together, with equal total agent counts in each trainer buffer.
Only click, tree and numeric use eight lanes; the other twenty families use four.
Each DSO has its own runtime mutex and heap. Same-family calls are serialized;
different family libraries have independent gates. Initialize shared text
encoders before worker threads. Current checkpoint files persist weights only,
not optimizer state; do not imply exact optimizer resume is already supported.

The shared v2 structs do **not** yet provide shared action semantics:

- Editing may use target zero (focused field) or an explicit field reference.
- Pointer parameters may mean pixel coordinates, fixed-point coordinates,
  normalized slider positions, time-cell refs, hover targets or drag parameters.
- Selection ranges, options and scroll axes have different argument bounds.
- Some accepted action codes only become wait in the Bend wire decoder.
- Public focus, selection, gesture state and capacity information is incomplete
  or inconsistent. Syntactic command acceptance is not a useful affordance.
- Some models can address more nodes than fit the current public projection.
  The unified contract must report and handle that representability limit.

Add a versioned public capability descriptor rather than changing the layout of
`WFView`/`WFFamily` in place and silently breaking the v2 ABI. It needs effective
action kinds, target/focus rules, parameter roles/units/bounds, text limits,
selection/option mapping and gesture state. Masks must include valid wrong
choices: they must never reveal a private goal by advertising only a solution.
Private-goal changes must leave both observations and capabilities unchanged.
The old proofs check their stated task properties; they do not establish this
missing cross-family policy contract.
