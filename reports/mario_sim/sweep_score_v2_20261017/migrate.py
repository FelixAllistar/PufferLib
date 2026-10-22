"""One-time, auditable conversion of the October 17 sweep to score version 2."""
import fcntl
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys

REPORT = Path(__file__).resolve().parent
ROOT = REPORT.parents[2]
HISTORY = ROOT / 'logs/mario_sim/sweep_20261017.history'
LOGS = HISTORY.parent
spec = importlib.util.spec_from_file_location('mario_sweep_worker', ROOT / 'ocean/mario_sim/sweep_worker.py')
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def prepare():
    original = HISTORY.read_text()
    lines = [line for line in original.splitlines() if line.strip()]
    magic, dimension = lines[0].split()
    assert magic == 'PUFFER_SWEEP_HISTORY_V1'
    dimension = int(dimension)
    definitions = [line.split() for line in lines[1:dimension+1]]
    rows = {}
    for line in lines[dimension+1:]:
        fields = line.split()
        assert len(fields) == dimension + 4
        run = int(fields[0])
        assert run not in rows
        rows[run] = fields
    trials = {}
    for path in LOGS.glob('sweep_*.eval/trial.json'):
        trial = json.loads(path.read_text())
        settings = trial['settings']
        if (ROOT / settings['sweep.history_path']).resolve() != HISTORY.resolve():
            continue
        run = int(trial['run'].rsplit('_', 1)[1])
        assert run not in trials, f'duplicate trial index {run}'
        trials[run] = (path, trial)
    assert all(int(row[3]) or run in trials for run, row in rows.items())
    assert len(trials) >= 6, 'the active trial must finish before conversion'
    updates = []
    for run, (path, trial) in sorted(trials.items()):
        final = path.parent / 'final'
        backup = final / 'score.pre_v2.json'
        if not backup.exists():
            shutil.copy2(final / 'score.json', backup)
        checkpoint = Path(trial['checkpoint'])
        assert checkpoint.exists()
        fingerprint = digest(checkpoint)
        result = worker.fixed_evaluation(checkpoint, trial['settings'], final)
        assert result['score_version'] == 2 and 0 <= result['score'] <= 1
        assert result['reused_panels'] >= 44, 'existing ROM tests must remain reusable'
        assert len(result['panels']) == 58
        assert all(panel['passed'] and panel['mismatches'] == 0 for panel in result['panels'].values())
        assert digest(checkpoint) == fingerprint
        updated = dict(trial)
        updated['previous_objective'] = {'score': trial['score'], 'cost': trial['cost'],
                                         'objective': '0.75 * level clears + 0.25 * FPG 96/256/425'}
        updated.update(score=result['score'], score_version=2,
                       cost=trial['cost'] + result['additional_seconds'])
        if run in rows:
            assert rows[run][3] == '0'
            fields = list(rows[run])
        else:
            assert run > max(rows), 'only the just-finished trial can be unflushed'
            values = [float(trial['settings'][definition[0]]) for definition in definitions]
            for index, definition in enumerate(definitions):
                if definition[0] == 'train.total_timesteps' and trial['steps'] < .98 * values[index]:
                    values[index] = max(float(definition[1]) * 1.00001, trial['steps'])
            fields = [str(run), '0', '0', '0'] + [f'{value:.9g}' for value in values]
        fields[1], fields[2] = f'{result["score"]:.9g}', f'{updated["cost"]:.9g}'
        assert len(fields) == dimension + 4
        for field, definition in zip(fields[4:], definitions):
            value, low, high = float(field), float(definition[1]), float(definition[2])
            assert math.isfinite(value) and low - 1e-5*max(abs(low), 1) <= value <= high + 1e-5*max(abs(high), 1)
        rows[run] = fields
        entry = {'run': run, 'old_score': trial['score'], 'score': result['score'],
                 'level_clear_rate': result['level_clear_rate'],
                 'game_progress_rate': result['game_progress_rate'],
                 'fpg_clear_rate': result['fpg_clear_rate'], 'reused_panels': result['reused_panels'],
                 'parity_mismatches': sum(panel['mismatches'] for panel in result['panels'].values()),
                 'checkpoint_sha256': fingerprint, 'trial_path': str(path), 'trial': updated}
        updates.append(entry)
        print(json.dumps({key: value for key, value in entry.items() if key != 'trial'}), flush=True)
    # This sweep has not yet reached an intermediate evaluation rung.
    rung = LOGS / 'sweep_20261017.rungs.jsonl'
    assert not rung.exists() or not rung.read_text().strip(), 'rung history also needs conversion'
    header = '\n'.join(lines[:dimension+1])
    converted = header + '\n\n' + '\n\n'.join(' '.join(rows[run]) for run in sorted(rows)) + '\n'
    receipt = {'score_version': 2, 'history_sha256_before': digest(HISTORY),
               'history_text': converted, 'updates': updates,
               'completed': sum(int(row[3]) == 0 for row in rows.values()),
               'next_run': max(rows) + 1}
    (REPORT / 'history.pre_v2').write_text(original)
    write_json(REPORT / 'prepared.json', receipt)
    print('PREPARED', receipt['completed'], 'completed; next run', receipt['next_run'], flush=True)


def commit():
    receipt = json.loads((REPORT / 'prepared.json').read_text())
    with HISTORY.open('r+') as stream:
        fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        assert digest(HISTORY) == receipt['history_sha256_before'], 'history changed after preparation'
        for update in receipt['updates']:
            trial = update['trial']
            assert digest(Path(trial['checkpoint'])) == update['checkpoint_sha256']
            write_json(Path(update['trial_path']), trial)
            ini = LOGS / f'{trial["run"]}.ini'
            text = ini.read_text()
            text, count = re.subn(r'^env/fixed_eval_score\s*=.*$',
                                 f'env/fixed_eval_score = {trial["score"]:.12g}', text, flags=re.M)
            assert count == 1
            temporary = ini.with_suffix('.ini.tmp')
            temporary.write_text(text)
            temporary.replace(ini)
        temporary = HISTORY.with_suffix('.history.tmp')
        with temporary.open('w') as converted:
            converted.write(receipt['history_text'])
            converted.flush()
            os.fsync(converted.fileno())
        temporary.replace(HISTORY)
    receipt['history_sha256_after'] = digest(HISTORY)
    receipt.pop('history_text')
    for update in receipt['updates']:
        update.pop('trial')
    write_json(REPORT / 'migration.json', receipt)
    print(json.dumps(receipt, indent=2), flush=True)


if __name__ == '__main__':
    {'prepare': prepare, 'commit': commit}[sys.argv[1]]()
