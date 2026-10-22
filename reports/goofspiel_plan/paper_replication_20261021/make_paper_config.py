# Rewrites config/goofspiel.ini in place for the EMAgnet paper-replication sweep
# (arXiv 2606.23995, Table 2 PPO-EMAg ranges; Figure 2b budget of 2M environment steps).
# Only the paper's 9 dimensions plus the unreported rollout shape are searched.
import re,sys
p=sys.argv[1]; s=open(p).read()
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
# Drop every existing search dimension, then add back only the ones below.
# Dimensions inherited from config/default.ini are pinned (min = max) after the
# fixed values are set; an absent section would fall back to the default range.
default=open(sys.argv[2]).read()
inherited=re.findall(r"^\[sweep\.([^\]]+)\]\n((?:(?!\[).*\n)*)", s+default, re.M)
s=re.sub(r"^\[sweep\.[^\]]+\]\n(?:(?!\[).*\n)*", "", s, flags=re.M)
# 2M environment steps = 4M agent steps (both players act every step).
fixed = {
    "train": {"total_timesteps": "4_194_304", "anneal_magnet_coef": "0", "min_lr_ratio": "0",
              "min_ent_coef_ratio": "0", "vtrace": "0", "reward_clip": "1"},
    "selfplay": {"enabled": "0"},
    "env": {"exact_exploiter": "0", "exploitability_interval_steps": "1_048_576"},
    "base": {"async": "0", "magnet_path": "None"},
    "policy": {"hidden_size": "128", "num_layers": "4"},
}
for sec, values in fixed.items():
    for k, v in values.items(): key(sec, k, v)
key("sweep", "max_runs", "700")
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
    dist=dists[name]
    v=current(name)
    section("sweep."+name, "distribution = %s\nmin = %s\nmax = %s\nscale = auto" % (dist, v, v))
# Paper Table 2 (PPO-EMAg). LR keeps our Muon range; GAE caps below 1 for logit_normal.
section("sweep.train.magnet_coef","distribution = log_normal\nmin = 0.01\nmax = 32\nscale = auto")
section("sweep.train.magnet_tau","distribution = log_normal\nmin = 0.00001\nmax = 0.1\nscale = auto")
section("sweep.train.ent_coef","distribution = log_normal\nmin = 0.0001\nmax = 0.1\nscale = auto")
section("sweep.train.anneal_ent_coef","distribution = int_uniform\nmin = 0\nmax = 1\nscale = auto")
section("sweep.train.learning_rate","distribution = log_normal\nmin = 0.00001\nmax = 0.05\nscale = auto")
section("sweep.train.anneal_lr","distribution = int_uniform\nmin = 0\nmax = 1\nscale = auto")
section("sweep.train.clip_coef","distribution = log_normal\nmin = 0.0001\nmax = 0.4\nscale = auto")
section("sweep.train.gae_lambda","distribution = logit_normal\nmin = 0.6\nmax = 0.999\nscale = auto")
section("sweep.train.vf_coef","distribution = uniform\nmin = 0.1\nmax = 5\nscale = auto")
# Rollout shape: not reported in the paper; small batches give many more updates per 2M steps.
section("sweep.vec.total_agents","distribution = uniform_pow2\nmin = 64\nmax = 4096\nscale = auto")
section("sweep.train.horizon","distribution = uniform_pow2\nmin = 4\nmax = 32\nscale = auto")
section("sweep.train.minibatch_size","distribution = uniform_pow2\nmin = 256\nmax = 8192\nscale = auto")
section("sweep.train.replay_ratio","distribution = uniform\nmin = 1\nmax = 8\nscale = auto")
open(p,"w").write(s)
