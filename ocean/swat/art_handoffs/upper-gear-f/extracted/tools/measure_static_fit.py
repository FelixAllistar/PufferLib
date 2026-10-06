"""Map source foot-support IDs to actual exported vertices and candidate preview basis."""
import hashlib,json
from pathlib import Path
import numpy as np
from gltf_math import GLB
r=Path(__file__).resolve().parent;p=r/'package';g=GLB(p/'swat_upper_gear_remake_f_v1.glb');doc=g.doc;skin=doc['skins'][0];joints=skin['joints'];ib=g.acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1);names={n['name']:i for i,n in enumerate(doc['nodes'])};slots={n:i for i,n in enumerate(joints)};W=g.world_at(doc['animations'][0],[0])[0];z=np.load(r/'work/native_static.npz');meta=json.loads((r/'work/native_static.json').read_text());bn=list(z['bone_names']);mi=meta['mesh_names'].index('SWAT_Wearer');C=np.array([[1,0,0],[0,0,-1],[0,1,0.]])
native=z[f'p{mi}']@C;sw=z[f'w{mi}'];sw/=sw.sum(1)[:,None];order=[bn.index(doc['nodes'][n]['name'])for n in joints];node=names['SWAT_Wearer'];mesh=doc['nodes'][node]['mesh'];source_map=json.loads((p/'native_vertex_and_polygon_map.json').read_text())['native_vertex_map'];inverse_ids={v:int(k)for k,v in source_map.items()}
prims=[]
for pi,pr in enumerate(doc['meshes'][mesh]['primitives']):
 at=pr['attributes'];pos=g.acc(at['POSITION']);weights=np.zeros((len(pos),72))
 for suffix in sorted(int(k.split('_')[1])for k in at if k.startswith('WEIGHTS_')):
  ji=g.acc(at[f'JOINTS_{suffix}']).astype(int);we=g.acc(at[f'WEIGHTS_{suffix}']).astype(float)
  for col in range(4):np.add.at(weights,(np.arange(len(pos)),ji[:,col]),we[:,col])
 weights/=weights.sum(1)[:,None];actual=np.zeros((len(pos),3));v4=np.c_[pos,np.ones(len(pos))]
 for sj in range(72):
  use=weights[:,sj]>0
  if np.any(use):actual[use]+=(v4[use]@(W[joints[sj]]@ib[sj]).T)[:,:3]*weights[use,sj,None]
 prims.append((pi,pos,weights,actual))
body=np.concatenate([a for _,_,_,a in prims]);source_eval=z[f'eval_0_{mi}']@C;soles={}
for side in ['Left','Right']:
 b=[i for i,n in enumerate(bn)if n.startswith('mixamorig:'+side)and('Foot'in n or'Toe'in n)];selected=np.flatnonzero(sw[:,b].sum(1)>.5);minimum=float(source_eval[selected,1].min());ids=selected[source_eval[selected,1]<=minimum+.001];mapping=[];actual_points=[]
 for vi in ids:
  matches=[];matched_points=[]
  for pi,pos,weights,actual in prims:
   candidates=np.flatnonzero(np.linalg.norm(pos-native[vi],axis=1)<1e-7)
   for j in candidates:
    if np.max(abs(weights[j]-sw[vi,order]))<1e-7:matches.append({'primitive':pi,'vertex':int(j),'bind_mesh_point':pos[j].tolist()});matched_points.append(actual[j])
  assert matches;actual_points.append(np.mean(matched_points,axis=0));mapping.append({'F_native_vertex':int(vi),'original_N_native_vertex':inverse_ids[int(vi)],'gltf_matches':matches})
 js=[slots[names[bn[i]]]for i in b];export_subset=np.concatenate([actual[weights[:,js].sum(1)>.5]for _,_,weights,actual in prims]);soles[side.lower()]={'selection':'Same-side Foot/Toe cumulative normalized weight>0.5; source support points within1mm of posed foot minimum','native_subset_count':len(selected),'F_native_support_vertices':ids.tolist(),'support_mapping':mapping,'actual_export_min_y_m':float(export_subset[:,1].min()),'native_evaluated_min_y_m':minimum,'exported_subset_count_with_seams':len(export_subset),'support_centroid_model_xyz_m':np.mean(actual_points,axis=0).tolist(),'centroid_sampling':'One position per unique F source support vertex; coincident exported seam copies averaged first','original_ID_namespace':'Original N Rebuilt SWAT full body24850 vertices','F_ID_namespace':'Compacted SWAT_Wearer22645 vertices','GLTF_ID_namespace':'Per-primitive seam-split accessor indices; actual mapping supplied'}
a=json.loads((p/'physical_and_optical_bindings.json').read_text());rn=a['rigid_rifle']['node'];J=np.array(a['rigid_rifle']['J_joint_local']).reshape(4,4).T;rigid=W[rn]@J;forward=rigid[:3,0].copy();forward[1]=0;forward/=np.linalg.norm(forward);up=np.array([0.,1.,0.]);right=np.cross(forward,up);basis=np.stack([forward,up,right]);origin=(np.array(soles['left']['support_centroid_model_xyz_m'])+soles['right']['support_centroid_model_xyz_m'])/2;origin[1]=min(x['actual_export_min_y_m']for x in soles.values());F=np.eye(4);F[:3,:3]=basis;F[:3,3]=-basis@origin
result={'glb_sha256':hashlib.sha256(g.raw).hexdigest(),'clip':'Neutral Carry / Anatomical Gear F','time_s':0,'units':'metres','up':'+Y','root_node':names['SWAT_Mixamo_Rig'],'root_world_column_major':W[names['SWAT_Mixamo_Rig']].T.ravel().tolist(),'wearer_only_body_bounds_model_xyz_m':[body.min(0).tolist(),body.max(0).tolist()],'sole_support':soles,'preview_alignment_candidate':{'heading':'Horizontal projection of physical rifle forward','model_to_engine_heading0_feet_origin_column_major':F.T.ravel().tolist(),'uniform_scale':1,'baked_into_asset':False,'engine_axes':'+Xforward,+Yup,+Zright'},'optic':'physical_and_optical_bindings.json optical surface only','limits':'Reference fitting only. Feet preserve source clearance; no new ground solve, camera origin, collider height, gameplay pose or ADS calibration.'}
(p/'measured_static_fit.json').write_text(json.dumps(result,indent=2)+'\n');print('STATIC_FIT',[(n,d['actual_export_min_y_m'])for n,d in soles.items()])
