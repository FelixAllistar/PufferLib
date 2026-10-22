from pathlib import Path
import concurrent.futures
import configparser
import datetime
import hashlib
import json
import shutil
import subprocess
import time

repo = Path('/home/felix/puffertank/pufferlib')
out = Path('/tmp/mario_6442m_eval_path').read_text().strip()
out = Path(out)
manifest = json.loads((out / 'download_manifest.json').read_text())
model = manifest['checkpoint']
binary = repo / 'build/mario_sim/fpg/sim2rom'
shutil.copyfile(repo / 'reports/mario_sim/rom_eval_run12_20261012_102453/new_game.ini', out / 'new_game.ini')
shutil.copyfile(__file__, out / 'run_evaluation.py')

training = configparser.ConfigParser()
evaluation = configparser.ConfigParser()
training.read(out / 'training.ini')
evaluation.read(out / 'evaluation.ini')
differences = {}
for section in ('env', 'policy', 'game', 'fpg'):
    for key, value in evaluation[section].items():
        original = training.get(section, key, fallback=None)
        if original != value:
            differences[f'{section}.{key}'] = {'training': original, 'evaluation': value}
print('CONFIG_DIFFERENCES', json.dumps(differences), flush=True)

panels = [
    ('game_all_stages', 32, 'all-stages', 0, 'game', 'evaluation.ini'),
    ('new_game_1_1', 32, 'new-game', 0, 'clear', 'new_game.ini'),
    ('fpg_96', 64, 'build/mario_sim/fpg/curriculum', 96, 'fpg', 'evaluation.ini'),
    ('fpg_425', 64, 'build/mario_sim/fpg/curriculum', 425, 'fpg', 'evaluation.ini'),
]

def evaluate(panel):
    name, episodes, bank, depth, objective, config = panel
    command = [str(binary), model, str(out / config), str(out / name),
               str(episodes), '137', '0', bank, str(depth), objective]
    start = time.time()
    with (out / (name + '.log')).open('w') as log:
        result = subprocess.run(command, cwd=repo, stdout=log, stderr=subprocess.STDOUT)
    summary_path = out / name / 'summary.json'
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else None
    record = {'command': command, 'returncode': result.returncode,
              'elapsed_seconds': time.time() - start, 'summary': summary}
    if summary and not summary['passed']:
        record['failures'] = [case for line in (out / name / 'episodes.jsonl').read_text().splitlines()
                              if (case := json.loads(line)).get('failure')]
    print('PANEL', name, json.dumps(record), flush=True)
    return name, record

results = {}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    futures = [pool.submit(evaluate, panel) for panel in panels]
    for future in concurrent.futures.as_completed(futures):
        name, result = future.result()
        results[name] = result
        (out / 'partial_results.json').write_text(json.dumps(results, indent=2) + '\n')

baselines = {}
old12 = repo / 'reports/mario_sim/rom_eval_run12_20261012_102453'
old42 = repo / 'reports/mario_sim/trend_checkin_20261014_215952'
old26 = repo / 'reports/mario_sim/trend_checkin_20261013_141556'
for name in ('game_all_stages', 'new_game_1_1', 'fpg_96', 'fpg_425'):
    path = old12 / name / 'summary.json'
    baselines['run12_' + name] = json.loads(path.read_text())
for name, path in [('run42_game', old42 / 'rom_run42_game/summary.json'),
                   ('run42_fpg425', old42 / 'rom_run42_fpg425/summary.json')]:
    baselines[name] = json.loads(path.read_text())
report = {'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'checkpoint': manifest, 'evaluator_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
          'seed': 137, 'deterministic': False, 'config_differences': differences,
          'all_validated': all(x['returncode'] == 0 and x['summary'] and x['summary']['passed']
                               for x in results.values()),
          'panels': results, 'baselines': baselines}
(out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
print('DONE', out / 'summary.json', flush=True)
