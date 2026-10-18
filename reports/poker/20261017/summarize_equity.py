"""Rebuild the range-equity experiment summary from completed, retained runs."""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

ROOT=Path(__file__).resolve().parent/'equity_v1'

def load(path): return json.loads(path.read_text())
def values(suite,label):
    with (ROOT/suite/f'{label}.csv').open() as f:
        return [float(r['mean_bb']) for r in csv.DictReader(f)]
def difference(a,b):
    xs=[y-x for x,y in zip(a,b,strict=True)]
    return {'gain_bb100':100*statistics.mean(xs),
            'ci95_half_width_bb100':196*statistics.stdev(xs)/math.sqrt(len(xs)),
            'independent_clusters':len(xs)}

result={'local_suites':{},'budget_comparisons':[],'range_ablations':[],
        'history_comparisons':[],'slumbot':{},'interval_note':'Approximate normal 95% intervals, unadjusted for multiple comparisons. Local candidate comparisons use paired deal/session clusters; Slumbot samples are independent.'}
for suite in ('development','holdout','history_off','history_on'):
    rows=load(ROOT/suite/'results.json')
    result['local_suites'][suite]={'matches':len(rows),'hands':sum(r['hands'] for r in rows),
        'results':rows,'paired_differences':load(ROOT/suite/'paired-differences.json')}
for r in result['local_suites']['holdout']['results']:
    if r['hero_style']!=7: continue
    label=r['label']; other=label.replace('style7','style8')
    result['budget_comparisons'].append({'baseline':label,'candidate':other,
        **difference(values('holdout',label),values('holdout',other))})
for r in result['local_suites']['development']['results']:
    if r['hero_style']!=6: continue
    label=r['label']; other=label.replace('style6','style7')
    result['range_ablations'].append({'baseline':label,'candidate':other,
        **difference(values('development',label),values('development',other))})
for r in result['local_suites']['history_off']['results']:
    label=r['label']; delta=difference(values('history_off',label),values('history_on',label))
    if r['hero_style']==1: assert delta['gain_bb100']==delta['ci95_half_width_bb100']==0
    result['history_comparisons'].append({'label':label,**delta})
for name in ('control','range32'):
    path=ROOT/f'slumbot_{name}.jsonl'
    records=[json.loads(line) for line in path.read_text().splitlines()]
    summary=next(r['summary'] for r in reversed(records) if 'summary' in r)
    faced=[0]*4;folds=[0]*4
    for r in records:
        if 'winnings_chips' not in r: continue
        for d in r.get('decisions',[]):
            if d['call_chips']:
                faced[d['street']]+=1;folds[d['street']]+=d['native_action']<0
    result['slumbot'][name]={**summary,'log_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'faced_by_street':faced,'folds_by_street':folds,
        'fold_percent_by_street':[100*f/n if n else None for f,n in zip(folds,faced)]}
a,b=(result['slumbot'][name] for name in ('control','range32'))
result['slumbot_difference']={'gain_bb100':b['bb100']-a['bb100'],
    'ci95_half_width_bb100':math.hypot(a['ci95_half_width_bb100'],b['ci95_half_width_bb100']),
    'method':'Fresh independent 4000-hand samples; candidate chosen before held-out/live evaluation.'}
result['selfplay']=load(ROOT/'6p_range_selfplay.json')
result['policy_latency']=load(ROOT/'policy-latency2.json')+load(ROOT/'policy-latency6.json')
result['validation']=load(ROOT/'final-equity-tests.json')
result['local_hands_total']=sum(s['hands'] for s in result['local_suites'].values())+result['selfplay']['hands']
(ROOT/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:result[k] for k in ('local_hands_total','slumbot','slumbot_difference','history_comparisons','budget_comparisons')},indent=2))
