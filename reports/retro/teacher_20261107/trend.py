import re,sys
s=open(sys.argv[1],errors='ignore').read().replace('\r','\n')
frames=s.split('PufferLib 5.0')
keys=['perf','clears','game_wins','maze_loops','deaths','distance','progress_pixels','novel_areas','episode_return','episode_length','area_transitions','entropy','warps']
rows=[]
for f in frames[1:]:
    m=re.search(r'Steps\s+([\d.]+)([KMB])',f)
    if not m: continue
    st=float(m.group(1))*{'K':1e3,'M':1e6,'B':1e9}[m.group(2)]
    d={'steps':st}
    for k in keys:
        mm=re.search(r'\b'+k+r'\s+(-?[\d.]+)',f)
        if mm: d[k]=float(mm.group(1))
    rows.append(d)
print(len(rows),'frames')
B=50e6
import collections
b=collections.defaultdict(list)
for r in rows: b[int(r['steps']//B)].append(r)
print('steps(M) '+' '.join(f'{k[:9]:>9}' for k in keys))
for i in sorted(b):
    rs=b[i]
    print(f'{i*50:>4}-{i*50+50:<4}'+' '.join(f'{sum(r.get(k,0) for r in rs)/len(rs):9.3f}' for k in keys))
