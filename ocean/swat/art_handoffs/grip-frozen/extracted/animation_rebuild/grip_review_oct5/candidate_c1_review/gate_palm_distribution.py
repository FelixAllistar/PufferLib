import bpy,json,math,importlib.util,collections
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path('/workspace/scratch/eac4961518d4/animation_rebuild');O=R/'grip_review_oct5/candidate_c1_review';spec=importlib.util.spec_from_file_location('m','/workspace/shared/practical_ads_audit_recovery_algorithms.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

from gate_config import INPUTS,verify_loaded,sha
def evaluate(LABEL,SOURCE_PATH,PIN):
 assert sha(SOURCE_PATH)==PIN
 bpy.ops.wm.open_mainfile(filepath=str(SOURCE_PATH));verify_loaded(bpy,LABEL);bpy.context.scene.frame_set(0);bpy.context.view_layer.update();b=bpy.data.objects['SWAT_Wearer'];lookup={int(q.value):i for i,q in enumerate(b.data.attributes['native_vertex_id'].data)};g=bpy.data.objects['Rifle 7'];r=bpy.data.objects['SWAT_Mixamo_Rig'];b.data.calc_loop_triangles();g.data.calc_loop_triangles();T=np.array([q.vertices[:] for q in b.data.loop_triangles]);GT=np.array([q.vertices[:] for q in g.data.loop_triangles]);RV=np.array([b.matrix_world@v.co for v in b.data.vertices]);V=np.array(m.evaluated_positions(b,bpy.context.evaluated_depsgraph_get()));G=np.array(m.evaluated_positions(g,bpy.context.evaluated_depsgraph_get()));GR=np.array([v.co[:] for v in g.data.vertices]);A=np.eye(4);A[:,:3]=np.linalg.lstsq(np.c_[GR,np.ones(len(GR))],G,rcond=None)[0];inv=np.linalg.inv(A);W=[{b.vertex_groups[q.group].name:q.weight for q in v.groups} for v in b.data.vertices];tags,tw=m.triangle_weight_tags(T,W);C,groups=m.components(T,len(V));gun=BVHTree.FromPolygons([Vector(p) for p in G],GT,all_triangles=True);out={}
 for side,seed in [('Left',7923),('Right',9145)]:
  handname='mixamorig:'+side+'Hand';bone=r.data.bones[handname];H=np.array(r.matrix_world@bone.matrix_local);ri=np.linalg.inv(H);handlocal=(np.c_[RV,np.ones(len(RV))]@ri.T)[:,:3];idx=[j for j,t in enumerate(T) if C[int(t[0])]==C[lookup[seed]] and tags[j]==handname and tw[j][handname]>.6];bands=collections.defaultdict(list);renderrows=[]
  for j in idx:
   t=T[j];rest=RV[t];rn=np.cross(rest[1]-rest[0],rest[2]-rest[0]);rn/=np.linalg.norm(rn)
   if rn@H[:3,2]<=.3:continue
   tv=V[t];n=np.cross(tv[1]-tv[0],tv[2]-tv[0]);area=np.linalg.norm(n)/2;n/=np.linalg.norm(n);frac=float(handlocal[t,1].mean()/bone.length);label='below_wrist' if frac<0 else 'proximal_0_25' if frac<.25 else 'central_25_50' if frac<.5 else 'central_50_75' if frac<.75 else 'distal_75_100' if frac<1 else 'beyond_100';count=max(2,min(16,math.ceil(np.linalg.norm(tv[[1,2,0]]-tv,axis=1).max()/.002)));N=(count+1)*(count+2)//2
   for u in range(count+1):
    for w in range(count+1-u):
     point=tv[0]+(tv[1]-tv[0])*u/count+(tv[2]-tv[0])*w/count;hit,normal,k,gap=gun.find_nearest(Vector(point));row={'gap_mm':gap*1000,'area_mm2':area*1e6/N,'opposed':float(n@np.array(normal))<-.1,'point_rifle_m':(np.r_[point,1]@inv)[:3].tolist(),'rifle_point_m':(np.r_[np.array(hit),1]@inv)[:3].tolist(),'source_native_triangle':j};bands[label].append(row);bands['all'].append(row)
  result={}
  for label,rows in bands.items():
   gaps=np.array([x['gap_mm'] for x in rows]);areas=np.array([x['area_mm2'] for x in rows]);opp=np.array([x['opposed'] for x in rows]);order=np.argsort(gaps);cum=np.cumsum(areas[order])/sum(areas);pcts={str(q):float(gaps[order][np.searchsorted(cum,q/100)]) for q in [10,25,50,75,90]};result[label]={'skin_area_mm2':sum(areas),'opposed_skin_area_within_mm':{str(n):float(sum(areas[(gaps<n)&opp])) for n in [1,2,3,5,10,15,20,30]},'area_weighted_nearest_gap_percentiles_mm':pcts,'skin_center_rifle_mm':(np.average(np.array([q['point_rifle_m'] for q in rows]),axis=0,weights=areas)*1000).tolist()}
  out[side]={'method':'Actual volar Palm-tagged skin, Hand weight>0.6 and native normal dot Hand+Z>0.3; divided by rest material position along wrist-to-metacarpal Hand segment. 2mm max-edge target, up to16 divisions/triangle. Gap percentiles are unsigned nearest-surface distances including all normals; area thresholds count opposed normals only. Broad all region includes below-wrist glove material, so use central/distal bands for support interpretation.','bands':result,'samples':bands['all']};print(side,json.dumps(result,indent=2),flush=True)
 (O/(LABEL+'_palm_support_distribution.json')).write_text(json.dumps(out,indent=2)+'\n')
 assert sha(SOURCE_PATH)==PIN
for source in INPUTS:evaluate(*source)
