import bpy,json,sys,math,hashlib
from pathlib import Path
from collections import defaultdict
D=Path(__file__).parent;src=D/'packed_only/asset.blend'
bpy.ops.wm.open_mainfile(filepath=str(src));dep=bpy.context.evaluated_depsgraph_get();groups=defaultdict(list)
for o in bpy.data.objects:
 if o.type!='MESH':continue
 ev=o.evaluated_get(dep);me=ev.to_mesh();me.calc_loop_triangles();vs=[o.matrix_world@v.co for v in me.vertices]
 for ti,t in enumerate(me.loop_triangles):
  pts=[vs[k] for k in t.vertices];cross=(pts[1]-pts[0]).cross(pts[2]-pts[0])
  if cross.length<1e-12:continue
  norm=cross.normalized();dist=norm.dot(pts[0]);key=tuple(round(float(v)*1e6) for v in [*norm,dist]);axis=max(range(3),key=lambda i:abs(norm[i]));xy=[tuple(float(p[i]) for i in range(3) if i!=axis) for p in pts]
  if (xy[1][0]-xy[0][0])*(xy[2][1]-xy[0][1])-(xy[1][1]-xy[0][1])*(xy[2][0]-xy[0][0])<0:xy.reverse()
  groups[key].append((o.name,ti,xy,[min(p[0] for p in xy),min(p[1] for p in xy),max(p[0] for p in xy),max(p[1] for p in xy)],abs(norm[axis])))
 ev.to_mesh_clear()
def cross(a,b):return a[0]*b[1]-a[1]*b[0]
def sub(a,b):return a[0]-b[0],a[1]-b[1]
def overlap(A,B):
 poly=A[:]
 for i in range(3):
  a,b=B[i],B[(i+1)%3];edge=sub(b,a);out=[]
  for j in range(len(poly)):
   p,q=poly[j],poly[(j+1)%len(poly)];dp=cross(edge,sub(p,a));dq=cross(edge,sub(q,a))
   if dp>=0:out.append(p)
   if (dp>=0)!=(dq>=0):
    t=dp/(dp-dq);out.append((p[0]+t*(q[0]-p[0]),p[1]+t*(q[1]-p[1])))
  poly=out
  if not poly:return 0
 return abs(sum(cross(poly[i],poly[(i+1)%len(poly)]) for i in range(len(poly))))/2
pairs=defaultdict(lambda:{'triangle_pairs':0,'area_m2':0,'planes':set()})
for key,tri in groups.items():
 for i,a in enumerate(tri):
  for b in tri[i+1:]:
   if a[0]==b[0]:continue
   if min(a[3][2],b[3][2])-max(a[3][0],b[3][0])<=1e-8 or min(a[3][3],b[3][3])-max(a[3][1],b[3][1])<=1e-8:continue
   area=overlap(a[2],b[2])/a[4]
   if area>1e-10:
    p=pairs[tuple(sorted([a[0],b[0]]))];p['triangle_pairs']+=1;p['area_m2']+=area;p['planes'].add(key)
r={'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'plane_quantization':1e-6,'method':'Same-facing triangle planes grouped within 1e-6 normal/distance rounding, cross-object triangle intersection by 2D convex clipping. Exposed-versus-buried classification requires manual review; not a global intersection absence proof.','pairs':[{'parts':list(k),'triangle_pairs':v['triangle_pairs'],'overlap_area_m2':v['area_m2'],'planes':[[x/1e6 for x in p] for p in v['planes']]} for k,v in sorted(pairs.items())]};(D/'coplanar_scan.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
