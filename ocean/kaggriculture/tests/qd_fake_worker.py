#!/usr/bin/env python3
"""Small process fixture for QD scheduling tests; never calls CUDA or PufferLib."""
import json
import os
from pathlib import Path
import sys
import time

mode = sys.argv[1]
config = dict(arg[2:].split("=", 1) for arg in sys.argv[2:] if arg != "--headless")
if mode == "train":
    run = config["base.run_id"]
    if run == "qd_0002":
        print("synthetic failed candidate", flush=True)
        sys.exit(2)
    path = Path(config["base.checkpoint_dir"]) / "kaggriculture" / run
    path.mkdir(parents=True, exist_ok=True)
    (path / "0000000000000000.bin").write_bytes(Path(config["base.load_model_path"]).read_bytes())
    (path / "device.json").write_text(json.dumps({"device": os.environ["CUDA_VISIBLE_DEVICES"],
        "source": config["base.load_model_path"], "started": time.time()}))
    time.sleep(.1)
    batch = int(config["vec.total_agents"]) * int(config["train.horizon"])
    steps = int(config["train.total_timesteps"]) // batch * batch
    (path / f"{steps:016d}.bin").write_text(json.dumps(config))
metrics = dict(root_money=12000, reset_money=0, reset_fraction=0, plants=40,
    animal_places=5, ending_plots=2, crop_ref_value=100, animal_ref_value=200,
    animal_delay=.3, animal_seen=1, plot2_delay=.4, plot3_delay=1)
assert os.environ["KAG_QD_METRICS"] == "1"
print("KAG_QD_METRICS " + json.dumps(metrics), flush=True)
print("╭────╮\n│ Steps 0.1M SPS 100K │\n╰────╯", flush=True)
if mode == "match":
    assert config["env.reset_state_prob"] == "0"
    print("CUDA_EVAL env=kaggriculture score=0.625 perf=0.625 games=" +
        config["base.eval_episodes"], flush=True)
