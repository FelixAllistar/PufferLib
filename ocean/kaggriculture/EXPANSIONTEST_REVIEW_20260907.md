# Win-only expansiontest: normal-start evaluation

Training run: `expansiontest`; macro mode 2; 256x3. Recorded reset-state
probability .5. All shaping/cash/expansion rewards disabled; terminal win scale 1.
The deterministic executor and economic observation features remain present.

PSRO analysis prefix on Vast:
`logs/kaggriculture/psro_expansiontest_roots_20260907`.
Coarse screening now explicitly disables state/opening resets and curriculum;
previously it inherited the training reset setting. The later match evaluator
already disabled state-bank resets. No active training config was changed.

Confirmed four-policy matrix, 100 games per pairing:

| Policy | Mean game score | Mean money |
| --- | ---: | ---: |
| autofarm 603.98M | .793333 | 74893.9 |
| autofarm 999.29M | .790000 | 76331.5 |
| expansiontest 805.31M | .246667 | 61231.3 |
| expansiontest 1999.63M | .170000 | 62022.1 |

The script's "Auto-selected learner" means best candidate from the requested
run, not overall champion. It selected expansiontest 805.31M; the incumbent
autofarm policies remain clearly stronger here. Analysis left active league
membership/config unchanged. User requested an experimental Kaggle submission
of expansiontest anyway; selected checkpoint is `0000000805306368.bin`.

Earlier 402.65M candidate was strongest in the coarse screen but not selected
by final PSRO. Its package passed four official 720-step games versus passive
opponents. An additional seat-0 seed-7 trace had one plot/23 plants/2 animals
at turn 180, two plots/43 plants/2 animals at turn 300, and two plots at end.
This single trace is not a population expansion metric.

Autofarm reference settings preserved on Vast in
`logs/kaggriculture/autofarm_4096_1b_20260905_source_config.ini`:
4096 agents, horizon 256, minibatch 2048, LR .004 without annealing; resets off;
cash 1, money 1, progress 1, terminal-money 10, win 2, crop 10, animal 40,
product 1, land 1, expansion .5; moving EMAG coefficient .1/tau .1.
This is a saved reference, not a claim that each setting caused its advantage.

Next useful causal experiment: hold implementation, optimizer and evaluation
panel fixed; isolate reset probability/coverage from reward changes. Three
total plots means two additional land purchases. No new reward sweep or
training was launched as part of this analysis.
