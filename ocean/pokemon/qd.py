#!/usr/bin/env python3
"""Native PPO + reward-space CMA-ES + a realized-behavior archive.

Run from the repository root. This is not PPGA or a surrogate team simulator.
Every candidate is trained and scored in real battles. Oracle names are used
only by the separate post-hoc report command, never by run().
"""
from __future__ import annotations
import argparse
from collections import Counter
import configparser
import fcntl
import hashlib
from itertools import combinations
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import numpy as np
from cma_qd import CMAES, Archive, personality, behavior_distance

NAMES = ('paralysis', 'sleep', 'offense', 'defense', 'reserve')


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def read_ini(path):
    c = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=('#', ';'))
    with open(path) as f:
        c.read_file(f)
    return c


def dump(path, value):
    path = Path(path)
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n')
    tmp.replace(path)


def execute(cmd, log):
    # Child failures are errors, never synthetic low fitness.
    print(' '.join(map(str, cmd)), flush=True)
    with open(log, 'w') as f:
        subprocess.run(list(map(str, cmd)), stdout=f, stderr=subprocess.STDOUT, check=True)


def snapshot(checkpoint, directory):
    checkpoint = Path(checkpoint).resolve(strict=True)
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    dest = directory / checkpoint.name
    for src, dst in ((checkpoint, dest), (checkpoint.parent/'config.ini', directory/'config.ini')):
        if dst.exists() and digest(src) != digest(dst):
            raise ValueError('snapshot mismatch')
        if not dst.exists():
            shutil.copyfile(src, dst)
    return str(dest)


def aggregate(rows):
    if not rows:
        raise ValueError('empty evaluation')
    species, pairs, teams, leads = Counter(), Counter(), Counter(), Counter()
    styles = []
    for r in rows:
        ids, sets, v = r.get('species_ids', []), r.get('set_ids', []), r.get('personality')
        if (r.get('qd_version') != 1 or len(ids) != 6 or len(set(ids)) != 6 or len(sets) != 6
                or any(type(i) is not int or not 1 <= i <= 149 for i in ids)
                or any(type(i) is not int or not 0 <= i < 556 for i in sets)
                or v is None or len(v) != 5 or not np.all(np.isfinite(v))
                or np.max(np.abs(v)) > 1.000001):
            raise ValueError('invalid QD profile; rebuild the evaluator and check catalog compatibility')
        species.update(map(str, ids))
        pairs.update(','.join(map(str, p)) for p in combinations(sorted(ids), 2))
        teams.update([','.join(map(str, sorted(sets)))])
        leads.update([str(ids[0])])
        styles.append(v)
    n = len(rows)
    return dict(species={k:v/n for k,v in species.items()}, pairs={k:v/n for k,v in pairs.items()},
                teams={k:v/n for k,v in teams.items()}, leads={k:v/n for k,v in leads.items()},
                style=np.mean(styles, axis=0).tolist())


def evaluate(binary, checkpoint, panel, games, seed, cap, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    rows, first, second, payoffs = [], [], [], {}
    for i, opponent in enumerate(panel):
        log = directory / f'panel_{i:03}.log'
        execute([binary, 'eval', checkpoint, opponent['path'], '--profile-json', f'--games={games}',
                 f'--seed={seed}', '--team-a=None', 'env.team_selection=1', f'env.max_updates={cap}'], log)
        match = [json.loads(s[11:]) for s in log.read_text().splitlines() if s.startswith('PK_PROFILE ')]
        if len(match) != games or any(r.get('score') not in (0, .5, 1) for r in match):
            raise ValueError('missing or invalid native match results')
        aggregate(match)
        rows += match
        first += match[:games//2]
        second += match[games//2:]
        payoffs[str(i)] = sum(r['score'] for r in match)/games
    return dict(quality=float(np.mean(list(payoffs.values()))), payoffs=payoffs,
                behavior=aggregate(rows), noise=behavior_distance(aggregate(first), aggregate(second)),
                games=len(rows), timeout_rate=sum(r.get('descriptors', {}).get('timeout', 0) for r in rows)/len(rows))


def build_panel(args, out):
    native = read_ini(args.native_league)
    count = native.getint('native', 'banks')
    if not 1 <= count <= 32:
        raise ValueError('native league must contain 1..32 banks')
    panel = []
    for i in range(count):
        section = f'bank.{i}'
        path = snapshot(native[section]['path'], out/'panel'/str(i))
        cfg = read_ini(Path(path).parent/'config.ini')
        if cfg['env'].get('learner_team', 'None') != native[section]['team']:
            raise ValueError('bank team does not match checkpoint config')
        native[section]['path'] = path
        panel.append(dict(path=path, team=native[section]['team']))
    with open(out/'native.ini', 'w') as f:
        native.write(f)
    dump(out/'panel.json', panel)
    return panel


def fingerprint(args, panel):
    files = [args.trainer, args.evaluator, 'config/default.ini', 'config/pokemon.ini',
             'ocean/pokemon/data/catalog.json', 'ocean/pokemon/qd.py', 'ocean/pokemon/cma_qd.py',
             'ocean/pokemon/personality.h', 'ocean/pokemon/pokemon.h', 'ocean/pokemon/profile.h',
             str(Path(args.out)/'native.ini')]
    for r in panel:
        files.extend([r['path'], str(Path(r['path']).parent/'config.ini')])
    if args.seed_model:
        files.extend([args.seed_model, str(Path(args.seed_model).parent/'config.ini')])
    contract = {k:v for k,v in vars(args).items() if k not in ('generations', 'resume', 'command')}
    contract['files'] = {str(Path(p).resolve()):digest(p) for p in files}
    return contract


def candidate(args, panel, contract, label, parent, weights, train_seed):
    job = Path(args.out)/'candidates'/label
    job.mkdir(parents=True, exist_ok=True)
    result = job/'result.json'
    if result.exists():
        r = json.loads(result.read_text())
        if (r['parent'] != parent or r['weights'] != weights or r['train_seed'] != train_seed
                or r['checkpoint_sha256'] != digest(r['checkpoint'])
                or r['config_sha256'] != digest(Path(r['checkpoint']).parent/'config.ini')):
            raise ValueError('candidate cache mismatch')
        return r
    # Keep failed attempts instead of overwriting their diagnostics.
    run_id = f'{label}_{time.time_ns()}'
    ckroot = job/'checkpoints'
    cmd = [args.trainer, 'train', 'pokemon', f'--base.run-id={run_id}',
           f'--base.load-model-path={parent}', f'--base.checkpoint-dir={ckroot}',
           f'--base.log-dir={job / "logs"}', f'--env.native-league={Path(args.out)/"native.ini"}',
           '--env.learner-team=None', '--env.opponent-team=None', '--env.team-selection=1',
           f'--env.max-updates={args.max_updates}', '--env.reward-win=1',
           '--env.reward-hp-scale=0', '--env.reward-ko-scale=0', '--selfplay.enabled=0',
           f'--train.seed={train_seed}', f'--train.total-timesteps={args.train_steps}',
           f'--train.horizon={args.horizon}', f'--train.gae-lambda={args.gae_lambda}',
           '--train.anneal-lr=0', '--train.anneal-ent-coef=0', f'--train.emag-kl-coef={args.emag}',
           '--train.reward-clip=0', '--train.epoch-sampling=1', '--train.prio-alpha=0']
    if parent != 'None':
        cfg = read_ini(Path(parent).parent/'config.ini')
        for k in ('hidden_size', 'num_layers'):
            cmd.append(f'--policy.{k}={cfg.getint("policy", k)}')
    cmd += [f'--env.personality-{k}={v:.12g}' for k,v in zip(NAMES, weights)]
    train_log = job/f'train_{run_id}.log'
    execute(cmd, train_log)
    prefix = 'PK_PERSONALITY_CONFIG v=1 weights='
    markers = [s[len(prefix):] for s in train_log.read_text().splitlines() if s.startswith(prefix)]
    reported = np.array([float(x) for x in markers[-1].split(',')]) if markers else np.array([])
    if reported.shape != (5,) or not np.allclose(reported, weights, rtol=1e-8, atol=1e-10):
        raise ValueError('trainer personality handshake missing/mismatched; rebuild the native trainer')
    checkpoints = sorted((ckroot/'pokemon'/run_id).glob('*.bin'))
    if not checkpoints:
        raise ValueError('trainer produced no checkpoint')
    checkpoint = str(checkpoints[-1].resolve())
    r = evaluate(args.evaluator, checkpoint, panel, args.games, args.seed, args.max_updates, job/'evaluation')
    r.update(id=label, checkpoint=checkpoint, parent=parent, weights=weights, train_seed=train_seed,
             checkpoint_sha256=digest(checkpoint), config_sha256=digest(Path(checkpoint).parent/'config.ini'))
    if fingerprint(args, panel) != contract:
        raise ValueError('run inputs changed during candidate; results rejected')
    dump(result, r)
    return r


def run(args):
    args.out = str(Path(args.out).resolve())
    for name in ('trainer', 'evaluator', 'native_league'):
        setattr(args, name, str(Path(getattr(args, name)).resolve(strict=True)))
    if args.seed_model:
        args.seed_model = str(Path(args.seed_model).resolve(strict=True))
        cfg = read_ini(Path(args.seed_model).parent/'config.ini')
        if cfg['env'].get('learner_team', 'None') != 'None':
            raise ValueError('seed must be an unrestricted learner, not a fixed-team specialist')
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with open(out/'lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        saved = out/'state.json'
        if args.resume:
            state = json.loads(saved.read_text())
            panel = json.loads((out/'panel.json').read_text())
            contract = fingerprint(args, panel)
            if contract != state['contract']:
                raise ValueError('resume contract mismatch')
            archive = Archive(args.radius, args.capacity)
            archive.records = state['archive']
            es = CMAES.restore(state['es'])
            parent, age, champion = state['parent'], state['age'], state['champion']
            start, budget = state['generation'], state['requested_train_steps']
            rng = np.random.default_rng()
            rng.bit_generator.state = state['rng']
        else:
            if saved.exists() or (out/'panel.json').exists():
                raise ValueError('output exists; use --resume or a new directory')
            panel = build_panel(args, out)
            contract = fingerprint(args, panel)
            archive = Archive(args.radius, args.capacity)
            seed = candidate(args, panel, contract, 'seed', args.seed_model or 'None', [0.0]*5, args.seed)
            archive.add_batch([seed])
            champion, parent = seed, seed['checkpoint']
            es = CMAES(population=args.population, seed=args.seed)
            age = start = 0
            rng = np.random.default_rng(args.seed)
            budget = args.train_steps
        def save(generation):
            dump(saved, dict(version=1, generation=generation, contract=contract, archive=archive.records,
                             champion=champion, es=es.state(), parent=parent, age=age,
                             rng=rng.bit_generator.state, requested_train_steps=budget))
        save(start)
        for g in range(start, args.generations):
            if fingerprint(args, panel) != contract:
                raise ValueError('run inputs changed')
            xs = es.ask()
            batch = [candidate(args, panel, contract, f'g{g:04}_c{i:03}', parent,
                               personality(x), args.seed+g+1) for i,x in enumerate(xs)]
            control = candidate(args, panel, contract, f'g{g:04}_control', parent, [0.0]*5, args.seed+g+1)
            ranks = archive.add_batch(batch+[control])
            es.tell([ranks[r['id']] for r in batch])
            champion = max([champion]+batch+[control], key=lambda r:r['quality'])
            budget += (args.population+1)*args.train_steps
            age += 1
            if age >= args.emitter_generations or all(ranks[r['id']][0] == 0 for r in batch):
                chosen = champion if rng.random() < .5 else archive.records[int(rng.integers(len(archive.records)))]
                parent, age = chosen['checkpoint'], 0
                # New parent changes the reward-to-policy map: restart CMA.
                es = CMAES(population=args.population, seed=int(rng.integers(1, 2**31)))
            save(g+1)
            print(f'Generation {g+1}: archive={len(archive.records)} best_panel_score={champion["quality"]:.3f} '
                  f'control={control["quality"]:.3f} requested_steps={budget}', flush=True)
        print(f'Champion: {champion["checkpoint"]}\nArchive/state: {saved}', flush=True)


def report(args):
    """Post-hoc only; never called by the optimizer or archive."""
    text = Path('ocean/pokemon/species_labels.h').read_text()
    body = re.search(r'pk_species_labels\[150\]\s*=\s*\{(.*?)\}', text, re.S)
    if not body:
        raise ValueError('unrecognized species label table')
    labels = re.findall(r'"([^\"]*)"', body.group(1))
    if len(labels) != 150:
        raise ValueError('species label table changed')
    mapping = {s.split('_', 1)[0].casefold():i for i,s in enumerate(labels)}
    names = [n.strip().casefold() for n in args.core.split(',')]
    if len(names) != 3 or len(set(names)) != 3 or any(n not in mapping or not n for n in names):
        raise ValueError('three distinct species names required')
    core = {mapping[n] for n in names}
    rows = []
    for p in Path(args.directory).glob('**/panel_*.log'):
        rows += [json.loads(s[11:]) for s in p.read_text().splitlines() if s.startswith('PK_PROFILE ')]
    aggregate(rows)
    hit = [r for r in rows if core <= set(r['species_ids'])]
    print(json.dumps(dict(games=len(rows), core_games=len(hit), core_frequency=len(hit)/len(rows),
                          score_with_core=sum(r['score'] for r in hit)/len(hit) if hit else None), indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    q = sub.add_parser('run')
    q.add_argument('--native-league', required=True)
    q.add_argument('--out', required=True)
    q.add_argument('--seed-model')
    q.add_argument('--trainer', default='./puffer')
    q.add_argument('--evaluator', default='./pokemon')
    q.add_argument('--generations', type=int, default=8)
    q.add_argument('--population', type=int, default=8)
    q.add_argument('--train-steps', type=int, default=50000000)
    q.add_argument('--games', type=int, default=64)
    q.add_argument('--seed', type=int, default=42)
    q.add_argument('--max-updates', type=int, default=512)
    q.add_argument('--horizon', type=int, default=512)
    q.add_argument('--gae-lambda', type=float, default=.995)
    q.add_argument('--emag', type=float, default=0)
    q.add_argument('--radius', type=float, default=.12)
    q.add_argument('--capacity', type=int, default=64)
    q.add_argument('--emitter-generations', type=int, default=3)
    q.add_argument('--resume', action='store_true')
    r = sub.add_parser('report')
    r.add_argument('directory')
    r.add_argument('--core', required=True)
    a = p.parse_args()
    if a.command == 'report':
        report(a)
        return
    if (a.generations < 1 or a.population < 4 or a.population % 2 or a.games < 8 or a.games % 4
            or a.seed < 0 or a.max_updates < 1 or a.train_steps < 1 or a.horizon < 1
            or a.emitter_generations < 1 or not 0 <= a.gae_lambda <= 1
            or not np.isfinite(a.emag) or a.emag < 0):
        p.error('invalid settings; games must be a multiple of four >=8, population even >=4')
    Archive(a.radius, a.capacity)
    cfg = read_ini('config/pokemon.ini')
    agents, mb = cfg.getint('vec', 'total_agents'), cfg.getint('train', 'minibatch_size')
    if mb % a.horizon or mb > a.horizon*agents or a.train_steps < 2*a.horizon*agents:
        p.error('horizon must divide minibatch_size; allow >=2 complete rollout batches')
    run(a)


if __name__ == '__main__':
    main()
