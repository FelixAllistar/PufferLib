# Modern browser evaluation

Keep the fast CPU Bend environments and one C/CUDA learner. Evaluate that same
checkpoint on the original WebArena-Verified application, using a public DOM
projection, trusted browser input and the unchanged official offline scorer.
[SURVEY.md](SURVEY.md) compares the current benchmark choices and primitive gaps.

The first preset covers **18 shopping-admin navigation tasks**, selected before
running the policy. WebArena-Verified has 812 tasks overall. This subset does not
cover retrieval, mutations, other sites or the full benchmark. It is an initial
transfer test; MiniWoB success does not imply success here.

## Runtime and storage

Large assets live at `/mnt/d/puffertank/webnav-bench` (Windows
`D:\puffertank\webnav-bench`). The isolated Docker daemon uses a new 20 GiB ext4
backing file there, mounted at `/mnt/webnav-bench-docker`. It does not use the
default Docker socket or storage directory. Small native executables stay in
`build/webnav/benchmarks/docker` because starting them directly from DrvFS caused
containerd startup timeouts. Windows C:/D: free space was queried with PowerShell;
Linux filesystem capacity is not used to infer Windows free space.

The `webnav-benchmark.slice` has a 3 GiB aggregate memory cap, no swap and up to
two CPUs. The website container is capped at 2304 MiB. Build/browser commands use the
existing serialized WebNav guard, at most 2 GiB and no swap. Training and browser
evaluations are run serially. The site binds only loopback ports 7780/7781.

Setup from the repository root:

```sh
bash ocean/webnav/benchmarks/setup.sh
# Initial machine dependency, if absent: apt-get install iptables (as root).
# Start the dedicated daemon/mount after a WSL restart:
/mnt/c/Windows/System32/wsl.exe -d Ubuntu-24.04 -u root --exec bash \
  /home/felix/puffertank/pufferlib/ocean/webnav/benchmarks/runtime.sh install
bash ocean/webnav/benchmarks/runtime.sh pull
bash ocean/webnav/benchmarks/runtime.sh start
node ocean/webnav/families/build.cjs primitive:response_form --test
node ocean/webnav/families/build.cjs primitive:browser_contexts --test
bash ocean/webnav/benchmarks/run.sh build
bash ocean/webnav/benchmarks/run.sh test
bash ocean/webnav/benchmarks/run.sh test-rpc
bash ocean/webnav/benchmarks/run.sh test-response
bash ocean/webnav/benchmarks/run.sh test-browser-response
# Native public composition and schema checks (build the primitives first):
node ocean/webnav/families/build.cjs primitive:finish --test
node ocean/webnav/families/build.cjs app:record_browser --test
bash ocean/webnav/benchmarks/run.sh test-finish
bash ocean/webnav/benchmarks/run.sh test-app-policy
# Browser/tab primitives and their public-control interface:
node ocean/webnav/families/build.cjs primitive:browser_contexts --test
bash ocean/webnav/benchmarks/run.sh test-browser-contexts
bash ocean/webnav/benchmarks/run.sh test-contexts-policy
```

The reference commit and Docker/Playwright versions are pinned in source.
The scorer is installed into an isolated venv on D:; no Python is involved in
the Bend step loop, learner, public feature encoder or policy inference.
`setup.sh` does not download another browser: it reuses the repository's pinned
Chrome-for-Testing installation (`ocean/webnav/setup_browser.sh` if missing).

## Training and evaluation

```sh
# Shared 125-task CPU Bend training, one policy:
bash ocean/webnav_unified/run.sh train --train.total_timesteps=1000000

# Inspect the fixed 18-task evaluation preset:
node ocean/webnav/benchmarks/manifest.cjs

# Scripted positive control for the browser/HAR/grader pipeline (not an RL score):
bash ocean/webnav/benchmarks/run.sh calibrate \
  /mnt/d/puffertank/webnav-bench/runs/my-calibration
bash ocean/webnav/benchmarks/run.sh score --task-ids 157 \
  --config /mnt/d/puffertank/webnav-bench/runs/my-calibration/config.json \
  --output-dir /mnt/d/puffertank/webnav-bench/runs/my-calibration

# A single real-site task; --tasks all selects all 18, --headed shows Chromium:
bash ocean/webnav/benchmarks/run.sh eval --checkpoint CHECKPOINT.bin \
  --tasks 157 --steps 64 --out /mnt/d/puffertank/webnav-bench/runs/my-run

# The same selection with native explicit response controls:
bash ocean/webnav/benchmarks/run.sh eval --preset response-v1 --checkpoint CHECKPOINT.bin \
  --tasks 157 --steps 64 --out /mnt/d/puffertank/webnav-bench/runs/my-response-run
# Scripted positive control can exercise the same controls:
bash ocean/webnav/benchmarks/run.sh calibrate \
  /mnt/d/puffertank/webnav-bench/runs/my-response-calibration --preset response-v1

# Grade actual browser network traffic with the pinned official evaluator:
bash ocean/webnav/benchmarks/run.sh score --task-ids 157 \
  --config /mnt/d/puffertank/webnav-bench/runs/my-run/config.json \
  --output-dir /mnt/d/puffertank/webnav-bench/runs/my-run
```

Use `--checkpoint random` with the same task IDs/budget for the baseline. Each
checkpoint evaluation defaults to greedy; `--sample` uses its masked softmax
distribution, matching the training action-selection method (CPU inference
still uses FP32 rather than the trainer's default BF16). Each task recreates
only the dedicated `webnav-bench-admin` container and starts a
new browser context. This restores server state as well as browser state.
The pinned image initializes itself; the runner waits for that process to
finish successfully before checking service health. It does not start a
concurrent second initialization. Cold startup on the D: backing file can take
several minutes. A run requires a fresh output directory to preserve evidence.
Do not use that container for unrelated work. To release website resources:
`bash ocean/webnav/benchmarks/runtime.sh stop`.

Per-task evidence contains `trajectory.jsonl`, `network.har`, `final.png`,
`agent_response.json`, `runner.json`, and (after grading) `eval_result.json`.
`run.json` records checkpoint/source hashes, selected tasks and preset.
The policy subprocess sees only the task instruction and public page fields;
it never receives expected answers, evaluator expressions or a task ID.

## Current limitations

The default `legacy-navigation` preset submits a NAVIGATE candidate at a fixed
budget. The optional `response-v1` preset exposes type/status selectors, JSON data
and detail fields, and a Finish control through the existing 3832-action model.
Only Finish creates a response. Budget expiry or a harness failure without Finish
writes JSON `null` as an absence marker; the pinned scorer records this as failure.
Explicit answers travel as exact JSON text, including large integers and exponent
tokens. A submitted status is a claim; **only the official scorer determines
success**. Error cases remain in the denominator. Neither preset supplies URL
entry, back/forward or tab management yet.

Public DOM observations retain at most 128 nodes and 16 KiB of text. Omissions,
text clipping, unsupported frames/shadow roots and richer widget behavior are
reported as incomplete. This is a bounded DOM projection, not the full browser
accessibility algorithm. Page timing and action semantics differ from the
synthetic primitives. The flat MLP, small frozen text model and untrained browser
controls remain substantial learning/transfer limitations.

`response-v1` declares a split of that same ABI budget: 112 DOM nodes/12 KiB plus
up to 16 host response nodes/4 KiB. Remaining draft text is clipped only in the
observation and flagged; final answers retain the full native capacities. Native
controls appear in observations and traces, while screenshots show the site.
Selecting a response control hands keyboard ownership to the native form; a
non-WAIT browser action hands it back without changing either draft. Companion
page metadata is recorded in traces but is not yet encoded into policy features.
The original default observation limits and learner dimensions are unchanged.

Next steps are full browser-shell controls, the CPU Bend workflow compositions
in [TODO.md](../TODO.md), and independent generated goals before expanding
official held-out evaluation.

The native browser/tab composition now has shared WF controls for tabs,
back/forward, reload, new/close and retry. It passes 32,000 independent native
steps, 32 public generated-world checks and 512 actions from the unchanged
random/sampled policies. Three local Chromium fixtures pass 34 comparisons of
settled tab/history behavior, including repeated URLs and blank-page history.
These controls are not yet bound into `evaluate.cjs`, and address entry remains
open. The comparisons do not qualify synthetic timing, pending navigation
commits, original-site workflows or learned success. Receipts live in
`build/webnav/primitives/browser_contexts` and
`build/webnav/benchmarks/browser-contexts-validation.json`.

Browser control binding is implemented in `browser_controls_rpc.c` and
`browser_shell.cjs`, but qualification is **resource-paused**. The native RPC
checks pass. The unchanged 2026-10-07 retry passed four of five composed Chromium
fixtures, including recovery and policy execution. During the 16-tab capacity
fixture, the kernel again rejected process/thread creation at the existing
`TasksMax=128` limit and Chromium's page crashed. The October 4 run peaked at
about 664 MiB under the 2 GiB cap; this retry did not retain a memory peak.
The capacity fixture remains unqualified; `evaluate.cjs` does not
expose this preset yet. The goal was paused under the user's standing rule,
with no guard, timeout, browser-setting or coverage workaround.
`build/webnav/benchmarks/browser-shell-resource-pause-20261007.json` preserves
kernel/unit evidence and source/binary hashes; the accompanying test log retains
the failure. All owned test processes have exited.

The reusable engine now has native response controls using the same WF actions,
plus a versioned page metadata companion. `collectPage` opts into table structure,
spans, header/label relationships, headings and link destinations; `collectView`
retains the legacy projection. The 21 browser fixtures pass. The response form
and direct finish API each pass 28 cases against the pinned public schema, and
the shared policy drives generated application/form episodes without interface
errors. Four additional browser composition fixtures pass, including the unchanged
random and sampled checkpoint. The strict public-view parser is shared by both
native RPCs, and malformed inputs preserve state. These checks add no WebArena
success claims. Broader live workflow qualification and companion metadata features remain open.

The first live `response-v1` qualification attempt is preserved in
`runs/20261003-response-calibration`. It stopped after two scripted browser clicks:
the second click completed its input event but exceeded the existing 2000 ms
navigation wait. The website's 2304 MiB cgroup recorded 393 memory-limit hits and
no OOM kill; those events do not prove why navigation timed out. The goal was
paused under the user's resource instruction, without changing guards or
timeouts. `resource-pause.json` records the limits, counters and failed run.
This attempt has no submitted response and is not a policy performance result.

The requested unchanged-source retry in `runs/20261003-response-calibration-retry`
passed: two public browser clicks plus native Finish scored **1/1** under the
unchanged official grader. Website memory peaked at **1.70 GiB**, with **zero**
memory-limit hits or OOM events; limits and timeouts were unchanged. This is a
scripted submission-transport calibration, not a learned-policy result or full
workflow port. `qualification.json` records the result, source/binary hashes and
resource evidence. The benchmark services were stopped after the check.

## Native training baseline

One shared H64/L1 checkpoint was warm-started for another **998,400 steps** in
**7m45s**, after the earlier 31,616-step run. The policy has 1,848,896 parameters;
its FP32 checkpoint is 7,395,584 bytes. This was a weights-only warm start, not an
optimizer-state resume. The same initial 604 instances cover all 125 task names:

| Evaluation | Wins / episodes | Average task success |
|---|---:|---:|
| Earlier checkpoint, greedy | 88 / 604 | 11.1% |
| New checkpoint, greedy | 29 / 604 | 3.2% |
| New checkpoint, sampled | 107 / 604 | 14.0% |
| Uniform legal actions | 92 / 604 | 12.0% |

Average task success weights each task equally; it is not wins divided by all
episodes. These small cohorts do not establish a reliable learning advantage.
The sampled policy spent 81.3% of its steps choosing local parameter/text
registers. Greedy performance regressed despite finite weights. The next
learning change is a bounded command/argument selection process, followed by
shared node scoring and workflow curricula; a longer run alone is not justified
by these results.

Checkpoint: `checkpoints/webnav_unified/1790999586398/0000000000998400.bin`.
Raw native evaluations are `build/webnav/benchmarks/native-million*.jsonl`.

## Real-site pilot — 2026-10-03

The declared preset contains 18 tasks. This first measured pilot ran **three**
of them: 157 (customer list), 374 (Magento Blank theme settings), and 676 (fraud
order filter). Both policies used the same seeds, 64 policy choices per task,
adapter/runner source hashes, browser settings, image and official evaluator.
These are initial integration measurements, not full-suite leaderboard scores.

| Driver | Official full successes | Harness failures | Browser commands / choices |
|---|---:|---:|---:|
| Learned checkpoint, sampled | 0 / 3 | 0 | 22 / 192 |
| Uniform legal actions | 0 / 3 | 0 | 30 / 192 |
| Scripted positive control, task 157 only | 1 / 1 | 0 | 2 commands |

The positive control is excluded from policy performance. Official scores were
zero for all six policy episodes. There was one browser action error/timeout in
the learned run and two in the random run; episodes continued and retained these
events. Local argument choices occupied 88.5% and 84.4% of choices, respectively.
Bounded observations omitted nodes in 23/25 learned and 29/33 random snapshots.
The learner remains weak and the action/observation interface needs improvement.

Fresh container initialization plus initial page loading took 129–192 seconds
per task; the 64 policy choices took 2–16 seconds. This is why the training path
stays in CPU Bend and the real website is a periodic transfer evaluation.

[RESULTS.json](RESULTS.json) records per-task grades, hashes, native evaluations,
resource measurements and qualifications. Full evidence is on D: under:

- `runs/20261003-learned-2`
- `runs/20261003-random`
- `runs/20261003-calibration-2` (scripted control)

Each policy run also archives its verified runner/adapter sources and C policy
binary in `source/`. The pinned official scorer was version **1.2.3**. The 18
browser regression fixtures and C RPC checks passed. No runtime OOM kills were
recorded under the 3 GiB website/runtime and 2 GiB browser ceilings, with swap
disabled. Website resources were stopped after the pilot; `eval` recreates them.

Two earlier qualification attempts are preserved separately: a duplicate
initialization timeout, and an observation/navigation race that aborted one
episode. Both were repaired before the matched three-task runs. The navigation
repair has dedicated regression fixtures; these attempts are not completed
performance cohorts.

Regenerate the checked report from the recorded artifacts:

```sh
node ocean/webnav/benchmarks/report.cjs \
  /mnt/d/puffertank/webnav-bench/runs/20261003-learned-2 \
  /mnt/d/puffertank/webnav-bench/runs/20261003-random \
  /mnt/d/puffertank/webnav-bench/runs/20261003-calibration-2
```
