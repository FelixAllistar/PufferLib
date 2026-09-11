#!/usr/bin/env python3
"""Confirm trial-zero checkpoints on new seeds, then optionally continue one."""
import argparse
import csv
import fcntl
import json
from pathlib import Path
import signal
import statistics
import sys
from expansion_experiment import HERE, ROOT, argv, execute, save
from policy_identity import digest, identity


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--experiment', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--train', action='store_true')
    p.add_argument('--continuation', type=Path, help='Also evaluate every checkpoint from this continuation output; evaluation only')
    a = p.parse_args()
    if a.continuation and a.train:
        p.error('--continuation is evaluation only; no automatic new training')
    out, source = a.output.resolve(), a.experiment.resolve()
    out.mkdir(parents=True, exist_ok=True)
    lock = (out/'lock').open('w')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    plan = json.loads((source/'plan.json').read_text())
    if digest(source/'plan.json') != (source/'plan.sha256').read_text().strip():
        raise ValueError('Source plan changed')
    for path, expected in plan['artifacts'].items():
        if digest(path) != expected: raise ValueError(f'Artifact changed: {path}')
    for opponent in plan['opponents']:
        if identity(opponent['checkpoint']) != opponent['identity']:
            raise ValueError('Opponent changed')
    attempt = json.loads((source/'state.json').read_text())['trials'][0]['attempts'][-1]
    records = json.loads((Path(attempt['directory'])/'checkpoints.json').read_text())
    candidates = [min(records, key=lambda r:abs(r['steps']-target)) for target in (200000000,300000000)]
    for record in candidates:
        if digest(record['path']) != record['sha256']: raise ValueError('Candidate changed')
    if a.continuation:
        continuation = a.continuation.resolve()
        status = json.loads((continuation/'training_exit.json').read_text())
        if status['exit_code'] != 0: raise ValueError('Continuation did not finish successfully')
        for path in sorted(continuation.glob('checkpoints/kaggriculture/*/[0-9]'+'[0-9]'*15+'.bin')):
            candidates.append(dict(path=str(path),sha256=digest(path),steps=int(path.stem)))
        if len(candidates) == 2: raise ValueError('No continuation checkpoints found')
    manifest = out/'candidates.tsv'
    manifest.write_text('id\tpolicy\tcheckpoint\n'+''.join(
        f'{i}\t{"parent" if i<2 else "continuation"}_{r["steps"]}\t{r["path"]}\n' for i,r in enumerate(candidates)))
    opponents = out/'opponents.tsv'
    opponents.write_text('id\tpolicy\tcheckpoint\n'+''.join(
        f'{i}\topponent_{i}\t{r["checkpoint"]}\n' for i,r in enumerate(plan['opponents'])))
    protocol = dict(candidates=candidates, seeds=[82001,83001], games_per_pair=100,
                    modes=[1,0], selection='mean match score across seeds, modes and opponents; cash tie-break',
                    additional_steps=300000000, source_plan_sha256=digest(source/'plan.json'))
    if (out/'protocol.json').exists():
        if json.loads((out/'protocol.json').read_text()) != protocol: raise ValueError('Protocol changed')
    else: save(out/'protocol.json',protocol)
    scores = {i:[] for i in range(len(candidates))}
    for seed in protocol['seeds']:
        for mode in protocol['modes']:
            result = out/f'seed{seed}_d{mode}.tsv'
            if not result.exists():
                values = dict(plan['values'])
                values.update({'base.run_id':out.name+'_eval', 'base.eval_deterministic':str(mode),
                               'base.seed':str(seed), 'env.seed':str(seed), 'train.total_timesteps':'0',
                               'train.horizon':'8', 'vec.total_agents':'64', 'selfplay.enabled':'0'})
                command = [sys.executable,str(HERE/'eval_observation_versions.py'),'screen',
                           '--candidates',str(manifest),'--opponents',str(opponents),
                           '--output',str(result),'--games','100','--']+[f'{k}={v}' for k,v in values.items()]
                rc, _ = execute(command,out/f'seed{seed}_d{mode}.log')
                if rc: raise RuntimeError('Confirmation evaluation failed')
            rows = list(csv.reader(result.open(),delimiter='\t'))
            expected = {(i,j) for i in scores for j in range(len(plan['opponents']))}
            if len(rows)!=len(expected) or {(int(r[0]),int(r[1])) for r in rows}!=expected:
                raise ValueError('Incomplete confirmation panel')
            for i in scores:
                cohort = [r for r in rows if int(r[0])==i]
                entry = dict(seed=seed, deterministic=mode, score=statistics.mean(float(r[2]) for r in cohort),
                             money=statistics.mean(float(r[4]) for r in cohort))
                scores[i].append(entry)
                print(f'CONFIRM candidate={i} {entry}',flush=True)
    summary = [dict(candidate=i,checkpoint=candidates[i]['path'],panels=scores[i],
                    score=statistics.mean(r['score'] for r in scores[i]),
                    money=statistics.mean(r['money'] for r in scores[i])) for i in scores]
    winner = max(summary,key=lambda r:(r['score'],r['money']))
    save(out/'summary.json',dict(results=summary,selected=winner,protocol=protocol))
    with (out/'by_mode.tsv').open('w') as stream:
        writer=csv.writer(stream,delimiter='\t',lineterminator='\n')
        writer.writerow(['candidate','kind','steps','deterministic','score','money','checkpoint'])
        for row in summary:
            for mode in protocol['modes']:
                panels=[r for r in row['panels'] if r['deterministic']==mode]
                writer.writerow([row['candidate'],'parent' if row['candidate']<2 else 'continuation',
                                 candidates[row['candidate']]['steps'],mode,
                                 statistics.mean(r['score'] for r in panels),
                                 statistics.mean(r['money'] for r in panels),row['checkpoint']])
    print('SELECTED',json.dumps(winner),flush=True)
    if not a.train: return
    if (out/'training_started.json').exists():
        raise ValueError('Training already launched; refusing to overwrite or restart it')
    values = json.loads((Path(attempt['directory'])/'starting_config.json').read_text())
    run_id = out.name+'_ft'
    values.update({'base.run_id':run_id,'base.load_model_path':winner['checkpoint'],
                   'base.checkpoint_dir':str(out/'checkpoints'),'base.log_dir':str(out/'training_logs'),
                   'train.total_timesteps':'300000000'})
    if float(values['train.emag_kl_coef'])>0 and not Path(winner['checkpoint']+'.emag').exists():
        raise ValueError('Missing parent EMAG state')
    directory = out/'checkpoints/kaggriculture'/run_id
    directory.mkdir(parents=True,exist_ok=False)
    save(directory/'run_metadata.json',dict(run_id=run_id,observation_version=int(values['env.observation_version']),
         hidden_size=int(values['policy.hidden_size']),num_layers=int(values['policy.num_layers']),macro_mode=2,
         parent=winner['checkpoint'],optimizer_restarted=True,config=values))
    save(out/'training_started.json',values)
    print(f'START continuation {run_id}: 300M additional steps; optimizer restarted; LR={values["train.learning_rate"]}',flush=True)
    def observed(path,seconds):
        Path(str(path)+'.obs_version').write_text(values['env.observation_version']+'\n')
    rc, seconds = execute(argv('train',values),out/'train.log',directory,observed)
    for path in directory.glob('*.bin'): observed(path,seconds)
    save(out/'training_exit.json',dict(exit_code=rc,seconds=seconds))
    if rc: raise RuntimeError('Continuation failed; see train.log')
    print('Continuation finished; no promotion or submission.',flush=True)


if __name__=='__main__':
    signal.signal(signal.SIGTERM,lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    main()
