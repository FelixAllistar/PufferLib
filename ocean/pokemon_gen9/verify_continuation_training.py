#!/usr/bin/env python3
"""Validate finite PPO artifacts and ABI identity for the autonomous batch backend."""
import array
import configparser
import hashlib
import json
import math
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
mode = sys.argv[1]
assert mode in ('smoke', 'train-bench')
bench = mode == 'train-bench'
base = root / 'build/pokemon_gen9/continuation' / mode
log = max((base / 'logs/pokemon_gen9_batch').glob('*.ini'), key=lambda p: p.stat().st_mtime)
ini = configparser.ConfigParser(interpolation=None, strict=False)
ini.read(log)
schema = json.loads((root / 'build/pokemon_gen9/compact/schema.json').read_text())
engine = json.loads((root / 'build/pokemon_gen9/continuation/manifest.json').read_text())
for section, key, value in [('env', 'worker_abi', '2'), ('env', 'observation_schema', schema['schema_sha256']),
        ('vec', 'total_agents', str(256 if bench else 8)), ('vec', 'num_threads', str(4 if bench else 1)),
        ('train', 'total_timesteps', str(32768 if bench else 1024))]:
    assert ini[section][key] == value, (section, key, ini[section][key])
assert ini['env']['source_revision'] == '9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e'
checkpoints = sorted((base / 'checkpoints/pokemon_gen9_batch' / ini['base']['run_id']).glob('*.bin'))
assert [int(p.stem) for p in checkpoints] == ([8192, 16384, 24576, 32768] if bench else list(range(128, 1025, 128)))
first, last = checkpoints[0].read_bytes(), checkpoints[-1].read_bytes()
assert len(first) == len(last) and first != last
for checkpoint in checkpoints:
    weights = array.array('f')
    weights.frombytes(checkpoint.read_bytes())
    assert all(math.isfinite(x) for x in weights), checkpoint
transcript = (base / 'trainer.log').read_text()
assert not re.search(r'\b(?:nan|[+-]?inf)\b', transcript, re.I)
metrics = dict(ini['metrics'])
for key, values in metrics.items():
    assert all(math.isfinite(float(x)) for x in values.split(',')), key
assert float(metrics['agent_steps'].split(',')[-1]) == (32768 if bench else 1024)
result = {'status': 'passed', 'mode': mode, 'agents': 256 if bench else 8,
    'workers': 4 if bench else 1, 'steps': 32768 if bench else 1024,
    'finite_weights_and_metrics': True, 'weights_changed': True, 'parameters': len(weights),
    'abi': 2, 'schema_sha256': schema['schema_sha256'],
    'continuation_source_ir_sha256': engine['source_ir_sha256'],
    'kernel_source_ir_sha256': engine['kernel_source_ir_sha256'],
    'checkpoint': str(checkpoints[-1].relative_to(root)), 'checkpoint_sha256': hashlib.sha256(last).hexdigest(),
    'log': str(log.relative_to(root)), 'metrics': metrics,
    'scope': 'PPO integration and throughput only; no playing-strength qualification'}
(base / 'validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k: v for k, v in result.items() if k != 'metrics'}, indent=2))
