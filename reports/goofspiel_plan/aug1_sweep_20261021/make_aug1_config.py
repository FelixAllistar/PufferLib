# Rewrites config/goofspiel.ini in place for the Aug 1 neighborhood sweep: every setting starts at
# the original Aug 1 config (commit 797ae7550; 0.0169 mean final magnet on current code) and only
# ten dimensions around it are searched. Trials are scored by seed_worker.py (mean of 4 seeds).
# Usage: make_aug1_config.py config/goofspiel.ini config/default.ini
import re,sys
p=sys.argv[1]; s=open(p).read(); default=open(sys.argv[2]).read()
def section(name, body):
    global s
    pat=re.compile(r"^\[%s\]\n(?:(?!\[).*\n)*" % re.escape(name), re.M)
    new="[%s]\n%s\n" % (name, body.strip()+"\n")
    if pat.search(s): s=pat.sub(new, s, count=1)
    else: s=s.rstrip("\n")+"\n\n"+new
def key(sec, k, v):
    global s
    m=re.search(r"^\[%s\]\n((?:(?!\[).*\n)*)" % re.escape(sec), s, re.M)
    body=m.group(1)
    if re.search(r"^%s = .*$" % k, body, re.M): body=re.sub(r"^%s = .*$" % k, "%s = %s" % (k,v), body, count=1, flags=re.M)
    else: body=body.rstrip("\n")+"\n%s = %s\n\n" % (k,v)
    s=s[:m.start(1)]+body+s[m.end(1):]
# Every inherited dimension is pinned below unless searched; drop the existing ones first.
inherited=re.findall(r"^\[sweep\.([^\]]+)\]\n((?:(?!\[).*\n)*)", s+default, re.M)
s=re.sub(r"^\[sweep\.[^\]]+\]\n(?:(?!\[).*\n)*", "", s, flags=re.M)
fixed = {
    "base": {"async": "0", "cudagraphs": "0", "magnet_path": "None"},
    "vec": {"total_agents": "2048", "num_buffers": "1", "num_threads": "4", "num_policies": "1", "hist_policy_percent": "0"},
    "selfplay": {"enabled": "0"},
    "policy": {"hidden_size": "64", "num_layers": "2"},
    "env": {"exact_exploiter": "0", "exploitability_interval_steps": "4096000"},
    "train": {"total_timesteps": "32768000", "learning_rate": "0.00860058703", "anneal_lr": "1", "min_lr_ratio": "0",
              "gamma": "0.999", "gae_lambda": "0.999", "replay_ratio": "4", "clip_coef": "0.0435373858",
              "vf_coef": "0.767722487", "vf_clip_coef": "0.806706", "max_grad_norm": "0.5", "ent_coef": "0.0164826009",
              "anneal_ent_coef": "0", "magnet_coef": "0.117609896", "magnet_tau": "0.00094514084",
              "anneal_magnet_coef": "0", "momentum": "0.916658", "minibatch_size": "4096", "horizon": "8",
              "vtrace": "0", "reward_clip": "1"},
    "sweep": {"max_runs": "300", "max_suggestion_cost": "1800",
              "worker_path": "reports/goofspiel_plan/aug1_sweep_20261021/seed_worker.py",
              "trainer_path": "./puffer_ablate"},
}
for sec, values in fixed.items():
    for k, v in values.items(): key(sec, k, v)
# Ten searched dimensions, each range containing its Aug 1 value.
section("sweep.train.total_timesteps","distribution = log_normal\nmin = 16000000\nmax = 65000000\nscale = time")
section("sweep.train.magnet_coef","distribution = log_normal\nmin = 0.03\nmax = 0.5\nscale = auto")
section("sweep.train.magnet_tau","distribution = log_normal\nmin = 0.0001\nmax = 0.01\nscale = auto")
section("sweep.train.ent_coef","distribution = log_normal\nmin = 0.004\nmax = 0.05\nscale = auto")
section("sweep.train.learning_rate","distribution = log_normal\nmin = 0.002\nmax = 0.03\nscale = auto")
section("sweep.train.clip_coef","distribution = log_normal\nmin = 0.015\nmax = 0.15\nscale = auto")
section("sweep.train.vf_coef","distribution = log_normal\nmin = 0.3\nmax = 2\nscale = auto")
section("sweep.train.replay_ratio","distribution = uniform\nmin = 2\nmax = 6\nscale = auto")
section("sweep.vec.total_agents","distribution = uniform_pow2\nmin = 1024\nmax = 4096\nscale = auto")
section("sweep.policy.hidden_size","distribution = uniform_pow2\nmin = 32\nmax = 128\nscale = auto")
def current(name):
    sec, k = name.rsplit(".", 1)
    for text in (s, default):
        m=re.search(r"^\[%s\]\n((?:(?!\[).*\n)*)" % re.escape(sec), text, re.M)
        v=m and re.search(r"^%s = (.*)$" % re.escape(k), m.group(1), re.M)
        if v: return v.group(1).strip()
    raise KeyError(name)
dists={}
for name, body in inherited:
    m=re.search(r"^distribution = (.*)$", body, re.M)
    if m: dists.setdefault(name, m.group(1).strip())
for name in dists:
    if "[sweep.%s]" % name in s: continue  # searched above, or already pinned
    v=current(name)
    section("sweep."+name, "distribution = %s\nmin = %s\nmax = %s\nscale = auto" % (dists[name], v, v))
open(p,"w").write(s)
