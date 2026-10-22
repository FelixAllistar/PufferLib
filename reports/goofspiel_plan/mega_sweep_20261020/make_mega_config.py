# Rewrites config/goofspiel.ini sweep ranges in place for the Goofspiel mega sweep.
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
key("sweep","max_runs","1000")
section("sweep.train.total_timesteps","distribution = log_normal\nmin = 2_097_152\nmax = 104_857_600\nscale = time")
section("sweep.policy.hidden_size","distribution = uniform_pow2\nmin = 16\nmax = 256\nscale = auto")
section("sweep.policy.num_layers","distribution = int_uniform\nmin = 1\nmax = 8\nscale = auto")
section("sweep.train.horizon","distribution = uniform_pow2\nmin = 2\nmax = 64\nscale = auto")
section("sweep.train.minibatch_size","distribution = uniform_pow2\nmin = 512\nmax = 16384\nscale = auto")
section("sweep.train.replay_ratio","distribution = uniform\nmin = 1\nmax = 12\nscale = auto")
section("sweep.train.learning_rate","distribution = log_normal\nmin = 0.00001\nmax = 0.05\nscale = auto")
section("sweep.train.ent_coef","distribution = log_normal\nmin = 0.00001\nmax = 0.3\nscale = auto")
section("sweep.train.gamma","distribution = logit_normal\nmin = 0.9\nmax = 0.9999\nscale = auto")
section("sweep.train.momentum","distribution = logit_normal\nmin = 0.5\nmax = 0.999\nscale = auto")
section("sweep.train.gae_lambda","distribution = logit_normal\nmin = 0.5\nmax = 0.999\nscale = auto")
section("sweep.train.clip_coef","distribution = log_normal\nmin = 0.001\nmax = 0.5\nscale = auto")
section("sweep.train.vf_coef","distribution = uniform\nmin = 0.1\nmax = 5\nscale = auto")
# EMAgnet dimensions, paper ranges (arXiv 2606.23995 Table 2)
section("sweep.train.magnet_coef","distribution = log_normal\nmin = 0.01\nmax = 32\nscale = auto")
section("sweep.train.magnet_tau","distribution = log_normal\nmin = 0.00001\nmax = 0.1\nscale = auto")
section("sweep.train.anneal_magnet_coef","distribution = int_uniform\nmin = 0\nmax = 1\nscale = auto")
section("sweep.train.min_magnet_coef_ratio","distribution = uniform\nmin = 0\nmax = 1\nscale = auto")
open(p,"w").write(s)
