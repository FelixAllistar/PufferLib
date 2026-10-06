"""Read-only differential skin-material and actual-surface clearance checks, F/A to B."""
import bpy,json,hashlib,collections,importlib.util,math
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path('/workspace/scratch/eac4961518d4/animation_rebuild');O=R/'grip_review_oct5/candidate_c1_review';P='mixamorig:';spec=importlib.util.spec_from_file_location('m','/workspace/shared/practical_ads_audit_recovery_algorithms.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
ref={};out={}
def mesh(o):o.data.calc_loop_triangles();return np.array([q.vertices[:] for q in o.data.loop_triangles])
def pos(o):return np.array(m.evaluated_positions(o,bpy.context.evaluated_depsgraph_get()))
def cross(V,T,I,X,U,J,same=False):return m.finite_crossings([Vector(p) for p in V],list(map(tuple,T)),list(map(int,I)),[Vector(p) for p in X],list(map(tuple,U)),list(map(int,J)),same=same)
def qstats(x):return {'min':float(np.min(x)),'p01':float(np.quantile(x,.01)),'p05':float(np.quantile(x,.05)),'median':float(np.median(x)),'max':float(np.max(x))}
from gate_config import INPUTS,verify_loaded
for label,rel,pin in INPUTS:
 src=R/rel;assert hashlib.sha256(src.read_bytes()).hexdigest()==pin;bpy.ops.wm.open_mainfile(filepath=str(src));verify_loaded(bpy,label);bpy.context.scene.frame_set(0);bpy.context.view_layer.update();r=bpy.data.objects['SWAT_Mixamo_Rig'];b=bpy.data.objects['SWAT_Wearer'];V=pos(b);T=mesh(b);RV=np.array([b.matrix_world@v.co for v in b.data.vertices]);W=[{b.vertex_groups[q.group].name:q.weight for q in v.groups} for v in b.data.vertices];native=np.array([q.value for q in b.data.attributes['native_vertex_id'].data]);lookup={int(i):j for j,i in enumerate(native)};C,groups=m.components(T,len(V));tags,tw=m.triangle_weight_tags(T,W);nativec={k:int(min(native[ids])) for k,ids in groups.items()};handc=C[lookup[7923]];HTi=np.array([i for i,t in enumerate(T) if C[int(t[0])]==handc]);armti=np.array([i for i,tag in enumerate(tags) if tag in [P+'LeftArm',P+'LeftForeArm']]);armc={C[int(T[i,0])] for i in armti};wholearm=np.array([i for i,t in enumerate(T) if C[int(t[0])] in armc]);otherbody=np.array([i for i,t in enumerate(T) if C[int(t[0])] not in armc|{handc}]);notglove=np.array([i for i,t in enumerate(T) if C[int(t[0])]!=handc]);ids=np.array(groups[handc]);nativeTri=[tuple(sorted(map(int,native[t]))) for t in T]
 row={'source':str(src),'sha256':pin,'action':r.animation_data.action.name,'left_hand_native_component':nativec[handc],'left_arm_native_components':[nativec[i] for i in sorted(armc)],'material':{},'clearances':{}}
 pb={x.name:np.array(r.matrix_world@x.matrix) for x in r.pose.bones};rb={x.name:np.array(r.matrix_world@x.matrix_local) for x in r.data.bones};lbs,J=m.normalized_all_influence_lbs(RV,W,pb,rb);row['all_body_normalized_LBS_max_blender_error_mm']=float(np.linalg.norm(lbs-V,axis=1).max()*1000);norm=m.material_normal_transport(RV,V,T,J);area=norm['area_ratio'];dots=norm['normal_alignment'];sv=np.linalg.svd(J,compute_uv=False)[:,-1];det=np.linalg.det(J)
 groupsel={'left_hand':HTi,'left_arm_skin':armti};
 for name,ti in groupsel.items():
  vertices=np.unique(T[ti]);rrow={'triangle_count':len(ti),'skin_area_over_rest':qstats(area[ti]),'geometric_normal_vs_transported_rest_normal':qstats(dots[ti]),'minimum_normalized_skin_J_singular_value':float(sv[vertices].min()),'minimum_skin_J_determinant':float(det[vertices].min()),'skin_J_nonpositive_determinant_vertices':int(sum(det[vertices]<=0)),'face_normal_alignment_below0':int(sum(dots[ti]<0)),'face_normal_alignment_below0_5':int(sum(dots[ti]<.5)),'face_area_below_10pct_rest':int(sum(area[ti]<.1)),'worst_normal_faces':[{'native_vertices':list(nativeTri[int(i)]),'dominant_weight':tags[int(i)],'alignment':float(dots[i]),'area_ratio':float(area[i])} for i in ti[np.argsort(dots[ti])[:10]]]}
  if label=='A':ref[name]={'area':area[ti].copy(),'dots':dots[ti].copy()}
  else:rrow['area_ratio_candidate_over_A']=qstats(area[ti]/ref[name]['area']);rrow['new_negative_normal_faces_vs_A']=int(sum((dots[ti]<0)&(ref[name]['dots']>=0)))
  row['material'][name]=rrow
 # Inspect all finger/glove pairs, including same digit and palm, not only cross digits.
 adjacency=collections.defaultdict(set)
 for t in T[HTi]:
  for i in t:adjacency[int(i)].update(map(int,t))
 near={}
 for i in ids:
  seen={int(i)};front=seen.copy()
  for _ in range(3):front=set.union(*(adjacency[j] for j in front))-seen if front else set();seen|=front
  near[int(i)]=seen
 selfhit=cross(V,T,HTi,V,T,HTi,True);nonlocalhit={k:q for k,q in selfhit.items() if not any(set(T[k[1]])&near[int(v)] for v in T[k[0]])};selfpairs={tuple(sorted((nativeTri[i],nativeTri[j]))) for i,j in selfhit};nonlocalpairs={tuple(sorted((nativeTri[i],nativeTri[j]))) for i,j in nonlocalhit};row['material']['left_hand']['exact_self_triangle_pairs_total']=len(selfhit);row['material']['left_hand']['nonlocal_self_triangle_pairs']=len(nonlocalhit);row['material']['left_hand']['nonlocal_self_details']=[{'native_triangles':[nativeTri[i],nativeTri[j]],'dominant_regions':[tags[i],tags[j]],'point_world_m':q['point']} for (i,j),q in nonlocalhit.items()]
 if label=='A':ref['selfpairs']=selfpairs;ref['nonlocalpairs']=nonlocalpairs
 else:row['material']['left_hand']['new_exact_self_pairs_vs_A']=len(selfpairs-ref['selfpairs']);row['material']['left_hand']['new_nonlocal_self_pairs_vs_A']=len(nonlocalpairs-ref['nonlocalpairs'])
 def categorized(hits,otherT=None,otherTags=None):
  counts=collections.Counter();detail=[]
  for (i,j),q in hits.items():
   key=f'{nativec[C[int(T[j,0])]]}:{tags[j]}' if otherT is None else otherTags[j];counts[key]+=1
   if len(detail)<100:detail.append({'triangles':[i,j],'target':key,'point_world_m':q['point']})
  return {'exact_triangle_pairs':len(hits),'by_target':dict(counts),'witnesses_first100':detail}
 for name,sourceI,targetI in [('left_glove_vs_other_wearer',HTi,notglove),('left_arm_components_vs_other_body',wholearm,otherbody),('left_arm_dominant_skin_vs_other_body',armti,otherbody)]:
  hits=cross(V,T,sourceI,V,T,targetI,True);row['clearances'][name]=categorized(hits)
  if label=='A':ref[name]={tuple(sorted((nativeTri[i],nativeTri[j]))) for i,j in hits}
  else:row['clearances'][name]['new_native_triangle_pairs_vs_A']=len({tuple(sorted((nativeTri[i],nativeTri[j]))) for i,j in hits}-ref[name])
 g=bpy.data.objects['Rifle 7'];G=pos(g);GT=mesh(g);leftnonglove=np.array([i for i,tag in enumerate(tags) if tag in [P+'LeftArm',P+'LeftForeArm',P+'LeftShoulder']]);hits=cross(G,GT,np.arange(len(GT)),V,T,leftnonglove);row['clearances']['rifle_vs_left_nonhand_skin']=categorized(hits)
 for name in ['SWAT_ElbowCap_L','SWAT_ElbowCap_R','SWAT_Headset_L','SWAT_Headset_R','Rebuilt spare magazine sleeve','Removed magazine','Fresh magazine']:
  o=bpy.data.objects.get(name)
  if not o:continue
  X=pos(o);U=mesh(o)
  for target,targetI in [('left_glove',HTi),('left_arm',wholearm)]:
   hits=cross(X,U,np.arange(len(U)),V,T,targetI);row['clearances'][name+'_vs_'+target]=categorized(hits)
 print('MATERIAL_CLEARANCE',label,json.dumps({'material':row['material'],'clearance_counts':{k:(v['exact_triangle_pairs'],v.get('new_native_triangle_pairs_vs_A')) for k,v in row['clearances'].items()}}),flush=True)
 assert hashlib.sha256(src.read_bytes()).hexdigest()==pin;out[label]=row;(O/'material_clearance_comparison.json').write_text(json.dumps(out,indent=2)+'\n')
