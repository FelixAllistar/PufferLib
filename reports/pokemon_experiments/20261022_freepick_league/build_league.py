#!/usr/bin/env python3
"""Build the upgraded 13-bank frozen league (2026-10-22): per team, the better of the old rr20m member
(B0, its latest round as scored in B0) and the B1 700M retrain, by the SyntheticRLV2+TaurosV0 mean.
Each bank keeps its own team and lead from its checkpoint config. Run from /workspace/PufferLib."""
import configparser,glob,os
ROOT='/workspace/PufferLib'
OUT='reports/pokemon_experiments/20261022_freepick_league/league'
MEMBERS='leagues/pokemon/rr20m_256x2_20261020/members'
B1='reports/pokemon_experiments/20261022_b1_teams'
# (B0 mean, B1 700M mean) from reports/pokemon_benchmarks/leaderboard_20261022.md and the B1 evals.
SCORES={'gar_egg_zap':(60.0,59.9),'zam_egg_cloy_don':(58.2,41.2),'zam_egg_zap':(50.0,58.0),'zam_egg_mie':(53.0,56.8),
        'gar_egg_mie':(56.8,54.9),'mie_egg_zap':(56.8,52.8),'gar_egg_mie_zap':(56.2,58.2),'jynx_mie_don':(54.7,55.2),
        'mie_egg_cloy':(51.4,51.9),'jynx_egg_zap':(50.9,56.2),'jynx_egg_gar_zap':(50.4,51.0),'zam_egg_gar':(47.6,48.1),
        'mie_egg_don':(47.6,44.6)}
def cfg(path):
    c=configparser.ConfigParser(interpolation=None,strict=False);c.read(path);return c
banks=[]
for name,(b0,b1) in sorted(SCORES.items(),key=lambda kv:-max(kv[1])):
    if b1>b0:
        weights=glob.glob(f'{B1}/{name}/checkpoints/pokemon/*/0000000700186624.bin')[0];source='B1 700M'
    else:
        latest=sorted(d for d in os.listdir(f'{MEMBERS}/{name}') if '.' not in d)[-1]
        weights=f'{MEMBERS}/{name}/{latest}/weights.bin';source='B0 '+latest
    c=cfg(os.path.join(os.path.dirname(weights),'config.ini'))
    banks.append(dict(name=name,path=f'{ROOT}/{weights}',team=c['env']['learner_team'],lead=c['env']['learner_lead'],
                      source=source,score=max(b0,b1),hidden=c['policy']['hidden_size'],layers=c['policy']['num_layers'],rules=c['env']['rules_sha']))
assert len({(b['hidden'],b['layers'],b['rules']) for b in banks})==1
os.makedirs(OUT,exist_ok=True)
opp=f'{ROOT}/{OUT}/opponents.txt'
with open(f'{OUT}/league.ini','w') as f:
    f.write(f"[native]\nversion = 3\nbanks = {len(banks)}\nhidden_size = {banks[0]['hidden']}\nnum_layers = {banks[0]['layers']}\n"
            f"rules_sha = {banks[0]['rules']}\nopponents = {opp}\n")
    for i,b in enumerate(banks):
        f.write(f"\n# {b['source']}, strong mean {b['score']}\n[bank.{i}]\nname = {b['name']}\npath = {b['path']}\nteam = {b['team']}\nlead = {b['lead']}\n")
with open(f'{OUT}/opponents.txt','w') as f: f.write(''.join(b['path']+'\n' for b in banks))
for b in banks: print(f"{b['name']:18} {b['source']:16} {b['score']:5} {b['team']} lead {b['lead']}")
