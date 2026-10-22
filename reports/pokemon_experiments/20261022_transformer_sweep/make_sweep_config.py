#!/usr/bin/env python3
"""Write the Transformer self-play sweep config (2026-10-22) over config/pokemon.ini, run on the A/B box
from /workspace/PufferLib. Base = the A/B transformer arm (self-play only, zam_egg_mie, recipe hypers),
fixed 250M steps, final-only live TaurosV0 score (144 games) as the metric. The previous config is
kept as config/pokemon.ini.before_xfmr_sweep."""
import re,shlex,shutil
D='reports/pokemon_experiments/20261022_transformer_sweep'
import os
if not os.path.exists('config/pokemon.ini.before_xfmr_sweep'): shutil.copy('config/pokemon.ini','config/pokemon.ini.before_xfmr_sweep')
src='config/pokemon.ini.before_xfmr_sweep'
sections={};order=[];cur=None
for line in open(src):
    m=re.match(r'^\[(.+)\]\s*$',line)
    if m: cur=m.group(1);order.append(cur);sections[cur]=[];continue
    if cur is None: continue
    kv=re.match(r'^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$',line)
    if kv: sections[cur].append([kv.group(1),kv.group(2)])
order=[s for s in order if not s.startswith('sweep.')]
body=open('reports/pokemon_experiments/20261022_transformer_ab/run_arm.sh').read()
args=shlex.split(body.split('./puffer train')[1].split('$EXTRA')[0].replace('\\\n',' '))
over={}
for a in args:
    if a.startswith('--') and '=' in a:
        k,v=a[2:].split('=',1); over[k]=v
over.update({'policy.pokemon_attention_layers':'2','policy.pokemon_attention_width':'128','policy.pokemon_attention_heads':'4',
    'train.total_timesteps':'250000000','metamon.enabled':'1','metamon.interval_fraction':'1','metamon.final_games':'48',
    'base.checkpoint_dir':D+'/checkpoints','base.log_dir':D+'/logs','base.checkpoint_interval':'0',
    'sweep.history_path':D+'/sweep.history','sweep.max_runs':'60','sweep.metric':'metamon_score','sweep.goal':'maximize'})
for k,v in over.items():
    if '$' in v: continue
    sec,key=k.rsplit('.',1)
    if sec not in sections: sections[sec]=[];order.append(sec)
    for kv in sections[sec]:
        if kv[0]==key: kv[1]=v;break
    else: sections[sec].append([key,v])
dims=[('policy.pokemon_attention_width','uniform_pow2',64,256),('policy.pokemon_attention_layers','int_uniform',1,4),
      ('train.learning_rate','log_normal',0.0001,0.003),('train.ent_coef','log_normal',0.0001,0.01),
      ('train.clip_coef','uniform',0.05,0.3),('train.replay_ratio','uniform',1,4),
      ('train.magnet_coef','log_normal',0.01,1),('train.magnet_tau','log_normal',0.001,0.1)]
# config/default.ini also declares [sweep.*] dimensions, and the trainer searches every one. Pin
# each default dimension that is not ours to the base value (min = max holds it fixed).
ours={k for k,*_ in dims}
pinned=[];cur=None;dist={}
for line in open('config/default.ini'):
    m=re.match(r'^\[sweep\.(.+)\]\s*$',line)
    if m: cur=m.group(1);continue
    if re.match(r'^\[',line): cur=None;continue
    kv=re.match(r'^\s*distribution\s*=\s*(\S+)',line)
    if cur and kv: dist[cur]=kv.group(1)
def base_value(key):
    sec,k=key.rsplit('.',1)
    for kk,v in sections.get(sec,[]):
        if kk==k: return v
    cur=None
    for line in open('config/default.ini'):
        m=re.match(r'^\[(.+)\]\s*$',line)
        if m: cur=m.group(1);continue
        kv=re.match(r'^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$',line)
        if cur==sec and kv and kv.group(1)==k: return kv.group(2)
    raise SystemExit('no base value for '+key)
for key,d in dist.items():
    if key in ours: continue
    pinned.append((key,d,base_value(key)))
with open('config/pokemon.ini','w') as f:
    f.write('# Transformer self-play sweep (2026-10-22): see '+D+'/README.md\n')
    for s in order:
        f.write(f'\n[{s}]\n'+''.join(f'{k} = {v}\n' for k,v in sections[s]))
    for k,d,lo,hi in dims:
        f.write(f'\n[sweep.{k}]\ndistribution = {d}\nmin = {lo}\nmax = {hi}\nscale = auto\n')
    for k,d,v in pinned:
        f.write(f'\n# Pinned default dimension\n[sweep.{k}]\ndistribution = {d}\nmin = {v}\nmax = {v}\nscale = auto\n')
print('wrote config/pokemon.ini with',len(dims),'sweep dims and',len(pinned),'pinned defaults:',', '.join(k for k,*_ in pinned))
