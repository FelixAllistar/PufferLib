"""Add one evaluated opponent without pruning or changing the learner/config."""
import argparse
import configparser
import csv
import datetime
from pathlib import Path
import shutil
from policy_identity import identity, digest
from eval_observation_versions import executor_version

def main():
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--league', type=Path, required=True)
    p.add_argument('--checkpoint', type=Path, required=True)
    p.add_argument('--label', required=True)
    p.add_argument('--weight', type=float, default=.15)
    p.add_argument('--evidence', type=Path, required=True)
    a=p.parse_args()
    if not 0<a.weight<1 or not a.label.replace('_','').isalnum(): raise ValueError('Invalid label/weight')
    cfg=configparser.ConfigParser(interpolation=None); cfg.read(a.league/'league.ini')
    sections=[s for s in cfg if s.startswith('policy.')]
    if len(sections)>=cfg.getint('league','max_active',fallback=8): raise ValueError('No empty slot; refusing to prune')
    key=identity(a.checkpoint)
    executor=executor_version(a.checkpoint)
    if key.split(':')[0]!='obs1': raise ValueError('Expected obs1')
    evidence=list(csv.DictReader(a.evidence.open(),delimiter='\t'))
    if not any(identity(r['checkpoint'])==key for r in evidence): raise ValueError('Candidate not evaluated')
    for s in sections:
        if executor_version(cfg[s]['path'].strip("'\"")) != executor:
            raise ValueError('Mixed executor league requires version-aware analysis; refusing admission')
        other=identity(cfg[s]['path'].strip("'\""))
        if other==key or other.split(':')[0]!='obs1': raise ValueError('Duplicate or mixed layout')
        if not cfg.getboolean(s,'enabled',fallback=True): raise ValueError('Disabled member requires review')
    dest=a.league/(a.label+'.bin')
    if dest.exists(): raise ValueError('Destination exists')
    total=sum(float(cfg[s]['train_weight']) for s in sections)
    if total<=0: raise ValueError('Invalid weights')
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    backup=a.league.parent/(a.league.name+'_archive')/('diversity_add_'+stamp)
    shutil.copytree(a.league,backup)
    shutil.copy2(a.checkpoint,dest)
    Path(str(dest)+'.obs_version').write_text('1\n')
    Path(str(dest)+'.executor_version').write_text(str(executor)+'\n')
    if Path(str(a.checkpoint)+'.emag').exists(): shutil.copy2(str(a.checkpoint)+'.emag',str(dest)+'.emag')
    if digest(dest)!=digest(a.checkpoint): raise ValueError('Copy verification failed')
    for s in sections: cfg[s]['train_weight']=f'{float(cfg[s]["train_weight"])/total*(1-a.weight):.12f}'
    section='policy.'+a.label
    cfg[section]=dict(path=f"'{dest}'",role='diversity',train_weight=str(a.weight),enabled='1')
    sections.append(section)
    with (a.league/'.manifest.new.tsv').open('w') as f:
        w=csv.writer(f,delimiter='\t',lineterminator='\n');w.writerow(['policy','role','base_weight','source'])
        for s in sections:w.writerow([s[7:],cfg[s]['role'],cfg[s]['train_weight'],cfg[s]['path'].strip("'\"")])
    with (a.league/'.league.new.ini').open('w') as f: cfg.write(f)
    (a.league/'.manifest.new.tsv').replace(a.league/'manifest.tsv')
    (a.league/'.league.new.ini').replace(a.league/'league.ini')
    print(f'Admitted {a.label}: share={a.weight}, active={len(sections)}, no pruning; backup={backup}; learner/config unchanged')

if __name__=='__main__':main()
