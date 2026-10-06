"""Targeted exact finite-triangle distance for the index's borderline2mm gap."""
import bpy,json,hashlib,importlib.util,math
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path('/workspace/scratch/eac4961518d4/animation_rebuild');O=R/'grip_review_oct5/candidate_c1_review';p=R/'grip_review_oct5/candidate_c1/support_grip_index_release_c1.editable.blend';pin='17c1c13ffbea9d8fac459914eb2bbb1c6c336b785f9382450002f0ad6a53ac99';assert hashlib.sha256(p.read_bytes()).hexdigest()==pin;spec=importlib.util.spec_from_file_location('m','/workspace/shared/practical_ads_audit_recovery_algorithms.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);bpy.ops.wm.open_mainfile(filepath=str(p));bpy.context.scene.frame_set(0);bpy.context.view_layer.update();b=bpy.data.objects['SWAT_Wearer'];g=bpy.data.objects['Rifle 7'];b.data.calc_loop_triangles();g.data.calc_loop_triangles();T=np.array([q.vertices[:] for q in b.data.loop_triangles]);GT=np.array([q.vertices[:] for q in g.data.loop_triangles]);V=np.array(m.evaluated_positions(b,bpy.context.evaluated_depsgraph_get()));G=np.array(m.evaluated_positions(g,bpy.context.evaluated_depsgraph_get()));N=np.array([q.value for q in b.data.attributes['native_vertex_id'].data]);W=[{b.vertex_groups[q.group].name:q.weight for q in v.groups} for v in b.data.vertices];tags,tw=m.triangle_weight_tags(T,W);it=np.array([i for i,n in enumerate(tags) if n.startswith('mixamorig:LeftHandIndex')]);tree=BVHTree.FromPolygons([Vector(x) for x in G],GT,all_triangles=True);best=min(tree.find_nearest(Vector(V[i]))[3] for i in np.unique(T[it]));glo=G[GT].min(1);ghi=G[GT].max(1)
def clamp(s):return min(1.,max(0.,s))
def pointtri(p,t):
 a,b,c=t;u=b-a;v=c-a;w=p-a;uu=u@u;uv=u@v;vv=v@v;wu=w@u;wv=w@v;det=uu*vv-uv*uv
 if det>1e-25:
  s=(wu*vv-wv*uv)/det;z=(wv*uu-wu*uv)/det
  if s>=0 and z>=0 and s+z<=1:return a+s*u+z*v
 pts=[a+clamp((p-a)@(b-a)/max((b-a)@(b-a),1e-30))*(b-a),b+clamp((p-b)@(c-b)/max((c-b)@(c-b),1e-30))*(c-b),c+clamp((p-c)@(a-c)/max((a-c)@(a-c),1e-30))*(a-c)];return min(pts,key=lambda q:(q-p)@(q-p))
def edges(p,q,r,z):
 d=q-p;f=z-r;v=p-r;a=d@d;e=f@f;c=d@v;b=d@f;w=f@v
 if a<1e-25:return p,r+clamp(w/max(e,1e-30))*f
 if e<1e-25:return p+clamp(-c/a)*d,r
 den=a*e-b*b;s=clamp((b*w-c*e)/den) if den>1e-25 else 0.;t=(b*s+w)/e
 if t<0:t=0;s=clamp(-c/a)
 elif t>1:t=1;s=clamp((b-c)/a)
 return p+s*d,r+t*f
witness=None;tested=0
for i in it:
 a=V[T[i]];lo=a.min(0);hi=a.max(0);sep=np.maximum(np.maximum(glo-hi,lo-ghi),0);jj=np.flatnonzero(np.linalg.norm(sep,axis=1)<best+1e-7)
 for j in jj:
  z=G[GT[j]];pairs=[(q,pointtri(q,z)) for q in a]+[(pointtri(q,a),q) for q in z]
  for x,y in [(0,1),(1,2),(2,0)]:
   for u,v in [(0,1),(1,2),(2,0)]:pairs.append(edges(a[x],a[y],z[u],z[v]))
  q,r=min(pairs,key=lambda pr:np.linalg.norm(pr[0]-pr[1]));d=float(np.linalg.norm(q-r));tested+=1
  if d<=best:best=d;witness={'native_index_triangle':N[T[i]].tolist(),'index_segment':tags[i],'rifle_triangle':int(j),'index_point_world_m':q.tolist(),'rifle_point_world_m':r.tolist()}
cross=m.finite_crossings([Vector(v) for v in V],list(map(tuple,T)),list(map(int,it)),[Vector(v) for v in G],list(map(tuple,GT)),list(range(len(GT))));out={'source':str(p),'sha256':pin,'method':'Float64 vertex/triangle and all edge/edge finite distance tests on every potentially closer triangle pair, culled only by conservative AABB lower distance; exact finite crossing tested separately. Applies to native skin triangles dominated by LeftHandIndex weights, including all three phalangeal tags.','tested_triangle_pairs':tested,'minimum_finite_triangle_surface_gap_mm':best*1000,'index_rifle_exact_crossing_pairs':len(cross),'closest_pair':witness,'margin_over_2mm':best*1000-2};assert hashlib.sha256(p.read_bytes()).hexdigest()==pin;(O/'index_surface_gap_exact.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2),flush=True)
