#!/usr/bin/env python3
"""Bounded event-space CMA/archive experiment and an uninterrupted baseline.

Selection only sees one frozen common panel/seed. Held-out reports run after
search and baseline finish, and never update the archive, champion, or CMA.
"""
import argparse
from collections import Counter
import fcntl
import itertools
import json
import math
from pathlib import Path
import re
from types import SimpleNamespace

import numpy as np
from behavior_experiment import event_summary, train_command
from cma_qd import Archive, CMAES, sparse_l2
from qd import aggregate, digest, dump, evaluate, execute, read_ini, snapshot

BATCH_STEPS = 1024*512


def event_weights(x):
    """Stay inside the validated magnitude: each coordinate and L1 <= .5."""
    values = np.tanh(np.asarray(x, dtype=float))
    if values.shape != (2,) or not np.all(np.isfinite(values)):
        raise ValueError('two finite search coordinates required')
    return (.5*values/max(1.0, float(np.abs(values).sum()))).tolist()


def event_descriptor(rows):
    base = aggregate(rows)
    raw = np.asarray([r.get('behavior_events', []) for r in rows], dtype=float)
    if (any(r.get('behavior_version') != 1 for r in rows) or raw.shape != (len(rows), 2)
            or not np.all(np.isfinite(raw)) or np.any(raw < 0) or np.any(raw > [1, 6])
            or np.any(raw != np.floor(raw))):
        raise ValueError('invalid event profile')
    # Per-battle cap before averaging matches the actual reward objective.
    base['events'] = (np.minimum(raw, [1, 3])/[1, 3]).mean(axis=0).tolist()
    species_teams = Counter(','.join(map(str, sorted(r['species_ids']))) for r in rows)
    base['species_teams'] = {k: v/len(rows) for k, v in species_teams.items()}
    return base


def event_distance(a, b):
    """Half weight on events; quarter each on complete species/set teams.

    Marginal species/pair/lead/style summaries are diagnostics, not admission
    features. Set IDs include movesets. Lead diversity is reported separately.
    """
    event_sq = float(np.mean((np.asarray(a['events'])-np.asarray(b['events']))**2))
    return math.sqrt(max(0, .5*event_sq + .25*sparse_l2(a['teams'], b['teams'])/2
                         + .25*sparse_l2(a['species_teams'], b['species_teams'])/2))


def identity(label, checkpoint):
    path = Path(checkpoint).resolve(strict=True)
    return dict(id=label, checkpoint=str(path), checkpoint_sha256=digest(path),
                config_sha256=digest(path.parent/'config.ini'))


def validate(record):
    if (digest(record['checkpoint']) != record['checkpoint_sha256'] or
            digest(Path(record['checkpoint']).parent/'config.ini') != record['config_sha256']):
        raise ValueError('checkpoint or saved config changed')


def train(args, parent, label, weights, seed, steps):
    job = Path(args.out)/'jobs'/label
    job.mkdir(parents=True, exist_ok=True)
    target = job/'trained.json'
    expected = dict(parent=parent, weights=weights, train_seed=seed, requested_steps=steps)
    if target.exists():
        record = json.loads(target.read_text())
        if any(record[k] != v for k, v in expected.items()):
            raise ValueError('training cache mismatch')
        validate(record)
        return record
    log = job/'train.log'
    if log.exists():
        raise ValueError(f'incomplete training in {job}; no silent restart of optimizer/budget')
    local = SimpleNamespace(trainer=args.trainer, train_steps=steps)
    cmd = train_command(local, parent, job, label, seed, weights)
    dump(job/'command.json', cmd)
    execute(cmd, log)
    matches = re.findall(r'PK_BEHAVIOR_CONFIG v=1 weights=([^ ]+) early_turns=5 paralysis_cap=3', log.read_text())
    actual = [float(x) for x in matches[-1].split(',')] if matches else []
    if len(actual) != 2 or not np.allclose(actual, weights, rtol=1e-9, atol=1e-12):
        raise ValueError('event reward handshake mismatch')
    if 'PK_PERSONALITY_CONFIG v=1 weights=0,0,0,0,0' not in log.read_text():
        raise ValueError('old personality rewards must be off')
    checkpoints = sorted((job/'checkpoints'/'pokemon'/label).glob('*.bin'))
    if not checkpoints or int(checkpoints[-1].stem) != steps//BATCH_STEPS*BATCH_STEPS:
        raise ValueError('missing final checkpoint or training budget mismatch')
    checkpoint = checkpoints[-1]
    cfg = read_ini(checkpoint.parent/'config.ini')
    for flag in cmd[3:]:
        key, value = flag[2:].split('=', 1)
        section, key = key.split('.', 1)
        saved = cfg[section][key]
        if saved != value:
            try:
                equal = math.isclose(float(saved), float(value), rel_tol=1e-9, abs_tol=1e-12)
            except ValueError:
                equal = False
            if not equal:
                raise ValueError(f'saved config mismatch: {section}.{key}')
    record = {**identity(label, checkpoint), **expected, 'actual_steps': int(checkpoint.stem)}
    dump(target, record)
    return record


def score(args, record, panel, panel_name, seed):
    validate(record)
    folder = Path(args.out)/'evaluations'/panel_name/f'seed{seed}'/record['id']
    target = folder/'result.json'
    contract = dict(panel=panel, seed=seed, games_per_opponent=args.games,
                    checkpoint_sha256=record['checkpoint_sha256'], config_sha256=record['config_sha256'])
    if target.exists():
        result = json.loads(target.read_text())
        if result['evaluation_contract'] != contract:
            raise ValueError('evaluation cache mismatch')
        return result
    report = evaluate(args.evaluator, record['checkpoint'], panel, args.games, seed, 512, folder)
    report.update(event_summary(folder))
    matches = [[json.loads(line[11:]) for line in log.read_text().splitlines()
                if line.startswith('PK_PROFILE ')] for log in sorted(folder.glob('panel_*.log'))]
    # Balance split halves within each opponent, not across opponents.
    first = list(itertools.chain.from_iterable(rows[:len(rows)//2] for rows in matches))
    second = list(itertools.chain.from_iterable(rows[len(rows)//2:] for rows in matches))
    report['behavior'] = event_descriptor(first+second)
    report['noise'] = event_distance(event_descriptor(first), event_descriptor(second))
    result = {**record, **report, 'evaluation_contract': contract}
    dump(target, result)
    return result


def pairwise(records):
    return [dict(a=a['id'], b=b['id'],
                 event_l2=float(np.linalg.norm(np.asarray(a['behavior']['events'])-b['behavior']['events'])),
                 whole_team_l2=math.sqrt(sparse_l2(a['behavior']['teams'], b['behavior']['teams'])),
                 whole_species_team_l2=math.sqrt(sparse_l2(a['behavior']['species_teams'], b['behavior']['species_teams'])),
                 matchup_l2=float(np.linalg.norm(np.asarray(list(a['payoffs'].values()))-list(b['payoffs'].values()))),
                 archive_distance=event_distance(a['behavior'], b['behavior']))
            for a, b in itertools.combinations(records, 2)]


def next_parent(archive, previous):
    # Revisit a retained region, not just the overall winner. One common parent
    # per generation keeps the zero-weight control genuinely parent-matched.
    return max(archive.records, key=lambda r: (event_distance(r['behavior'], previous['behavior']),
                                                r['quality'], r['id']))


def freeze_inputs(args):
    out = Path(args.out)
    initial = identity('initial', snapshot(args.parent, out/'inputs'/'initial'))
    warm = [identity(f'warm{i:02}', snapshot(path, out/'inputs'/f'warm{i:02}'))
            for i, path in enumerate(args.warmstart)]
    source = json.loads(Path(args.panel).read_text())['opponents']
    for ref in source:
        if digest(ref['path']) != ref['sha256'] or digest(Path(ref['path']).parent/'config.ini') != ref['config_sha256']:
            raise ValueError('selection panel changed')
    paths = [ref['path'] for ref in source]
    if set(map(digest, paths)) & set(map(digest, args.holdout)):
        raise ValueError('selection and held-out opponents must be disjoint')
    panels = {}
    for label, selected in (('selection', paths), ('holdout', args.holdout)):
        panels[label] = []
        for i, path in enumerate(selected):
            copied = snapshot(path, out/'inputs'/label/str(i))
            panels[label].append(dict(path=copied, team='None'))
    files = [args.trainer, args.evaluator, args.panel, 'config/default.ini', 'config/pokemon.ini']
    files += [str(p) for p in Path('ocean/pokemon').glob('*.h')]
    files += [f'ocean/pokemon/{f}' for f in ('bridge.zig', 'data/catalog.json', 'event_qd.py',
                                           'cma_qd.py', 'qd.py', 'behavior_experiment.py')]
    files += [str(p) for p in (out/'inputs').rglob('*') if p.is_file()]
    contract = dict(version=1, args=vars(args), initial=initial, warmstarts=warm, panels=panels,
                    files={str(Path(p).resolve()): digest(p) for p in files})
    path = out/'contract.json'
    if path.exists() and json.loads(path.read_text()) != contract:
        raise ValueError('run contract changed; use a new output directory')
    dump(path, contract)
    return initial, warm, panels, contract


def verify_inputs(contract):
    if any(digest(path) != sha for path, sha in contract['files'].items()):
        raise ValueError('run input changed')


def run(args):
    args.out = str(Path(args.out).resolve())
    for name in ('trainer', 'evaluator', 'parent', 'panel'):
        setattr(args, name, str(Path(getattr(args, name)).resolve(strict=True)))
    for name in ('warmstart', 'holdout'):
        setattr(args, name, [str(Path(p).resolve(strict=True)) for p in getattr(args, name)])
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with open(out/'run.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        initial, warm, panels, contract = freeze_inputs(args)
        archive = Archive(radius=.12, capacity=32, distance=event_distance)
        saved = out/'state.json'
        if saved.exists():
            state = json.loads(saved.read_text())
            archive.records = state['archive']
            champion, parent = state['champion'], state['parent']
            es, start = CMAES.restore(state['es']), state['generation']
        else:
            records = [score(args, r, panels['selection'], 'selection', args.seed+1000) for r in [initial]+warm]
            archive.add_batch(records)
            champion = max(records, key=lambda r: (r['quality'], r['id']))
            parent = records[0]
            es, start = CMAES(dimension=2, population=4, seed=args.seed, sigma=1), 0
        def save(generation, phase):
            verify_inputs(contract)
            dump(saved, dict(version=1, generation=generation, phase=phase,
                             archive=archive.records, champion=champion, parent=parent, es=es.state(),
                             search_requested_steps=generation*5*args.train_steps,
                             search_actual_steps=generation*5*(args.train_steps//BATCH_STEPS)*BATCH_STEPS,
                             baseline_target_steps=args.generations*5*(args.train_steps//BATCH_STEPS)*BATCH_STEPS))
        save(start, 'search' if start < args.generations else 'baseline_pending')
        for generation in range(start, args.generations):
            verify_inputs(contract)
            xs = es.ask(mirrored=True)
            weights = [event_weights(x) for x in xs]
            folder = out/'generations'/f'g{generation+1:02}'
            folder.mkdir(parents=True, exist_ok=True)
            plan = dict(parent=parent['checkpoint'], coordinates=xs.tolist(), weights=weights,
                        train_seed=args.seed+generation, event_order=['sleep', 'paralysis'])
            plan_path = folder/'plan.json'
            if plan_path.exists() and json.loads(plan_path.read_text()) != plan:
                raise ValueError('generation replay mismatch')
            dump(plan_path, plan)
            print(f'Generation {generation+1}/{args.generations}: parent={parent["id"]}; event weights={weights}', flush=True)
            batch = []
            for i, w in enumerate(weights+[[0.0, 0.0]]):
                label = f'g{generation+1:02}_'+(f'c{i:02}' if i < 4 else 'control')
                trained = train(args, parent['checkpoint'], label, w, args.seed+generation, args.train_steps)
                batch.append(score(args, trained, panels['selection'], 'selection', args.seed+1000))
            ranks = archive.add_batch(batch)
            es.tell([ranks[r['id']] for r in batch[:4]])
            champion = max([champion]+batch, key=lambda r: (r['quality'], r['id']))
            dump(folder/'summary.json', dict(parent=parent['id'], ranks=ranks, batch=batch,
                     archive_ids=[r['id'] for r in archive.records], champion_id=champion['id'],
                     diversity=pairwise(archive.records)))
            parent = next_parent(archive, parent)
            save(generation+1, 'search' if generation+1 < args.generations else 'baseline_pending')
            print(f'Generation {generation+1} complete: archive={len(archive.records)}, '
                  f'champion={champion["id"]} score={champion["quality"]:.4f}; next parent={parent["id"]}', flush=True)
        # One trainer invocation, one optimizer/history trajectory, original
        # common initial checkpoint. Never branch from a search-selected parent.
        steps = args.generations*5*(args.train_steps//BATCH_STEPS)*BATCH_STEPS
        print(f'Search stopped at generation {args.generations}. Running uninterrupted win-only baseline: {steps} steps.', flush=True)
        baseline = train(args, initial['checkpoint'], 'baseline', [0.0, 0.0], args.seed, steps)
        baseline_common = score(args, baseline, panels['selection'], 'selection', args.seed+1000)
        save(args.generations, 'heldout_reporting')
        # Freeze identities before seeing any held-out scores. These reports are
        # never fed back into CMA, archive admission, or champion selection.
        selected = {r['id']: r for r in archive.records+[champion, baseline_common, initial]}
        dump(out/'finalists.json', list(selected.values()))
        holdout = {}
        for seed in (args.seed+2000, args.seed+3000):
            holdout[str(seed)] = [score(args, r, panels['holdout'], 'holdout', seed) for r in selected.values()]
        dump(out/'review.json', dict(generations=args.generations,
             search_requested_steps=args.generations*5*args.train_steps, search_actual_steps=steps,
             baseline_actual_steps=baseline['actual_steps'], selection_champion=champion,
             baseline_common=baseline_common, archive=archive.records,
             diversity=pairwise(archive.records), heldout=holdout,
             caveat='Incremental training-step matched, not wall-time or historical-compute matched. '
                    'Warmstarts reuse the earlier experiment. One search/baseline seed is exploratory. '
                    'Held-out opponents/seeds are excluded from this search, not guaranteed globally unseen.'))
        save(args.generations, 'review_ready')
        print(f'Review ready: {out/"review.json"}. No additional generations queued.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('parent', 'panel', 'out'):
        parser.add_argument('--'+name, required=True)
    parser.add_argument('--warmstart', nargs='*', default=[])
    parser.add_argument('--holdout', nargs='+', required=True)
    parser.add_argument('--trainer', default='./build/pokemon/puffer_behavior')
    parser.add_argument('--evaluator', default='./build/pokemon/pokemon_behavior')
    parser.add_argument('--train-steps', type=int, default=100000000)
    parser.add_argument('--generations', type=int, choices=(1, 2), default=2)
    parser.add_argument('--games', type=int, default=64)
    parser.add_argument('--seed', type=int, default=601)
    args = parser.parse_args()
    if args.train_steps < 2*BATCH_STEPS or args.games < 2 or args.games % 2:
        parser.error('need >=1048576 steps and a positive even game count >=2')
    run(args)


if __name__ == '__main__':
    main()
