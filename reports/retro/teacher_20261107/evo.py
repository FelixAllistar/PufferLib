import sys,glob,collections
sys.argv=[sys.argv[0]]+sys.argv[1:]
exec(open('/tmp/groups.py').read().split("gs=load(sys.argv[1])")[0])
files=sorted(glob.glob('*.archive.gz'))
pick=[files[0],files[len(files)//2],files[-1]] if len(files)>=3 else files
snaps=[(int(f.split('.')[0])/1e6,{(g['level'],g['area'],g['section']):g for g in load(f)}) for f in pick]
print('checkpoints (M steps):',[round(s[0]) for s in snaps])
name=lambda l:f"{l//4+1}-{l%4+1}"
# per-level windowed win rate
print('level   '+'  '.join(f'win<= {round(s[0])}M' if i==0 else f'win {round(snaps[i-1][0])}-{round(s[0])}M' for i,s in enumerate(snaps))+'   prob@each')
for l in range(32):
    out=[];pr=[]
    for i,(st,gs) in enumerate(snaps):
        a=sum(g['att'] for k,g in gs.items() if k[0]==l);w=sum(g['wins'] for k,g in gs.items() if k[0]==l)
        if i:
            pa=sum(g['att'] for k,g in snaps[i-1][1].items() if k[0]==l);pw=sum(g['wins'] for k,g in snaps[i-1][1].items() if k[0]==l)
            a-=pa;w-=pw
        out.append(f'{w/max(1,a):.3f} ({a:>5})');pr.append(f"{sum(g['prob'] for k,g in gs.items() if k[0]==l):.3f}")
    print(f'{name(l):>5}  '+'   '.join(out)+'   '+' '.join(pr))
for l in (15,27,31,2,18,28,30):
    print('--',name(l),'per section: windowed win rate (attempts) per checkpoint | prob, deathmass at last')
    keys=sorted({k for _,gs in snaps for k in gs if k[0]==l},key=lambda k:(k[1],k[2]))
    for k in keys:
        out=[]
        for i,(st,gs) in enumerate(snaps):
            g=gs.get(k);a=g['att'] if g else 0;w=g['wins'] if g else 0
            if i and snaps[i-1][1].get(k):a-=snaps[i-1][1][k]['att'];w-=snaps[i-1][1][k]['wins']
            out.append(f'{w/a:.2f}({a:>4})' if a else '  - (   0)')
        g=snaps[-1][1].get(k)
        print(f"  area {k[1]&0xffffff:06x} s{k[2]:<2} "+' '.join(out)+f"  p={g['prob']:.4f} dm={g['deaths']:.0f}")
