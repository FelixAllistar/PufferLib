# Goofspiel mega sweep (2026-10-20)

Runs on the Goofspiel Vast box (56827926, `ssh -p 13372 root@ssh2.vast.ai`) from `/workspace/PufferLib-mega`,
in tmux `mega`, logging to `/workspace/goof_mega.log`. The order is PufferNet ×1000, then Transformer ×500. Both use the same GPU
and the same ranges. Reports go to `reports/goofspiel_sweeps/mega_{puffernet,transformer}_01`.

That tree is the box's old tree plus the `magnet-kl` commits (magnet KL, teacher removal, exact EMA endpoints), with two more edits:

- `ocean/goofspiel/goofspiel.h` scores `<ckpt>.magnet` too, and `best_exploitability` covers learner and magnet.
- `ocean/goofspiel/training_config.h` rounds the swept step budget to an integer.

Both edits are also in the local 5.0 working tree.

Differences from the transformer_vast_01 sweep (`goofspiel_mega.ini`, made by `make_mega_config.py`):

- Run length is searched (2M–105M steps, log-normal, time scale), as in upstream defaults.
- Ranges are widened where earlier winners sat on an edge:
  - lr 1e-5–0.05
  - entropy 1e-5–0.3
  - clip 0.001–0.5 (log)
  - gamma 0.9–0.9999
  - momentum 0.5–0.999
  - GAE λ 0.5–0.999
  - width 16–256
  - layers 1–8
  - horizon 2–64
  - minibatch 512–16384
  - replay 1–12
  - VF coef 0.1–5
- Four magnet dimensions use the paper's ranges: coef 0.01–32, τ 1e-5–0.1, anneal on/off, end ratio 0–1.
- `max_train_graph_work=0`. CUDA graphs are off in this config, and on/off measured identical speed and memory here.

Trial 0 starts from the old PufferNet winner with the magnet on (coef 1, τ 0.01).
