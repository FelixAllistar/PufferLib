from pathlib import Path
import configparser, json, subprocess, concurrent.futures, hashlib, shutil, time
ROOT=Path('/home/felix/puffertank/pufferlib')
OUT=Path(__file__).resolve().parent
MODEL=ROOT/'checkpoints/mario_fpg_time/1791505432976/0000000268435456.bin'
c=configparser.ConfigParser(interpolation=None);c.read(ROOT/'logs/mario_fpg_time/1791505432976.ini')
for name,cap in [('fpg_eval.ini',1800),('full_level.ini',6000)]:
 cfg=configparser.ConfigParser(interpolation=None)
 for section in ['policy','env']:cfg[section]=dict(c[section])
 cfg['env']['max_frames']=str(cap)
 with (OUT/name).open('w') as f:cfg.write(f)
shutil.copyfile(ROOT/'logs/mario_fpg_time/1791505432976.ini',OUT/'training_run.ini')
shutil.copyfile(str(MODEL)+'.curriculum.json',OUT/'training_curriculum.json')
cases=[(f'fpg_{d}',64,0,'build/mario_fpg_time/curriculum',d,'fpg') for d in [64,72,80,88,96,425]]
cases += [('pipe_sampled',64,0,'build/mario_fpg_time/curriculum',425,'clear'),('pipe_greedy',1,1,'build/mario_fpg_time/curriculum',425,'clear'),('clear_control',8,0,'build/mario_fpg_time/curriculum',64,'clear'),('new_game_greedy',1,1,'new-game',0,'clear'),('new_game_sampled',64,0,'new-game',0,'clear')]
def run(case):
 name,n,greedy,bank,depth,objective=case
 command=[str(ROOT/'build/mario_fpg_time/sim2rom'),str(MODEL),str(OUT/('fpg_eval.ini' if objective=='fpg' else 'full_level.ini')),str(OUT/name),str(n),'137',str(greedy),bank,str(depth),objective]
 started=time.monotonic()
 with (OUT/(name+'.log')).open('w') as f:
  f.write(json.dumps(command)+'\n');f.flush();result=subprocess.run(command,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
 if result.returncode:raise RuntimeError(f'{name} failed: see log')
 report=json.loads((OUT/name/'summary.json').read_text())
 assert report['passed'] and report['episodes_completed']==n and report['mismatches']==0
 report['wall_seconds']=time.monotonic()-started;report['command']=command
 print(name,json.dumps(report),flush=True)
 return name,report
results={}
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 for future in concurrent.futures.as_completed([pool.submit(run,case) for case in cases]):
  name,result=future.result();results[name]=result
  (OUT/'progress.json').write_text(json.dumps(results,indent=2)+'\n')
report={'checkpoint':str(MODEL.relative_to(ROOT)),'checkpoint_sha256':hashlib.sha256(MODEL.read_bytes()).hexdigest(),'training_additional_steps':268435456,'training_total_across_two_runs':536870912,'time_bonus':0.1,'training_curriculum':json.loads((OUT/'training_curriculum.json').read_text()),'results':results,'totals':{'episodes':sum(r['episodes_completed'] for r in results.values()),'native_and_rom_frames_compared':sum(r['frames'] for r in results.values()),'mismatches':sum(r['mismatches'] for r in results.values())},'comparison':'Same starts, seed 137, episode counts, caps and FP32 CPU inference as eval_1791497853012_20261008.'}
(OUT/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('DONE',json.dumps(report['totals']),flush=True)
