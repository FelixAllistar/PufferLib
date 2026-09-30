# Navigation learner results — 2026-09-29

Nine panels and two menus tasks now connect to native PPO: **29/125 task names** have trainable adapters including the earlier click/forms adapters. All 125 retain their separate bounded behavior-check evidence.

Two separate H64/L1 policies trained for **2,998,272 steps each** (106.5 s panels, 102.7 s menus). Each has 283,264 parameters. Training used 128 agents, horizon 16, minibatch 1024, learning rate 0.003 and entropy coefficient 0.01. No new embedding model was used.

| Task | Native random /100 | Native learned /100 | Original browser /20 |
|---|---:|---:|---:|
| click-tab | 28 | 100 | 20 |
| click-tab-2 | 14 | 100 | 19 |
| click-tab-2-easy | 54 | 100 | 20 |
| click-tab-2-medium | 32 | 100 | 20 |
| click-tab-2-hard | 12 | 97 | 13 |
| click-collapsible | 36 | 100 | 20 |
| click-collapsible-nodelay | 36 | 100 | 20 |
| click-collapsible-2 | 13 | 100 | 19 |
| click-collapsible-2-nodelay | 17 | 100 | 20 |
| click-menu | 16 | 100 | 17 |
| click-menu-2 | 15 | 100 | 20 |

Native evaluations use greedy actions and held-out seed streams (4,000,000 onward). Browser evaluations use independently generated original pages, real CDP clicks/moves and the same public feature projection. Every score is full credit, not merely positive reward. Random baselines sample legal actions including wait. They are native baselines, not browser baselines.

The first panel browser diagnostic scored 152/180 using full-instruction substring matching. It exposed a feature bug: words from the instruction itself could make distractor links look like targets. The corrected extractor matches the quoted link label exactly, preserving case. On the same original seeds, this improved 152/180 to 171/180. The fresh corrected cohort scored 171/180. The policy weights stayed frozen. The JSON retains the initial diagnostic, a matched-seed corrected evaluation, and a fresh corrected cohort (base seed 300100) used in the table. This is a feature correction, not an extra training run. Native hard-tab success changed from 100/100 to 97/100 after removing the permissive match; the corrected native total is 1,097/1,100. Menu observations contain no WF_LINK nodes, so the correction does not change their features.

Remaining gaps: native panels generate exactly two unique links per panel; originals sample links from 20-word paragraphs with probability 0.2 and may repeat words. Native click-menu has one compact fixed topology, while original trees have varying widths and depths. A flat per-position encoder can overfit these structures. The hard-tab transfer gap motivates broader Bend generator distributions and shared node scoring, before simply increasing training steps.

Next: vary link counts, duplicate positives, distractors and menu topology in Bend; keep generator laws and original-page comparisons; train a node scorer that shares parameters across positions; use untouched browser seed cohorts for evaluation. Then integrate numeric tasks and site-like catalog/email workflows.

Checkpoints:

- panels: `checkpoints/webnav_navigation/1790677442296/0000000002998272.bin`
- menus: `checkpoints/webnav_navigation/1790677592646/0000000002998272.bin`

Reproduce evaluation with commands in [README.md](README.md). Source hashes, checkpoint hashes and per-task raw results are in [RESULTS.json](RESULTS.json). These are small diagnostic browser samples under settled-animation/controlled-clock presets, not full DOM/rendering/timing parity or proof that the tasks are solved.

Validation: adapter tests passed across all eleven tasks (masking, stable-ref
history, menu action routing, autoresets, deadlines, finite features and exact
quoted-label regression cases). Native CPU evaluators and the final CUDA
trainer compiled successfully. Builds, training and browser runs were serialized;
CUDA builds, training and browser jobs used the 6 GiB/no-swap resource guard.
No family Bend model or law was changed for this adapter work.
