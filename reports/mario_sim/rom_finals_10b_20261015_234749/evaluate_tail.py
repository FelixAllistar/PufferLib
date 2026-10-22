from pathlib import Path
import importlib.util,json,concurrent.futures,shutil
repo=Path('/home/felix/puffertank/pufferlib');root=Path(Path('/tmp/mario_finals_eval_path').read_text());out=root/'local_tail';out.mkdir()
spec=importlib.util.spec_from_file_location('eval_rom',repo/'ocean/mario_sim/evaluate_rom.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
for n in ['evaluation.ini','levels.ini']:shutil.copyfile(root/n,out/n)
report=json.loads((root/'local_results_partial/manifest.json').read_text())
for m in report['models'].values():m['panels']={}
module.write_json(out/'manifest.json',report)
models=[(label,Path(m['checkpoint'])) for label,m in report['models'].items()]
for label,path in models:(out/label).mkdir()
panels=[p for p in module.panels(32,True) if p[0].startswith(('level_7_','level_8_'))]
jobs=[(m,p) for p in panels for m in models]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 futures=[pool.submit(module.execute,repo,repo/'build/mario_sim/fpg/sim2rom',out,m,p,137,15) for m,p in jobs]
 for i,f in enumerate(concurrent.futures.as_completed(futures),1):
  label,name,result=f.result();report['models'][label]['panels'][name]=result
  print(i,len(jobs),label,name,(result['summary'] or {}).get('rom_successes'),result['valid'],flush=True)
  module.write_json(out/'progress.json',report)
report['all_validated']=all(p['valid'] for m in report['models'].values() for p in m['panels'].values())
module.write_json(out/'summary.json',report)
print('DONE',report['all_validated'],flush=True)
