"""One deterministic one-dimensional release of C's Index1 flexion.
Prepared code must not run without the parent's verified C-backup receipt.
C and every other local pose channel remain immutable; save only one chosen C1.
"""
import bpy,sys,json,math,hashlib,importlib.util,collections
from pathlib import Path
import numpy as np
from mathutils import Vector,Quaternion
from mathutils.bvhtree import BVHTree
assert '--backup-verified' in sys.argv,'Preparation only until parent verifies C backup'
O=Path(__file__).resolve().parent;ROOT=O.parents[1];P='mixamorig:';C=O.parent/'candidate_c/support_grip_surface_c.editable.blend';CPIN='f1b39d78b32da500f9fd4370d942d364c49a1d9d788ac1122282b4275e371dcb';assert hashlib.sha256(C.read_bytes()).hexdigest()==CPIN
A=json.loads((O.parent/'candidate_c/CANDIDATE_C_AUTHORING.json').read_text());cx=A['parameters']['Index1_native_local_x_deg'];spread=A['parameters']['Index1_native_local_z_spread_deg'];bracket=[cx,3.,1.5,0.]
spec=importlib.util.spec_from_file_location('m','/workspace/shared/practical_ads_audit_recovery_algorithms.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
bpy.ops.wm.open_mainfile(filepath=str(C));s=bpy.context.scene;r=bpy.data.objects['SWAT_Mixamo_Rig'];baseact=r.animation_data.action;s.frame_set(0);bpy.context.view_layer.update();target=P+'LeftHandIndex1';b=r.pose.bones[target];baseQ=b.rotation_quaternion.copy();BASE={q.name:{'location':q.location.copy(),'rotation_quaternion':q.rotation_quaternion.copy(),'scale':q.scale.copy(),'matrix_basis':q.matrix_basis.copy(),'matrix':q.matrix.copy()}for q in r.pose.bones};r.animation_data.action=None
body=bpy.data.objects['SWAT_Wearer'];gun=bpy.data.objects['Rifle 7'];body.data.calc_loop_triangles();gun.data.calc_loop_triangles();T=np.array([q.vertices[:]for q in body.data.loop_triangles]);GT=np.array([q.vertices[:]for q in gun.data.loop_triangles]);native=np.array([q.value for q in body.data.attributes['native_vertex_id'].data]);lookup={int(i):j for j,i in enumerate(native)};W=[{body.vertex_groups[g.group].name:g.weight for g in v.groups}for v in body.data.vertices];RV=np.array([body.matrix_world@v.co for v in body.data.vertices]);tags,tw=m.triangle_weight_tags(T,W);comp,groups=m.components(T,len(RV));hcomp=comp[lookup[7923]];hti=np.array([j for j,t in enumerate(T)if comp[int(t[0])]==hcomp]);HT=T[hti];iti=np.array([j for j in hti if tags[j].startswith(P+'LeftHandIndex')]);targettri=next(j for j,t in enumerate(T)if set(map(int,native[t]))=={8513,8515,8516});RT={q.name:np.array(r.matrix_world@q.matrix_local)for q in r.data.bones};Fface=json.loads((O.parent/'candidate_c_review/low_area_face_comparison.json').read_text())['A'][0];assert set(Fface['native_vertices'])=={8513,8515,8516}
def positions(o):return np.array(m.evaluated_positions(o,bpy.context.evaluated_depsgraph_get()))
def tree(V,T):return BVHTree.FromPolygons([Vector(p)for p in V],T,all_triangles=True)
V0=positions(body);G=positions(gun);gtree=tree(G,GT);G0=G.copy();indexids=np.unique(T[iti]);bounds=(V0[indexids].min(0)-.010,V0[indexids].max(0)+.010);GS=m.dense_surface_samples(G,GT,bounds);adj=collections.defaultdict(set)
for t in HT:
 for i in t:adj[int(i)].update(map(int,t))
near={}
for i in groups[hcomp]:
 seen={int(i)};front=seen.copy()
 for _ in range(3):front=set.union(*(adj[j]for j in front))-seen if front else set();seen|=front
 near[int(i)]=seen
RH=np.array(r.matrix_world@r.data.bones[P+'LeftHand'].matrix_local);handlocal=(np.c_[RV,np.ones(len(RV))]@np.linalg.inv(RH).T)[:,:3];palmtri=[]
for j in hti:
 if tags[j]!=P+'LeftHand' or tw[j][P+'LeftHand']<=.6:continue
 tv=RV[T[j]];rn=np.cross(tv[1]-tv[0],tv[2]-tv[0]);rn/=max(np.linalg.norm(rn),1e-20)
 if rn@RH[:3,2]<=.3:continue
 frac=handlocal[T[j],1].mean()/r.data.bones[P+'LeftHand'].length
 if .25<=frac<.75:palmtri.append((j,'central_25_50'if frac<.5 else'central_50_75'))
def rotvecq(x,z):
 v=Vector((x,0,z));return Quaternion(v.normalized(),math.radians(v.length))if v.length else Quaternion()
Q0=rotvecq(cx,spread)
def apply(x):
 b.rotation_quaternion=baseQ if x==cx else baseQ@(Q0.inverted()@rotvecq(x,spread));bpy.context.view_layer.update()
def crosses(V,ti,X,tt,tj,same=False):return m.finite_crossings([Vector(p)for p in V],list(map(tuple,T)),list(map(int,ti)),[Vector(p)for p in X],list(map(tuple,tt)),list(map(int,tj)),same=same)
def distal_digit_gap(V):
 index_faces=[j for j in hti if tags[j] in [P+'LeftHandIndex2',P+'LeftHandIndex3']]
 middle_faces=[j for j in hti if tags[j] in [P+'LeftHandMiddle2',P+'LeftHandMiddle3']]
 minimum=1e9
 for aa,bb in [(index_faces,middle_faces),(middle_faces,index_faces)]:
  other=tree(V,T[bb]);samples=m.dense_surface_samples(V,T[aa],pitch=.0015,dedup=.001)
  for point in samples:minimum=min(minimum,other.find_nearest(Vector(point))[3]*1000)
 return minimum

def palm_measure(V):
 out={}
 for band in ['central_25_50','central_50_75']:
  gaps=[];areas=[];opposed=[]
  for j,label in palmtri:
   if label!=band:continue
   tv=V[T[j]];n=np.cross(tv[1]-tv[0],tv[2]-tv[0]);area=np.linalg.norm(n)/2;n/=max(2*area,1e-20);num=max(2,min(16,math.ceil(np.linalg.norm(tv[[1,2,0]]-tv,axis=1).max()/.002)));N=(num+1)*(num+2)//2
   for u in range(num+1):
    for w in range(num+1-u):
     p=tv[0]+u/num*(tv[1]-tv[0])+w/num*(tv[2]-tv[0]);hit,normal,k,d=gtree.find_nearest(Vector(p));gaps.append(d*1000);areas.append(area*1e6/N);opposed.append(float(n@np.array(normal))<-.1)
  gap=np.array(gaps);area=np.array(areas);opp=np.array(opposed);order=np.argsort(gap);median=float(gap[order][np.searchsorted(np.cumsum(area[order])/sum(area),.5)]);out[band]={'median_gap_mm':median,'opposed_area_within2mm_mm2':float(sum(area[(gap<2)&opp])),'opposed_area_within5mm_mm2':float(sum(area[(gap<5)&opp]))}
 return out
rows=[]
for x in bracket:
 apply(x);V=positions(body);PB={q.name:np.array(r.matrix_world@q.matrix)for q in r.pose.bones};_,J=m.normalized_all_influence_lbs(RV,W,PB,RT);material=m.material_normal_transport(RV,V,T,J);tv=V[T[targettri]];area=np.linalg.norm(np.cross(tv[1]-tv[0],tv[2]-tv[0]))/2*1e6;edges=np.linalg.norm(tv[[1,2,0]]-tv,axis=1)*1000;VV,TT,_=m.analysis_only_wrist_cap(V,HT);inside=m.hand_inside_samples(tree(VV,TT),GS);indexinside=[q for q in inside if q['nearest_hand_or_cap_triangle']<len(hti)and tags[hti[q['nearest_hand_or_cap_triangle']]].startswith(P+'LeftHandIndex')];selfhit=crosses(V,hti,V,T,hti,True);nonlocalhit={str(k):q for k,q in selfhit.items()if not any(set(T[k[1]])&near[int(v)]for v in T[k[0]])};weaponhit=crosses(V,iti,G,GT,range(len(GT)));indexcontact=m.opposed_contact_patch(V,T[iti],['Index'for j in iti],[0. for j in iti],np.eye(4),gtree)['Index'];palm=palm_measure(V);unchanged_local=max(max(abs(q.matrix_basis[i][j]-BASE[q.name]['matrix_basis'][i][j])for i in range(4)for j in range(4))for q in r.pose.bones if q.name!=target);assert unchanged_local==0.;fixedworld=max(max(abs(r.pose.bones[n].matrix[i][j]-BASE[n]['matrix'][i][j])for i in range(4)for j in range(4))for n in BASE if not n.startswith(P+'LeftHandIndex'));assert fixedworld==0.
 row={'index1_added_native_local_x_deg':x,'index1_added_native_local_z_spread_deg':spread,'release_from_C_deg':cx-x,'target_native_triangle':[8513,8515,8516],'triangle_area_mm2':float(area),'triangle_area_over_F':float(area/Fface['posed_area_mm2']),'triangle_area_over_rest':float(material['area_ratio'][targettri]),'triangle_altitude_to_longest_edge_mm':float(2*area/max(edges)),'triangle_normal_transport_alignment':float(material['normal_alignment'][targettri]),'left_glove_faces_below_10pct_rest':int(sum(material['area_ratio'][hti]<.1)),'left_glove_negative_normal_faces':int(sum(material['normal_alignment'][hti]<0)),'index_rifle_exact_triangle_pairs':len(weaponhit),'dense_local_rifle_sample_count':len(GS),'maximum_local_index_parity_confirmed_depth_mm':max([q['depth_mm']for q in indexinside],default=0.),'local_index_samples_deeper_than1mm':sum(q['depth_mm']>1 for q in indexinside),'deepest_index_witnesses':sorted(indexinside,key=lambda q:-q['depth_mm'])[:5],'nonlocal_glove_self_triangle_pairs':len(nonlocalhit),'bidirectional_dense_index23_middle23_minimum_gap_mm':distal_digit_gap(V),'nonlocal_self_witnesses':list(nonlocalhit.values()),'index_opposed_contact':indexcontact,'central_palm':palm,'unchanged_other_local_matrix_max_error':unchanged_local,'unchanged_hand_arm_gear_other_digits_world_matrix_max_error':fixedworld,'index1_rotation_quaternion_wxyz':list(b.rotation_quaternion),'target_triangle_evaluated_world_vertices_m':tv.tolist()};rows.append(row);(O/'BRACKET_EVIDENCE.json').write_text(json.dumps({'source_C_sha256':CPIN,'F_target_face':Fface,'method':'One deterministic native-local Index1 flexion bracket; all other local channels are fixed. Local rifle samples use <=1.5mm triangle edges and 1mm coordinate deduplication, three-ray parity; whole candidate still requires independent final gate.','bracket':rows},indent=2)+'\n');print('BRACKET',x,'area',area,'normal',row['triangle_normal_transport_alignment'],'index_depth',row['maximum_local_index_parity_confirmed_depth_mm'],'self',len(nonlocalhit),'palm',palm,flush=True)
base=rows[0]
def clear(z):return z['nonlocal_glove_self_triangle_pairs']==0 and z['local_index_samples_deeper_than1mm']==0 and z['maximum_local_index_parity_confirmed_depth_mm']<=max(.80,base['maximum_local_index_parity_confirmed_depth_mm']+.10)and z['left_glove_negative_normal_faces']<=base['left_glove_negative_normal_faces']and z['triangle_normal_transport_alignment']>.5 and z['central_palm']['central_50_75']['opposed_area_within2mm_mm2']>=base['central_palm']['central_50_75']['opposed_area_within2mm_mm2']*.9 and z['central_palm']['central_50_75']['median_gap_mm']<=base['central_palm']['central_50_75']['median_gap_mm']+.30
admissible=[z for z in rows[1:]if clear(z)and z['triangle_area_over_rest']>=.10]
if not admissible:
 (O/'CONSTRAINT_FAILURE.json').write_text(json.dumps({'status':'No bracket point restores crease without exceeding bounded surface gates','bracket_file':'BRACKET_EVIDENCE.json'},indent=2)+'\n');raise RuntimeError('No admissible C1; no derivative saved')
nearF=[z for z in admissible if z['triangle_area_over_F']>=.90];chosen=max(nearF,key=lambda z:z['index1_added_native_local_x_deg'])if nearF else max(admissible,key=lambda z:z['triangle_area_mm2']);apply(chosen['index1_added_native_local_x_deg']);q=b.rotation_quaternion.copy();act=baseact.copy();act.name='Support Grip / Index Base Release Candidate C1';act.use_fake_user=True;path='pose.bones["'+target+'"].rotation_quaternion';curve_changes=[]
for i,v in enumerate(q):
 fc=act.fcurves.find(path,index=i);assert fc is not None;before=[list(k.co)for k in fc.keyframe_points]
 for k in list(fc.keyframe_points):fc.keyframe_points.remove(k)
 fc.keyframe_points.insert(0,float(v)).interpolation='LINEAR';curve_changes.append({'data_path':path,'index':i,'C_key_values':before,'C1_key_values':[[0.,float(v)]]})
r.animation_data.action=act;s.frame_set(0);bpy.context.view_layer.update();bpy.context.preferences.filepaths.save_version=0;out=O/'support_grip_index_release_c1.editable.blend';bpy.ops.wm.save_as_mainfile(filepath=str(out),compress=True);report={'status':'Chosen one-parameter release; requires independent combined final gate','source_C':str(C),'source_C_sha256':CPIN,'output':str(out),'output_sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'action':act.name,'frame':0,'chosen':chosen,'changed_curves':curve_changes,'local_delta_quaternion_wxyz':list(Q0.inverted()@rotvecq(chosen['index1_added_native_local_x_deg'],spread)),'selection':'Closest-to-C admissible bracket point reaching >=90% F face area; otherwise maximal restored face area among admissible points with >=10% rest area','scope':'Only Index1 local quaternion changes. C hand/arm/gear/rifle/right grip and all other finger local curves remain exact. Children follow their unchanged local poses. Native geometry, rests, weights, C files and bank/engine exports unchanged.'};(O/'CANDIDATE_C1_AUTHORING.json').write_text(json.dumps(report,indent=2)+'\n');assert hashlib.sha256(C.read_bytes()).hexdigest()==CPIN;print('SAVED',out,report['output_sha256'],chosen['index1_added_native_local_x_deg'],flush=True)
