"""Replay actual exported goggle vertices at independent Blender evaluated control poses.
python verify_optical_proxy.py FILE.glb optical_proxy_bindings.json output.json
Requires NumPy only. This portable replay covers the explicit control poses;
the separately reported dense native-capture check is not repeated here.
"""
import hashlib,json,struct,sys
from pathlib import Path
import numpy as np
glb,bindings_path,output=map(Path,sys.argv[1:])
b=json.loads(bindings_path.read_text())
from gltf_math import GLB
parsed=GLB(glb);raw=parsed.raw;doc=parsed.doc;nodes=parsed.nodes;skin=doc['skins'][0];joints=skin['joints'];acc=parsed.acc;world_at=parsed.world_at
assert hashlib.sha256(raw).hexdigest()==b['glb_sha256']
node=next(i for i,n in enumerate(nodes)if n.get('name')=='mixamorig:Head');slot=joints.index(node)
assert node==b['head_node_index'] and slot==b['head_skin_joint_slot']
IBM=acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)[slot].astype(float)
assert np.array_equal(IBM,np.array(b['head_inverse_bind_column_major']).reshape(4,4).T)
B=np.array(b['optical_frame_to_bind_mesh_gltf_column_major']).reshape(4,4).T
J=np.array(b['optical_frame_to_head_node_local_gltf_column_major']).reshape(4,4).T
assert np.max(abs(J-IBM@B))<1e-12
controls=b['native_blender_evaluated_control_samples'];times=np.array([x['time_s']for x in controls]);W=world_at(doc['animations'][0],times)
body=doc['meshes'][nodes[b['body_node']]['mesh']];actual={};vertex_map_checks=0
for side,mapping in b['lens_vertex_mapping'].items():
 for match in mapping['matches']:
  p=body['primitives'][match['primitive']];a=p['attributes'];v=match['vertex'];pos=acc(a['POSITION'])[v]
  assert np.array_equal(pos,match['position_bind_mesh_gltf']);weights=np.zeros(len(joints))
  for suffix in sorted(int(k.split('_')[1])for k in a if k.startswith('WEIGHTS_')):
   ji=acc(a[f'JOINTS_{suffix}'])[v].astype(int);ws=acc(a[f'WEIGHTS_{suffix}'])[v]
   np.add.at(weights,ji,ws)
  assert weights[slot]==1 and np.count_nonzero(weights)==1
  vertex_map_checks+=1
 actual[side]=(W[:,node]@IBM@np.r_[mapping['matches'][0]['position_bind_mesh_gltf'],1])[:,:3]
M=W[:,node]@IBM@B;via=W[:,node]@J;mid=(actual['left']+actual['right'])/2
errors={side:float(np.linalg.norm(actual[side]-np.array([c['native_evaluated_lens_points_model_xyz_m'][side]for c in controls]),axis=1).max())for side in actual}
miderr=float(np.linalg.norm(mid-M[:,:3,3],axis=1).max());two=float(abs(M-via).max())
assert max(errors.values())<1e-5 and miderr<1e-7 and two<1e-12
report={'status':'PASS','glb_sha256':b['glb_sha256'],'classification':b['classification'],'head_node':node,'head_skin_joint_slot':slot,'control_pose_count':len(times),'times_s':times.tolist(),'actual_vertex_map_checks':vertex_map_checks,'max_exported_vs_native_evaluated_lens_error_m':errors,'max_midpoint_frame_error_m':miderr,'two_correct_forms_max_error':two,'scope':'Explicit independent Blender evaluated control poses only; no anatomy, camera, runtime or contact approval'}
output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
