import struct,sys,subprocess,json
def load(path):
    t=subprocess.run(f"zcat {path} | tail -c 400000",shell=True,capture_output=True).stdout
    for n in range(1,4000):
        off=len(t)-n*96-16
        if off<0: break
        a,b=struct.unpack_from('<QQ',t,off)
        if a==n and b==96:
            gs=[]
            for k in range(n):
                key,area,level,section,elig,res,fast,slow,att,wins,deaths,dstamp,lastsel,prob=struct.unpack_from('<QIiiiiddQQdQQd',t,off+16+k*96)
                gs.append(dict(area=area,level=level,section=section,elig=elig,fast=fast,slow=slow,att=att,wins=wins,deaths=deaths,dstamp=dstamp,last=lastsel,prob=prob))
            return gs
    raise SystemExit('no groups found')
def rate(g,a,raw):
    n=g['att']; 
    return raw/(1-(1-a)**n) if n else 0
gs=load(sys.argv[1])
name=lambda l:f"{l//4+1}-{l%4+1}"
el=[g for g in gs if g['elig']]
print(f"groups={len(gs)} eligible={len(el)} attempts={sum(g['att'] for g in gs)} wins={sum(g['wins'] for g in gs)} sumprob={sum(g['prob'] for g in gs):.3f}")
# per level
import collections
L=collections.defaultdict(lambda:[0,0,0,0.0,0.0])
for g in gs:
    x=L[g['level']];x[0]+=1;x[1]+=g['att'];x[2]+=g['wins'];x[3]+=g['prob'];x[4]+=g['deaths']
print("level groups attempts winrate  prob   deathmass")
for l in sorted(L):
    x=L[l];print(f"{name(l):>5} {x[0]:>5} {x[1]:>8} {x[2]/max(1,x[1]):7.3f} {x[3]:6.3f} {x[4]:9.1f}")
if len(sys.argv)>2:
    print("top groups by prob: level sec area prob att winrate fast slow |LP| deaths")
    for g in sorted(gs,key=lambda g:-g['prob'])[:int(sys.argv[2])]:
        f=rate(g,0.05,g['fast']);s=rate(g,0.005,g['slow'])
        print(f"{name(g['level']):>5} s{g['section']:<3} {g['area']&0xffffff:06x} {g['prob']:.4f} {g['att']:>7} {g['wins']/max(1,g['att']):.3f} {f:.3f} {s:.3f} {abs(f-s):.3f} {g['deaths']:.1f}")
    print("lowest win-rate groups with >=200 attempts:")
    for g in sorted([g for g in gs if g['att']>=200],key=lambda g:g['wins']/g['att'])[:int(sys.argv[2])]:
        f=rate(g,0.05,g['fast']);s=rate(g,0.005,g['slow'])
        print(f"{name(g['level']):>5} s{g['section']:<3} {g['area']&0xffffff:06x} {g['prob']:.4f} {g['att']:>7} {g['wins']/max(1,g['att']):.3f} {f:.3f} {s:.3f} {abs(f-s):.3f} {g['deaths']:.1f}")
