"""Independent full-vertex native controls, sole namespace and preview metadata replay."""
import argparse,json,hashlib
from collections import Counter
from pathlib import Path
import numpy as np
from replay_unit import GLB,C
from inspect_binary import matrix
ap=argparse.ArgumentParser(description='Independent full-vertex native capture, sole namespace and preview metadata replay; Python3 + NumPy only.')
ap.add_argument('--package',type=Path,required=True)
ap.add_argument('--capture-dir',type=Path,required=True)
ap.add_argument('--output-dir',type=Path,required=True)
args=ap.parse_args();p=args.package;out=args.output_dir;work=args.capture_dir;out.mkdir(parents=True,exist_ok=True)
g=GLB(p/'crouch_forward_shared_n_c1_loop_a.glb');src=np.load(work/'source.npz');capture=json.loads((work/'capture_report.json').read_text());times=np.load(work/'sample_times.npy');fit=json.loads((p/'measured_preview_fit.json').read_text());event=json.loads((p/'events_and_cycle.json').read_text());travel=json.loads((p/'SOURCE_PINS.json').read_text())
assert capture['glb_sha256']==g.sha==fit['glb_sha256']
assert capture['source_sha256']=='897771dc55846b4ad2b1181fcb560a55d364a4e22fc5510177b905bbb28ee442'
bones=list(src['bone_names']);order=[bones.index(n) for n in g.joint_names];mesh_names=capture['mesh_names'];controls=capture['control_indices'];ctimes=times[controls];W,_,_=g.world(ctimes);allW,_,_=g.world(times)
native_deform=C@src['deformation'][:,order]@C.T
GLB_deform=allW@g.ibm[None]
deformation_error=float(np.max(np.abs(native_deform-GLB_deform)))
assert deformation_error<1e-5
meshes=[];mappings={};source_world={};export_world={};export_triangles={}
def face_key(face):
 a,b,c=map(int,face);return min((a,b,c),(b,c,a),(c,a,b))
for pr in g.primitives:
 mi=mesh_names.index(pr['name']);position=src[f'p{mi}']@C[:3,:3].T
 weights=src[f'w{mi}'];weights=weights[:,order]/weights.sum(axis=1)[:,None]
 lookup={}
 for vi,pos in enumerate(position):lookup.setdefault(tuple(pos),[]).append(vi)
 gw=np.zeros((len(pr['p']),len(g.joints)))
 for col in range(pr['j'].shape[1]):np.add.at(gw,(np.arange(len(gw)),pr['j'][:,col]),pr['w'][:,col])
 indices=[];ambiguous=0;max_weight_error=0
 for v,point in enumerate(pr['p'][:,:3]):
  matches=lookup.get(tuple(point),[]);valid=[vi for vi in matches if np.max(np.abs(weights[vi]-gw[v]))<1e-7]
  assert valid,(pr['name'],pr['primitive'],v)
  ambiguous+=len(valid)>1;indices.append(valid[0]);max_weight_error=max(max_weight_error,float(np.max(np.abs(weights[valid[0]]-gw[v]))))
 indices=np.array(indices);actual=g.skin(W,pr);expected=np.stack([src[f'eval_{ci}_{mi}'][indices]@C[:3,:3].T for ci in controls]);err=np.linalg.norm(actual-expected,axis=-1)
 assert err.max()<1e-5
 # Complete surfaces at all captured times; transform captured Blender native skin matrices into glTF space.
 full_max=0.;full_sum_sq=0.;full_count=0
 for start in range(0,len(times),16):
  end=min(start+16,len(times));actual_all=g.skin(allW[start:end],pr)
  expected_all=np.zeros_like(actual_all)
  for slot in range(len(g.joints)):
   mask=weights[indices,slot]>0
   if not mask.any():continue
   z=np.einsum('tij,vj->tvi',native_deform[start:end,slot,:3,:],pr['p'][mask])
   expected_all[:,mask]+=z*weights[indices[mask],slot][None,:,None]
  e=np.linalg.norm(actual_all-expected_all,axis=-1)
  full_max=max(full_max,float(e.max()));full_sum_sq+=float(np.sum(e*e));full_count+=e.size
 assert full_max<1e-5
 key=(pr['node'],pr['primitive']);mappings[key]=indices;source_world[key]=expected;export_world[key]=actual
 primitive=g.doc['meshes'][g.nodes[pr['node']]['mesh']]['primitives'][pr['primitive']]
 faces=indices[g.acc(primitive['indices']).astype(int).reshape(-1,3)]
 export_triangles.setdefault(pr['name'],Counter()).update(face_key(f) for f in faces)
 meshes.append({'name':pr['name'],'primitive':pr['primitive'],'vertices':len(indices),'bind_positions_exact':True,'normalized_weight_max_error':max_weight_error,'source_matches_ambiguous':ambiguous,'control_count':len(controls),'all_phase_count':len(times),'all_phase_max_position_error_vs_native_capture_m':full_max,'all_phase_rms_position_error_vs_native_capture_m':float(np.sqrt(full_sum_sq/full_count)),'max_position_error_vs_native_evaluated_m':float(err.max()),'rms_position_error_vs_native_evaluated_m':float(np.sqrt(np.mean(err*err)))})
topology=[]
for name,exported in export_triangles.items():
 mi=mesh_names.index(name);expected=Counter(face_key(f) for f in src[f'tris{mi}']);assert expected==exported,name
 topology.append({'name':name,'oriented_surface_triangles_exact':True,'triangle_count':sum(exported.values()),'unreferenced_native_vertices':len(src[f'p{mi}'])-len(np.unique(src[f'tris{mi}']))})
soles=[]
mi=mesh_names.index('Rebuilt SWAT full body');native_position=src[f'p{mi}']@C[:3,:3].T
native_weights=src[f'w{mi}'];native_weights=native_weights[:,order]/native_weights.sum(1)[:,None]
for side,data in fit['sole_support'].items():
 assert 'Native source Blender' in data['vertex_index_namespace'];assert data['native_mesh_vertex_count']==len(native_position)
 max_bind=0;max_weight=0;max_pos=0;count=0;points=[]
 assert data['phase_zero_support_vertex_indices']==[r['native_vertex'] for r in data['phase_zero_support_export_mapping']]
 for row in data['phase_zero_support_export_mapping']:
  vi=row['native_vertex'];got=[]
  for match in row['gltf_matches']:
   key=(data['exported_body_node'],match['primitive']);pr=next(x for x in g.primitives if (x['node'],x['primitive'])==key);ev=match['vertex']
   assert mappings[key][ev]==vi
   assert np.array_equal(pr['p'][ev,:3],match['position_bind_mesh_gltf_m'])
   max_bind=max(max_bind,float(np.linalg.norm(pr['p'][ev,:3]-native_position[vi])))
   gw=np.zeros(len(g.joints));np.add.at(gw,pr['j'][ev],pr['w'][ev]);max_weight=max(max_weight,float(np.max(np.abs(gw-native_weights[vi]))))
   val=export_world[key][0,ev];expect=src[f'eval_0_{mi}'][vi]@C[:3,:3].T;max_pos=max(max_pos,float(np.linalg.norm(val-expect)));got.append(val);count+=1
  assert np.max(np.abs(np.array(got)-got[0]))<1e-12;points.append(got[0])
 centroid_error=float(np.linalg.norm(np.mean(points,axis=0)-data['phase_zero_support_centroid_model_xyz_m']))
 slots=[i for i,n in enumerate(g.joint_names) if n in ['mixamorig:'+side.capitalize()+'Foot','mixamorig:'+side.capitalize()+'ToeBase','mixamorig:'+side.capitalize()+'Toe_End']]
 native_subset=np.sum(native_weights[:,slots],axis=1)>.5
 assert int(native_subset.sum())==data['subset_vertices']
 export_count=sum(int((np.sum(pr['w']*np.isin(pr['j'],slots),axis=1)>.5).sum()) for pr in g.primitives if pr['name']=='Rebuilt SWAT full body')
 assert export_count==data['exported_subset_vertex_count_including_seams']
 assert max_pos<1e-5 and centroid_error<1e-5
 soles.append({'side':side,'native_support_vertices':len(points),'actual_gltf_matches':count,'max_bind_position_error_m':max_bind,'max_normalized_weight_error':max_weight,'max_phase_zero_error_vs_evaluated_native_m':max_pos,'centroid_metadata_error_m':centroid_error,'native_subset_vertices':int(native_subset.sum()),'exported_subset_vertices':export_count})
preview=fit['preview_alignment_candidate'];F=matrix(preview['model_to_engine_heading0_feet_origin_column_major']);heading=np.array(preview['source_forward_model_xyz']);origin=np.array(preview['source_feet_origin_model_xyz_m']);assert np.max(np.abs(F[:3,:3]@heading-[1,0,0]))<1e-12;assert np.max(np.abs((F@np.r_[origin,1])[:3]))<1e-12
ref=fit['source_travel_reference'];assert ref['source_measurement_sha256']==hashlib.sha256((p/'SOURCE_PINS.json').read_bytes()).hexdigest()
v=np.array(travel['source_root_displacement_m']);gv=C[:3,:3]@v;ev=F[:3,:3]@gv
assert np.array_equal(v,ref['source_displacement_blender_xyz_m']);assert np.max(np.abs(gv-ref['source_displacement_gltf_xyz_m']))<1e-12;assert np.max(np.abs(ev-ref['source_displacement_after_preview_alignment_engine_xyz_m']))<1e-12;assert np.array_equal(v,event['source_pace']['root_vector_removed_blender_m'])
assert event['duration_s']==1 and event['source_fps']==30 and event['source_frame_range']==[0,30] and event['animation_name']==g.doc['animations'][0]['name']
assert event['repair_windows_s']==[[0,.125],[.875,1]] and event['events']==[]
assert event['visibility']['stored_spare_B_collapsed'] is False
assert event['visibility']['stored_spare_node']==g.names.index('Prop_Magazine_B')
assert event['visibility']['stored_spare_skin_slot']==g.joint_names.index('Prop_Magazine_B')
assert event['visibility']['sleeve_skin_owner']=='mixamorig:Hips'
assert event['source_pace']['nominal_baked_m_s']==travel['nominal_speed_m_s']
assert event['source_pace']['original_source_fps']==travel['source_fps']==60
assert event['runtime_loop_quality_approved'] is False and fit['runtime_or_gameplay_fit_tested'] is False
b=fit['weapon_bridge'];slot=g.joint_names.index('Prop_Rifle');B=matrix(b['rigid_stockpad_engine_frame_to_bind_mesh_gltf_column_major']);J=matrix(b['rigid_stockpad_engine_frame_to_Prop_Rifle_joint_local_gltf_column_major']);assert np.array_equal(matrix(b['inverse_bind_for_Prop_Rifle_column_major']),g.ibm[slot]);assert np.max(np.abs(g.ibm[slot]@B-J))<1e-12;assert np.max(np.abs(W[0,slot]@J-matrix(b['phase_zero_rigid_stockpad_frame_to_model_column_major'])))<1e-12
r={'status':'PASS','glb_sha256':g.sha,'source_sha256':capture['source_sha256'],'source_capture_sha256':hashlib.sha256((work/'source.npz').read_bytes()).hexdigest(),'scope':'Independent GLB byte evaluator checked against separately captured Blender evaluated native vertices at16 controls and1243 separately captured native deformation phases. Complete exported vertices matched by exact bind coordinates plus all normalized weights. Neither capture generation nor the fixture writer is imported.','controls_s':ctimes.tolist(),'all_captured_phase_count':len(times),'all_captured_deformation_matrix_abs_error':deformation_error,'total_exported_vertices':sum(x['vertices'] for x in meshes),'meshes':meshes,'native_oriented_topology':topology}
(out/'native_control_validation.json').write_text(json.dumps(r,indent=2)+'\n')
s={'status':'PASS','glb_sha256':g.sha,'rows':soles,'scope':'Original native Blender IDs resolve to separately checked exported primitive/vertex pairs, exact bind positions, all normalized weights and evaluated phase-zero positions.'};(out/'sole_mapping_validation.json').write_text(json.dumps(s,indent=2)+'\n')
f={'status':'PASS','glb_sha256':g.sha,'heading_maps_to_engine_X':True,'feet_origin_maps_to_zero':True,'travel_matches_native_source':True,'source_travel_preview_angle_from_engine_X_degrees':float(np.degrees(np.arctan2(np.linalg.norm(ev[1:]),ev[0]))),'source_travel_blender_m':v.tolist(),'source_travel_after_preview_m':ev.tolist(),'source_pace_m_s':travel['nominal_speed_m_s'],'events_and_timing_match_glb':True,'corrected_bridge_matches_binary':True,'classification':'Fixed unbaked preview candidate only, no runtime placement or ADS validation'};(out/'preview_metadata_validation.json').write_text(json.dumps(f,indent=2)+'\n')
print(json.dumps({'meshes':meshes,'sole_mappings':soles,'preview':f},indent=2))
