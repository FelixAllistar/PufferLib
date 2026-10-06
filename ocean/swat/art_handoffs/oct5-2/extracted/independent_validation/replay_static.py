"""Portable independent glTF 2 binary/static STEP replay. Python 3 + NumPy only.
No exporter/producer imports. Normalized XYZW quaternion FK and all stored skin influences.
A single key at zero defines one static pose; no duration or motion is synthesized.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from collections import Counter
import numpy as np
C=np.array([[1.,0,0,0],[0,0,1,0],[0,-1,0,0],[0,0,0,1.]])
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def mat(v):return np.array(v,dtype=float).reshape(4,4).T
def trs(p,q,s):
 q=np.array(q,dtype=float);q/=np.linalg.norm(q);x,y,z,w=q
 R=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
 M=np.eye(4);M[:3,:3]=R*np.array(s)[None,:];M[:3,3]=p;return M
class GLB:
 def __init__(self,path,static=True):
  self.path=Path(path);raw=self.path.read_bytes();self.sha=sha(path);magic,ver,length=struct.unpack_from('<III',raw);assert magic==0x46546c67 and ver==2 and length==len(raw)
  chunks=[];off=12
  while off<len(raw):
   n,k=struct.unpack_from('<II',raw,off);assert n%4==0 and off+8+n<=len(raw);chunks.append((k,raw[off+8:off+8+n]));off+=8+n
  assert off==len(raw) and [k for k,b in chunks]==[0x4e4f534a,0x004e4942]
  self.doc=json.loads(chunks[0][1]);self.bin=chunks[1][1];d=self.doc;assert len(d['buffers'])==1 and 'uri' not in d['buffers'][0];assert 0<=len(self.bin)-d['buffers'][0]['byteLength']<=3
  self.cache={};self.nodes=d['nodes'];self.names=[n.get('name',str(i)) for i,n in enumerate(self.nodes)];self.parents={}
  for i,n in enumerate(self.nodes):
   for ch in n.get('children',[]):assert ch not in self.parents and 0<=ch<len(self.nodes);self.parents[ch]=i
  for i in range(len(self.nodes)):
   seen=set()
   while i in self.parents:assert i not in seen;seen.add(i);i=self.parents[i]
  for i in range(len(d['accessors'])):self.acc(i)
  assert len(d['skins'])==1;self.joints=d['skins'][0]['joints'];assert len(set(self.joints))==len(self.joints)==70;self.joint_names=[self.names[i] for i in self.joints];assert len(set(self.joint_names))==70
  self.ibm=self.acc(d['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1);assert self.ibm.shape==(70,4,4)
  self.channels={};self.primitives=[];self.static=static
  if static:
   assert len(d['animations'])==1;an=d['animations'][0];norms=[]
   for ch in an['channels']:
    key=(ch['target']['node'],ch['target']['path']);assert key not in self.channels
    s=an['samplers'][ch['sampler']];assert s['interpolation']=='STEP';t=self.acc(s['input']);v=self.acc(s['output']);a=d['accessors'][s['input']];b=d['accessors'][s['output']]
    assert a['type']=='SCALAR' and a['componentType']==5126 and b['componentType']==5126;assert b['type']==('VEC4' if key[1]=='rotation' else 'VEC3');assert t.shape==(1,1) and t[0,0]==0 and len(v)==1
    self.channels[key]=v[0]
    if key[1]=='rotation':norms.append(abs(np.linalg.norm(v[0])-1))
   assert set(self.channels)=={(j,k) for j in self.joints for k in ['translation','rotation','scale']}
   self.contract={'channels':len(self.channels),'samplers':len(an['samplers']),'interpolation':'STEP','keys_per_channel':1,'only_authored_time_s':0.0,'stored_animation_span_s':0.0,'authored_duration_s':None,'quaternion_norm_residual_max':max(norms)};assert max(norms)<1e-6
  for ni,n in enumerate(self.nodes):
   if 'mesh' not in n:continue
   assert n['skin']==0
   for pi,p in enumerate(d['meshes'][n['mesh']]['primitives']):
    a=p['attributes'];pos=self.acc(a['POSITION']);assert all(len(self.acc(i))==len(pos) for i in a.values());assert p.get('mode',4)==4
    sets=sorted(int(k[7:]) for k in a if k.startswith('JOINTS_'));assert sets==list(range(len(sets))) and sets==sorted(int(k[8:]) for k in a if k.startswith('WEIGHTS_'))
    j=np.concatenate([self.acc(a['JOINTS_'+str(k)]) for k in sets],1).astype(int);w=np.concatenate([self.acc(a['WEIGHTS_'+str(k)]) for k in sets],1);s=w.sum(1)
    assert np.all(w>=0) and np.all(s>0) and j.min()>=0 and j.max()<70
    ix=self.acc(p['indices']).ravel().astype(int);assert len(ix)%3==0 and ix.min()>=0 and ix.max()<len(pos)
    self.primitives.append({'node':ni,'name':n['name'],'primitive':pi,'p':np.c_[pos,np.ones(len(pos))],'j':j,'w':w/s[:,None],'weight_sum_error':float(abs(s-1).max()),'tris':ix.reshape(-1,3),'attributes':a,'material':p.get('material')})
 def acc(self,i):
  if i in self.cache:return self.cache[i]
  a=self.doc['accessors'][i];assert 'sparse' not in a;v=self.doc['bufferViews'][a['bufferView']];assert v.get('buffer',0)==0
  dt=np.dtype({5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]);n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
  vstart=v.get('byteOffset',0);offset=a.get('byteOffset',0);stride=v.get('byteStride',dt.itemsize*n);assert vstart>=0 and vstart+v['byteLength']<=self.doc['buffers'][0]['byteLength'];assert stride>=dt.itemsize*n and stride%dt.itemsize==0;assert offset>=0 and (vstart+offset)%dt.itemsize==0;assert a['count']>0 and offset+(a['count']-1)*stride+dt.itemsize*n<=v['byteLength']
  r=np.ndarray((a['count'],n),dtype=dt,buffer=self.bin,offset=vstart+offset,strides=(stride,dt.itemsize)).astype(float)
  if a.get('normalized'):
   ct=a['componentType'];assert ct in [5120,5121,5122,5123];r/= {5120:127,5121:255,5122:32767,5123:65535}[ct]
   if ct in [5120,5122]:r=np.maximum(r,-1)
  assert np.isfinite(r).all()
  if 'min' in a:assert np.allclose(r.min(0),a['min'],atol=1e-7,rtol=1e-6)
  if 'max' in a:assert np.allclose(r.max(0),a['max'],atol=1e-7,rtol=1e-6)
  self.cache[i]=r;return r
 def world(self):
  assert self.static;local=[];world={}
  for i,n in enumerate(self.nodes):
   if 'matrix' in n:assert not any(k[0]==i for k in self.channels);m=mat(n['matrix'])
   else:m=trs(*[self.channels.get((i,k),n.get(k,default)) for k,default in [('translation',[0,0,0]),('rotation',[0,0,0,1]),('scale',[1,1,1])]])
   local.append(m)
  def get(i):
   if i not in world:world[i]=(get(self.parents[i])@local[i] if i in self.parents else local[i])
   return world[i]
  return np.array([get(i) for i in range(len(self.nodes))])
 def skin(self,W,pr):
  X=W[self.joints]@self.ibm;out=np.zeros((len(pr['p']),3))
  for k in range(pr['j'].shape[1]):out+=np.einsum('vij,vj->vi',X[pr['j'][:,k],:3],pr['p'])*pr['w'][:,k,None]
  return out

def identity(g,b):
 checks={k:g.doc.get(k)==b.doc.get(k) for k in ['nodes','skins','meshes','materials','scenes','scene','images','textures','samplers']};ids=set()
 for m in b.doc['meshes']:
  for p in m['primitives']:ids.update(p['attributes'].values());ids.add(p['indices'])
 ids.update(s['inverseBindMatrices'] for s in b.doc['skins']);checks['all_static_accessor_arrays_exact']=all(np.array_equal(g.acc(i),b.acc(i)) for i in ids);checks['all_static_accessor_definitions_exact']=all(g.doc['accessors'][i]==b.doc['accessors'][i] for i in ids);checks['embedded_image_bytes_exact']=all(g.bin[g.doc['bufferViews'][i['bufferView']].get('byteOffset',0):g.doc['bufferViews'][i['bufferView']].get('byteOffset',0)+g.doc['bufferViews'][i['bufferView']]['byteLength']]==b.bin[b.doc['bufferViews'][i['bufferView']].get('byteOffset',0):b.doc['bufferViews'][i['bufferView']].get('byteOffset',0)+b.doc['bufferViews'][i['bufferView']]['byteLength']] for i in b.doc.get('images',[]));assert all(checks.values()),checks
 return {'status':'PASS','reference_glb_sha256':b.sha,'static_accessor_count':len(ids),'checks':checks}
def facekey(f):a,b,c=map(int,f);return min((a,b,c),(b,c,a),(c,a,b))
def source_parity(g,prefix):
 meta=json.loads(Path(str(prefix)+'.json').read_text());z=np.load(str(prefix)+'.npz');assert sha(str(prefix)+'.npz')==meta['capture_npz_sha256'];assert meta['source_sha256']=='b316df0d4c4af329dffe5f5330c502da14d17c0d3af77eecd257262a435445d9';assert meta['action']==g.doc['animations'][0]['name'];assert meta['curve_count']==meta['key_count']==700 and meta['action_frame_range']==[0,0] and all(len(c['keys'])==1 and c['keys'][0][0]==0 for c in meta['curves'])
 W=g.world();order=[list(z['bone_names']).index(n) for n in g.joint_names];expected=C@z['bone_world'][order];joint_error=float(abs(W[g.joints]-expected).max());assert joint_error<1e-5
 rows=[];maps={};worlds={};topology={};weightmax=0
 for pr in g.primitives:
  mi=meta['mesh_names'].index(pr['name']);p=z[f'p{mi}']@C[:3,:3].T;w=z[f'w{mi}'][:,order];w/=w.sum(1)[:,None];gw=np.zeros((len(pr['p']),70))
  for k in range(pr['j'].shape[1]):np.add.at(gw,(np.arange(len(gw)),pr['j'][:,k]),pr['w'][:,k])
  lookup={}
  for vi,pos in enumerate(p):lookup.setdefault(tuple(pos),[]).append(vi)
  ids=[];ambig=0
  for v,pos in enumerate(pr['p'][:,:3]):
   possible=lookup.get(tuple(pos),[]);valid=[vi for vi in possible if abs(w[vi]-gw[v]).max()<1e-7];assert valid,(pr['name'],pr['primitive'],v);ids.append(valid[0]);ambig+=len(valid)>1
  ids=np.array(ids);errw=float(abs(w[ids]-gw).max());P=g.skin(W,pr);e=np.linalg.norm(P-z[f'eval{mi}'][ids]@C[:3,:3].T,axis=1);assert e.max()<1e-5
  key=(pr['node'],pr['primitive']);maps[key]=ids;worlds[key]=P;topology.setdefault(pr['name'],Counter()).update(facekey(f) for f in ids[pr['tris']]);rows.append({'name':pr['name'],'primitive':pr['primitive'],'exported_vertices':len(ids),'maximum_influences':int(np.count_nonzero(pr['w'],axis=1).max()),'max_weight_error':errw,'max_position_error_m':float(e.max()),'rms_position_error_m':float(np.sqrt(np.mean(e*e))),'ambiguous_equivalent_bind_matches':ambig})
 for name,t in topology.items():assert t==Counter(facekey(f) for f in z[f'tris{meta["mesh_names"].index(name)}']),name
 return {'status':'PASS','source_sha256':meta['source_sha256'],'capture_npz_sha256':meta['capture_npz_sha256'],'bone_world_max_abs_error':joint_error,'every_oriented_source_triangle_preserved':True,'total_exported_vertices':sum(r['exported_vertices'] for r in rows),'meshes':rows},z,meta,maps,worlds

def native_identity(prefix,baseline):
 a=json.loads(Path(str(prefix)+'.json').read_text());b=json.loads(Path(str(baseline)+'.json').read_text());x=np.load(str(prefix)+'.npz');y=np.load(str(baseline)+'.npz');checks={'bone_names':a['bone_names']==b['bone_names'],'bone_parent_hierarchy':a['bone_parents']==b['bone_parents'],'bone_rest':np.array_equal(x['bone_rest'],y['bone_rest']),'rig_world':np.array_equal(x['rig_world'],y['rig_world'])};rows=[]
 for name in a['mesh_names']:
  i=a['mesh_names'].index(name);j=b['mesh_names'].index(name);am=a['meshes'][i];bm=b['meshes'][j];r={'name':name,'material_slots_exact':am['materials']==bm['materials'],'uv_names_exact':am['uv_names']==bm['uv_names']}
  for key in ['p','local_p','world','w','tris','mat','loop_vert','loop_normal']:r[key+'_exact']=np.array_equal(x[key+str(i)],y[key+str(j)])
  for k in range(len(am['uv_names'])):r[f'uv{k}_exact']=np.array_equal(x[f'uv{i}_{k}'],y[f'uv{j}_{k}'])
  assert all(v for k,v in r.items() if k!='name'),r;rows.append(r)
 assert all(checks.values()),checks
 return {'status':'PASS','native_baseline_sha256':b['source_sha256'],'checks':checks,'meshes':rows}

def reimport_parity(g,prefix,source_prefix):
 meta=json.loads(Path(str(prefix)+'.json').read_text());z=np.load(str(prefix)+'.npz');sm=json.loads(Path(str(source_prefix)+'.json').read_text());src=np.load(str(source_prefix)+'.npz');assert meta['source_sha256']==g.sha;assert sha(str(prefix)+'.npz')==meta['capture_npz_sha256'];assert meta['action_frame_range']==[0,0] and meta['curve_count']==meta['key_count']==700
 W=g.world();source,m,ms,maps,worlds=source_parity(g,source_prefix);rows=[]
 for name in meta['mesh_names']:
  mi=meta['mesh_names'].index(name);si=sm['mesh_names'].index(name);prs=[p for p in g.primitives if p['name']==name];p=np.concatenate([pr['p'][:,:3] for pr in prs]);q=z[f'p{mi}']@C[:3,:3].T;assert p.shape==q.shape;bind_error=float(np.linalg.norm(p-q,axis=1).max());assert bind_error<2e-6
  predicted=np.concatenate([g.skin(W,pr) for pr in prs]);actual=z[f'eval{mi}']@C[:3,:3].T;e=np.linalg.norm(predicted-actual,axis=1);assert e.max()<1e-5
  ids=np.concatenate([maps[(pr['node'],pr['primitive'])] for pr in prs]);native=src[f'eval{si}'][ids]@C[:3,:3].T;ne=np.linalg.norm(actual-native,axis=1);assert ne.max()<1e-5
  w=z[f'w{mi}'][:,[list(z['bone_names']).index(n) for n in g.joint_names]];w/=w.sum(1)[:,None];gw=[];triangles=[];offset=0
  for pr in prs:
   ww=np.zeros((len(pr['p']),70))
   for k in range(pr['j'].shape[1]):np.add.at(ww,(np.arange(len(ww)),pr['j'][:,k]),pr['w'][:,k])
   gw.append(ww);triangles.extend(pr['tris']+offset);offset+=len(pr['p'])
  weight_error=float(abs(w-np.concatenate(gw)).max());assert weight_error<1e-7;assert Counter(facekey(f) for f in triangles)==Counter(facekey(f) for f in z[f'tris{mi}'])
  rows.append({'name':name,'vertices':len(p),'bind_position_max_error_m':bind_error,'actual_binary_to_stock_blender_evaluated_max_error_m':float(e.max()),'native_to_stock_blender_evaluated_max_error_m':float(ne.max()),'all_normalized_weight_max_error':weight_error,'oriented_index_topology_exact':True})
 poserror=max(float(np.linalg.norm(W[node,:3,3]-C[:3,:3]@z['bone_world'][list(z['bone_names']).index(g.names[node]),:3,3])) for node in g.joints)
 return {'status':'PASS','glb_sha256':g.sha,'blender_version':meta['blender_version'],'read_only_glb_hash_preserved':meta['read_only_source_hash_preserved'],'single_key_no_authored_duration_preserved':True,'bone_world_origin_max_error_m':poserror,'total_vertices':sum(r['vertices'] for r in rows),'meshes':rows,'scope':'Stock glTF import evaluated at frame0. All exported vertices, normalized influences and oriented topology checked. Blender may choose new display-bone bind frames; imported head/tail basis is not treated as a byte-preserved GLB joint frame.'}

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--glb',required=True,type=Path);ap.add_argument('--source-capture',type=Path);ap.add_argument('--baseline-glb',type=Path);ap.add_argument('--baseline-native-capture',type=Path);ap.add_argument('--reimport-capture',type=Path);ap.add_argument('--output',required=True,type=Path);args=ap.parse_args();g=GLB(args.glb);W=g.world()
 B=g.joints[g.joint_names.index('Prop_Magazine_B')];sv=np.linalg.svd(W[B,:3,:3],compute_uv=False);assert sv.min()>.99 and sv.max()<1.01
 r={'schema':'independent-static-step-validation/1','status':'PASS','glb_sha256':g.sha,'method':'Actual binary accessor validation, normalized-quaternion hierarchical FK, all-influence normalized linear blend skinning; no producer imports','static_contract':g.contract,'nodes':len(g.nodes),'skin_joints':len(g.joints),'accessors':len(g.doc['accessors']),'primitives':len(g.primitives),'prop_magazine_B':{'node':B,'skin_joint_slot':g.joints.index(B),'world_linear_determinant':float(np.linalg.det(W[B,:3,:3])),'world_linear_singular_values':sv.tolist(),'noncollapsed':True},'scene_roots':[{'node':i,'name':g.names[i],'world_matrix_column_major':W[i].T.ravel().tolist()} for i in g.doc['scenes'][g.doc.get('scene',0)]['nodes']]}
 if args.baseline_glb:r['static_identity']=identity(g,GLB(args.baseline_glb,False))
 if args.source_capture:r['source_parity']=source_parity(g,args.source_capture)[0]
 if args.baseline_native_capture:r['native_static_identity']=native_identity(args.source_capture,args.baseline_native_capture)
 if args.reimport_capture:r['stock_blender_reimport']=reimport_parity(g,args.reimport_capture,args.source_capture)
 args.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
if __name__=='__main__':main()
