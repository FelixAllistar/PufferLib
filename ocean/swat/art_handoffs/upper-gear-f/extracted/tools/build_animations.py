"""Add two source-pinned clips to fresh F bind geometry using evaluated joint worlds.
This bakes Blender inherit-scale NONE into ordinary glTF parent-local TRS.
"""
import copy,hashlib,json,struct
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from gltf_math import GLB
r=Path(__file__).resolve().parent;p=r/'package';p.mkdir(exist_ok=True)
g=GLB(r/'work/gear_f_bind.glb');doc=copy.deepcopy(g.doc);blob=bytearray(g.binary);doc['animations']=[]
nodes=doc['nodes'];skin=doc['skins'][0];joints=skin['joints'];ibm=g.acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1).astype(float)
rest=g.world_at(None,[0])[0];C=np.array([[1,0,0,0],[0,0,-1,0],[0,1,0,0],[0,0,0,1.]])
assert len(joints)==72 and not any('matrix'in nodes[n]for n in joints)
def add(a,kind,is_time=False):
 a=np.asarray(a,dtype='<f4');blob.extend(b'\0'*((-len(blob))%4));vi=len(doc['bufferViews']);doc['bufferViews'].append({'buffer':0,'byteOffset':len(blob),'byteLength':a.nbytes});blob.extend(a.tobytes());ac={'bufferView':vi,'componentType':5126,'count':len(a),'type':kind}
 if is_time:ac.update(min=[float(a.min())],max=[float(a.max())])
 ai=len(doc['accessors']);doc['accessors'].append(ac);return ai
clips=[]
for label in ['static','movement']:
 reference=json.loads((r/f'work/native_{label}.json').read_text());z=np.load(r/f'work/native_{label}.npz');names=list(z['bone_names']);order=[names.index(nodes[n]['name'])for n in joints];keys=z['key_times'];ids=np.array([int(np.argmin(abs(z['times']-t)))for t in keys]);assert np.max(abs(z['times'][ids]-keys))<1e-12
 D=z['deformation'][ids][:,order];W=np.broadcast_to(rest,(len(keys),*rest.shape)).copy();W[:,joints]=C.T@D@C@np.linalg.inv(ibm)
 animation={'name':reference['action'],'samplers':[],'channels':[],'extras':{'source_sha256':reference['source_sha256'],'authored_duration_s':None if label=='static'else 3,'intended_use':'Natural static carry hold'if label=='static'else'Bounded raise/return assembly proof; nonlooping review use','source_sampling':'frame0 single key'if label=='static'else'181 original samples at60Hz over3seconds','runtime_or_ADS_approved':False}}
 rows=[];maxres=0.;ii=add(keys,'SCALAR',True)
 for n in joints:
  parent=g.parents.get(n);local=W[:,n]if parent is None else np.linalg.inv(W[:,parent])@W[:,n];A=local[:,:3,:3];u,s,vh=np.linalg.svd(A);R=u@vh;assert np.all(np.linalg.det(R)>0);scales=np.diagonal(R.transpose(0,2,1)@A,axis1=1,axis2=2).copy();q=Rotation.from_matrix(R).as_quat()
  for k in range(1,len(q)):
   if q[k]@q[k-1]<0:q[k]*=-1
  reconstructed=R*scales[:,None,:];res=float(abs(A-reconstructed).max());maxres=max(maxres,res);assert res<1e-5
  row={'node':n,'name':nodes[n]['name'],'skin_slot':joints.index(n),'native_inherit_scale':'NONE'if nodes[n]['name']in ['Gear_Elbow_L','Gear_Elbow_R']else'FULL','local_TRS_shear_residual_max_abs':res,'keys':len(keys)}
  for path,values,kind in [('translation',local[:,:3,3],'VEC3'),('rotation',q,'VEC4'),('scale',scales,'VEC3')]:
   oi=add(values,kind);si=len(animation['samplers']);animation['samplers'].append({'input':ii,'output':oi,'interpolation':'STEP'if label=='static'else'LINEAR'});animation['channels'].append({'sampler':si,'target':{'node':n,'path':path}})
  rows.append(row)
 doc['animations'].append(animation);clips.append({'label':label,'name':reference['action'],'source_sha256':reference['source_sha256'],'sample_count':len(keys),'times_s':keys.tolist(),'source_fps':reference['fps_context'],'authored_duration_s':None if label=='static'else 3,'stored_span_s':0 if label=='static'else 3,'local_TRS_shear_residual_max_abs':maxres,'joint_rows':rows})
doc['buffers']=[{'byteLength':len(blob)}];doc['asset']['generator']='SWAT F geometry Blender4.3.2; explicit evaluated-world animation bake';doc['asset']['extras']={'asset_version':'upper-gear-remake-f-v1','rig_joints':72,'original_native_joints':70,'new_carriers':['Gear_Elbow_L','Gear_Elbow_R'],'original_70_bind_changed':False,'shared_mesh_compatible':False,'intended_use':'Natural hold and bounded3second articulation; not ADS','source_checkpoint_sha256':'1e586e869db06e5e6a0026b5c56860e5e0ed7f2d9bc1ae9bbee0eb8ca1fd3e35'}
js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*((-len(js))%4);blob+=b'\0'*((-len(blob))%4);data=struct.pack('<III',0x46546c67,2,12+8+len(js)+8+len(blob))+struct.pack('<II',len(js),0x4e4f534a)+js+struct.pack('<II',len(blob),0x004e4942)+blob
out=p/'swat_upper_gear_remake_f_v1.glb';out.write_bytes(data)
report={'glb':out.name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'geometry_export_sha256':hashlib.sha256(g.raw).hexdigest(),'clips':clips,'bake_method':'For every native authored key, desired exported Wnode = C^-1 * native_deformation * C * inverse(IBM). Resolve actual exported parent worlds, decompose local matrices into TRS with nearest proper rotation. Small float32-induced shear residual is measured, not silently claimed zero. Carrier inherit-scale NONE is represented by these baked locals. Original source and times remain unchanged.','interpolation_limit':'Source evaluates normalized component-linear quaternions; glTF LINEAR rotation uses shortest-path slerp. Mid-key all-vertex comparisons quantify this and evaluated-local interpolation differences. No C1 repair, loop or ADS acceptance is claimed.'}
(p/'conversion_report.json').write_text(json.dumps(report,indent=2)+'\n');print({k:report[k]for k in ['glb','bytes','sha256']});print('local residuals',[(x['label'],x['local_TRS_shear_residual_max_abs'])for x in clips])
