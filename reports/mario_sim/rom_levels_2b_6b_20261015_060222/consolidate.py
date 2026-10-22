from pathlib import Path
from datetime import datetime, timezone
import copy
import csv
import hashlib
import importlib.util
import json
import shutil

repo = Path('/home/felix/puffertank/pufferlib')
paths = json.loads(Path(Path('/tmp/mario_level_eval_paths').read_text()).read_text())
root = Path(paths['local'])
out = root / 'results'
original = out / 'summary.json'
backup = out / 'per_level_summary.json'
if not backup.exists():
    shutil.copyfile(original, backup)
report = json.loads(backup.read_text())
sources = {'steps_2_15b': Path(Path('/tmp/mario_interim_eval_path').read_text().strip()),
           'steps_6_44b': Path(Path('/tmp/mario_6442m_eval_path').read_text().strip())}
labels = list(sources)
for label, source in sources.items():
    old = json.loads((source / 'summary.json').read_text())
    assert report['models'][label]['sha256'] == old['checkpoint']['sha256'][Path(old['checkpoint']['checkpoint']).name]
    before = [json.loads(line) for line in (source / 'new_game_1_1/episodes.jsonl').read_text().splitlines()]
    after = [json.loads(line) for line in (out / label / 'level_1_1/episodes.jsonl').read_text().splitlines()]
    assert [{key: case[key] for key in before[0]} for case in after] == before, label
    for name in ('game_all_stages', 'new_game_1_1', 'fpg_96', 'fpg_425'):
        record = copy.deepcopy(old['panels'][name])
        record['valid'] = bool(record['returncode'] == 0 and record['summary']['passed'])
        record['reused_existing_evaluation'] = True
        record['source_report'] = str(source / 'summary.json')
        record['evaluator_sha256'] = old['evaluator_sha256']
        report['models'][label]['panels'][name] = record
    report['models'][label]['historical_main_report_sha256'] = hashlib.sha256((source / 'summary.json').read_bytes()).hexdigest()
    report['models'][label]['repeated_1_1_matches_existing_32_episodes_exactly'] = True
report['consolidated_utc'] = datetime.now(timezone.utc).isoformat()
report['main_tests_retained'] = True
report['levels_only'] = False
report['metric_clarification'] = 'Continuing-game level_clears counts clear-reward events, including flag contacts. Per-level rom_successes requires a completed level exit and terminates on first death.'
report['all_validated'] = all(x['valid'] for m in report['models'].values() for x in m['panels'].values())
report['new_per_level_attempts'] = sum((x.get('summary') or {}).get('episodes_requested', 32) for m in report['models'].values()
                                      for name, x in m['panels'].items() if name.startswith('level_'))
report['new_per_level_mismatches'] = sum((x.get('summary') or {}).get('mismatches', 0) for m in report['models'].values()
                                        for name, x in m['panels'].items() if name.startswith('level_'))
rows = []
for stage in range(32):
    name = f'level_{stage // 4 + 1}_{stage % 4 + 1}'
    a, b = (report['models'][label]['panels'][name] for label in labels)
    row = {'level': f'{stage // 4 + 1}-{stage % 4 + 1}',
           'valid': a['valid'] and b['valid'],
           'old_clears': (a.get('summary') or {}).get('rom_successes'),
           'new_clears': (b.get('summary') or {}).get('rom_successes')}
    row['clear_count_change'] = row['new_clears'] - row['old_clears'] if row['valid'] else None
    rows.append(row)
with (out / 'changes.csv').open('w', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
report['per_level_changes'] = rows
report['per_level_totals'] = {}
for label, model in report['models'].items():
    tests = [model['panels'][f'level_{s // 4 + 1}_{s % 4 + 1}'] for s in range(32)]
    valid = all(test['valid'] for test in tests)
    wins = [(test.get('summary') or {}).get('rom_successes', 0) for test in tests]
    report['per_level_totals'][label] = {
        'valid': valid, 'clears': sum(wins) if valid else None,
        'attempts_requested': 1024,
        'attempts_completed': sum((test.get('summary') or {}).get('episodes_completed', 0) for test in tests),
        'levels_with_any_clear': sum(x > 0 for x in wins) if valid else None,
        'levels_with_all_clears': sum(x == 32 for x in wins) if valid else None}
original.write_text(json.dumps(report, indent=2) + '\n')
spec = importlib.util.spec_from_file_location('mario_evaluate_rom', repo / 'ocean/mario_sim/evaluate_rom.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.render(out, report, labels, 32)
readme = out / 'README.md'
text = readme.read_text()
text += '\nThe game/FPG tests above are retained from their completed evaluations. The 2,048 per-level attempts are new. Both 32-episode 1-1 retests reproduced the prior episode records exactly.\n'
text += f'\nAll panels validated: {report["all_validated"]}. New per-level simulator/ROM mismatches: {report["new_per_level_mismatches"]}.\n'
readme.write_text(text)
(root / 'README.md').write_text(text.replace('`summary.json`', '`results/summary.json`').replace('`per_level.csv`', '`results/per_level.csv`'))
for source in sources.values():
    p = source / 'README.md'
    link = f'\n[Paired per-level retest and consolidated game/FPG report]({readme})\n'
    if link not in p.read_text():
        with p.open('a') as stream:
            stream.write(link)
shutil.copyfile(__file__, root / 'consolidate.py')
print(json.dumps({'report': str(readme), 'all_validated': report['all_validated'],
                  'mismatches': report['new_per_level_mismatches'],
                  'totals': report['per_level_totals'], 'changes': rows}, indent=2))
