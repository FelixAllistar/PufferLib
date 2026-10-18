import bpy,json,sys,math
from pathlib import Path
from collections import defaultdict
args=sys.argv[sys.argv.index('--')+1:]
bpy.ops.wm.open_mainfile(filepath=args[0]);dep=bpy.context.evaluated_depsgraph_get();groups=defaultdict(list)
for o in bpy.data.objects:
 if o.type!='MESH':continue
 ev=o.evaluated_get(dep);me=ev.to_mesh();me.calc_loop_triangles()
 for t in me.loop_triangles:
  v=[o.matrix_world@me.vertices[i].co for i in t.vertices];n=(v[1]-v[0]).cross(v[2]-v[0]).normalized();d=n.dot(v[0])
  key=tuple(round(float(x),5) for x in n)+(round(float(d),5),)
  ax=max(range(3),key=lambda i:abs(n[i]));axes=[i for i in range(3) if i!=ax]
  pts=[(float(p[axes[0]]),float(p[axes[1]])) for p in v]
  groups[key].append((o.name,pts))
 ev.to_mesh_clear()
def cross(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
def area(p):return abs(sum(p[i][0]*p[(i+1)%len(p)][1]-p[(i+1)%len(p)][0]*p[i][1] for i in range(len(p))))*.5 if len(p)>2 else 0
def clip(subject,tri):
 ori=1 if cross(tri[0],tri[1],tri[2])>=0 else -1
 for a,b in zip(tri,tri[1:]+tri[:1]):
  old=subject;subject=[]
  if not old:break
  for p,q in zip(old,old[1:]+old[:1]):
   dp=ori*cross(a,b,p);dq=ori*cross(a,b,q);pin=dp>=-1e-10;qin=dq>=-1e-10
   if pin:subject.append(p)
   if pin!=qin:
    t=dp/(dp-dq);subject.append((p[0]+t*(q[0]-p[0]),p[1]+t*(q[1]-p[1])))
 return subject
hits=defaultdict(float)
for plane,ts in groups.items():
 for i,(na,a) in enumerate(ts):
  for nb,b in ts[i+1:]:
   if na==nb:continue
   if any(max(p[k] for p in a)<min(p[k] for p in b)-1e-8 or max(p[k] for p in b)<min(p[k] for p in a)-1e-8 for k in range(2)):continue
   ar=area(clip(a,b))
   if ar>1e-8:hits[tuple(sorted([na,nb]))]+=ar
out={'method':'Same-facing triangle surfaces grouped by rounded normal and plane; 1e-5 approximate plane tolerance. Projected intersection areas in m2; opposite-facing legitimate seated contacts excluded.','pairs':[{'parts':list(k),'summed_projected_overlap_m2':v} for k,v in sorted(hits.items(),key=lambda kv:-kv[1])]}
Path(args[1]).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
