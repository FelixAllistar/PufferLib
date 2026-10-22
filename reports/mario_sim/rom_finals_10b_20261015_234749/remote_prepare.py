from pathlib import Path
import subprocess,tarfile,json,shlex,hashlib,shutil
repo=Path('/home/felix/puffertank/pufferlib');root=Path(Path('/tmp/mario_finals_eval_path').read_text())
remote='/workspace/PufferLib/reports/mario_sim/'+root.name
archive=root/'remote_assets.tar.gz'
with tarfile.open(archive,'w:gz') as t:
 for name,path in [('sim2rom',repo/'build/mario_sim/fpg/sim2rom'),('evaluate_rom.py',repo/'ocean/mario_sim/evaluate_rom.py'),('evaluation.ini',root/'evaluation.ini'),('levels.ini',root/'levels.ini')]:t.add(path,arcname='assets/'+name)
 for name in ['states','bank.bin']:t.add(repo/'build/mario_sim/fpg/curriculum'/name,arcname='runtime/build/mario_sim/fpg/curriculum/'+name)
 t.add(repo/'build/mario_sim/fpg/curriculum_targets.bin',arcname='runtime/build/mario_sim/fpg/curriculum_targets.bin')
cmd=['scp','-q','-P','25023',str(archive),'root@ssh5.vast.ai:/tmp/'+root.name+'.tar.gz']
subprocess.run(cmd,check=True)
script='from pathlib import Path\nimport tarfile,subprocess,json,shutil,hashlib\nroot=Path('+repr(remote)+')\narchive=Path('+repr('/tmp/'+root.name+'.tar.gz')+')\n'
script+='assert hashlib.sha256(archive.read_bytes()).hexdigest()=='+repr(hashlib.sha256(archive.read_bytes()).hexdigest())+'\n'
script+='''root.mkdir(exist_ok=False)
with tarfile.open(archive) as t:t.extractall(root,filter='data')
repo=Path('/workspace/PufferLib')
shutil.copytree(repo/'ocean/retro/roms',root/'runtime/ocean/retro/roms')
'''
command=['taskset','-c','46,47','python3',remote+'/assets/evaluate_rom.py']
for i,run in enumerate(['sweep_1792016021935_0000','sweep_1792052933835_0001']):command+=['--checkpoint',f'run{i}_10b','/workspace/PufferLib/reports/mario_sim/sweep_tight_run26_10b_20261014/checkpoints/mario_sim/'+run+'/0000009999745024.bin']
command+=['--config',remote+'/assets/evaluation.ini','--level-config',remote+'/assets/levels.ini','--output',remote+'/results','--repo-root',remote+'/runtime','--binary',remote+'/assets/sim2rom','--workers','4','--nice','15']
launch=shlex.join(command)+' > '+shlex.quote(remote+'/evaluation.log')+' 2>&1'
session='mario-finals-'+root.name.split('_')[-1]
script+='command='+repr(command)+'\nsession='+repr(session)+'\n'
script+='(root/"launch.json").write_text(json.dumps({"command":command,"tmux":session,"cpu_affinity":[46,47]},indent=2)+"\\n")\n'
script+='subprocess.run(["tmux","new-session","-d","-s",session,"-c",str(repo),'+repr(launch)+'],check=True)\nprint(json.dumps({"root":str(root),"tmux":session}))\n'
p=subprocess.run(['ssh','-p','25023','root@ssh5.vast.ai','python3','-'],input=script,text=True,capture_output=True,check=True)
(root/'remote_launch.log').write_text(p.stdout+p.stderr)
paths={'local':str(root),'remote':remote,'tmux':session}
(root/'remote_paths.json').write_text(json.dumps(paths,indent=2)+'\n')
shutil.copyfile(__file__,root/'remote_prepare.py')
print(p.stdout,flush=True)
