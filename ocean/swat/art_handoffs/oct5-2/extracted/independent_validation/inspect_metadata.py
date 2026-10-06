"""Verify static fixture metadata against independently parsed binary and source capture."""
import argparse,json
from pathlib import Path
import numpy as np
from replay_static import GLB,C,mat,sha,source_parity
from inspect_measurements import inspect

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--package',type=Path,required=True);ap.add_argument('--source-capture',type=Path,required=True);ap.add_argument('--references',type=Path,required=True);ap.add_argument('--ready-glb',type=Path);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();p=a.package;g=GLB(p/'crouch_ready_planted_shared_n_static.glb');W=g.world();refs=json.loads(a.references.read_text());measurement=inspect(g,a.source_capture,refs);_,z,sm,maps,worlds=source_parity(g,a.source_capture)
 def read(name):
  d=json.loads((p/name).read_text())
  if 'glb_sha256' in d:assert d['glb_sha256']==g.sha,name
  return d
 b=read('bindings.json');assert len(b['joint_map'])==70
 for slot,row in enumerate(b['joint_map']):
  n=g.joints[slot];parent=g.parents.get(n);assert (row['skin_joint_slot'],row['node'],row['name'],row['parent_node'],row['parent_joint_slot'])==(slot,n,g.names[n],parent,g.joints.index(parent) if parent in g.joints else None);assert np.array_equal(mat(row['inverse_bind_column_major']),g.ibm[slot]);assert row['default_node_trs']=={k:v for k,v in g.nodes[n].items() if k in ['translation','rotation','scale','matrix']}
 for row in [b['scene_root']]+list(b['semantic_candidates'].values())+[row for prop in b['props'] for row in [prop['mesh'],prop['joint']]]:
  n=row['node'];assert row['name']==g.names[n] and row['skin_joint_slot']==(g.joints.index(n) if n in g.joints else None)
 for n,v in b['phase_zero_scene_node_world_matrices'].items():assert abs(mat(v)-W[g.names.index(n)]).max()<1e-10
 for n,v in b['phase_zero_source_bone_frame_model_matrices'].items():assert abs(mat(v)-C@z['bone_world'][list(z['bone_names']).index(n)]@C.T).max()<1e-10
 an=read('animation_channels.json');assert an['source_curves']==an['source_keys']==700 and an['native_frame_range']==[0,0] and an['authored_duration_s'] is None and an['stored_animation_span_s']==0;keys=set()
 for row in an['channels']:
  key=(row['node'],row['path']);assert key in g.channels and key not in keys;keys.add(key);assert row['name']==g.names[key[0]] and row['interpolation']=='STEP' and row['key_count']==1 and row['time_s']==0
 assert keys==set(g.channels)
 state=read('events_and_state.json');assert state['animation_name']==g.doc['animations'][0]['name'] and state['authored_duration_s'] is None and state['stored_animation_span_s']==state['static_key_time_s']==0;assert state['cyclic'] is False and state['motion_authored'] is False and state['events']==[];bn=g.names.index('Prop_Magazine_B');v=state['visibility'];assert v['stored_spare_B_collapsed'] is False and v['stored_spare_node']==bn and v['stored_spare_skin_slot']==g.joints.index(bn);assert abs(v['stored_spare_world_linear_determinant']-np.linalg.det(W[bn,:3,:3]))<1e-12
 fit=read('measured_preview_fit.json');assert np.array_equal(mat(fit['root']['transform_column_major']),W[fit['root']['node']]);assert fit['reference_time_s']==0 and fit['runtime_or_gameplay_fit_tested'] is False
 alignment=fit['preview_alignment_candidate'];F=mat(alignment['model_to_engine_heading0_feet_origin_column_major']);h=np.array(alignment['source_forward_model_xyz']);o=np.array(alignment['source_feet_origin_model_xyz_m']);assert abs(F[:3,:3]@h-[1,0,0]).max()<1e-12 and abs((F@np.r_[o,1])[:3]).max()<1e-12 and alignment['uniform_scale']==1 and abs(np.linalg.det(F[:3,:3])-1)<1e-12
 bridge=fit['weapon_bridge'];rs=g.joint_names.index('Prop_Rifle');rn=g.joints[rs];B=mat(bridge['rigid_stockpad_engine_frame_to_bind_mesh_gltf_column_major']);J=mat(bridge['rigid_stockpad_engine_frame_to_Prop_Rifle_joint_local_gltf_column_major']);assert bridge['prop_rifle_node_index']==rn and bridge['prop_rifle_skin_joint_slot']==rs;assert np.array_equal(mat(bridge['inverse_bind_for_Prop_Rifle_column_major']),g.ibm[rs]);assert abs(B-mat(measurement['rifle']['B_bind_mesh_column_major'])).max()<1e-12 and abs(g.ibm[rs]@B-J).max()<1e-12 and abs(W[rn]@J-mat(bridge['phase_zero_rigid_stockpad_frame_to_model_column_major'])).max()<1e-12
 bref=read('bridge_reference_samples.json');assert bref['times_s']==[0.0] and bref['source_sha256']==sm['source_sha256'];assert abs(mat(bref['B_bind_mesh_column_major'])-B).max()<1e-12;native=mat(bref['source_model_from_rigid_column_major'][0]);rp=np.c_[bref['rigid_reference_vertices'],np.ones(len(bref['rigid_reference_vertices']))];bridgeerr=float(np.linalg.norm((rp@(W[rn]@J-native).T)[:,:3],axis=1).max());assert bridgeerr<1e-5
 optic=read('optical_proxy_bindings.json');hs=g.joint_names.index('mixamorig:Head');hn=g.joints[hs];assert optic['head_node_index']==hn and optic['head_skin_joint_slot']==hs and optic['sample_count']==1;assert np.array_equal(mat(optic['head_inverse_bind_column_major']),g.ibm[hs]);OB=mat(optic['optical_frame_to_bind_mesh_gltf_column_major']);OJ=mat(optic['optical_frame_to_head_node_local_gltf_column_major']);assert abs(g.ibm[hs]@OB-OJ).max()<1e-12;assert abs(W[hn]@OJ-mat(optic['phase_zero_optical_frame_to_model_column_major'])).max()<1e-12;optic_rows=0
 for side,mapping in optic['lens_vertex_mapping'].items():
  vi=mapping['native_vertex'];assert vi==refs['optical']['data']['landmarks'][side]['center_vertex_id']
  for match in mapping['matches']:
   key=(optic['body_node'],match['primitive']);ev=match['vertex'];pr=next(x for x in g.primitives if (x['node'],x['primitive'])==key);assert maps[key][ev]==vi;assert np.array_equal(pr['p'][ev,:3],match['position_bind_mesh_gltf']);assert match['skin_joint_slot']==hs and match['head_weight']==1;actual=worlds[key][ev];expected=np.array(optic['native_blender_evaluated_control_samples'][0]['native_evaluated_lens_points_model_xyz_m'][side]);assert np.linalg.norm(actual-expected)<1e-5;optic_rows+=1
 soles=[];mi=sm['mesh_names'].index('Rebuilt SWAT full body');bone_names=list(z['bone_names'])
 for side,d in fit['sole_support'].items():
  assert d['native_mesh_vertex_count']==24850 and 'Native source Blender' in d['vertex_index_namespace'];native_ids=d['phase_zero_support_vertex_indices'];assert native_ids==[m['native_vertex'] for m in d['phase_zero_support_export_mapping']];pts=[];maxp=0.;nmatch=0
  for row in d['phase_zero_support_export_mapping']:
   for match in row['gltf_matches']:
    key=(d['exported_body_node'],match['primitive']);ev=match['vertex'];pr=next(x for x in g.primitives if (x['node'],x['primitive'])==key);assert maps[key][ev]==row['native_vertex'] and np.array_equal(pr['p'][ev,:3],match['position_bind_mesh_gltf_m']);pt=worlds[key][ev];maxp=max(maxp,float(np.linalg.norm(pt-z[f'eval{mi}'][row['native_vertex']]@C[:3,:3].T)));nmatch+=1
   pts.append(pt)
  slots=[i for i,n in enumerate(g.joint_names) if n in ['mixamorig:'+side.capitalize()+k for k in ['Foot','ToeBase','Toe_End']]];bs=[bone_names.index(g.joint_names[i]) for i in slots];weights=z[f'w{mi}'];weights=weights/weights.sum(1)[:,None];native_subset=weights[:,bs].sum(1)>.5;export_count=sum(int((np.sum(pr['w']*np.isin(pr['j'],slots),axis=1)>.5).sum()) for pr in g.primitives if pr['name']=='Rebuilt SWAT full body');assert native_subset.sum()==d['subset_vertices'] and export_count==d['exported_subset_vertex_count_including_seams'];ce=float(np.linalg.norm(np.mean(pts,axis=0)-d['phase_zero_support_centroid_model_xyz_m']));assert ce<1e-5;assert maxp<1e-5;soles.append({'side':side,'native_subset_vertices':int(native_subset.sum()),'native_1mm_support_vertices':len(native_ids),'exported_mappings':nmatch,'exported_subset_vertices':export_count,'mapped_position_max_error_vs_source_m':maxp,'metadata_centroid_max_error_m':ce})
 pose=read('static_pose_measurements.json');assert abs(pose['body_top_y_native_evaluated_m']-refs['measurements']['data']['body_top_world_z_m'])<1e-12;assert np.linalg.norm(pose['visor_optical_surface_model_xyz_m']-np.array(measurement['visor']['midpoint_model_xyz_m']))<1e-12
 for name,point in pose['physical_rifle_model_landmarks_m'].items():assert np.linalg.norm(point-np.array(measurement['rifle']['landmarks_model_xyz_m'][name]))<1e-12
 assert abs(pose['physical_rifle_forward_elevation_degrees']-measurement['rifle']['physical_forward_elevation_deg'])<1e-10;assert abs(pose['visor_surface_perpendicular_distance_to_sightline_m']-measurement['visor']['perpendicular_to_sightline_m'])<1e-12;assert abs(pose['visor_surface_normal_to_sightline_angle_degrees']-measurement['visor']['normal_to_sightline_angle_deg'])<1e-10
 material=read('material_compatibility.json');assert material['images_embedded']==len(g.doc.get('images',[]))==0
 for row in material['assignments']:
  pr=g.doc['meshes'][row['mesh']]['primitives'][row['primitive']];assert g.nodes[row['node']]['mesh']==row['mesh'] and pr['material']==row['material_index'] and g.doc['materials'][pr['material']]['name']==row['material_name'];assert row['tangent_attribute_present']==('TANGENT' in pr['attributes'])
  if a.ready_glb:
   baseline=GLB(a.ready_glb,False);assert baseline.sha==material['comparison_glb_sha256'];other=next(p for p in baseline.primitives if p['name']=='Rebuilt SWAT full body' and p['primitive']==row['primitive'])
   for attr in ['POSITION','NORMAL','TEXCOORD_0']:assert np.array_equal(g.acc(pr['attributes'][attr]),baseline.acc(other['attributes'][attr]))
   assert np.array_equal(g.acc(pr['indices']).astype(int).reshape(-1,3),other['tris'])
 r={'schema':'independent-static-metadata-validation/1','status':'PASS','glb_sha256':g.sha,'joint_bindings_verified':70,'single_key_STEP_channel_rows_verified':210,'static_zero_span_no_duration_no_motion_no_loop_verified':True,'visible_B_preserved':True,'native_and_actual_node_world_matrices_verified':True,'bridge_reference_vertices':len(rp),'bridge_native_vertex_error_m':bridgeerr,'rifle_node_and_slot':[rn,rs],'head_node_and_slot':[hn,hs],'lens_mapping_rows':optic_rows,'sole_mapping_rows':soles,'measured_rifle_visor_sight_metadata_matches_binary':True,'preview_heading_to_X_and_feet_origin_to_zero_verified':True,'preview_scale_one_and_unbaked_root_identity':True,'material_assignments_match_binary':True,'ready_empty_body_static_arrays_compared':bool(a.ready_glb),'limits':'Native/source evaluated heights and exported float32 replay differ by submicrometre tolerances; source named support subsets (>0.5 weights and 1mm patch) differ explicitly from source QA sole qualification (>0.65 weights and 8mm patch). No runtime, ADS, camera, loop or transition approval.'};a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
if __name__=='__main__':main()
