"""Paired duplicate-deal comparison across frozen native builds."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--candidate', type=Path, required=True)
p.add_argument('--reference', type=Path, default=Path('build/poker/equity_v2'))
p.add_argument('--candidate-style', type=int, default=7)
p.add_argument('--reference-style', type=int, default=7)
p.add_argument('--hands', type=int, default=30000)
p.add_argument('--seed', type=int, default=202610182)
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
rows, comparisons = [], []
for players, field, opponents in [(2,'original','1'), (2,'fullhouse','4'),
        (6,'original','1'), (6,'fullhouse','4'), (6,'mixed','1,4,1,4,4')]:
    values = []
    for role, build, style in [('control',args.reference,args.reference_style),
                              ('candidate',args.candidate,args.candidate_style)]:
        label = f'{players}p_{field}_{role}'
        csv_path = args.output / f'{label}.csv'
        binary = build / f'baseline_bench{players}'
        command = [str(binary), str(style), opponents, str(args.hands), str(args.seed),
                   '20000', str(csv_path), label]
        row = json.loads(subprocess.check_output(command, text=True))
        row.update(command=command, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
        rows.append(row)
        with csv_path.open() as f:
            values.append([float(r['mean_bb']) for r in csv.DictReader(f)])
    delta = [b-a for a,b in zip(*values, strict=True)]
    result = {'players':players, 'field':field, 'gain_bb100':100*statistics.mean(delta),
              'ci95_half_width_bb100':196*statistics.stdev(delta)/math.sqrt(len(delta)),
              'independent_duplicate_blocks':len(delta)}
    comparisons.append(result)
    print(json.dumps(result), flush=True)
    (args.output/'results.json').write_text(json.dumps({'matches':rows,'paired':comparisons},indent=2)+'\n')
