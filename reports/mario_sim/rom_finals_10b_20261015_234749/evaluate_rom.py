#!/usr/bin/env python3
"""Frozen-policy ROM tests, including repeated single-life clears of every level."""
import argparse
import concurrent.futures
import configparser
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def panels(episodes, levels_only):
    result = [] if levels_only else [
        ('game_all_stages', 32, 'all-stages', 0, 'game', 'evaluation.ini'),
        ('new_game_1_1', 32, 'new-game', 0, 'clear', 'levels.ini'),
        ('fpg_96', 64, 'build/mario_sim/fpg/curriculum', 96, 'fpg', 'evaluation.ini'),
        ('fpg_425', 64, 'build/mario_sim/fpg/curriculum', 425, 'fpg', 'evaluation.ini'),
    ]
    for stage in range(32):
        world, level = stage // 4 + 1, stage % 4 + 1
        result.append((f'level_{world}_{level}', episodes, f'level:{world}-{level}',
                       0, 'clear', 'levels.ini'))
    return result


def execute(root, binary, output, model, panel, seed, nice):
    label, checkpoint = model
    name, episodes, selector, depth, objective, config = panel
    directory = output / label / name
    command = ['nice', '-n', str(nice), str(binary), str(checkpoint),
               str(output / config), str(directory), str(episodes), str(seed), '0',
               selector, str(depth), objective]
    start = time.monotonic()
    with (output / label / f'{name}.log').open('x') as log:
        process = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT,
                                 env={**os.environ, 'OMP_NUM_THREADS': '1'})
    summary_path = directory / 'summary.json'
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else None
    valid = bool(process.returncode == 0 and summary and summary['passed']
                 and summary['episodes_completed'] == episodes and summary['mismatches'] == 0)
    cases = []
    if (directory / 'episodes.jsonl').exists():
        cases = [json.loads(line) for line in (directory / 'episodes.jsonl').read_text().splitlines()]
    if name.startswith('level_') and summary:
        world, level = map(int, name.split('_')[1:])
        valid = valid and summary['starting_stage'] == (world - 1) * 4 + level - 1
        valid = valid and len(cases) == episodes and all(
            case['scene'] == (world - 1) * 4 + level - 1
            and case['start_world'] == world and case['start_level'] == level for case in cases)
    record = {'command': command, 'returncode': process.returncode,
              'seconds': time.monotonic() - start, 'valid': valid, 'summary': summary,
              'failures': [case for case in cases if case['failure']]}
    return label, name, record


def render(output, report, labels, episodes):
    rows = []
    for stage in range(32):
        world, level = stage // 4 + 1, stage % 4 + 1
        row = {'level': f'{world}-{level}'}
        for label in labels:
            result = report['models'][label]['panels'][f'level_{world}_{level}']
            s = result['summary'] or {}
            row[f'{label}_valid'] = result['valid']
            for key in ('rom_successes', 'episodes_requested', 'episodes_completed',
                        'deaths', 'timeouts', 'mean_success_frames', 'mismatches'):
                row[f'{label}_{key}'] = s.get(key)
        rows.append(row)
    with (output / 'per_level.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    text = '# ROM evaluation\n\n'
    if all('game_all_stages' in report['models'][label]['panels'] for label in labels):
        text += '| Existing game/FPG tests | ' + ' | '.join(labels) + ' |\n'
        text += '| --- | ' + ' | '.join('---:' for _ in labels) + ' |\n'
        for title, name, metric, denominator in [
            ('Continuing-game mean progress (pixels)', 'game_all_stages', 'mean_progress_pixels', None),
            ('Continuing-game clear-reward events', 'game_all_stages', 'level_clears', None),
            ('Starting game episodes with any clear-reward event', 'game_all_stages', 'game_clear_episodes', 32),
            ('Full-game wins', 'game_all_stages', 'rom_successes', None),
            ('Fresh 1-1 clears', 'new_game_1_1', 'rom_successes', 32),
            ('FPG depth 96', 'fpg_96', 'rom_successes', 64),
            ('FPG depth 425', 'fpg_425', 'rom_successes', 64),
        ]:
            cells = []
            for label in labels:
                result = report['models'][label]['panels'][name]
                value = result['summary'][metric] if result['valid'] else 'INVALID'
                cells.append(str(value) + (f'/{denominator}' if denominator and result['valid'] else ''))
            text += '| ' + title + ' | ' + ' | '.join(cells) + ' |\n'
        text += '\nContinuing-game clear-reward events include flag contacts and can precede a completed level transition.\n\n'
    text += (f'{episodes} sampled attempts per level per checkpoint, seed {report["seed"]}. '
             f'Each attempt starts naturally at that level with fresh policy state and stops at '
             f'a completed level transition, the first death, or {report["level_frame_limit"]:,} frames. '
             'Clear counts in this table describe only the starting level. '
             'Simulator/ROM parity failures are reported as invalid tests.\n\n')
    text += '| Level | ' + ' | '.join(labels) + ' |\n'
    text += '| --- | ' + ' | '.join('---:' for _ in labels) + ' |\n'
    for row in rows:
        cells = [f'{row[label + "_rom_successes"]}/{episodes}' if row[label + '_valid']
                 else 'INVALID' for label in labels]
        text += '| ' + row['level'] + ' | ' + ' | '.join(cells) + ' |\n'
    text += '\n| Checkpoint | Clears / attempts | Levels with any clears | Levels with all attempts cleared |\n'
    text += '| --- | ---: | ---: | ---: |\n'
    for label in labels:
        results = [report['models'][label]['panels'][f'level_{s // 4 + 1}_{s % 4 + 1}'] for s in range(32)]
        if not all(result['valid'] for result in results):
            text += f'| {label} | INVALID | — | — |\n'
            continue
        wins = [result['summary']['rom_successes'] for result in results]
        text += f'| {label} | {sum(wins)}/{episodes * 32} | {sum(x > 0 for x in wins)}/32 | {sum(x == episodes for x in wins)}/32 |\n'
    text += '\nFull counts, deaths, timeouts, success durations and commands: `summary.json` and `per_level.csv`. '
    text += 'Continuing-game totals and the separate FPG tests measure different tasks.\n'
    (output / 'README.md').write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint', nargs=2, action='append', metavar=('LABEL', 'PATH'), required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--level-config', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repo-root', type=Path, default=ROOT)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--episodes-per-level', type=int, default=32)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--seed', type=int, default=137)
    parser.add_argument('--nice', type=int, default=15)
    parser.add_argument('--levels-only', action='store_true', help='Add per-level tests to an existing main evaluation.')
    args = parser.parse_args()
    if args.episodes_per_level < 1 or args.workers < 1 or not 0 < args.seed <= 0xffffffff:
        parser.error('episodes, workers and seed must be positive; seed must fit uint32')
    labels = [label for label, _ in args.checkpoint]
    if len(labels) != len(set(labels)) or any(label in ('.', '..') or not re.fullmatch(r'[A-Za-z0-9_.-]+', label) for label in labels):
        parser.error('checkpoint labels must be unique simple directory names')
    models = [(label, Path(path).resolve()) for label, path in args.checkpoint]
    root = args.repo_root.resolve()
    binary = (args.binary or root / 'build/mario_sim/fpg/sim2rom').resolve()
    config_text = args.config.read_text()
    config = configparser.ConfigParser(interpolation=None)
    config.read_string(config_text)
    if args.level_config:
        level_text = args.level_config.read_text()
    else:
        config['fpg']['max_frames'] = '6000'
        import io
        stream = io.StringIO()
        config.write(stream)
        level_text = stream.getvalue()
    level_config = configparser.ConfigParser(interpolation=None)
    level_config.read_string(level_text)
    frame_limit = level_config.getint('fpg', 'max_frames')
    if frame_limit <= 0:
        parser.error('level frame limit must be positive')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / 'evaluation.ini').write_text(config_text)
    (output / 'levels.ini').write_text(level_text)
    report = {'created_utc': datetime.now(timezone.utc).isoformat(), 'seed': args.seed,
              'episodes_per_level': args.episodes_per_level, 'level_frame_limit': frame_limit,
              'levels_only': args.levels_only, 'evaluator_sha256': digest(binary),
              'config_sha256': digest(output / 'evaluation.ini'),
              'level_config_sha256': digest(output / 'levels.ini'),
              'rom_sha256': digest(root / 'ocean/retro/roms/smb1_ntsc.nes'), 'models': {}}
    for label, path in models:
        (output / label).mkdir()
        report['models'][label] = {'checkpoint': str(path), 'sha256': digest(path), 'panels': {}}
    jobs = [(model, panel) for panel in panels(args.episodes_per_level, args.levels_only) for model in models]
    write_json(output / 'manifest.json', report)
    completed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = [pool.submit(execute, root, binary, output, model, panel, args.seed, args.nice)
                   for model, panel in jobs]
        for future in concurrent.futures.as_completed(futures):
            label, name, result = future.result()
            report['models'][label]['panels'][name] = result
            completed += 1
            s = result['summary'] or {}
            print(f'{completed}/{len(jobs)} {label} {name}: {s.get("rom_successes", "?")}/'
                  f'{s.get("episodes_requested", "?")} valid={result["valid"]}', flush=True)
            write_json(output / 'progress.json', report)
    report['all_validated'] = all(result['valid'] for model in report['models'].values()
                                  for result in model['panels'].values())
    write_json(output / 'summary.json', report)
    render(output, report, labels, args.episodes_per_level)
    print(f'DONE {output} all_validated={report["all_validated"]}', flush=True)
    return 0 if report['all_validated'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
