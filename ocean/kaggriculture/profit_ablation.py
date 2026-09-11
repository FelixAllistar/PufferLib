#!/usr/bin/env python3
"""Four matched continuations: expansion subsidy x terminal win reward."""
import argparse
import csv
import fcntl
import json
from pathlib import Path
import shutil
import signal
import sys
from expansion_experiment import HERE, argv, execute, save, evaluate_checkpoint
from policy_identity import digest, identity
from sweep_macro_memory import is_oom


def variants():
    return [dict(seed=42,deadline=240,expansion=e,win=w) for e,w in [(3,1),(0,1),(3,0),(0,0)]]


def trial_values(plan, params, out, run_id):
    values=dict(plan['values'])
    values.update({'base.run_id':run_id,'base.load_model_path':plan['parent'],
                   'base.checkpoint_dir':str(out/'checkpoints'),'base.log_dir':str(out/'training_logs'),
                   'train.total_timesteps':str(plan['steps']),
                   'env.reward_expansion_scale':str(params['expansion']),
                   'env.reward_expansion_deadline':str(params['deadline']),
                   'env.reward_progress_win_scale':str(params['win'])})
    for key in ('base.seed','train.seed','env.seed','selfplay.seed'): values[key]=str(params['seed'])
    if 'phase' in params: values['env.reward_phase_scale']=str(params['phase'])
    return values


def reports(out):
    plan=json.loads((out/'plan.json').read_text())
    rows=[]; herd=[]
    for path in out.glob('trial_*/attempt_*/eval_*_d[01].json'):
        result=json.loads(path.read_text())
        row={k:result[k] for k in ('trial','run_id','steps','mode','money','score','training_seconds')}
        row.update(plan['trials'][result['trial']]); rows.append(row)
        behavior=json.loads(path.with_suffix('.behavior.json').read_text())
        for turn in (24,48,96,144,192,240,288,360,480,600):
            samples=[next(r for r in e['trajectory'] if r['turn']==turn) for e in behavior['episodes']]
            import statistics
            herd.append(dict(trial=result['trial'],steps=result['steps'],mode=result['mode'],turn=turn,
                             **{k:statistics.mean(r[k] for r in samples) for k in ('cows','animals','plants','plots','cash')}))
    for filename, records in [('cash_ranking.tsv',sorted(rows,key=lambda r:(r['mode'],-r['money']))),('herd_timeline.tsv',herd)]:
        if not records: continue
        with (out/filename).open('w') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(records[0]),delimiter='\t',lineterminator='\n')
            writer.writeheader();writer.writerows(records)


def prepare(out, source, parent):
    if out.exists(): raise ValueError('Output already exists; use run or a new name')
    old=json.loads((source/'plan.json').read_text())
    if digest(source/'plan.json')!=(source/'plan.sha256').read_text().strip(): raise ValueError('Source plan changed')
    for path, expected in old['artifacts'].items():
        if digest(path)!=expected: raise ValueError(f'Artifact changed: {path}')
    if identity(parent).split(':')[0]!='obs1': raise ValueError('Expected obs1 parent')
    if not Path(str(parent)+'.emag').exists(): raise ValueError('Missing parent EMAG state')
    out.mkdir(parents=True)
    frozen=out/'parent.bin';shutil.copy2(parent,frozen)
    shutil.copy2(str(parent)+'.emag',str(frozen)+'.emag')
    Path(str(frozen)+'.obs_version').write_text('1\n')
    plan=dict(old,format='kag_profit_ablation_v1',start='continuation_optimizer_restart',
              parent=str(frozen),parent_source=str(parent),steps=300000000,trials=variants())
    plan['artifacts']=dict(old['artifacts'])
    for path in (parent,frozen,Path(str(frozen)+'.emag'),Path(str(frozen)+'.obs_version'),Path(__file__)):
        plan['artifacts'][str(path)]=digest(path)
    plan['values']=dict(old['values'])
    plan['values']['base.checkpoint_interval']='95'
    save(out/'plan.json',plan);(out/'plan.sha256').write_text(digest(out/'plan.json')+'\n')
    save(out/'state.json',{'trials':[dict(status='pending',attempts=[]) for _ in variants()]})
    print(f'Prepared four 300M continuations from {parent}; LR={plan["values"]["train.learning_rate"]}; cash=4',flush=True)


def run(out):
    if digest(out/'plan.json')!=(out/'plan.sha256').read_text().strip(): raise ValueError('Plan changed')
    plan=json.loads((out/'plan.json').read_text())
    for path, expected in plan['artifacts'].items():
        if digest(path)!=expected: raise ValueError(f'Artifact changed: {path}')
    for opponent in plan['opponents']:
        if identity(opponent['checkpoint'])!=opponent['identity']: raise ValueError('Opponent changed')
    lock=(out/'lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    state=json.loads((out/'state.json').read_text())
    baseline=out/'baseline';baseline.mkdir(exist_ok=True)
    # Numeric parent source retains its architecture/version metadata.
    evaluate_checkpoint(out,plan,dict(directory=str(baseline),run_id=out.name+'_parent',
                        checkpoint_seconds={},train_seconds=0),Path(plan['parent_source']),0)
    for index, params in enumerate(plan['trials']):
        trial=state['trials'][index]
        if trial['status'] in ('complete','oom','failed'): continue
        if trial['status']!='evaluating':
            if trial['attempts']: trial['attempts'][-1]['status']='interrupted'
            number=len(trial['attempts']);run_id=f'{out.name}_t{index}_a{number}'
            directory=out/f'trial_{index:02d}'/f'attempt_{number}';directory.mkdir(parents=True)
            ckpt=out/'checkpoints/kaggriculture'/run_id;ckpt.mkdir(parents=True,exist_ok=False)
            values=trial_values(plan,params,out,run_id)
            save(directory/'starting_config.json',values)
            save(ckpt/'run_metadata.json',dict(run_id=run_id,observation_version=1,hidden_size=int(values['policy.hidden_size']),
                 num_layers=int(values['policy.num_layers']),macro_mode=2,parent=plan['parent'],optimizer_restarted=True,config=values))
            attempt=dict(directory=str(directory),run_id=run_id,checkpoint_dir=str(ckpt),checkpoint_seconds={},status='training')
            trial['attempts'].append(attempt);trial['status']='training';save(out/'state.json',state)
            def observed(path,seconds):
                Path(str(path)+'.obs_version').write_text('1\n');attempt['checkpoint_seconds'][path.name]=seconds
                save(out/'state.json',state)
            print(f'START {index+1}/{len(plan["trials"])} {params}; SAME PARENT; optimizer restarted',flush=True)
            rc,seconds=execute(argv('train',values),directory/'train.log',ckpt,observed)
            attempt['train_seconds']=seconds
            if rc:
                attempt['status']=trial['status']='oom' if is_oom((directory/'train.log').read_text(errors='replace')) else 'failed'
                save(out/'state.json',state);continue
            paths=sorted(ckpt.glob('[0-9]'*16+'.bin'))
            if not paths or int(paths[-1].stem)<plan['steps']-int(values['vec.total_agents'])*int(values['train.horizon']):
                raise ValueError('Missing completed training budget')
            for path in paths:
                if path.name not in attempt['checkpoint_seconds']: observed(path,seconds)
            save(directory/'checkpoints.json',[dict(path=str(p),sha256=digest(p)) for p in paths])
            trial['status']=attempt['status']='evaluating';save(out/'state.json',state)
        attempt=trial['attempts'][-1]
        records=json.loads((Path(attempt['directory'])/'checkpoints.json').read_text())
        for record in records:
            if digest(record['path'])!=record['sha256']: raise ValueError('Checkpoint changed')
        paths=[Path(r['path']) for r in records]
        for path in dict.fromkeys(min(paths,key=lambda p:abs(int(p.stem)-n)) for n in (100000000,200000000,300000000)):
            evaluate_checkpoint(out,plan,attempt,path,index);reports(out)
        trial['status']=attempt['status']='complete';save(out/'state.json',state)
    print(f'COMPLETE: {out}/cash_ranking.tsv and herd_timeline.tsv; no automatic promotion/submission',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('command',choices=['prepare','run','report'])
    p.add_argument('--output',type=Path,required=True);p.add_argument('--source',type=Path);p.add_argument('--parent',type=Path)
    a=p.parse_args();out=a.output.resolve()
    signal.signal(signal.SIGTERM,lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    if a.command=='prepare':
        if not a.source or not a.parent: p.error('prepare requires --source and --parent')
        prepare(out,a.source.resolve(),a.parent.resolve())
    elif a.command=='run': run(out)
    else: reports(out)
