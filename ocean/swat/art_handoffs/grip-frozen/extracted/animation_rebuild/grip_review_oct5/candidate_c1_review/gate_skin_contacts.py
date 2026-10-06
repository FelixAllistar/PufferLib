"""Read-only saved N/F/R5 native skin contact and palm/digit review. No asset writes."""
import bpy,sys,json,hashlib,collections,importlib.util,math
from pathlib import Path
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path('/workspace/scratch/eac4961518d4/animation_rebuild'); OUT=ROOT/'grip_review_oct5/candidate_c1_review'
spec=importlib.util.spec_from_file_location('measure','/workspace/shared/practical_ads_audit_recovery_algorithms.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
from gate_config import INPUTS,verify_loaded
FILES={label:(path,pin) for label,path,pin in INPUTS}
refs=json.loads((ROOT/'shared_pose_fit/portable_recipe/native_frame_reference.json').read_text())['anatomy'];refdata={}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def unit(a):return a/max(np.linalg.norm(a),1e-20)
def angle(a,b):return math.degrees(math.acos(float(np.clip(unit(a)@unit(b),-1,1))))
def positions(o):return np.array(m.evaluated_positions(o,bpy.context.evaluated_depsgraph_get()))
def getmesh(o):
 o.data.calc_loop_triangles();return np.array([t.vertices[:] for t in o.data.loop_triangles]),[{o.vertex_groups[g.group].name:g.weight for g in v.groups} for v in o.data.vertices]
def tree(V,T):return BVHTree.FromPolygons([Vector(p) for p in V],T,all_triangles=True)
def surface_region_samples(V,T,tags,hw,H,gun,rifle_inverse,restV,restH):
 # Surface normals are computed from evaluated skin triangles. Rest geometry chooses
 # the volar material subset; it does not substitute a bone normal for the surface.
 reg=collections.defaultdict(lambda:{'area_within_1mm_mm2':0.,'area_within_2mm_mm2':0.,'area_within_3mm_mm2':0.,'minimum_sampled_surface_gap_mm':1e9,'minimum_opposed_surface_gap_mm':1e9,'closest_opposed':None,'contact_samples_within2mm':0})
 palmfaces=[];palmcenters=[];palmareas=[];contactnormals=[];contactweights=[];contacttargetnormals=[];volar_ids=set();landmarks={}
 for j,t in enumerate(T):
  tv=V[t];norm=np.cross(tv[1]-tv[0],tv[2]-tv[0]);area=np.linalg.norm(norm)/2;norm=unit(norm)
  rv=restV[t];rn=unit(np.cross(rv[1]-rv[0],rv[2]-rv[0]));tag=tags[j];digit=next((f for f in ['Thumb','Index','Middle','Ring','Pinky'] if f in tag),'Palm')
  volar=digit=='Palm' and hw[j]>.6 and rn@restH[:3,2]>.3
  if digit=='Palm' and not volar:continue
  if volar:palmfaces.append(norm);palmcenters.append(tv.mean(0));palmareas.append(area);volar_ids.update(map(int,t))
  region='Palm' if volar else tag.split('Hand',1)[1];rr=reg[region];count=max(2,min(12,math.ceil(np.linalg.norm(tv[[1,2,0]]-tv,axis=1).max()/.003)));N=(count+1)*(count+2)//2;near=[0,0,0]
  for u in range(count+1):
   for w in range(count+1-u):
    p=tv[0]+(tv[1]-tv[0])*u/count+(tv[2]-tv[0])*w/count;hit,normal,k,d=gun.find_nearest(Vector(p));dot=float(norm@np.asarray(normal));rr['minimum_sampled_surface_gap_mm']=min(rr['minimum_sampled_surface_gap_mm'],d*1000)
    if dot<-.1:
     if d*1000<rr['minimum_opposed_surface_gap_mm']:rr.update(minimum_opposed_surface_gap_mm=d*1000,closest_opposed={'skin_point_rifle_m':((np.r_[p,1]@rifle_inverse)[:3]).tolist(),'rifle_point_m':((np.r_[np.array(hit),1]@rifle_inverse)[:3]).tolist(),'surface_normal_dot':dot,'hand_local_triangle':j,'skin_vertices_local':list(map(int,t))})
     for z,threshold in enumerate([.001,.002,.003]):near[z]+=d<threshold
     if d<.002:
      rr['contact_samples_within2mm']+=1
      if volar:contactnormals.append(norm);contactweights.append(area/N);contacttargetnormals.append(np.array(normal))
  for z,n in enumerate([1,2,3]):rr[f'area_within_{n}mm_mm2']+=area*1e6*near[z]/N
 palmnormal=unit(np.average(palmfaces,axis=0,weights=palmareas));palmcenter=np.average(palmcenters,axis=0,weights=palmareas)
 palm={'material_skin_vertices_local':sorted(volar_ids),'native_volar_area_mm2':sum(palmareas)*1e6,'area_weighted_skin_normal_world':palmnormal.tolist(),'area_weighted_skin_normal_rifle':unit(palmnormal@rifle_inverse[:3,:3]).tolist(),'skin_center_rifle_m':(np.r_[palmcenter,1]@rifle_inverse)[:3].tolist(),'skin_normal_vs_hand_z_degrees':angle(palmnormal,H[:3,2]),'method':'Volar material triangles selected in rest geometry with hand weight >0.6 and native face normal dot hand +Z >0.3. Reported normal and centroid are area weighted evaluated SKIN faces, not bone origin/axis.'}
 if contactweights:
  pn=unit(np.average(contactnormals,axis=0,weights=contactweights));gn=unit(np.average(contacttargetnormals,axis=0,weights=contactweights));palm.update(contact_skin_normal_rifle=unit(pn@rifle_inverse[:3,:3]).tolist(),contact_rifle_normal_rifle=unit(gn@rifle_inverse[:3,:3]).tolist(),opposed_contact_normal_dot=float(pn@gn),contact_normal_vs_opposed_rifle_degrees=angle(pn,-gn))
 return dict(reg),palm
for label,(rel,pin) in FILES.items():
 p=ROOT/rel;before=sha(p);assert before==pin;bpy.ops.wm.open_mainfile(filepath=str(p));verify_loaded(bpy,label);bpy.context.scene.frame_set(0);bpy.context.view_layer.update();rig=bpy.data.objects['SWAT_Mixamo_Rig'];body=bpy.data.objects.get('SWAT_Wearer') or bpy.data.objects['Rebuilt SWAT full body'];gunobj=bpy.data.objects['Rifle 7'];V=positions(body);G=positions(gunobj);BT,W=getmesh(body);GT,_=getmesh(gunobj);native=np.array([q.value for q in body.data.attributes['native_vertex_id'].data]) if 'native_vertex_id' in body.data.attributes else np.arange(len(V));lookup={int(v):i for i,v in enumerate(native)};comp,groups=m.components(BT,len(V));alltags,tw=m.triangle_weight_tags(BT,W);restV=np.array([body.matrix_world@v.co for v in body.data.vertices]);restG=np.array([v.co[:] for v in gunobj.data.vertices]);fit=np.linalg.lstsq(np.c_[restG,np.ones(len(G))],G,rcond=None)[0];affine=np.eye(4);affine[:,:3]=fit;inv=np.linalg.inv(affine);fiterr=np.linalg.norm(np.c_[restG,np.ones(len(G))]@fit-G,axis=1).max()*1000;assert fiterr<.001;gun=tree(G,GT);glocal=(np.c_[V,np.ones(len(V))]@inv)[:,:3]
 mag=bpy.data.objects['Removed magazine'];MV=positions(mag);MT,_=getmesh(mag);magtree=tree(MV,MT)
 out={'label':label,'source':str(p),'sha256_before':before,'action':rig.animation_data.action.name,'frame':0,'body_object':body.name,'body_vertices':len(V),'native_vertex_mapping':bool('native_vertex_id' in body.data.attributes),'rifle_native_to_evaluated_affine_fit_max_error_mm':fiterr,'scope':'Saved static grips only. Direct evaluated actual skin surfaces and native Rifle 7; no pose application, solve, or asset edits. Three-ray parity verifies finite penetration witnesses. Finite samples do not certify global zero contact/intersection or engine transition/FOV/armIK behavior.','hands':{}}
 for side,seed in [('Left',7923),('Right',9145)]:
  cid=comp[lookup[seed]];ids=np.array(groups[cid]);ti=np.array([i for i,t in enumerate(BT) if comp[int(t[0])]==cid]);HT=BT[ti];fulltags=[alltags[k] for k in ti];tags=[next((f for f in ['Thumb','Index','Middle','Ring','Pinky'] if f in x),'Palm') for x in fulltags];hw=[sum(W[int(i)].get('mixamorig:'+side+'Hand',0) for i in t)/3 for t in HT];H=np.array(rig.matrix_world@rig.pose.bones['mixamorig:'+side+'Hand'].matrix);RH=np.array(rig.matrix_world@rig.data.bones['mixamorig:'+side+'Hand'].matrix_local);VV,TT,cap=m.analysis_only_wrist_cap(V,HT);assert cap['boundary_edges']==16;closed=tree(VV,TT);nativehand=tree(V,HT);bounds=(V[ids].min(0)-.004,V[ids].max(0)+.004);samples=m.dense_surface_samples(G,GT,bounds);inside=m.hand_inside_samples(closed,samples);regions=collections.defaultdict(lambda:{'inside_samples':0,'all3_inside_samples':0,'samples_deeper_1mm':0,'max_depth_mm':0.})
  for q in inside:
   k=q['nearest_hand_or_cap_triangle'];r=regions[tags[k] if k<len(HT) else 'Wrist cap'];r['inside_samples']+=1;r['all3_inside_samples']+=sum(q['votes'])==3;r['samples_deeper_1mm']+=q['depth_mm']>1
   if q['depth_mm']>r['max_depth_mm']:r.update(max_depth_mm=q['depth_mm'],deepest_rifle_point_m=(np.r_[q['point_m'],1]@inv)[:3].tolist(),deepest_votes=q['votes'])
  patch,palm=surface_region_samples(V,HT,fulltags,hw,H,gun,inv,restV,RH)
  palm['material_skin_vertices_native']=[int(native[i]) for i in palm.pop('material_skin_vertices_local')]
  # Bone weight identifies each material finger segment. Centroids are evaluated
  # mesh skin vertices in that segment, rather than bone heads/tails.
  digits={}
  for digit in ['Thumb','Index','Middle','Ring','Pinky']:
   centers=[];segs=[]
   for k in [1,2,3]:
    name='mixamorig:'+side+'Hand'+digit+str(k);sel=np.array([i for i in ids if W[int(i)].get(name,0)>.5]);ww=np.array([W[int(i)].get(name,0) for i in sel]);center=np.average(V[sel],axis=0,weights=ww);centers.append(center);segs.append({'segment':k,'skin_vertex_count':len(sel),'native_skin_vertex_ids':[int(native[i]) for i in sel],'skin_center_rifle_m':(np.r_[center,1]@inv)[:3].tolist(),'surface_contact':patch.get(digit+str(k),{})})
   bones=[rig.pose.bones['mixamorig:'+side+'Hand'+digit+str(k)] for k in [1,2,3]];dirs=[np.array(rig.matrix_world.to_3x3()@(b.tail-b.head)) for b in bones];digits[digit]={'skin_segments':segs,'skin_centroid_chain_turn_degrees':angle(centers[1]-centers[0],centers[2]-centers[1]),'supplemental_bone_segment_turns_degrees':[angle(dirs[0],dirs[1]),angle(dirs[1],dirs[2])],'supplemental_bone_method':'Angles between consecutive posed bone directions; not substituted for skin contact evidence.'}
  adj=collections.defaultdict(set)
  for t in HT:
   for i in t:adj[int(i)].update(map(int,t))
  near={}
  for i in ids:
   seen={int(i)};front=seen.copy()
   for _ in range(3):front=set.union(*(adj[j] for j in front))-seen if front else set();seen|=front
   near[int(i)]=seen
  cross=m.finite_crossings([Vector(p) for p in V],list(map(tuple,BT)),list(map(int,ti)),[Vector(p) for p in V],list(map(tuple,BT)),list(map(int,ti)),same=True);tag_by={int(k):tags[j] for j,k in enumerate(ti)};nonlocalpairs=[]
  for (i,j),q in cross.items():
   if tag_by[i]==tag_by[j] or 'Palm' in [tag_by[i],tag_by[j]]:continue
   if any(set(BT[j])&near[int(v)] for v in BT[i]):continue
   nonlocalpairs.append(q)
  riflecross=m.finite_crossings([Vector(p) for p in G],list(map(tuple,GT)),list(range(len(GT))),[Vector(p) for p in V],list(map(tuple,BT)),list(map(int,ti)));mcross=m.finite_crossings([Vector(p) for p in MV],list(map(tuple,MT)),list(range(len(MT))),[Vector(p) for p in V],list(map(tuple,BT)),list(map(int,ti)));msamples=m.dense_surface_samples(MV,MT,bounds);mins=m.hand_inside_samples(closed,msamples)
  wristbones=[rig.pose.bones['mixamorig:'+side+n] for n in ['ForeArm','Hand']];wrist=angle(np.array(wristbones[0].tail-wristbones[0].head),np.array(wristbones[1].tail-wristbones[1].head))
  # Native dorsal panel skin landmark is carried through compact F mapping.
  dorsalids=[lookup[i] for i in refs[side]['hand_dorsal_ids'] if i in lookup];dorsal=V[dorsalids].mean(0);palm['dorsal_marker_count']=len(dorsalids);palm['dorsal_skin_centroid_rifle_m']=(np.r_[dorsal,1]@inv)[:3].tolist()
  hand={'native_component_id':int(min(native[ids])),'vertices':len(ids),'triangles':len(HT),'diagnostic_wrist_cap':cap,'rifle_sample_count':len(samples),'confirmed_inside_samples':len(inside),'maximum_sampled_confirmed_depth_mm':max([q['depth_mm'] for q in inside],default=0),'inside_by_region':dict(regions),'exact_rifle_hand_triangle_crossing_pairs':len(riflecross),'nonlocal_cross_digit_exact_pairs':nonlocalpairs,'palm_skin_orientation':palm,'contact_by_skin_segment':patch,'finger_skin_segments':digits,'supplemental_wrist_axis_bend_degrees':wrist,'installed_magazine':{'exact_triangle_crossings':len(mcross),'near_hand_surface_samples':len(msamples),'inside_samples':len(mins),'minimum_glove_vertex_to_magazine_gap_mm':min(magtree.find_nearest(Vector(p))[3] for p in V[ids])*1000,'minimum_magazine_vertex_to_glove_gap_mm':min(nativehand.find_nearest(Vector(p))[3] for p in MV)*1000}}
  origids=[int(native[i]) for i in ids];skinmap={int(native[i]):glocal[i].tolist() for i in ids}
  if label=='A':refdata[side]={'skinmap':skinmap,'restmap':{int(native[i]):restV[i].tolist() for i in ids},'weights':{int(native[i]):W[int(i)] for i in ids},'triangles':sorted(sorted(int(native[i]) for i in t) for t in HT)}
  else:
   R=refdata[side];errors=np.array([np.linalg.norm(np.array(skinmap[i])-np.array(R['skinmap'][i]))*1000 for i in skinmap]);hand['comparison_to_A']={'native_skin_surface_ids_equal':set(skinmap)==set(R['skinmap']),'max_native_rest_vertex_delta_mm':max(np.linalg.norm(restV[i]-R['restmap'][int(native[i])])*1000 for i in ids),'native_weights_equal':all(W[int(i)]==R['weights'][int(native[i])] for i in ids),'native_triangles_equal':sorted(sorted(int(native[i]) for i in t) for t in HT)==R['triangles'],'max_skin_delta_in_rifle_frame_mm':float(errors.max()),'rms_skin_delta_in_rifle_frame_mm':float(np.sqrt(np.mean(errors**2)))}
  out['hands'][side]=hand;print('HAND_RESULT',label,side,'depth_mm',hand['maximum_sampled_confirmed_depth_mm'],'palm_2mm',patch['Palm']['area_within_2mm_mm2'],'nonlocal_fingers',len(nonlocalpairs),'comparison',hand.get('comparison_to_A'),flush=True)
 out['sha256_after']=sha(p);assert out['sha256_after']==before;(OUT/(label+'_static_skin_grip_audit.json')).write_text(json.dumps(out,indent=2)+'\n');print('WRITTEN',label,flush=True)
