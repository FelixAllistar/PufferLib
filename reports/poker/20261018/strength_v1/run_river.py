import json,subprocess,time
from pathlib import Path
root=Path('reports/poker/20261018/strength_v1')
while True:
 p=root/'slumbot_blind.jsonl'
 try:
  with p.open('rb') as f:
   f.seek(max(0,p.stat().st_size-8192));last=json.loads(f.read().splitlines()[-1])
 except (FileNotFoundError,json.JSONDecodeError,IndexError):last={}
 if 'summary' in last:
  assert last['summary']['target_reached']
  break
 time.sleep(5)
command=['build/poker/bench-venv/bin/python','ocean/poker/eval.py','--style','7','--library','build/poker/strength_v1/river/libpoker_fullhouse2.so','--hands','3000','--games','5','--seed','2026101824','--contact','https://github.com/FelixAllistar','--output',str(root/'slumbot_river.jsonl')]
(root/'river_live_command.json').write_text(json.dumps(command,indent=2)+'\n')
with Path('build/poker/strength_river_live.log').open('w') as log:
 subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
print('Finished river',flush=True)
