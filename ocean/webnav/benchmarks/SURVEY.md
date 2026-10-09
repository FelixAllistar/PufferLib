# Browser benchmark survey — 2026-10-02

This is source research and a proposed integration order, not evidence that any
external benchmark has been installed or run here. Sources are the benchmark
authors' repositories, documentation and papers, inspected on 2026-10-02.
Subsequent implementation and measured evidence are recorded in [README.md](README.md).
Resource numbers below are published image/download sizes; they are not measured
resident memory or a promise that a suite fits the WebNav 6 GiB ceiling.

## Recommendation

Keep CPU Bend simulation and the shared C/CUDA learner as the training path.
First finish shared-policy evaluation on original MiniWoB pages. Then add a
bounded, explicitly declared subset of **WebArena-Verified** as the realistic
local evaluation target. Its audited tasks, site resets and deterministic offline
grader are the best match for reproducible evidence. Start with one site, not
the complete deployment. Shopping Admin has the smallest published application
image; Reddit is a useful second target for navigation, search and persistent
content changes. This ordering is an engineering judgment from the sources and
the local interface, not a published ranking.

**OpenApps** is the best lightweight intermediate target for multi-page and
cross-app state. It intentionally supports generated app variations and state
rewards, but its official app implementation runs Python. It can be isolated as
an external evaluation service; adopting it in the training hot path would
change the requested CPU Bend architecture. Its CC-BY-NC 4.0 license also needs
to be accounted for before copying app implementations or distributing ports.
[OpenApps overview](https://github.com/facebookresearch/OpenApps),
[license](https://github.com/facebookresearch/OpenApps/blob/main/LICENSE).

## Published coverage versus runnable coverage

| Candidate | Published material | Local reset and grading | Fit for this checkout |
|---|---|---|---|
| [WebArena-Verified](https://github.com/ServiceNow/webarena-verified) | 812 audited tasks; Hard is a 258-task subset, not extra tasks | Six supported sites; Docker environment control; deterministic response/network grading, offline rescoring | Primary realistic evaluation target after browser adapter qualification; selected-site results must be labeled as a subset |
| [VisualWebArena](https://github.com/web-arena-x/visualwebarena) | 910 tasks over Classifieds, Shopping and Reddit; supplementary Wikipedia service | Self-hosted sites and reset scripts; official execution grader includes LLM fuzzy matches and vision-model queries in some task configurations | Later visual track; the current text/node policy cannot claim multimodal coverage |
| [WorkArena / WorkArena++](https://github.com/ServiceNow/WorkArena) | L1: 33 atomic tasks, 19,912 instances; ++: 682 compositional tasks, each with many configurations | Hosted ServiceNow instances with task setup, validation and teardown; access is gated | Strong workflow target, but not an unrestricted local Docker benchmark |
| [Online-Mind2Web](https://github.com/OSU-NLP-Group/Online-Mind2Web) | 300 tasks across 136 live websites; tasks are revised as sites change | No resettable site bundle; human evaluation or WebJudge | Later live-web generalization evaluation; not deterministic local RL reward |
| [BU Bench V2.1](https://github.com/browser-use/benchmark) | Current release v2.1.1: 200 tasks; old 60-task chart is historical | Live sites; fresh browser profiles; weighted findings rubrics with an LLM judge | Evaluation only; upstream explicitly says not to train on decrypted tasks/rubrics |
| [OpenApps](https://facebookresearch.github.io/OpenApps/) | Configurable apps and task YAML; generated appearance/content variations; separate longer-horizon tasks | Isolated local deployment and underlying-state rewards; Python app runtime | Lightweight transfer ladder and potential independently generated curriculum, with runtime/license constraints |
| [BrowserGym](https://github.com/ServiceNow/BrowserGym) | A common harness integrating benchmarks, not another fixed task dataset | Reset/validate semantics come from each benchmark | Optional isolated reference harness; does not remove the underlying sites' requirements |
| [BrowserART](https://github.com/scaleapi/browser-art) | 100 behaviors for browser-agent red teaming, synthetic and real sites | Behavior classifiers and website tooling | Different evaluation objective from general navigation mastery; do not count these as 100 additional navigation tasks |

The table counts released task definitions, not locally demonstrated runnable
episodes. No external task is qualified for this policy yet. WorkArena counts
templates separately from sampled instances, and OpenApps variations do not
create independent task semantics. VisualWebArena's evaluator supports exact
URL/content tests, image similarity, vision questions and LLM fuzzy matching:
“execution-based” does not imply that its whole suite is judge-free.
[Official VWA evaluator](https://github.com/web-arena-x/visualwebarena/blob/main/evaluation_harness/evaluators.py).

## WebArena-Verified deployment and artifacts

The current official environment documentation gives these optimized image
sizes:

| Site | Image size | Additional data/setup |
|---|---:|---|
| Shopping Admin | 2.9 GB | Self-contained Magento admin application |
| Shopping | 13.3 GB | Self-contained storefront |
| Reddit | 8.41 GB | Self-contained Postmill forum |
| GitLab | 31.6 GB | Self-contained GitLab CE |
| Wikipedia | 115 MB | About 100 GB external download |
| Map | 3.28 GB | About 60 GB external download and persistent volumes |

Allow storage for image extraction, writable database layers, logs and HARs in
addition to these published numbers. The consulted docs do not specify reliable
per-site RAM requirements. Measure actual memory with a single browser and one
site before choosing concurrency. Old `/getting_started/environments/` docs
describe larger “slim” recipes; use the current `/latest/environments/` optimized
images when preparing a new deployment.
[Current environment sizes and setup](https://servicenow.github.io/webarena-verified/latest/environments/).

Concrete setup requirements:

1. Pin a WebArena-Verified package version/commit, dataset checksum, evaluator
   checksum and site image digest. `latest` in examples is a discovery aid,
   not a reproducible experiment pin.
2. Provide Docker and storage for the selected site. The official Admin image
   is `am1n3e/webarena-verified-shopping_admin`; UI/control ports are 7780/7781.
   Official auto-login uses `X-M2-Admin-Auto-Login` in browser HTTP headers.
   [Admin setup](https://servicenow.github.io/webarena-verified/latest/environments/shopping_admin/).
3. Restore baseline state between mutation tasks and wait for readiness. The
   environment-control HTTP API documents health and reset operations; keep
   one isolated site instance per concurrent mutable episode, or serialize
   episodes. A new browser profile alone does not reset server databases.
   [Environment control](https://servicenow.github.io/webarena-verified/latest/environments/environment_control/).
4. Export agent-facing instructions/start URLs with `agent-input-get` and a
   URL configuration. Keep reference answers and evaluator rules outside the
   observation and inference process.
5. Capture real browser network traffic and close/flush the recording. Per
   task, write `agent_response.json` and `network.har`. Responses declare
   `RETRIEVE`, `MUTATE` or `NAVIGATE`, status and retrieved values as appropriate.
   Run the official `eval-tasks` CLI/container afterward, outside the learner.
   [Official evaluator usage and file contract](https://servicenow.github.io/webarena-verified/latest/getting_started/usage/).

HAR capture must preserve the request methods, headers, query/form parameters,
status and ordering required by the official rules. Do not substitute a
simulator-produced “successful trace” for observed browser traffic. Network
grading does not establish visual appearance or arbitrary JavaScript/DOM state;
the reference evaluator also checks the structured response.
[Network evaluation and limits](https://servicenow.github.io/webarena-verified/latest/evaluation/network_event_based_evaluation/).

Archive `eval_result.json`, including the evaluator version and code/data
checksums. Score 1.0 means every required evaluator succeeded. Infrastructure
errors and skipped tasks must remain visible in the report alongside the fixed
task denominator.
[Result format](https://servicenow.github.io/webarena-verified/latest/evaluation/evaluation_results/).

## Other concrete setup requirements and blockers

VisualWebArena's official setup requires Python 3.10/3.11, Playwright, site URL
configuration, generated task configs and login cookies. Its recommended AMI
contains both VWA and original WebArena and recommends a 1000 GB root volume;
that is an AMI deployment recommendation, not a measured minimum for one site.
Reset scripts cover forum/store state and Classifieds exposes a reset endpoint.
Captioner baselines may need about 12 GB GPU VRAM. That GPU requirement belongs
to a baseline model, not to every browser task.
[VWA setup](https://github.com/web-arena-x/visualwebarena/blob/main/README.md),
[site deployment/reset](https://github.com/web-arena-x/visualwebarena/blob/main/environment_docker/README.md).

WorkArena requires approval to the gated `ServiceNow/WorkArena-Instances`
repository and Hugging Face authentication, plus `browsergym-workarena` and
Playwright. Its documented oracle demos call `cheat`; learned-agent evaluation
must execute policy actions through `env.step` instead. No self-hostable
ServiceNow clone or local server RAM budget is provided by the cited setup.
[Official access/setup](https://github.com/ServiceNow/WorkArena).

Online-Mind2Web requires the live starting websites and screenshot/action
trajectories. Its May 2026 v2 submission format records each action with its URL
and screenshot. Automatic WebJudge is an LLM judge, with the upstream
recommending o4-mini; human judging is also offered. Site changes and task
updates require dataset revision/date reporting. A locally reset browser
cannot freeze the remote websites.
[Official tasks, submission and evaluation](https://github.com/OSU-NLP-Group/Online-Mind2Web).

BU Bench's current README specifies Python 3.12 via uv, BrowserCode and API keys;
local Chromium avoids a cloud browser key when paid fetch is disabled, but its
official judge still needs an OpenAI key. Current V2.1.1 includes September 25,
2026 task/scoring revisions. Pin the release and encrypted dataset hash; the
200-task V2.1 result is not comparable to the historical 60-task chart. Fresh
profiles do not create local website snapshots. Preserve the upstream rule
against publishing decrypted tasks/rubrics or using them for model training.
[Current runner and policy](https://github.com/browser-use/benchmark).

OpenApps installs with `uv sync`, launches with `uv run launch.py`, and uses
Playwright Chromium for browser interaction. Official dependencies include
AgentLab/BrowserGym, FastHTML, transformers, spaCy, FAISS and Pyserini; “single
CPU” does not imply a tiny dependency installation. The consulted docs do not
give measured disk/RAM ceilings. Exclude optional WebShop and remote map needs
from an initial TODO/calendar/navigation subset until separately provisioned.
[Dependencies](https://github.com/facebookresearch/OpenApps/blob/main/pyproject.toml),
[installation](https://facebookresearch.github.io/OpenApps/installation/).

The inspected `original_tasks.yaml` has 28 top-level task definitions, including
cross-app navigation; this is a source count, not a runtime count. Goal variants
copy task parameters/reward and change instruction wording. `longer_horizon`
is a separate task group, with compositions across TODO/calendar/messages/maps.
Inventory the pinned selected group rather than reporting “unlimited tasks” as
an evaluated count.
[Original definitions](https://github.com/facebookresearch/OpenApps/blob/main/config/tasks/original_tasks.yaml),
[goal variations](https://facebookresearch.github.io/OpenApps/tasks/),
[compositions](https://github.com/facebookresearch/OpenApps/blob/main/config/tasks/longer_horizon.yaml).

OpenApps reward code compares target and current app state; navigation uses
the current URL. State comparison includes tolerances/normalization and special
handling of automatic message replies. A Bend port must preserve these exact
semantics and side effects; merely reproducing the instruction is insufficient.
[Official reward implementation](https://github.com/facebookresearch/OpenApps/blob/main/src/open_apps/tasks/tasks.py).

[TimeWarp](https://github.com/sparklabutah/timewarp) is a relevant newer
alternative: local Wiki/News/Shop apps with six UI eras, downloadable environment
data and BrowserGym integration. Its current scorer provides deterministic
string/number/list checks but still supports residual `llm_judge` tasks. Admit
only a declared judge-free subset for deterministic evaluation. The inspected
README does not establish total runnable task count or disk/RAM requirements;
this needs a pinned task inventory and data manifest before deployment.

## Primitive gaps and training/evaluation separation

The local [shared learner](../../webnav_unified/README.md) describes one policy
for 23 CPU Bend families/125 task names. [The registry](../families/registry.json)
explicitly leaves full original-browser parity unverified. Its current short
joint checkpoint does not beat the random baseline. The family API bounds
public nodes to 128, and shared-policy browser evaluation remains pending.

Reusable mechanics include clicking, typing, selection, forms, menus,
autocomplete, calendars, scrolling, editing, email, social and catalog tasks.
The following gaps are inferred by comparing that contract with the external
task requirements:

| Requirement | Required extension or qualification |
|---|---|
| Persistent multi-page/cross-app workflows | Public URL/page identity, navigation history, isolated server reset, state beyond one MiniWoB episode |
| Real data tables and search | Pagination, sort/filter composition, public record relationships and robust node truncation handling |
| Arbitrary output and long text | Policy-controlled copied/generated text and structured final retrieval/status output beyond bounded text/register presets |
| Visual references/images/charts | Actual public image or pixel features and multimodal policy; numeric/visual MiniWoB families alone do not provide this |
| Tabs, popups, frames and files | Explicit browser context, gesture and file capability contracts, with coverage reported per task |
| Authoritative grading | Actual original-site execution evidence and official grader; simulator reward is training signal only |

Generate Bend training tasks with held-out seeds, strings, page arrangements,
state sizes and workflow templates. Freeze the evaluation task IDs and checkpoint
before scoring. Tune on a separate development subset and report if any public
benchmark tasks were used for training; a public task file is not automatically
a clean held-out evaluation split. BU Bench expressly excludes training reuse.
For OpenApps, split reward/template families and app/content variations, not
just paraphrases of identical goals. For WorkArena, preserve its official
sampling curriculum rather than mixing arbitrary seeds into a claimed official
score.

Keep Python reference packages in an isolated evaluation environment/container;
export agent-facing inputs and score recorded episodes in batches. Keep C/CUDA
inference and CPU Bend rollouts native. Any Python app service or official
scorer is an explicitly external evaluation dependency, not a hidden replacement
for simulation. Before external evaluation, qualify public-input projection,
action execution, reset isolation and official artifact output on the chosen
task subset. These steps have not been executed by this survey.
