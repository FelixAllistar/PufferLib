"""Compare actual GLB skinning against pinned native and stock-reimport captures."""
import hashlib,json
from collections import Counter
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
from gltf_math import GLB
r=Path(__file__).resolve().parent;p=r/'package';g=GLB(p/'swat_upper_gear_remake_f_v1.glb');doc=g.doc;skin=doc['skins'][0];joints=skin['joints'];ibm=g.acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1).astype(float);C=np.array([[1,0,0,0],[0,0,-1,0],[0,1,0,0],[0,0,0,1.]])
def norm(w):return w/w.sum(1)[:,None]
def match(p,w,sp,sw):
 dist,idx=cKDTree(sp).query(p,k=min(16,len(sp)));e=np.max(abs(w[:,None,:]-sw[idx]),axis=2);pick=np.argmin(dist+e,axis=1);ids=idx[np.arange(len(p)),pick];return ids,float(np.linalg.norm(p-sp[ids],axis=1).max()),float(abs(w-sw[ids]).max())
def triangles(t,indices):
 def canonical(face):
  a,b,c=map(int,face);return min((a,b,c),(b,c,a),(c,a,b))
 return Counter(canonical(f)for f in indices[t])
def skin_points(pos,w,D):
 out=np.zeros((len(D),len(pos),3));w=norm(w)
 for j in range(w.shape[1]):
  vi=np.flatnonzero(w[:,j]>0)
  if len(vi):out[:,vi]+=(np.einsum('nij,vj->nvi',D[:,j,:3,:3],pos[vi])+D[:,j,None,:3,3])*w[vi,j][None,:,None]
 return out
results=[]
for label,anim in zip(['static','movement'],doc['animations']):
 src=np.load(r/f'work/native_{label}.npz');imp=np.load(r/f'work/reimport_{label}.npz');meta=json.loads((r/f'work/native_{label}.json').read_text());imeta=json.loads((r/f'work/reimport_{label}.json').read_text());times=src['times'];assert np.array_equal(times,imp['times']);bn=list(src['bone_names']);bi={n:i for i,n in enumerate(bn)};order=np.array([bi[doc['nodes'][n]['name']]for n in joints]);iorder=np.array([list(imp['bone_names']).index(n)for n in bn]);assert set(bn)==set(imp['bone_names'])
 W=g.world_at(anim,times);D=np.empty_like(src['deformation']);D[:,order]=C@W[:,joints]@ibm@C.T;idata=imp['deformation'][:,iorder]
 rows=[]
 for mi,name in enumerate(meta['mesh_names']):
  ni=next(i for i,n in enumerate(doc['nodes'])if n.get('name')==name);node=doc['nodes'][ni];positions=[];weights=[];faces=[];offset=0
  for primitive in doc['meshes'][node['mesh']]['primitives']:
   a=primitive['attributes'];pp=g.acc(a['POSITION']).astype(float)@C[:3,:3].T;w=np.zeros((len(pp),72))
   for suffix in sorted(int(k.split('_')[1])for k in a if k.startswith('WEIGHTS_')):
    ji=g.acc(a[f'JOINTS_{suffix}']).astype(int);ww=g.acc(a[f'WEIGHTS_{suffix}']).astype(float)
    for c in range(4):np.add.at(w,(np.arange(len(pp)),order[ji[:,c]]),ww[:,c])
   positions.append(pp);weights.append(w);faces.append(g.acc(primitive['indices']).astype(int).reshape(-1,3)+offset);offset+=len(pp)
  gp=np.concatenate(positions);gw=np.concatenate(weights);gt=np.concatenate(faces);sp=src[f'p{mi}'];sw=src[f'w{mi}'];st=src[f'tris{mi}'];ii=imeta['mesh_names'].index(name);ip=imp[f'p{ii}'];iw=imp[f'w{ii}'][:,iorder]
  ids,pe,we=match(gp,norm(gw),sp,norm(sw));imported,ipe,iwe=match(ip,norm(iw),sp,norm(sw));features={};canonical=[];normalized_source=norm(sw)
  for j in range(len(sp)):
   key=tuple(np.round(sp[j],7))+tuple(np.round(normalized_source[j],7));canonical.append(features.setdefault(key,len(features)))
  canonical=np.array(canonical);topology=triangles(gt,canonical[ids])==triangles(st,canonical);assert topology and pe<1e-7 and we<1e-6 and ipe<1e-6 and iwe<1e-6
  max_native=max_import=max_direct=max_reimport_direct=0.;bytime=[]
  for start in range(0,len(times),16):
   end=min(start+16,len(times));native=skin_points(sp,sw,src['deformation'][start:end]);actual=skin_points(gp,gw,D[start:end]);reimport=skin_points(ip,iw,idata[start:end]);err=np.linalg.norm(actual-native[:,ids],axis=2);max_native=max(max_native,float(err.max()));bytime.extend(err.max(1).tolist());max_import=max(max_import,float(np.linalg.norm(reimport-native[:,imported],axis=2).max()))
   for ci in meta['control_indices']:
    if start<=ci<end:
     max_direct=max(max_direct,float(np.linalg.norm(actual[ci-start]-src[f'eval_{ci}_{mi}'][ids],axis=1).max()));max_reimport_direct=max(max_reimport_direct,float(np.linalg.norm(reimport[ci-start]-imp[f'eval_{ci}_{ii}'],axis=1).max()))
  rows.append({'name':name,'source_vertices':len(sp),'source_triangles':len(st),'exported_vertices_with_seams':len(gp),'rendered_source_vertices':len(set(st.ravel())),'canonical_oriented_triangle_multiset_preserved':topology,'bind_position_max_error_m':pe,'normalized_weight_max_error':we,'native_max_influences':int((sw>0).sum(1).max()),'exported_max_influences':int((gw>0).sum(1).max()),'exported_vertices_over_four_weights':int(((gw>0).sum(1)>4).sum()),'raw_source_weight_sum_range':[float(sw.sum(1).min()),float(sw.sum(1).max())],'all_samples_source_to_GLTF_max_m':max_native,'all_samples_source_to_stock_reimport_max_m':max_import,'actual_evaluated_control_to_GLTF_max_m':max_direct,'stock_skin_formula_vs_evaluated_controls_max_m':max_reimport_direct,'per_sample_source_to_GLTF_max_m':bytime})
  print(label,name,max_native,max_import,flush=True)
 result={'label':label,'animation':anim['name'],'source_sha256':meta['source_sha256'],'samples':len(times),'times_s':times.tolist(),'controls':len(meta['control_indices']),'meshes':rows,'world_deformation_max_element_error':float(abs(D-src['deformation']).max()),'source_to_GLTF_max_m':max(x['all_samples_source_to_GLTF_max_m']for x in rows),'source_to_stock_reimport_max_m':max(x['all_samples_source_to_stock_reimport_max_m']for x in rows),'direct_control_to_GLTF_max_m':max(x['actual_evaluated_control_to_GLTF_max_m']for x in rows)}
 assert max(result['source_to_GLTF_max_m'],result['source_to_stock_reimport_max_m'])<.0001;results.append(result)
report={'status':'PASS','glb_sha256':hashlib.sha256(g.raw).hexdigest(),'clips':results,'pass_threshold_m':.0001,'scope':'Full positive-influence skinning; static pose and original181 motion keys plus180 midkeys and2 endpoint diagnostics. Direct native and unmodified stock reimport evaluated controls. No broad motion, C1, collision, mounting, ADS or runtime approval.'}
(p/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('PARITY_PASS',[(x['label'],x['source_to_GLTF_max_m'],x['source_to_stock_reimport_max_m'])for x in results])
