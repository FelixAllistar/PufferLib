# Shared WebNav learner

One environment header, observation/action format, PPO network, optimizer and
checkpoint across the 23 CPU Bend families and 125 registered task names.
The old family learners remain diagnostic baselines. Task transitions,
generation and rewards execute in Bend; C provides transport and public features.

Run from the repository root:

```sh
bash ocean/webnav_unified/run.sh test
bash ocean/webnav_unified/run.sh build
bash ocean/webnav_unified/run.sh train
bash ocean/webnav_unified/run.sh evaluator
bash ocean/webnav_unified/run.sh eval CHECKPOINT.bin 20
```

`build` creates `build/webnav_unified/puffer`. `train` uses
`config/webnav_unified.ini`, including all families, 104 lanes, one policy,
H64/one recurrent layer and a 1M-step budget. Each buffer must hold whole family
cycles; minibatch rows must divide its agent count. The default minibatch is
832 samples with a 16-step horizon. All commands serialize under the existing
resource lock, with a verified ceiling of 6 GiB/no swap. Build/test timeout is
five minutes; train/eval timeout is one hour, configurable with
`WEBNAV_RUN_SECONDS`.

The evaluator loads weights once and reuses them for every task. Its optional
third argument selects one task, for example `click-button`. It reports complete
episodes in every lane, so actual episode count can exceed the requested count
by up to seven. A one-episode request finishes each lane's initial instance,
giving both policies the same initial instance cohort. Results include
wrong-action rejections and incomplete public metadata. Append `--sample` to use
masked softmax sampling, for example `run.sh eval CHECKPOINT.bin 1 --sample`;
greedy is the default. Sampling matches training action selection, although this
CPU evaluator uses FP32 and the trainer defaults to BF16.
Per-task training columns remain stable, with an episode fraction distinguishing
unevaluated windows from measured zero success. The external
[browser runner](../webnav/benchmarks/README.md) now connects this same checkpoint
to public DOM observations and trusted browser input; `--headed` exposes its
Chromium window. The native simulator's `puf_render` remains empty.

## Representation

Every lane has 24,864 public observation floats and the same 3,832-action
catalog. Public nodes use view positions, preserving arbitrary element refs.
Features include instruction/name/value text order, lexical features, DOM roles,
hierarchy, geometry, selection, scroll, public capabilities and action history.
No task ID, private goal, seed or reward enters the observation encoder.

Execute actions share `(kind, public node)` semantics. The public capability
contract translates targets and advertises transport bounds, units, increments
and text limits. Parameter and text selection actions update visible agent
registers; execution submits the resulting command to Bend. Local selections
consume RL steps without advancing the model clock. Engine commands advance a
controlled 50 ms clock; the 512-step agent limit forces a deadline wait.
This is an explicit abstract action preset, not unrestricted browser pixels.

Frozen text features reuse the fingerprint-pinned Potion model. Its normalized
256-vector is projected to 32 dimensions for each instruction, name and value,
alongside ordered text features. The exact-string cache holds 1,024 entries of
up to 1,024 bytes, verifies bytes after lookup, and serializes shared access.
The projection is a speed/size experiment, not a new contextual NLP model.
`run.sh semantic-probe` compares it with the full vector on the existing 24-case
fixture and reports cold/warm latency independently of RL performance.

## Current qualification

A subsequent weights-only warm start ran another 998,400 steps in 7m45s.
On the same 604 initial instances, sampled evaluation won 107 episodes with
**14.0% average task success**, versus the random baseline's **12.0%**. Greedy
evaluation regressed to **3.2%**. This small cohort does not establish a reliable
advantage. Sampled actions spent **81.3% of steps selecting local parameters or
text**, so restructuring the action interface is the next learning priority.
The new checkpoint and browser evidence are recorded under
[modern benchmark results](../webnav/benchmarks/README.md).

The earlier joint checkpoint trained for 31,616 steps. On matching initial lane
instances across all 125 tasks (604 episodes per policy), greedy evaluation
won 88 episodes and random actions won 92. Equal-weight average task success
was **11.1% versus 12.0%**; episode-weighted success was 14.6% versus 15.2%.
This tiny baseline does not demonstrate a learning advantage. Neither policy
produced rejected commands; 64 episodes in each run exposed incomplete public
metadata. The encoder probe scored 15/24 for the compact vector versus 16/24
for the full frozen vector; those are standalone ranking results.

Native checks cover all 125 task projections/deadlines, mixed 4/8-lane batches,
public-input invariance, checkbox private-goal noninterference, command isolation,
autoresets, stable logging, terminal editing and email selection projection.
SIMD CPU evaluation is checked against scalar FP32 within a numerical tolerance;
its reduction order differs. These checks do not establish full original-browser
parity or prove a learned policy can solve every task.

Next work is shared-policy evaluation across original MiniWoB pages, remaining gesture/option
metadata, balanced curricula and a smaller policy that shares learned node
encoding and action scoring across positions. The initial flat MLP is costly and
the first short joint run is a routing/learning baseline. See [RESULTS.json](RESULTS.json) for
the exact checkpoint, preset, source/library hashes and recorded outcomes.
Regenerate that record after the same paired evaluations with
`node ocean/webnav_unified/report.cjs` (the script pins this baseline checkpoint).
