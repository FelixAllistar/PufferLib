import argparse,json,numpy as np
from pathlib import Path
from collections import Counter
from replay_f import GLB,C,sha,facekey
ap=argparse.ArgumentParser();ap.add_argument('--glb',type=Path,required=True);ap.add_argument('--capture',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();p=a.capture.parent;g=GLB(a.glb);meta=json.loads(Path(str(a.capture)+'.json').read_text());z=dict(np.load(str(a.capture)+'.npz'));z={k:np.moveaxis(v,-1,0) if meta.get('time_axis_last') and ('_eval' in k or k.endswith('_bones')) else v for k,v in z.items()};assert meta['source_sha256']==g.sha and meta['capture_npz_sha256']==sha(str(a.capture)+'.npz');from compact_reference import reconstruct;z,compact_report=reconstruct(meta,z);rows=[];mapnames={};order=[list(z['bone_names']).index(n) for n in g.joint_names]
for name in meta['mesh_names']:
 mi=meta['mesh_names'].index(name);prs=[pr for pr in g.primitives if pr['name']==name];bind=np.concatenate([pr['p'][:,:3] for pr in prs]);actual=z[f'p{mi}']@C[:3,:3].T;assert bind.shape==actual.shape;be=float(np.linalg.norm(bind-actual,axis=1).max());assert be<2e-6
 gw=[];tris=[];offset=0
 for pr in prs:
  ww=np.zeros((len(pr['p']),72))
  for k in range(pr['j'].shape[1]):np.add.at(ww,(np.arange(len(ww)),pr['j'][:,k]),pr['w'][:,k])
  gw.append(ww);tris.extend(pr['tris']+offset);offset+=len(pr['p'])
 w=z[f'w{mi}'][:,order];w/=w.sum(1)[:,None];we=float(abs(w-np.concatenate(gw)).max());assert we<1e-7
 assert Counter(facekey(t) for t in tris)==Counter(facekey(t) for t in z[f'tris{mi}'])
 rows.append({'name':name,'vertices':len(bind),'bind_max_error_m':be,'normalized_weight_max_error':we,'oriented_topology_exact':True});mapnames[name]=(mi,prs)
ani=[]
for label in ['neutral','motion']:
 wanted=meta['actions'][label]['name'];matches=[i for i,(n,ch) in enumerate(g.animations) if n in wanted];assert len(matches)==1;ai=matches[0];errs={n:0. for n in meta['mesh_names']};bone_origin=0.
 for ti,t in enumerate(z[label+'_times']):
  W=g.world(ai,float(t));expected=z[label+'_bones'][ti,order,:3,3]@C[:3,:3].T;bone_origin=max(bone_origin,float(np.linalg.norm(W[g.joints,:3,3]-expected,axis=1).max()))
  for name,(mi,prs) in mapnames.items():
   pred=np.concatenate([g.skin(W,pr) for pr in prs]);actual=z[f'{label}_eval{mi}'][ti]@C[:3,:3].T;errs[name]=max(errs[name],float(np.linalg.norm(pred-actual,axis=1).max()))
 assert max(errs.values())<1e-5,(label,errs);assert bone_origin<1e-5
 ani.append({'label':label,'name':g.animations[ai][0],'samples':len(z[label+'_times']),'max_joint_origin_error_m':bone_origin,'all_vertex_max_error_m_by_mesh':errs})
r={'schema':'independent-upper-gear-f-stock-reimport/1','status':'PASS','reference_mode':compact_report if compact_report else 'Direct stock-Blender-evaluated vertices at every sampled time','glb_sha256':g.sha,'capture_npz_sha256':meta['capture_npz_sha256'],'blender_version':meta['blender_version'],'source_hash_preserved':meta['read_only_source_hash_preserved'],'scope':'Stock Blender glTF import; all exported vertices at neutral and 361 movement times using the explicitly reported reference mode; normalized all-influence weights and oriented topology. Import display bone axes are not asserted to preserve native bone basis.','meshes':rows,'animations':ani};a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
