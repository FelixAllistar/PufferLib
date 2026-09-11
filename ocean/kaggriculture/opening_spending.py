"""Compare existing policy diagnostics with one public reference replay."""
import argparse
from collections import Counter
import json
from pathlib import Path
import statistics
from experiment_behavior import evaluate
from expansion_experiment import save, ROOT, HERE

def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--experiment', type=Path, required=True)
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    plan = json.loads((a.experiment/'plan.json').read_text())
    opponents = [r['checkpoint'] for r in plan['opponents']]
    env = {k[4:]:v for k,v in plan['values'].items() if k.startswith('env.')}
    rows = []
    turns = (24, 48, 96, 192, 288)
    for name, trial, step in [('control300',0,299892736), ('phase200',1,199229440), ('phase300',1,299892736)]:
        model = a.experiment/'checkpoints/kaggriculture'/f'{a.experiment.name}_t{trial}_a0'/f'{step:016d}.bin'
        target = a.output/f'{name}.json'
        if not target.exists():
            result = evaluate(model, opponents, [91001], False,
                              HERE/'build/libkag_spending.so', env, spending=True)
            save(target, result)
        episodes = json.loads(target.read_text())['episodes']
        for turn in turns:
            samples = [next(r for r in e['trajectory'] if r['turn']==turn) for e in episodes]
            means = {k:statistics.mean(r[k] for r in samples) for k in samples[0] if k!='turn'}
            rows.append(dict(policy=name, turn=turn, **means))
        print(f'COMPLETE {name}', flush=True)
    replay = json.loads(a.reference.read_text())
    for player in (0,1):
        for turn in turns:
            entry = next(s[0]['observation'] for s in replay['steps'] if s[0]['observation']['step']==turn)
            farm = entry['farms'][player]
            counts = Counter()
            for line in farm['tiles']:
                for tile in line:
                    if isinstance(tile,dict):
                        if tile.get('animal'): counts[tile['animal'].lower()] += 1
                        if tile.get('crop'): counts[tile['crop'].lower()] += 1
            rows.append(dict(policy=f'reference{player}', turn=turn, cash=farm['money'],
                             plots=len(farm['unlocked_quadrants']), cows=counts['cow'],
                             geese=counts['goose'], sheep=counts['sheep'],
                             **{k:counts[k] for k in ('wheat','carrot','tomato','strawberry','melon')}))
    save(a.output/'comparison.json', rows)
    columns = ('policy','turn','cash','plots','cows','geese','sheep','empty_animal_housing','wheat','carrot','tomato','strawberry','melon','hire_spend','purchase_spend','sales_revenue','unplaced_geese','unplaced_cows','unplaced_sheep','seed_wheat','seed_carrot','seed_tomato','seed_strawberry','seed_melon')
    lines = ['\t'.join(columns)]
    for r in rows:
        lines.append('\t'.join(str(round(r[k],2)) if isinstance(r.get(k),(int,float)) else str(r.get(k,'')) for k in columns))
    (a.output/'comparison.tsv').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines), flush=True)

if __name__ == '__main__': main()
