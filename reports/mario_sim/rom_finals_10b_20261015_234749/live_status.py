from pathlib import Path
import subprocess,json,shutil
root=Path(Path('/tmp/mario_finals_eval_path').read_text())
script='''from pathlib import Path
import json,datetime,subprocess
root=Path('/workspace/PufferLib/reports/mario_sim/sweep_tight_run26_10b_20261014')
checkpoints={}
for d in sorted((root/'checkpoints/mario_sim').glob('*')):
 fs=sorted(d.glob('*.bin'))
 checkpoints[d.name]=[{'name':f.name,'steps':int(f.stem),'saved_utc':datetime.datetime.fromtimestamp(f.stat().st_mtime,datetime.timezone.utc).isoformat(),'bytes':f.stat().st_size} for f in fs]
log=(root/'sweep.log').read_text()
score_lines=[line for line in log.splitlines() if line.startswith('sweep run=')]
print(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'checkpoints':checkpoints,'completed_trials':score_lines,'gpu':subprocess.check_output(['nvidia-smi','--query-gpu=utilization.gpu,memory.used','--format=csv,noheader,nounits'],text=True).strip(),'sweep_config':(root/'config.ini').read_text()}))
'''
p=subprocess.run(['ssh','-p','25023','root@ssh5.vast.ai','python3','-'],input=script,text=True,capture_output=True,check=True)
r=json.loads(p.stdout.splitlines()[-1]);(root/'live_status_after_evaluation.json').write_text(json.dumps(r,indent=2)+'\n');shutil.copyfile(__file__,root/'live_status.py')
print(json.dumps({k:v for k,v in r.items() if k!='sweep_config'},indent=2))
