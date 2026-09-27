"""Self-generated opponents only; version every selection panel and rescore incumbents.

No league files, pretrained seeds, or external post-hoc scores enter this search.
The native trainer manages each job's private rolling history. This module's
history is evaluation-only, shared across candidates at each generation.
"""
import fcntl
import hashlib
import json
from pathlib import Path

import numpy as np
from cma_qd import Archive, CMAES, personality


def validate(record, api):
    path = Path(record['checkpoint'])
    if (api.digest(path) != record['checkpoint_sha256'] or
            api.digest(path.parent/'config.ini') != record['config_sha256']):
        raise ValueError('selfplay checkpoint/config changed')


def remember(history, record, api):
    known = {r['checkpoint'] for r in history}
    for path in sorted(Path(record['checkpoint']).parent.glob('*.bin')):
        if str(path) not in known:
            history.append(dict(checkpoint=str(path), checkpoint_sha256=api.digest(path),
                                config_sha256=api.digest(path.parent/'config.ini')))


def freeze_panel(history, folder, size, api):
    # Age coverage without selecting opponents by external rankings or species.
    indices = np.linspace(0, len(history)-1, min(size, len(history)), dtype=int)
    panel = []
    for i in indices:
        r = history[int(i)]
        validate(r, api)
        panel.append(dict(path=r['checkpoint'], team='None',
                          sha256=r['checkpoint_sha256'], config_sha256=r['config_sha256']))
    panel_id = hashlib.sha256(json.dumps(panel, sort_keys=True).encode()).hexdigest()
    folder.mkdir(parents=True, exist_ok=True)
    path = folder/'panel.json'
    value = dict(id=panel_id, opponents=panel)
    if path.exists() and json.loads(path.read_text()) != value:
        raise ValueError('generation panel changed')
    if not path.exists():
        api.dump(path, value)
    return panel, panel_id


def score(record, panel, panel_id, folder, args, api):
    validate(record, api)
    target = folder/record['id']
    result = target/'result.json'
    if result.exists():
        cached = json.loads(result.read_text())
        if (cached['evaluation_panel_id'] != panel_id or
                cached['checkpoint_sha256'] != record['checkpoint_sha256'] or
                cached['config_sha256'] != record['config_sha256']):
            raise ValueError('historical evaluation cache mismatch')
        return cached
    measured = api.evaluate(args.evaluator, record['checkpoint'], panel, args.games,
                            args.seed, args.max_updates, target/'evaluation')
    updated = {**record, **measured, 'evaluation_panel_id':panel_id,
               'evaluation_directory':str(target/'evaluation')}
    api.dump(result, updated)
    return updated


def run_selfplay(args, api):
    args.out = str(Path(args.out).resolve())
    for name in ('trainer', 'evaluator'):
        setattr(args, name, str(Path(getattr(args, name)).resolve(strict=True)))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with open(out/'lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        contract = api.fingerprint(args, [])
        contract_path, saved = out/'contract.json', out/'state.json'
        if contract_path.exists():
            if not args.resume:
                raise ValueError('output exists; use --resume or a new directory')
            if json.loads(contract_path.read_text()) != contract:
                raise ValueError('resume contract mismatch')
        else:
            if args.resume:
                raise ValueError('no selfplay run to resume')
            api.dump(contract_path, contract)
        archive = Archive(args.radius, args.capacity)
        if saved.exists():
            state = json.loads(saved.read_text())
            if state['contract'] != contract or state['opponent_mode'] != 'selfplay':
                raise ValueError('selfplay state contract mismatch')
            history = state['history']
            archive.records = state['archive']
            champion, parent, age = state['champion'], state['parent'], state['age']
            es, start, budget = CMAES.restore(state['es']), state['generation'], state['requested_train_steps']
            rng = np.random.default_rng()
            rng.bit_generator.state = state['rng']
        else:
            seed = api.candidate(args, None, contract, 'seed', 'None', [0.0]*5, args.seed)
            history = []
            remember(history, seed, api)
            champion, parent, age = seed, seed['checkpoint'], 0
            es, start, budget = CMAES(population=args.population, seed=args.seed), 0, args.train_steps
            rng = np.random.default_rng(args.seed)
        def save(generation):
            api.dump(saved, dict(version=2, opponent_mode='selfplay', contract=contract,
                                 generation=generation, requested_train_steps=budget,
                                 history=history, archive=archive.records, champion=champion,
                                 parent=parent, age=age, es=es.state(), rng=rng.bit_generator.state))
        for r in history:
            validate(r, api)
        save(start)
        for g in range(start, args.generations):
            if api.fingerprint(args, []) != contract:
                raise ValueError('run inputs changed')
            for r in history:
                validate(r, api)
            folder = out/'generations'/f'g{g:04}'
            panel, panel_id = freeze_panel(history, folder, args.history_panel_size, api)
            print(f'Generation {g+1}: frozen historical panel {panel_id[:12]}, {len(panel)} opponents; no league', flush=True)
            # Never compare incumbent scores from a different opponent panel.
            archive.records = [score(r, panel, panel_id, folder, args, api) for r in archive.records]
            champion = score(champion, panel, panel_id, folder, args, api)
            if not archive.records:
                archive.add_batch([champion])
            xs = es.ask()
            batch = []
            for i, x in enumerate(xs):
                r = api.candidate(args, None, contract, f'g{g:04}_c{i:03}', parent, personality(x), args.seed+g+1)
                batch.append(score(r, panel, panel_id, folder, args, api))
            r = api.candidate(args, None, contract, f'g{g:04}_control', parent, [0.0]*5, args.seed+g+1)
            control = score(r, panel, panel_id, folder, args, api)
            ranks = archive.add_batch(batch+[control])
            es.tell([ranks[r['id']] for r in batch])
            champion = max([champion]+archive.records+batch+[control], key=lambda r:r['quality'])
            api.dump(folder/'summary.json', dict(panel_id=panel_id, ranks=ranks,
                     champion=champion['id'], scores={r['id']:r['quality'] for r in batch+[control]},
                     parent=parent))
            for r in batch+[control]:
                remember(history, r, api)
            budget += (args.population+1)*args.train_steps
            age += 1
            if age >= args.emitter_generations or all(ranks[r['id']][0] == 0 for r in batch):
                chosen = champion if rng.random() < .5 else archive.records[int(rng.integers(len(archive.records)))]
                parent, age = chosen['checkpoint'], 0
                es = CMAES(population=args.population, seed=int(rng.integers(1, 2**31)))
            for r in history:
                validate(r, api)
            if api.fingerprint(args, []) != contract:
                raise ValueError('run inputs changed during generation')
            save(g+1)
            print(f'Generation {g+1}: archive={len(archive.records)} current_panel_score={champion["quality"]:.3f} '
                  f'control={control["quality"]:.3f} requested_steps={budget}', flush=True)
        print(f'Champion: {champion["checkpoint"]}\nArchive/state: {saved}', flush=True)
