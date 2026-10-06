"""Portable independent preview, source pace, events and collapsed-prop metadata audit."""
import argparse,json,hashlib
from pathlib import Path
import numpy as np
from replay_unit import GLB,C
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--package',required=True,type=Path);ap.add_argument('--captures',required=True,type=Path);ap.add_argument('--output',required=True,type=Path);a=ap.parse_args();p=a.package;g=GLB(p/'walk_backward_shared_ready_n_c1_loop_a.glb');fit=json.loads((p/'measured_preview_fit.json').read_text());events=json.loads((p/'events_and_cycle.json').read_text());hand=json.loads((p/'SOURCE_HANDOFF.json').read_text());se=json.loads((p/'SOURCE_EVENTS.json').read_text());s=np.load(a.captures/'source.npz');m=json.loads((a.captures/'capture_manifest.json').read_text());maps=np.load(a.captures/'vertex_correspondence.npz')
mat=lambda x:np.array(x).reshape(4,4).T
assert fit['glb_sha256']==g.sha and not fit['runtime_or_gameplay_fit_tested']
assert events['events']==se['events']==[] and events['duration_s']==se['duration_s']==1
assert events['animation_name']==g.doc['animations'][0]['name']==hand['action'] and events['source_frame_range']==[0,30] and events['source_fps']==30
assert events['repair_windows_s']==[[0,.125],[.875,1]] and events['preserved_center_s']==[.125,.875] and not events['runtime_loop_quality_approved']
assert events['source_pace']['nominal_speed_m_s']==hand['nominal_source_pace_m_s'] and events['source_pace']['original_source_fps']==60
travel=fit['source_travel_reference'];assert travel['source_measurement_sha256']==hashlib.sha256((p/'SOURCE_HANDOFF.json').read_bytes()).hexdigest()
v=np.array(hand['native_world_travel_m']);assert np.array_equal(v,travel['source_displacement_blender_xyz_m']);assert np.array_equal(C[:3,:3]@v,travel['source_displacement_gltf_xyz_m'])
root=fit['root'];assert g.nodes[root['node']]['name']==root['name'] and not root['basis_change_applied_to_asset'];assert np.array_equal(mat(root['transform_column_major']),np.eye(4))
preview=fit['preview_alignment_candidate'];M=mat(preview['model_to_engine_heading0_feet_origin_column_major']);feet=np.array(preview['source_feet_origin_model_xyz_m']);forward=np.array(preview['source_forward_model_xyz']);assert np.linalg.norm(M[:3,:3]@forward-[1,0,0])<1e-12 and np.linalg.norm((M@np.r_[feet,1])[:3])<1e-12 and abs(np.linalg.det(M[:3,:3])-1)<1e-12
assert np.linalg.norm(M[:3,:3]@(C[:3,:3]@v)-travel['source_displacement_after_preview_alignment_engine_xyz_m'])<1e-12
Bnode=g.names.index('Prop_Magazine_B');Bslot=g.joints.index(Bnode);collapsed=events['visibility']['collapsed_prop'];assert collapsed['joint_node']==Bnode and collapsed['skin_joint_slot']==Bslot and collapsed['scale']==[0,0,0]
assert all(np.count_nonzero(v)==0 for (node,path),(t,v) in g.channels.items() if node==Bnode and path=='scale')
W,_,_=g.world([0]);mi=m['source']['mesh_names'].index('Rebuilt SWAT full body');sole=[]
for side,row in fit['sole_support'].items():
    ids=row['phase_zero_support_vertex_indices'];assert len(ids)==len(set(ids))
    native=s[f'eval{mi}'][0,ids]@C[:3,:3].T;err=float(np.linalg.norm(native.mean(0)-row['phase_zero_support_centroid_model_xyz_m']));assert err<2e-6
    count=0
    for mapping in row['phase_zero_support_export_mapping']:
        assert mapping['native_vertex'] in ids
        for match in mapping['gltf_matches']:
            pr=next(pr for pr in g.primitives if pr['name']=='Rebuilt SWAT full body' and pr['primitive']==match['primitive']);vi=match['vertex'];assert maps[f'{mi}_{pr["primitive"]}'][vi]==mapping['native_vertex'];assert np.array_equal(pr['p'][vi,:3],match['position_bind_mesh_gltf_m']);count+=1
    sole.append({'side':side,'unique_native_support_vertices':len(ids),'exported_mapping_rows':count,'source_control_centroid_error_m':err})
bridge=fit['weapon_bridge'];slot=g.joint_names.index('Prop_Rifle');assert bridge['prop_rifle_node_index']==g.joints[slot] and bridge['prop_rifle_skin_joint_slot']==slot
BW=mat(bridge['rigid_stockpad_engine_frame_to_bind_mesh_gltf_column_major']);WW,_,_=g.world(np.linspace(0,1,961));physical=WW[:,slot]@g.ibm[slot]@BW;fwd=physical[:,:3,0];elev=np.degrees(np.arctan2(fwd[:,1],np.linalg.norm(fwd[:,[0,2]],axis=1)));ref=fit['rifle_elevation_degrees_sampled'];assert max(abs(np.array([elev.min(),elev.max()])-ref))<.001
report={'status':'PASS','glb_sha256':g.sha,'duration_s':1,'authored_events':[],'changed_source_curves':604,'root_identity_and_no_baked_preview_basis':True,'unique_native_sole_support_remap':sole,'correct_rifle_elevation_deg_at_961_phases':[float(elev.min()),float(elev.max())],'backward_source_horizontal_travel_after_preview_alignment_engine_xyz_m':travel['source_displacement_after_preview_alignment_engine_xyz_m'],'nominal_source_pace_m_s':hand['nominal_source_pace_m_s'],'zero_B_node':Bnode,'zero_B_skin_slot':Bslot,'qualification':'Source gait and raised Ready retained; preview alignment is unbaked. No runtime movement-speed, camera, ADS or hand/grip override approval.'}
a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
