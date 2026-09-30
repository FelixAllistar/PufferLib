#!/usr/bin/env python3
"""Evaluate every Calm family under both Electrical strengths; retain episode tails."""
import argparse,json,subprocess,statistics,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('checkpoint',type=Path);p.add_argument('--episodes',type=int,default=100)
 p.add_argument('--hidden-size',type=int,default=128);p.add_argument('--num-layers',type=int,default=2)
 p.add_argument('--output',type=Path,default=Path('logs/abyss/tier_coverage'))
 p.add_argument('--cpu',action='store_true',help='Use abyss_eval without touching the GPU')
 p.add_argument('--dry-run',action='store_true');args=p.parse_args()
 if args.episodes<1:p.error('--episodes must be positive')
 archetypes=json.loads((ROOT/'ocean/abyss/data/tier_coverage.json').read_text())['archetypes']
 if not args.dry_run:
  if not args.checkpoint.is_file():p.error('checkpoint not found')
  args.output.mkdir(parents=True,exist_ok=True)
 results=[]
 for index,entry in enumerate(archetypes):
  for high in [0,1]:
   command=[str(ROOT/('abyss_eval' if args.cpu else 'abyss_puffer')),'eval','--headless',f'--base.load_model_path={args.checkpoint.resolve()}',f'--base.eval_episodes={args.episodes}','--vec.total_agents=32','--vec.num_buffers=1','--vec.num_threads=1',f'--policy.hidden_size={args.hidden_size}',f'--policy.num_layers={args.num_layers}',f'--env.encounter_archetype={index}',f'--env.weather_high_penalty_probability={high}','--env.episode_logs=1','--env.filament_tier=1']
   if args.cpu: command[3]=str(args.checkpoint.resolve())
   if args.dry_run:print(' '.join(command));continue
   label=f'{index:02d}_{entry["name"]}_{30+20*high}'
   print(f'Evaluating {label}',flush=True)
   with (args.output/(label+'.log')).open('w') as log:
    subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
   if args.cpu and 'untrained=1' in (args.output/(label+'.log')).read_text():
    raise RuntimeError('CPU evaluator failed to load checkpoint: '+label)
   episodes=[json.loads(m) for m in re.findall(r'ABYSS_EPISODE (\{[^\n]*\})',(args.output/(label+'.log')).read_text())]
   if len(episodes)<args.episodes:raise RuntimeError('missing episode telemetry: '+label)
   successes=[e for e in episodes if e['success']]
   result=dict(archetype=entry['name'],penalty=.5 if high else .3,episodes=len(episodes),completion_rate=len(successes)/len(episodes),mean_success_ticks=statistics.mean(e['ticks'] for e in successes) if successes else None,worst_success_ticks=max((e['ticks'] for e in successes),default=None),worst_cap=min(e['min_cap'] for e in episodes),worst_armor=min(e['min_armor'] for e in episodes),worst_hull=min(e['min_hull'] for e in episodes))
   results.append(result);(args.output/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
if __name__=='__main__':main()
