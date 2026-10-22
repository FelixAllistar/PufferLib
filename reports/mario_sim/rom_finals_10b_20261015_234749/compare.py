from pathlib import Path
import json,copy,importlib.util,sys,csv
repo=Path('/home/felix/puffertank/pufferlib')
root=Path(Path('/tmp/mario_finals_eval_path').read_text().strip())
history=repo/'reports/mario_sim/rom_levels_2b_6b_20261015_060222/results/summary.json'
old=json.loads(history.read_text())
new=json.loads((root/'results/summary.json').read_text())
report=copy.deepcopy(new)
report['models']={**old['models'],**new['models']}
assert old['seed']==new['seed']==137
assert old['config_sha256']==new['config_sha256']
assert old['level_config_sha256']==new['level_config_sha256']
assert old['evaluator_sha256']==new['evaluator_sha256']
report['models'].update(new['models'])
report['comparison_sources']=[str(history),str(root/'results/summary.json')]
report['all_validated']=all(x['valid'] for m in report['models'].values() for x in m['panels'].values())
out=root/'comparison';out.mkdir(exist_ok=True)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
spec=importlib.util.spec_from_file_location('eval_rom',repo/'ocean/mario_sim/evaluate_rom.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
labels=list(report['models']);module.render(out,report,labels,32)
totals={}
for label,m in report['models'].items():
 levels=[m['panels'][f'level_{w}_{l}'] for w in range(1,9) for l in range(1,5)]
 valid=all(x['valid'] for x in levels)
 wins=[(x.get('summary') or {}).get('rom_successes') for x in levels]
 totals[label]={'valid':valid,'clears':sum(wins) if valid else None,'attempts':1024,'levels_any_clear':sum(x>0 for x in wins) if valid else None,'perfect_levels':sum(x==32 for x in wins) if valid else None}
print(json.dumps(totals,indent=2))
(out/'totals.json').write_text(json.dumps(totals,indent=2)+'\n')
for label,m in new['models'].items():
 invalid=[name for name,result in m['panels'].items() if not result['valid']]
 print(label,'INVALID',invalid)
 for name in ['game_all_stages','new_game_1_1','fpg_96','fpg_425']:
  print(name,m['panels'][name]['summary'])
print((out/'README.md').read_text())
