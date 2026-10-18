# Context policy evaluation

This evaluator separates policy execution, transport validation, and settlement.
The source includes an explicit no-stakes Slumbot runner and an offline injected
HTTP test. Importing the modules does not contact a server.

## Source responsibilities

| Module | Responsibility |
|---|---|
| `context_eval.py` | One fixed-quota job, durable hand reservations, independent hand seeds, strict response validation, and complete actual-action receipts |
| `context_adapter.py` | The original-policy and retained-context adapters, plus a separate true-card settlement engine |
| `context_audit.py` | Full actual-policy replay, common-prefix comparison, independent contribution checks, and whole-hand statistics |
| `context_series.py` | Source and binary verification, ordered jobs, complete-series requirements, and offline analysis |
| `test_context_pipeline.py` | Fixed synthetic native hands and an uncertain-response fault injected through fake HTTP |
| `create_synthetic_plan.py` | A portable, source-bound plan for that offline test |

The original policy library must be supplied explicitly. The candidate library
must expose the retained-context API. See [the build and lifecycle guide](../RIVER_CONTINUATION.md).
The Python environment needs the existing evaluator dependencies, `requests` for
live execution, and `eval7` for independent showdown ranking.

## Run the offline pipeline

Build the context library, then create a plan using an explicitly selected
original-policy library. From the repository root:

```sh
bash ocean/poker/build_continuation.sh
python3 ocean/poker/context_evaluation/create_synthetic_plan.py \
  --control /absolute/path/to/original-policy.so \
  --candidate build/poker/libpoker2_context.so \
  --output build/poker/context-synthetic-plan.json
```

The command prints the plan's SHA256. Supply that exact value to the offline test
and choose a new output directory:

```sh
python3 ocean/poker/context_evaluation/test_context_pipeline.py \
  --plan build/poker/context-synthetic-plan.json \
  --expected-plan-sha256 PLAN_SHA256 \
  --output build/poker/context-synthetic-output
```

The fixed workload uses four specified decks, both seats, and both policy arms.
A separate fault applies one synthetic wager and loses its response. The runner
must preserve the interrupted reservation and stop without retrying the wager or
starting a replacement hand. This test cannot produce a playing-strength claim.

## Actual replay and the river comparison

For each completed hand, analysis first replays the actual recorded policy
through **every** decision, including decisions after the policies diverge.
Public state, legal actions, selected action, RNG state, and context receipts
must all reconcile. It then creates two separate policy shadows on the shared
public prefix and stops both at the first external-action or post-action RNG
difference. Internal context hashes alone are not a divergence.

The retained-river analysis requires that this first difference is a river
action with equal post-action RNG. An RNG-only or earlier-street difference is
an analysis failure. The control-variate calculation here cannot be reused for
a policy that changes earlier actions or consumes a different RNG sequence.

After all policy contexts close, a separate engine receives the actual revealed
cards. Independent integer contribution arithmetic and `eval7` ranking check
the native and reported payoff. At a qualifying first divergence, a shared
passive river continuation supplies the baseline used in the residual metric.
Hands without a divergence contribute zero residual and remain in the full
denominator. Raw and already-adjusted payoffs are reported separately.

Intervals use complete whole-hand means and sample standard deviations, with
independent-arm standard errors combined in quadrature. Equal policy seeds do
not create duplicate server deals. These approximate normal intervals are not
a certificate of unrestricted-poker exploitability.

## Explicit live plans

Only `context_series.py run` with an `integration` or `confirmatory` plan and a
matching authorization file can start the series runner. The separate
`context_eval.py run` command is the explicit low-level single-job entry.
Plans bind source files, both native libraries, policy settings, job order,
per-worker quotas, seeds, and the analysis definition before execution.

The series command requires `--plan`, `--expected-plan-sha256`, `--authorization`,
`--authorization-sha256`, and a new `--output` directory. The authorization JSON
contains `authorized: true`, the same `mode`, and the exact `plan_sha256`.
Creating a plan or importing a module is not an instruction to execute it.

Every hand is durably reserved before requesting it. Each HTTP action is sent
once. A transport uncertainty, malformed response, or protocol fault abandons
that hand, preserves the failure, and invalidates the planned comparison.
There is no resume, replacement-hand, or outcome-based early-stopping path.

After every planned job finishes, `context_series.py analyze` takes the same
plan and hash, the complete output directory, and a new `--report` path. It
rejects incomplete, interrupted, reordered, duplicated, altered, or omitted
jobs. A confirmatory report supports relative improvement only when the primary
candidate-minus-control interval is wholly positive. It supports an absolute
Slumbot win only when the candidate's **raw** interval is wholly positive.
Integration and synthetic modes cannot assert either result.

Experiment plans, execution authorizations, logs, and measured reports are
generated outside this source directory. This source package contains only the
implementation and fixed synthetic inputs.
