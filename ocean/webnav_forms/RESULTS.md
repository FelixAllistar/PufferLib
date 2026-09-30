# Learned forms-family benchmark, 2026-09-27

The final shared checkpoint is
`checkpoints/webnav_forms/1790567598909/0000000001998848.bin`
(SHA-256 `57026bb6992d68a88c4ba4385f3ff90d401ced813560328e891a2e46cb647387`).
It is a 191,680-parameter H64/L1 policy. All task behavior and instance
generation run in stock CPU Bend; the C adapter supplies public observations
and text-action routing. The final training phase continued a focused copy
checkpoint for two million mixed-task steps, with table curriculum examples
and optional public table-progress feedback. The checkpoint is local and is
not stored in Git.

| Task | Held-out Bend full credit | Original Chromium, greedy |
| --- | ---: | ---: |
| enter-text-dynamic | 100% (2,560 games) | 100/100 |
| enter-text-2 | 100% (2,560) | 100/100 |
| enter-password | 100% (1,536) | 100/100 |
| text-transform | 100% (2,560) | 100/100 |
| copy-paste | 100% (2,560) | 100/100 |
| copy-paste-2 | 100% (2,560) | 100/100 |
| read-table-2 | 100% (1,536) | 100/100 |
| login-user-popup | 100% (1,381) | 100/100 |

Native evaluation uses `seed_offset=1000000`, `data_mode=0`, and
`progress_reward=0`; the evaluator completes more than the requested 256
episodes for some tasks. The original-browser test uses the pinned MiniWoB++
revision `33c3b4ddef8c6eb67c57a29663d844b1eda7e614`, seeds
`900000 + task_index*1000 + episode_index`, a controlled logical clock, and
CDP clicks/Select All/text insertion. The policy sees only the public query,
control names/values, focus/selection, and public text candidates. The
browser fixture strips private goals and popup trigger mode before CDP
serializes an observation. All 800 original-page episodes reported zero
unhittable actions.

The improvement depended on correcting two synthetic-generator shortcuts.
The pinned pages draw mixed-case alphanumeric strings, while the prior Bend
generator produced lowercase strings; the first shared policy learned a case
shortcut and failed original passwords. The prior three-source copy generator
shared a seed stream between the requested ordinal and paragraph content;
its policy failed every original third-source request. The current generator
uses the original character classes, varied short paragraph text, independent
source/target streams, and per-episode table values with varied row order. A
regression test checks that third-source text can be shortest, middle, or
longest. For table lookup, binary rewards alone produced no PPO successes;
one-field Bend curriculum examples and an optional public potential reward
enabled learning. The final score uses the unmodified Bend raw reward.

The family laws, 17 native transition scenarios, 256 adapter episodes, 8,000
public-scripted generated episodes, and 160 original-page differential episodes
pass. These results establish strong
performance on eight bounded form tasks through a high-level candidate-text
interface. One action can insert an entire public candidate string; this is
not a raw-keystroke, full DOM/AX, visual, or live-website result. The eight
tasks are a subset of the 125-name MiniWoB++ registry, not full parity.

Reproduce the final measurements from the repository root after building the
guarded Bend family and native trainer:

```sh
bash ocean/webnav_forms/eval_tasks.sh checkpoints/webnav_forms/1790567598909/0000000001998848.bin 256
build/webnav_forms/browser_eval checkpoints/webnav_forms/1790567598909/0000000001998848.bin 100
```
