# Independent reference for test_teacher.cpp constants (pure Python, no shared code).
from math import pow, fabs
AF,AS=0.05,0.005
def ema_seq(outs,a):
    v=0.0
    for y in outs: v+=a*(y-v)
    return v
def corrected(raw,a,n): return raw/(1-pow(1-a,n))
# 1) five wins then five losses, per-outcome
seq=[1]*5+[0]*5
f5=ema_seq(seq[:5],AF); s5=ema_seq(seq[:5],AS)
f10=ema_seq(seq,AF); s10=ema_seq(seq,AS)
print("f5 %.17g s5 %.17g"%(f5,s5))
print("f10 %.17g s10 %.17g"%(f10,s10))
print("corr f10 %.17g s10 %.17g lp %.17g"%(corrected(f10,AF,10),corrected(s10,AS,10),fabs(corrected(f10,AF,10)-corrected(s10,AS,10))))
# 2) batched 7 wins at once equals sequential
print("batch7 %.17g seq7 %.17g"%(1-pow(0.95,7),ema_seq([1]*7,AF)))
# 3) hopeless factor 30 attempts 0 wins
p=(0+1)/(30+2); print("hopeless %.17g"%(4*p*(1-p)))
# 4) priority example: D=[2,1,0] L=[0,.3,.1] S=[100,50,50], power 1, w=.5/.35/.15, floor .1
import itertools
D=[2,1,0]; L=[0,.3,.1]; S=[100,50,50]
def ranks(v): return [1+sum(1 for x in v if x>y) for y in v]
rd,rl,rs=ranks(D),ranks(L),ranks(S); print("ranks",rd,rl,rs)
pr=[.5/rd[k]+.35/rl[k]+.15/rs[k] for k in range(3)]; tot=sum(pr)
P=[.9*pr[k]/tot+.1/3 for k in range(3)]
print("pr",["%.17g"%x for x in pr]); print("P",["%.17g"%x for x in P], sum(P))
# 5) L signal construction: record 5 losses then 5 wins -> corrected fast/slow and |diff|
seq2=[0]*5+[1]*5
f=corrected(ema_seq(seq2,AF),AF,10); s=corrected(ema_seq(seq2,AS),AS,10)
print("L5l5w fast %.17g slow %.17g lp %.17g"%(f,s,fabs(f-s)))
