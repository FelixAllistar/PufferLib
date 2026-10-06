"""Check corrected rifle attachment against independently captured source poses.
Usage: python validate_bridge.py PINNED.glb reference_samples.json output.json
Requires NumPy. Reads inputs without modifying them.
"""
import hashlib,json,struct,sys
from pathlib import Path
import numpy as np
glb,reference_path,output=map(Path,sys.argv[1:])
reference=json.loads(reference_path.read_text())
from gltf_math import GLB
parsed=GLB(glb);raw=parsed.raw;doc=parsed.doc;nodes=parsed.nodes;skin=doc['skins'][0];joints=skin['joints'];acc=parsed.acc;world_at=parsed.world_at
assert hashlib.sha256(raw).hexdigest()==reference['glb_sha256']
node=next(i for i,n in enumerate(nodes) if n.get('name')=='Prop_Rifle')
slot=joints.index(node)
ibms=acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1).astype(float)
B=np.array(reference['B_bind_mesh_column_major']).reshape(4,4).T
J=ibms[slot]@B
times=np.array(reference['times_s']);world=world_at(doc['animations'][0],times)
correct=world[:,node]@ibms[slot]@B
via_joint=world[:,node]@J
expected=np.array(reference['source_model_from_rigid_column_major']).reshape(-1,4,4).transpose(0,2,1)
wrong=world[:,node]@B
points=np.c_[np.array(reference['rigid_reference_vertices']),np.ones(len(reference['rigid_reference_vertices']))]
position_error=0.
for start in range(0,len(times),64):
 delta=correct[start:start+64]-expected[start:start+64]
 position_error=max(position_error,float(np.linalg.norm(np.einsum('nij,vj->nvi',delta[:,:3,:],points),axis=2).max()))
ready=np.array(reference['native_ready_model_from_rigid_column_major']).reshape(4,4).T
report={'status':'PASS','glb_sha256':hashlib.sha256(raw).hexdigest(),'skin_index':0,'prop_node':node,'resolved_joint_slot':slot,
 'samples':len(times),'sample_scope':reference['sample_scope'],'rigid_reference_vertices':len(points),
 'B_bind_mesh_column_major':B.T.ravel().tolist(),'inverse_bind_column_major':ibms[slot].T.ravel().tolist(),
 'J_joint_local_column_major':J.T.ravel().tolist(),
 'formula':'model_from_rigid(t) = world_node(t) * inverse_bind[resolved_slot] * B_bind_mesh = world_node(t) * J_joint_local',
 'max_matrix_element_error_vs_native_source':float(abs(correct-expected).max()),
 'ready_matrix_error_vs_native_reference':float(abs(correct[0]-ready).max()),
 'max_rigid_vertex_position_error_vs_native_source_m':position_error,
 'two_correct_forms_max_matrix_error':float(abs(correct-via_joint).max()),
 'missing_inverse_bind_formula_max_matrix_error':float(abs(wrong-expected).max()),
 'checks':'Actual GLB hierarchy, sampler interpolation and inverse binds; independent frozen native reference samples. No engine-runtime or gameplay mapping test.'}
assert report['max_matrix_element_error_vs_native_source']<1e-5
assert position_error<1e-5 and report['two_correct_forms_max_matrix_error']<1e-12
output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
