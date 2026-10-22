# Multi-seed confirmation of the top replication configs (paper-style: final learner and
# magnet exploitability at 2M environment steps, reported separately).
# Usage (in /workspace/PufferLib-paper): python3 run_seeds.py OUTDIR RUN [RUN ...]
import configparser,glob,json,os,re,subprocess,sys
out=sys.argv[1]; runs=[int(r) for r in sys.argv[2:]]; seeds=range(1,9)
os.makedirs(out,exist_ok=True)
results=json.load(open(f"{out}/results.json")) if os.path.exists(f"{out}/results.json") else {}
for run in runs:
    path=glob.glob(f"reports/goofspiel_sweeps/paper_replication_0*/logs/goofspiel/sweep_*_{run:04d}.ini")[0]
    c=configparser.ConfigParser(interpolation=None,strict=False); c.optionxform=str; c.read(path)
    # Trial logs add a [metrics] results section the trainer does not accept.
    base=[f"--{s}.{k}={v}" for s in c.sections() if not s.startswith("sweep.") and s!="metrics" for k,v in c[s].items()
          if f"{s}.{k}" not in ("base.run_id","base.result_fd","base.checkpoint_dir","base.log_dir")]
    for seed in seeds:
        key=f"{run}/{seed}"
        if key in results: continue
        d=f"{out}/run{run:04d}_seed{seed}"
        args=["./puffer","train",*base,f"--base.seed={seed}",f"--env.seed={1000+seed}",
              "--sweep.early_stop_patience_steps=0",f"--base.checkpoint_dir={d}/checkpoints",f"--base.log_dir={d}/logs"]
        text=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True).stdout
        learner=[float(x) for x in re.findall(r"Exact checkpoint: exploitability=([0-9.e-]+)",text)]
        magnet=[float(x) for x in re.findall(r"Exact magnet: exploitability=([0-9.e-]+)",text)]
        open(f"{d}.log","w").write(text)
        if not learner: print(key,"FAILED, see",f"{d}.log",flush=True); continue
        results[key]=dict(learner=learner[-1],magnet=magnet[-1] if magnet else None,
                          best=min(learner+magnet),checkpoints=len(learner))
        print(key,results[key],flush=True)
        json.dump(results,open(f"{out}/results.json","w"),indent=1)
