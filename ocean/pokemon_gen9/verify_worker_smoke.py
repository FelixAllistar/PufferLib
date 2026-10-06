#!/usr/bin/env python3
"""Check the artifacts from the completed source-worker/PufferLib PPO smoke."""
import array
import configparser
import hashlib
import json
import math
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
base = root / "build/pokemon_gen9/worker"
logs = sorted((base / "logs/pokemon_gen9").glob("*.ini"), key=lambda p: p.stat().st_mtime)
assert logs, "No completed trainer log"
log = logs[-1]
ini = configparser.ConfigParser(interpolation=None, strict=False)
ini.read(log)
assert ini["env"]["source_revision"] == "9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e"
assert ini["env"]["worker_abi"] == "1"
assert int(ini["train"]["total_timesteps"]) == 1024
run_id = ini["base"]["run_id"]
directory = base / "checkpoints/pokemon_gen9" / run_id
checkpoints = sorted(directory.glob("*.bin"))
assert [int(p.stem) for p in checkpoints] == list(range(128, 1025, 128))
first = checkpoints[0].read_bytes()
last = checkpoints[-1].read_bytes()
assert len(first) == len(last) and len(last) % 4 == 0 and first != last
weights = array.array("f")
weights.frombytes(last)
assert all(math.isfinite(x) for x in weights), "Nonfinite checkpoint weight"
transcript = (base / "smoke.log").read_text()
assert not re.search(r"\b(?:nan|[+-]?inf)\b", transcript, re.I)
assert "Epoch                 8" in transcript
assert "[metrics]" in log.read_text()
metrics = dict(ini["metrics"])
for key, values in metrics.items():
    for value in values.split(","):
        assert math.isfinite(float(value)), (key, value)
result = {"status": "passed", "backend": "pinned Showdown CPU workers + PufferLib CUDA PPO",
          "steps": 1024, "updates": 8, "agents": 8, "games_per_worker": 4,
          "checkpoint": str(checkpoints[-1].relative_to(root)), "parameters": len(weights),
          "weights_changed_after_first_update": True, "finite_weights_and_metrics": True,
          "checkpoint_sha256": hashlib.sha256(last).hexdigest(),
          "log": str(log.relative_to(root)), "metrics": metrics,
          "scope": "Training/integration smoke; no playing-strength qualification"}
(base / "training-smoke.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({k: v for k, v in result.items() if k != "metrics"}, indent=2))
