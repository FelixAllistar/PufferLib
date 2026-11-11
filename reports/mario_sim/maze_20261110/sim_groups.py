import struct,sys,glob,collections
def load(p):
    b=open(p,'rb').read()
    magic,ver,count,gs=struct.unpack_from('<IIII',b,0)
    assert magic==0x54434831 and gs==96 and len(b)==40+count*96,(hex(magic),gs,len(b))
    out=[]
    for k in range(count):
        key,area,level,section,elig,res,fast,slow,att,wins,deaths,dstamp,last,prob=struct.unpack_from('<QIiiii4xddQQdQQd',b,40+k*96)
        out.append(dict(area=area,level=level,section=section,att=att,wins=wins,deaths=deaths,prob=prob,fast=fast,slow=slow))
    return out
files=sorted(glob.glob('*.teacher'))
pick=[files[0],files[len(files)//2],files[-1]]
snaps=[(int(f.split('.')[0])/1e9,{(g['level'],g['area'],g['section']):g for g in load(f)}) for f in pick]
name=lambda l:f"{l//4+1}-{l%4+1}"
last=snaps[-1][1]
print('checkpoints (B steps):',[round(s[0],2) for s in snaps],' groups',len(last),' attempts',sum(g['att'] for g in last.values()),' sumprob %.3f'%sum(g['prob'] for g in last.values()))
print('level: win rate per window (attempts) | budget share at each checkpoint')
for l in range(32):
    o=[];pr=[]
    for i,(st,gs) in enumerate(snaps):
        a=sum(g['att'] for k,g in gs.items() if k[0]==l);w=sum(g['wins'] for k,g in gs.items() if k[0]==l)
        if i:
            a-=sum(g['att'] for k,g in snaps[i-1][1].items() if k[0]==l);w-=sum(g['wins'] for k,g in snaps[i-1][1].items() if k[0]==l)
        o.append(f'{w/max(1,a):.2f}({a:>7})');pr.append(f"{sum(g['prob'] for k,g in gs.items() if k[0]==l):.3f}")
    print(f'{name(l):>4} '+' '.join(o)+' | '+' '.join(pr))
for l in (15,27,31):
    print('--',name(l),'sections: win rate per window (attempts) | prob, death mass at last')
    for k in sorted({k for _,gs in snaps for k in gs if k[0]==l},key=lambda k:(k[1],k[2])):
        o=[]
        for i,(st,gs) in enumerate(snaps):
            g=gs.get(k);a=g['att'];w=g['wins']
            if i:a-=snaps[i-1][1][k]['att'];w-=snaps[i-1][1][k]['wins']
            o.append(f'{w/a:.2f}({a:>6})' if a else ' -  (     0)')
        g=last[k];print(f"  {k[1]&0xffff:04x} s{k[2]:<2} "+' '.join(o)+f"  p={g['prob']:.4f} dm={g['deaths']:.0f}")
