#!/usr/bin/env python3
"""Rerun chosen Aug 1 sweep trials with many fresh seeds (exact saved settings) and record each
seed's final learner and final magnet exploitability. Usage, in /workspace/PufferLib-paper:
  python3 seed_hunt.py OUTDIR SEEDS TRIAL [TRIAL ...]   e.g. seed_hunt.py out 64 0248 0098"""
import glob,json,os,re,subprocess,sys
out=sys.argv[1]; n=int(sys.argv[2]); trials=sys.argv[3:]
root='reports/goofspiel_sweeps/aug1_sweep_01/checkpoints/goofspiel'
os.makedirs(out,exist_ok=True)
path=f'{out}/results.json'
results=json.load(open(path)) if os.path.exists(path) else {}
for trial in trials:
    settings=json.load(open(glob.glob(f'{root}/sweep_*_{trial}/trial.json')[0]))['settings']
    for seed in range(101,101+n):  # new seeds; the sweep used 1-4
        key=f'{trial}/{seed}'
        if key in results: continue
        d=f'{out}/{trial}_seed{seed}'
        args=dict(settings)
        args.update({'base.result_fd':'0','base.seed':str(seed),'env.seed':str(2000+seed),'base.run_id':f'hunt_{trial}_{seed}',
                     'base.checkpoint_dir':f'{d}/c','base.log_dir':f'{d}/l','sweep.early_stop_patience_steps':'0'})
        text=subprocess.run(['./puffer_ablate','train',*[f'--{k}={v}' for k,v in args.items()]],
                            stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True).stdout
        learner=[float(x) for x in re.findall(r'Exact checkpoint: exploitability=([0-9.e-]+)',text)]
        magnet=[float(x) for x in re.findall(r'Exact magnet: exploitability=([0-9.e-]+)',text)]
        if not learner or not magnet:
            open(f'{d}.log','w').write(text); print(key,'FAILED',flush=True); continue
        subprocess.run(['rm','-rf',d])  # keep only scores; checkpoints are ~MBs each
        results[key]=dict(learner=learner[-1],magnet=magnet[-1])
        json.dump(results,open(path,'w'),indent=1)
        best=min(results.values(),key=lambda r:r['magnet'])
        print(f'{key} magnet {magnet[-1]:.4f} learner {learner[-1]:.4f} | best so far {best["magnet"]:.4f}',flush=True)
