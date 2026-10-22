# Native in-training Metamon evaluation

User requested normal manual `./puffer train`, benchmark scores in the existing
dashboard, and termination of existing training. Both active trainers were
terminated; no training was restarted. Saved checkpoints were retained.

The native trainer now saves/evaluates every 10% plus the final checkpoint.
Its existing dashboard displays latest/best Metamon scores, W/D/L, and progress.
Training briefly waits for each check and resumes its existing optimizer/RNN.
The native process directly owns the evaluator; no sweep worker or viewer is
needed. Linux parent-death signaling prevents orphan evaluation after exit.
Default 8 games/team is 24 total; `--metamon.games=2` gives 6 total.
`--metamon.interval_fraction=0.01` checks every 1% for quicker manual feedback.
These are noisy screening scores, not strength confirmation.

Vast model size, budget (1,342,177,280 steps), optimizer and opponent settings
were preserved. Only native evaluation hooks and benchmark settings changed.
The prior executable and changed source/config files are backed up remotely in
`reports/pokemon_experiments/20261019_native_metamon/before/`.

Validation: BF16 native executable compiled on Vast; native C lifecycle tests
passed (schedule, progress, score/best, invalid results, timeout and dashboard).
Python protocol/lifetime tests passed. A real six-game check of an existing
1024-wide checkpoint succeeded through the native evaluation harness (2W/4L).
No optimizer training was run during this validation. Full training-loop runtime
validation is deferred because the user requested training remain stopped.
