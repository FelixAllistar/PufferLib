"""Rebuild the compact comparison from the immutable experiment logs."""
import collections
import hashlib
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parent
paths={
 'original':ROOT/'baselines_v1/slumbot_original_control.jsonl',
 'focused':ROOT/'baselines_v2/slumbot_focused.jsonl',
 'fullhouse':ROOT/'baselines_v1/slumbot_fullhouse_low_concurrency.jsonl',
 'interrupted_fullhouse_pilot':ROOT/'baselines_v1/slumbot_fullhouse.jsonl',
}
result={'slumbot':{},'comparisons':{},'local_suites':{}}
for name,path in paths.items():
    records=[json.loads(s) for s in path.read_text().splitlines()]
    summary=next(r['summary'] for r in reversed(records) if 'summary' in r)
    faced=[0]*4; folds=[0]*4; outcomes=collections.defaultdict(lambda: [0,0.])
    for r in records:
        if 'winnings_chips' not in r: continue
        decisions=r.get('decisions',[])
        for d in decisions:
            if d['call_chips']:
                faced[d['street']]+=1;folds[d['street']]+=d['native_action']<0
        outcome='showdown'
        if r['action'].endswith('f'):
            outcome='hero_fold' if decisions and decisions[-1]['sent']=='f' else 'opponent_fold'
        outcomes[outcome][0]+=1;outcomes[outcome][1]+=r['winnings_chips']/100
    result['slumbot'][name]={**summary,'log_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'faced_by_street':faced,'folds_by_street':folds,
        'fold_percent_by_street':[100*f/n if n else None for f,n in zip(folds,faced)],
        'outcomes_count_and_bb':dict(outcomes)}
original=result['slumbot']['original']
for name in ('focused','fullhouse'):
    other=result['slumbot'][name]
    result['comparisons'][name+'_minus_original']={
        'gain_bb100':other['bb100']-original['bb100'],
        'ci95_half_width_bb100':math.hypot(other['ci95_half_width_bb100'],original['ci95_half_width_bb100']),
        'method':'Independent fresh samples; approximate normal 95% interval, exploratory comparison.'}
for name in ('baselines_v1/local','baselines_v2/local','baselines_v2/local100'):
    rows=json.loads((ROOT/name/'results.json').read_text())
    result['local_suites'][name]={'matches':len(rows),'total_hands':sum(r['hands'] for r in rows),
        'results':rows,'paired_differences':json.loads((ROOT/name/'paired-differences.json').read_text())}
result['local_hands_total']=sum(r['total_hands'] for r in result['local_suites'].values())
(ROOT/'baseline-comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'local_hands_total':result['local_hands_total'],'comparisons':result['comparisons'],
    'slumbot':{k:{j:v[j] for j in ('hands','bb100','ci95_half_width_bb100','unresolved_hands','fold_percent_by_street')}
               for k,v in result['slumbot'].items()}},indent=2))
