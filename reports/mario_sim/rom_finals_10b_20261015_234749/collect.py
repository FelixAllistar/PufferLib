from pathlib import Path
import subprocess,json,time,hashlib,tarfile,shutil,importlib.util,os,signal
repo=Path('/home/felix/puffertank/pufferlib');root=Path(Path('/tmp/mario_finals_eval_path').read_text());remote='/workspace/PufferLib/reports/mario_sim/'+root.name
required={'game_all_stages','new_game_1_1','fpg_96','fpg_425'}|{f'level_{w}_{l}' for w in range(1,7) for l in range(1,5)}
script='from pathlib import Path\nimport json\np=Path('+repr(remote)+')\nf=p/"results/summary.json"\nif not f.exists():f=p/"results/progress.json"\nr=json.loads(f.read_text()) if f.exists() else {}\nprint(json.dumps(r))\n'
while True:
 p=subprocess.run(['ssh','-p','25023','root@ssh5.vast.ai','python3','-'],input=script,text=True,capture_output=True)
 if p.returncode:
  print('Remote status unavailable; retrying',p.stderr[-120:],flush=True);time.sleep(30);continue
 r=json.loads(p.stdout.splitlines()[-1]);models=r.get('models',{})
 (root/'remote_progress_snapshot.json').write_text(json.dumps(r,indent=2)+'\n')
 all_done=len(models)==2 and all(len(m['panels'])==36 for m in models.values())
 prefix_done=len(models)==2 and all(required<=set(m['panels']) for m in models.values())
 tail_path=root/'local_tail/summary.json';tail_done=tail_path.exists()
 counts={label:len(m['panels']) for label,m in models.items()};tail_counts={}
 f=root/'local_tail/progress.json'
 if f.exists():tail_counts={k:len(v['panels']) for k,v in json.loads(f.read_text())['models'].items()}
 print(json.dumps({'remote':counts,'local_tail':tail_counts,'prefix_done':prefix_done,'tail_done':tail_done,'all_remote_done':all_done}),flush=True)
 if all_done or (prefix_done and tail_done):break
 time.sleep(40)
use_tail=not all_done
finish='from pathlib import Path\nimport json,subprocess,os,signal,time,tarfile,hashlib\nroot=Path('+repr(remote)+')\n'
finish+='''r=json.loads((root/'results/progress.json').read_text())
pids=[]
for d in Path('/proc').iterdir():
 if not d.name.isdigit():continue
 try:a=[x.decode() for x in (d/'cmdline').read_bytes().split(b'\\0') if x]
 except (OSError,UnicodeError):continue
 match=len(a)>1 and a[1]==str(root/'assets/evaluate_rom.py')
 match=match or (a and Path(a[0]).name=='sim2rom' and any(x.startswith(str(root/'results')) for x in a))
 if match:pids.append(int(d.name))
for pid in pids:
 try:os.kill(pid,signal.SIGTERM)
 except ProcessLookupError:pass
session=json.loads((root/'launch.json').read_text())['tmux']
subprocess.run(['tmux','kill-session','-t',session],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(.2)
(root/'remote_stop.json').write_text(json.dumps({'pids':pids,'reason':'All required panels completed; remaining panels supplied by local shard or full batch finished.'},indent=2)+'\\n')
archive=root/'completed_results.tar.gz'
with tarfile.open(archive,'w:gz') as t:
 for name in ['results','evaluation.log','launch.json','remote_stop.json']:
  t.add(root/name,arcname=name)
print(json.dumps({'path':str(archive),'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'bytes':archive.stat().st_size}))
'''
p=subprocess.run(['ssh','-p','25023','root@ssh5.vast.ai','python3','-'],input=finish,text=True,capture_output=True,check=True)
manifest=json.loads(p.stdout.splitlines()[-1]);archive=root/'completed_results.tar.gz'
subprocess.run(['scp','-q','-P','25023','root@ssh5.vast.ai:'+manifest['path'],str(archive)],check=True)
assert hashlib.sha256(archive.read_bytes()).hexdigest()==manifest['sha256']
snapshot=root/'remote_snapshot';snapshot.mkdir()
with tarfile.open(archive) as t:t.extractall(snapshot,filter='data')
shutil.move(snapshot/'results',root/'results')
report=json.loads((root/'results/progress.json').read_text())
(root/'remote_result_download_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for n in ['evaluation.log','launch.json','remote_stop.json']:shutil.copyfile(snapshot/n,root/('remote_'+n))
if use_tail:
 tail=json.loads(tail_path.read_text())
 for key in ['seed','config_sha256','level_config_sha256','evaluator_sha256','rom_sha256']:assert report[key]==tail[key],key
 for label,m in tail['models'].items():
  assert report['models'][label]['sha256']==m['sha256']
  for name,result in m['panels'].items():
   assert name.startswith(('level_7_','level_8_'))
   report['models'][label]['panels'][name]=result
   shutil.copytree(root/'local_tail'/label/name,root/'results'/label/name,dirs_exist_ok=True)
   shutil.copyfile(root/'local_tail'/label/(name+'.log'),root/'results'/label/(name+'.log'))
 report['execution_shards']={'remote':str(root/'remote_progress_snapshot.json'),'local_tail':str(tail_path)}
else:
 # Stop only this task's remaining local shard if the full remote suite finishes first.
 pids=[]
 for d in Path('/proc').iterdir():
  if not d.name.isdigit():continue
  try:a=[x.decode() for x in (d/'cmdline').read_bytes().split(b'\0') if x]
  except (OSError,UnicodeError):continue
  match=len(a)>1 and a[1]==str(root/'evaluate_tail.py')
  match=match or (a and Path(a[0]).name=='sim2rom' and any(x.startswith(str(root/'local_tail')) for x in a))
  if match:pids.append(int(d.name))
 for pid in pids:
  try:os.kill(pid,signal.SIGTERM)
  except ProcessLookupError:pass
for m in report['models'].values():assert len(m['panels'])==36
report['all_validated']=all(x['valid'] for m in report['models'].values() for x in m['panels'].values())
report['completed_by_shards']=use_tail
spec=importlib.util.spec_from_file_location('eval_rom',repo/'ocean/mario_sim/evaluate_rom.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
module.write_json(root/'results/summary.json',report)
module.render(root/'results',report,list(report['models']),32)
shutil.copyfile(__file__,root/'collect.py')
subprocess.run(['python3','/tmp/mario_finals_finish.py'],check=True)
print('COMPLETE',root,'all_validated',report['all_validated'],flush=True)
