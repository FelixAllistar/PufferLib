"""Run native duplicate-deal baselines, with paired candidate-vs-baseline CIs."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import math
from eval import ROOT


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--hands',type=int,default=60000)
    p.add_argument('--seed',type=int,default=202610171)
    p.add_argument('--build',type=Path,default=ROOT/'build/poker')
    p.add_argument('--stack',type=int,default=20000)
    p.add_argument('--candidates',type=int,nargs='+',default=[1,4,7])
    p.add_argument('--session-blocks',type=int,default=1)
    p.add_argument('--native-history',action='store_true')
    p.add_argument('--fields',nargs='+',choices=('original','fullhouse','mixed'),
                   default=['original','fullhouse','mixed'])
    args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    build=args.build.resolve()
    manifest=json.loads((build/'fullhouse-manifest.json').read_text())
    manifest.update(hands_per_match=args.hands,seed=args.seed,
        session_blocks=args.session_blocks,native_history=args.native_history,
        fields=args.fields,
        method='Same deal across a block of seat rotations. With session_blocks > 1, table history persists within independent session clusters; 95% normal intervals and paired differences use cluster means.',
        conditions=f'{args.stack/100} BB, 50/100, no rake, stacks reset each hand. Warm CPU timing includes dealing, all decisions, settlement and CSV output.',
        programs={f'baseline_bench{n}':hashlib.sha256((build/f'baseline_bench{n}').read_bytes()).hexdigest() for n in (2,6)})
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    results=[]
    for n in (2,6):
        fields={'original':'1','fullhouse':'4'}
        if n==6: fields.update(mixed='1,4,1,4,4')
        fields={name:opponents for name,opponents in fields.items() if name in args.fields}
        for field,opponents in fields.items():
            for hero in args.candidates:
                label=f'{n}p_style{hero}_vs_{field}'
                command=[str(build/f'baseline_bench{n}'),str(hero),opponents,str(args.hands),
                         str(args.seed),str(args.stack),str(args.output/f'{label}.csv'),label,
                         str(args.session_blocks),str(int(args.native_history))]
                row=json.loads(subprocess.check_output(command,text=True))
                row['opponents']=list(map(int,opponents.split(','))); row['command']=command
                results.append(row)
                (args.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
                print(f"{label}: {row['bb100']:+.2f} +/- {row['ci95_half_width_bb100']:.2f} BB/100; {row['decisions_per_second']:,.0f} decisions/s",flush=True)
    differences=[]
    for base in results:
        if base['hero_style']!=1: continue
        for style in (s for s in args.candidates if s!=1):
            other=next(r for r in results if r['hero_style']==style and r['players']==base['players'] and r['opponents']==base['opponents'])
            def values(row):
                with (args.output/f"{row['label']}.csv").open() as file:
                    return [float(r['mean_bb']) for r in csv.DictReader(file)]
            delta=[b-a for a,b in zip(values(base),values(other),strict=True)]
            differences.append({'baseline':base['label'],'candidate':other['label'],
                'gain_bb100':statistics.mean(delta)*100,
                'ci95_half_width_bb100':196*statistics.stdev(delta)/math.sqrt(len(delta))})
    (args.output/'paired-differences.json').write_text(json.dumps(differences,indent=2)+'\n')
    print(json.dumps(differences,indent=2))


if __name__=='__main__': main()
