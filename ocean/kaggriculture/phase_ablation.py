#!/usr/bin/env python3
"""Prepare an explicit new reward-code generation; preserve old experiment artifacts."""
import argparse
import json
from pathlib import Path
import shutil
import signal
from expansion_experiment import HERE, ROOT, save
from policy_identity import digest, identity
from profit_ablation import run, reports


def prepare(out, source, parent, steps=300000000):
    if out.exists(): raise ValueError('Output exists; use run or a new output')
    old=json.loads((source/'plan.json').read_text())
    if digest(source/'plan.json')!=(source/'plan.sha256').read_text().strip(): raise ValueError('Source plan changed')
    if identity(parent).split(':')[0]!='obs1': raise ValueError('Expected obs1 parent')
    metadata=json.loads((parent.parent/'run_metadata.json').read_text())
    values=dict(metadata['config'])
    if values['env.macro_mode']!='2' or values['train.anneal_lr']!='0':
        raise ValueError('Expected macro2, constant-LR continuation')
    for opponent in old['opponents']:
        if identity(opponent['checkpoint'])!=opponent['identity']: raise ValueError('Opponent changed')
    if not Path(str(parent)+'.emag').exists(): raise ValueError('Missing EMAG reference')
    out.mkdir(parents=True)
    frozen=out/'parent.bin';shutil.copy2(parent,frozen)
    shutil.copy2(str(parent)+'.emag',str(frozen)+'.emag')
    Path(str(frozen)+'.obs_version').write_text('1\n')
    values['env.reward_phase_scale']='0'
    values['env.reward_expansion_scale']='0'
    plan=dict(old,format='kag_phase_ablation_v1',steps=steps,start='matched_continuation',
              parent=str(frozen),parent_source=str(parent),values=values,
              trials=[dict(seed=42,deadline=240,expansion=0,win=1,phase=s) for s in (0,2)],
              reward_code_change='Fuzzy phase reward includes actual CUDA transition; original v1 comparison invalid (GPU call omitted). Backups in phase_install_v1 and phase_install_v2.')
    paths=[ROOT/'puffer',HERE/'phase_rewards.h',HERE/'kaggriculture.h',HERE/'kaggriculture.cu',
           HERE/'phase_ablation.py',HERE/'profit_ablation.py',HERE/'expansion_experiment.py',
           HERE/'experiment_behavior.py',HERE/'policy_identity.py',HERE/'eval_observation_versions.py',
           HERE/'submission/main.py',HERE/'replay_native.py',HERE/'build/libkag_experiment.so',
           Path(values['selfplay.opponent_league']),parent,frozen,Path(str(frozen)+'.emag'),Path(str(frozen)+'.obs_version')]
    plan['artifacts']={str(p):digest(p) for p in paths}
    save(out/'plan.json',plan);(out/'plan.sha256').write_text(digest(out/'plan.json')+'\n')
    save(out/'state.json',{'trials':[dict(status='pending',attempts=[]) for _ in plan['trials']]})
    print(f'Prepared control vs fuzzy phases: {steps:,} additional steps each; same parent; LR={values["train.learning_rate"]}',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('command',choices=['prepare','run','report'])
    p.add_argument('--output',type=Path,required=True);p.add_argument('--source',type=Path);p.add_argument('--parent',type=Path)
    p.add_argument('--steps',type=int,default=300000000)
    a=p.parse_args();out=a.output.resolve()
    signal.signal(signal.SIGTERM,lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    if a.command=='prepare':
        if not a.source or not a.parent or a.steps<1:p.error('prepare requires source, parent and positive steps')
        prepare(out,a.source.resolve(),a.parent.resolve(),a.steps)
    elif a.command=='run':run(out)
    else:reports(out)
