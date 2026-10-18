"""Recompute the iteration report without pooling different policy scores."""
import hashlib
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parent / 'strength_v1'


def load(path):
    return json.loads(path.read_text())


def log_result(name, limit=None):
    path = ROOT / f'slumbot_{name}.jsonl'
    values, faced, folds = [], [0]*4, [0]*4
    unresolved = 0
    summary = metadata = None
    with path.open() as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue  # A running writer may not have finished its last line.
            if 'metadata' in row:
                metadata = row['metadata']
            if 'summary' in row:
                summary = row['summary']
            if limit is not None and len(values) >= limit:
                continue
            if 'interrupted' in row:
                unresolved += 1
            if 'winnings_chips' not in row:
                continue
            assert row['hand'] == len(values)
            values.append(row['winnings_chips']/100)
            for decision in row.get('decisions', []):
                if decision['call_chips']:
                    street = decision['street']
                    faced[street] += 1
                    folds[street] += decision['native_action'] < 0
    return {
        'log': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'metadata': metadata, 'hands': len(values), 'net_bb': sum(values),
        'bb100': 100*statistics.mean(values) if values else None,
        'ci95_half_width_bb100': 196*statistics.stdev(values)/math.sqrt(len(values))
            if len(values)>1 else None,
        'unresolved_attempts_in_window': unresolved, 'full_run_summary': summary,
        'pending_outcomes_unknown': summary is None,
        'faced': faced, 'folds': folds,
        'fold_percent': [100*f/n if n else None for f,n in zip(folds,faced)],
        'window': f'first {limit} completed hands' if limit else 'entire retained log',
    }


result = {
    'selection': load(ROOT/'selection.json'),
    'development': {name: log_result(name) for name in
                    ('samples','context','blind','river_retry')},
    'incomplete_attempts': {name: log_result(name) for name in ('control','river')},
    'control_prefix': log_result('confirm_control', 6000),
    'control_prefix_method': load(ROOT/'confirm_control_prefix.json'),
    'local': {},
    'interval_note': 'Approximate normal 95% intervals. Local differences pair independent duplicate-deal blocks. Live differences use independent samples. Development comparisons are exploratory and unadjusted for selection; final candidate selected before its fresh run.',
}
assert result['control_prefix']['hands'] == 6000
assert result['control_prefix']['unresolved_attempts_in_window'] == 0
assert result['control_prefix']['sha256'] == result['control_prefix_method']['source_sha256']
for name in ('samples','context','blind','river','holdout'):
    path = ROOT / f'{name}_local/results.json'
    if path.exists():
        result['local'][name] = load(path)
result['local_hands'] = sum(row['hands'] for suite in result['local'].values()
                            for row in suite['matches'])
path = ROOT/'slumbot_final.jsonl'
if path.exists():
    result['final'] = log_result('final')
    final = result['final']; control = result['control_prefix']
    result['final_difference'] = {
        'gain_bb100': final['bb100']-control['bb100'],
        'ci95_half_width_bb100': math.hypot(final['ci95_half_width_bb100'],
                                         control['ci95_half_width_bb100']),
        'candidate_run_finished': bool(final['full_run_summary']),
        'candidate_clean': bool(final['full_run_summary'] and final['full_run_summary']['complete']),
    }
for name in ('final_equity_tests','final_fullhouse_parity','final_live_replay',
             'final_migration_replay','final_native_manifest','final_manifest',
             'final_control_replay','build_validation','latency2','latency6','selfplay6'):
    path = ROOT/f'{name}.json'
    if path.exists() and path.stat().st_size: result[name] = load(path)
result['local_hands_including_throughput'] = result['local_hands'] + result.get('selfplay6',{}).get('hands',0)
result['retained_completed_live_hands'] = sum(row['hands'] for row in result['development'].values())
result['retained_completed_live_hands'] += sum(row['hands'] for row in result['incomplete_attempts'].values())
result['retained_completed_live_hands'] += log_result('confirm_control')['hands']
result['retained_completed_live_hands'] += result.get('final',{}).get('hands',0)
(ROOT/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({key:result[key] for key in ('selection','local_hands','control_prefix',
    'final','final_difference') if key in result},indent=2))
