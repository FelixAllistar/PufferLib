#!/usr/bin/env python3
"""Runs Metamon unchanged (python -m metamon.rl.evaluate ...) with the same two shims our Metamon evaluator uses
(ocean/pokemon/benchmarks/metamon_opponent.py): the qualified torch attention backend NativeWindowAttention with each
model upstream FlashAttention window (no flash_attn package), and an integer traj_save_len (Python 3.12 randint rejects
amago float default). Usage: metamon_challenge.py ARGS... (as metamon.rl.evaluate; --agent must be in MODELS)."""
import runpy, sys
sys.path.insert(0, "/workspace/PufferLib/ocean/pokemon/benchmarks")
import gin, amago.cli_utils
from metamon_opponent import MODELS, NativeWindowAttention
agent = sys.argv[sys.argv.index("--agent") + 1]
_use_config = amago.cli_utils.use_config
def use_config(*args, **kwargs):
    kwargs["finalize"] = False
    out = _use_config(*args, **kwargs)
    gin.bind_parameter("traj_encoders.TformerTrajEncoder.attention_type", NativeWindowAttention)
    gin.bind_parameter("NativeWindowAttention.window", MODELS[agent]["window"])
    gin.bind_parameter("Experiment.traj_save_len", 10_000_000_000)
    return out
amago.cli_utils.use_config = use_config
sys.argv = ["metamon.rl.evaluate"] + sys.argv[1:]
runpy.run_module("metamon.rl.evaluate", run_name="__main__")
