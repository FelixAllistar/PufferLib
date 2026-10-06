"""Independent correspondence of F metadata to actual GLB bytes and pinned source capture."""
import argparse,json,numpy as np,math
from pathlib import Path
from replay_f import GLB,C,sha
ap=argparse.ArgumentParser();ap.add_argument('--package',type=Path,required=True);ap.add_argument('--source-capture',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();P=a.package;g=GLB(P/'swat_upper_gear_remake_f_v1.glb');meta=json.loads(Path(str(a.source_capture)+'.json').read_text());z=dict(np.load(str(a.source_capture)+'.npz'));z={k:np.moveaxis(v,-1,0) if meta.get('time_axis_last') and ('_eval' in k or k.endswith('_bones')) else v for k,v in z.items()};assert sha(str(a.source_capture)+'.npz')==meta['capture_npz_sha256']
names=['bindings.json','clips_and_state.json','geometry_material_changes.json','native_vertex_and_polygon_map.json','physical_and_optical_bindings.json','measured_attachment_poses.json'];d={n:json.loads((P/n).read_text()) for n in names};pins={n:sha(P/n) for n in names}
for n,v in d.items():
 if 'glb_sha256' in v:assert v['glb_sha256']==g.sha,n
bm=d['bindings.json'];assert len(bm['joint_map'])==72
for r in bm['joint_map']:
 j=r['skin_joint_slot'];node=g.joints[j];assert r['node']==node and r['name']==g.names[node] and r['parent_node']==g.parents.get(node);assert np.array_equal(np.array(r['inverse_bind_column_major']).reshape(4,4).T,g.ibm[j]);p=g.parents.get(node);assert r['parent_joint_slot']==(g.joints.index(p) if p in g.joints else None)
 for k,v in r['default_TRS'].items():assert v==g.nodes[node][k]
for name,r in bm['semantic_nodes'].items():assert g.names[r['node']]==name and g.joints[r['skin_joint_slot']]==r['node']
clips=d['clips_and_state.json']['clips'];assert len(clips)==2
for ai,r in enumerate(clips):
 name,ch=g.animations[ai];assert r['animation_index']==ai and r['animation_name']==name;tt=next(iter(ch.values()))[0];assert np.array_equal(np.array(r['key_times_s']),tt);assert r['TRS_channels']==len(ch) and all(v[2]==r['interpolation'] for v in ch.values());assert r['stored_span_s']==float(tt[-1]-tt[0]);assert r['events']==[] and r['looping_approved'] is False
 assert r['authored_duration_s']==(None if ai==0 else 3)
geo=d['geometry_material_changes.json'];assert geo['material_definitions']==g.doc['materials'];assert geo['textures_embedded']==len(g.doc.get('images',[]));inv={r['node']:r for r in geo['mesh_inventory']};assert len(inv)==9
for pr in g.primitives:
 r=inv[pr['node']];assert r['name']==pr['name'] and r['mesh']==g.nodes[pr['node']]['mesh'] and r['skin']==0;q=r['primitives'][pr['primitive']];assert q['vertices']==len(pr['p']) and q['triangles']==len(pr['tris']) and q['material_index']==pr['material'];assert q['material_name']==g.doc['materials'][pr['material']]['name'];counts=np.count_nonzero(pr['w'],axis=1);assert q['max_influences']==int(counts.max()) and q['vertices_above_four']==int(sum(counts>4));assert q['tangent_attribute_present']==('TANGENT' in pr['attributes'])
 if 'TEXCOORD_0' in pr['attributes']:
  uv=g.acc(pr['attributes']['TEXCOORD_0']);assert q['UV']=='TEXCOORD_0' and np.array_equal(q['UV_range'],[uv.min(0),uv.max(0)])
 else:assert q['UV'] is None
phy=d['physical_and_optical_bindings.json'];mat=lambda a:np.array(a).reshape(4,4).T
for label in ['rigid_rifle','optical_proxy']:
 r=phy[label];j=r['skin_slot'];assert g.joints[j]==r['node'];assert np.array_equal(mat(r['inverse_bind']),g.ibm[j]);assert abs(mat(r['J_joint_local'])-g.ibm[j]@mat(r['B_bind_mesh'])).max()<1e-12
opt=phy['optical_proxy'];bn=meta['bone_names'];mi=meta['mesh_names'].index('SWAT_Wearer');Fmap=d['native_vertex_and_polygon_map.json'];optpts=[];optic_match=[]
for side,native,fid in [('left',23263,21466),('right',23516,21719)]:
 assert Fmap['optical_original_native_ids'][side]==native and Fmap['optical_compacted_F_ids'][side]==fid and Fmap['native_vertex_map'][str(native)]==fid;r=opt['vertex_mapping'][side];assert r['original_native_vertex']==native and r['F_native_vertex']==fid;expect=z[f'p{mi}'][fid]@C[:3,:3].T;assert np.array_equal(z[f'w{mi}'][fid],np.eye(72)[bn.index('mixamorig:Head')]);assert r['gltf_matches']
 actual=[]
 for pr in g.primitives:
  if pr['node']!=opt['body_node']:continue
  for vi in np.flatnonzero(np.all(pr['p'][:,:3]==expect,axis=1)):
   actual.append((pr['primitive'],int(vi)));weight=sum(w for j,w in zip(pr['j'][vi],pr['w'][vi]) if j==opt['skin_slot']);assert weight==1.
 assert set(actual)=={(v['primitive'],v['vertex']) for v in r['gltf_matches']}
 for v in r['gltf_matches']:assert np.array_equal(v['bind_mesh_point'],expect) and v['head_weight']==1
 optpts.append(expect);optic_match.append({'side':side,'original_vertex':native,'F_vertex':fid,'gltf_vertices':actual})
assert np.array_equal(mat(opt['B_bind_mesh'])[:3,3],np.mean(optpts,axis=0))
for label in ['rigid_rifle','optical_proxy']:
 B=mat(phy[label]['B_bind_mesh']);assert abs(B[:3,:3].T@B[:3,:3]-np.eye(3)).max()<1e-6 and np.linalg.det(B[:3,:3])>0
measured=[];R=phy['rigid_rifle'];O=phy['optical_proxy'];lm=R['physical_landmarks_rigid_engine_xyz_m'];bodyprs=[p for p in g.primitives if p['name']=='SWAT_Wearer']
for r in d['measured_attachment_poses.json']['clips']:
 ai=r['animation_index'];assert g.animations[ai][0]==r['animation_name'];assert r['samples']==len(r['times_s'])==len(r['model_from_rigid_series'])==len(r['model_from_optical_series']);values={k:[] for k in r['ranges']};rerr=0.;oerr=0.;miderr=0.;formulaerr=0.
 for i,t in enumerate(r['times_s']):
  W=g.world(ai,t);G=W[R['node']]@g.ibm[R['skin_slot']]@mat(R['B_bind_mesh']);V=W[O['node']]@g.ibm[O['skin_slot']]@mat(O['B_bind_mesh']);rerr=max(rerr,float(abs(G-mat(r['model_from_rigid_series'][i])).max()));oerr=max(oerr,float(abs(V-mat(r['model_from_optical_series'][i])).max()));formulaerr=max(formulaerr,float(abs(G-W[R['node']]@mat(R['J_joint_local'])).max()),float(abs(V-W[O['node']]@mat(O['J_joint_local'])).max()))
  pts={n:(G@np.r_[p,1])[:3] for n,p in lm.items()};F=pts['front_sight']-pts['rear_sight'];F/=np.linalg.norm(F);v=V[:3,3]-pts['rear_sight'];perp=np.linalg.norm(v-F*np.dot(v,F));elev=math.degrees(math.asin(G[1,0]/np.linalg.norm(G[:3,0])));raw={'visor_surface_y_m':V[1,3],'physical_stock_y_m':pts['stock_contact'][1],'physical_muzzle_y_m':pts['muzzle'][1],'physical_rifle_elevation_deg':elev,'visor_surface_perpendicular_to_sightline_m':perp}
  for k in values:values[k].append(raw[k])
  D=W[O['node']]@g.ibm[O['skin_slot']];pactual=np.mean([(D@np.r_[p,1])[:3] for p in optpts],axis=0);miderr=max(miderr,float(np.linalg.norm(V[:3,3]-pactual)))
 assert rerr<1e-10 and oerr<1e-10 and formulaerr<1e-10 and miderr<1e-10
 errs={k:max(abs(min(v)-r['ranges'][k]['min']),abs(max(v)-r['ranges'][k]['max'])) for k,v in values.items()};assert max(errs.values())<1e-8,errs
 assert np.array_equal(r['static_model_from_rigid'],r['model_from_rigid_series'][0]) and np.array_equal(r['static_model_from_optical'],r['model_from_optical_series'][0])
 measured.append({'animation':r['animation_name'],'samples':r['samples'],'max_rifle_matrix_error':rerr,'max_optical_matrix_error':oerr,'max_equivalent_attachment_formula_error':formulaerr,'max_optic_midpoint_error_m':miderr,'range_residuals':errs})
report={'schema':'independent-upper-gear-f-metadata/1','status':'PASS','glb_sha256':g.sha,'metadata_sha256':pins,'joint_maps_and_parent_slots_exact':True,'clip_keys_spans_interpolation_exact':True,'mesh_material_UV_inventory_exact':True,'original_to_F_optical_remap_exact':optic_match,'matrix_layout_and_attachment_composition_checked':True,'measured_attachment_replay':measured,'limitations':['No eye/camera/ADS or gameplay claim','Original external rigid-rifle asset hash is referenced; separate asset bytes are not redistributed by this check','This metadata replay checks physical source landmark composition and midpoint proxy, not anatomical registration']};a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS',a.output)
