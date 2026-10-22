from pathlib import Path
from datetime import datetime,timezone
import subprocess,json,hashlib,shutil,shlex
repo=Path('/home/felix/puffertank/pufferlib')
root=repo/'reports/mario_sim'/('rom_finals_10b_'+datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S'))
root.mkdir()
remote='/workspace/PufferLib/reports/mario_sim/sweep_tight_run26_10b_20261014'
runs=['sweep_1792016021935_0000','sweep_1792052933835_0001']
names=['0000009999745024.bin'+suffix for suffix in ['', '.curriculum','.curriculum.json']]
files=[f'{remote}/checkpoints/mario_sim/{run}/{name}' for run in runs for name in names]
files += [f'{remote}/logs/mario_sim/{run}.ini' for run in runs]
script='from pathlib import Path\nimport hashlib,json\nfiles='+repr(files)+'\nprint(json.dumps({p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in files}))\n'
p=subprocess.run(['ssh','-p','25023','root@ssh5.vast.ai','python3','-'],input=script,text=True,capture_output=True,check=True)
hashes=json.loads(p.stdout.splitlines()[-1]);manifest=[]
for remote_file,expected in hashes.items():
 parts=Path(remote_file).parts
 if '/checkpoints/' in remote_file:
  dest=repo/'checkpoints/mario_sim'/parts[-2]/parts[-1]
 else: dest=root/parts[-1]
 dest.parent.mkdir(parents=True,exist_ok=True)
 subprocess.run(['scp','-q','-P','25023','root@ssh5.vast.ai:'+remote_file,str(dest)],check=True)
 actual=hashlib.sha256(dest.read_bytes()).hexdigest();assert actual==expected
 manifest.append({'remote':remote_file,'local':str(dest),'sha256':actual,'bytes':dest.stat().st_size})
(root/'download_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
old=repo/'reports/mario_sim/rom_levels_2b_6b_20261015_060222/results'
for name in ['evaluation.ini','levels.ini']:shutil.copyfile(old/name,root/name)
for name in ['evaluate_rom.py','sim2rom.cpp']:shutil.copyfile(repo/'ocean/mario_sim'/name,root/name)
command=['python3',str(root/'evaluate_rom.py')]
for i,run in enumerate(runs): command+=['--checkpoint',f'run{i}_10b',str(repo/'checkpoints/mario_sim'/run/names[0])]
command+=['--config',str(root/'evaluation.ini'),'--level-config',str(root/'levels.ini'),'--output',str(root/'results'),'--repo-root',str(repo),'--binary',str(repo/'build/mario_sim/fpg/sim2rom'),'--workers','4']
session='mario-rom-finals-'+datetime.now(timezone.utc).strftime('%H%M%S')
launch=shlex.join(command)+' > '+shlex.quote(str(root/'evaluation.log'))+' 2>&1'
(root/'launch.json').write_text(json.dumps({'command':command,'tmux':session,'created_utc':datetime.now(timezone.utc).isoformat()},indent=2)+'\n')
subprocess.run(['tmux','new-session','-d','-s',session,'-c',str(repo),launch],check=True)
Path('/tmp/mario_finals_eval_path').write_text(str(root))
shutil.copyfile(__file__,root/'prepare.py')
print('STARTED',root,session,flush=True)
