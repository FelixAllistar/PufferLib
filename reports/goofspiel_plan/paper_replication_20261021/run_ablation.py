# Setup-gap ablations on replication config 442 (8 seeds per arm, final learner and magnet
# exploitability at 2M environment steps). Every arm, baseline included, uses ./puffer_ablate.
# Usage (in /workspace/PufferLib-paper): python3 run_ablation.py OUTDIR [SEEDS] [ARM ...]
import configparser,glob,json,os,re,subprocess,sys
ARMS={
    "base":{},
    "adam_lr3e-4":{"train.optimizer":"1","train.learning_rate":"0.0003"},
    "adam_lr1e-3":{"train.optimizer":"1","train.learning_rate":"0.001"},
    "memoryless":{"policy.memoryless":"1"},
    "openspiel_obs":{"env.open_spiel_obs":"1"},
}
out=sys.argv[1]; seeds=range(1,int(sys.argv[2])+1 if len(sys.argv)>2 else 9); arms=sys.argv[3:] or list(ARMS)
os.makedirs(out,exist_ok=True)
path=glob.glob("reports/goofspiel_sweeps/paper_replication_0*/logs/goofspiel/sweep_*_0442.ini")[0]
c=configparser.ConfigParser(interpolation=None,strict=False); c.optionxform=str; c.read(path)
base=[f"--{s}.{k}={v}" for s in c.sections() if not s.startswith("sweep.") and s!="metrics" for k,v in c[s].items()
      if f"{s}.{k}" not in ("base.run_id","base.result_fd","base.checkpoint_dir","base.log_dir")]
results=json.load(open(f"{out}/results.json")) if os.path.exists(f"{out}/results.json") else {}
for arm in arms:
    for seed in seeds:
        key=f"{arm}/{seed}"
        if key in results: continue
        d=f"{out}/{arm}_seed{seed}"
        args=["./puffer_ablate","train",*base,*[f"--{k}={v}" for k,v in ARMS[arm].items()],
              f"--base.seed={seed}",f"--env.seed={1000+seed}","--sweep.early_stop_patience_steps=0",
              f"--base.checkpoint_dir={d}/checkpoints",f"--base.log_dir={d}/logs"]
        text=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True).stdout
        open(f"{d}.log","w").write(text)
        learner=[float(x) for x in re.findall(r"Exact checkpoint: exploitability=([0-9.e-]+)",text)]
        magnet=[float(x) for x in re.findall(r"Exact magnet: exploitability=([0-9.e-]+)",text)]
        if not learner: print(key,"FAILED, see",f"{d}.log",flush=True); continue
        results[key]=dict(learner=learner[-1],magnet=magnet[-1] if magnet else None,best=min(learner+magnet),checkpoints=len(learner))
        print(key,results[key],flush=True)
        json.dump(results,open(f"{out}/results.json","w"),indent=1)
