#!/usr/bin/env python3
"""Verify the 128-agent throughput run without changing smoke-test evidence."""
import array
import configparser
import json
import math
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
base = root / "build/pokemon_gen9/worker"
log = max((base / "batch-logs/pokemon_gen9").glob("*.ini"), key=lambda p: p.stat().st_mtime)
ini = configparser.ConfigParser(interpolation=None, strict=False)
ini.read(log)
for section, key, value in [("vec", "total_agents", "128"), ("vec", "num_threads", "4"),
        ("vec", "num_buffers", "1"), ("env", "games_per_worker", "16"),
        ("train", "horizon", "4"), ("train", "total_timesteps", "16384")]:
    assert ini[section][key] == value, (section, key)
assert ini["env"]["source_revision"] == "9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e"
directory = base / "batch-checkpoints/pokemon_gen9" / ini["base"]["run_id"]
checkpoints = sorted(directory.glob("*.bin"))
assert [int(p.stem) for p in checkpoints] == [4096, 8192, 12288, 16384]
assert checkpoints[0].read_bytes() != checkpoints[-1].read_bytes()
for checkpoint in checkpoints:
    weights = array.array("f")
    weights.frombytes(checkpoint.read_bytes())
    assert all(math.isfinite(x) for x in weights), checkpoint
transcript = (base / "batch.log").read_text()
assert not re.search(r"\b(?:nan|[+-]?inf)\b", transcript, re.I)
metrics = dict(ini["metrics"])
for key, values in metrics.items():
    assert all(math.isfinite(float(v)) for v in values.split(",")), key
assert float(metrics["agent_steps"].split(",")[-1]) == 16384
result = {"status": "passed", "agents": 128, "workers": 4, "games_per_worker": 16,
    "steps": 16384, "updates": 32, "horizon": 4, "async": False,
    "checkpoint": str(checkpoints[-1].relative_to(root)),
    "finite_saved_weights_and_metrics": True, "weights_changed": True,
    "metrics": metrics, "kind": "End-to-end throughput/integration run; final evaluation deliberately disabled. No strength claim."}
(base / "batch-training.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({k: v for k, v in result.items() if k != "metrics"}, indent=2))
