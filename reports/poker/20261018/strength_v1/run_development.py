import json,subprocess
from pathlib import Path
root=Path('reports/poker/20261018/strength_v1')
for index,name in ((2,'context'),(3,'blind')):
 command=['build/poker/bench-venv/bin/python','ocean/poker/eval.py','--style','7','--library',f'build/poker/strength_v1/{name}/libpoker_fullhouse2.so','--hands','3000','--games','5','--seed',str(2026101820+index),'--contact','https://github.com/FelixAllistar','--output',str(root/f'slumbot_{name}.jsonl')]
 (root/f'{name}_live_command.json').write_text(json.dumps(command,indent=2)+'\n')
 print('Starting',name,flush=True)
 with Path(f'build/poker/strength_{name}_live.log').open('w') as log:
  subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
 print('Finished',name,flush=True)
