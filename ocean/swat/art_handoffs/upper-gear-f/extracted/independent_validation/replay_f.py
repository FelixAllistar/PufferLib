"""Independent actual-byte glTF 2 replay for upper-gear F. Python + NumPy.
Does not import producer exporter. Evaluates glTF quaternion SLERP and full-influence skinning.
"""
import argparse,json,hashlib,struct,math
from pathlib import Path
from collections import Counter
import numpy as np
C=np.array([[1.,0,0,0],[0,0,1,0],[0,-1,0,0],[0,0,0,1.]])
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def facekey(f):
 a,b,c=map(int,f);return min((a,b,c),(b,c,a),(c,a,b))
def trs(p,q,s):
 q=np.array(q,dtype=float);q/=np.linalg.norm(q);x,y,z,w=q
 R=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
 M=np.eye(4);M[:3,:3]=R*np.array(s)[None,:];M[:3,3]=p;return M
class GLB:
 def __init__(self,path):
  raw=Path(path).read_bytes();self.sha=sha(path);magic,ver,size=struct.unpack_from('<III',raw);assert (magic,ver,size)==(0x46546c67,2,len(raw));off=12;chunks=[]
  while off<len(raw):
   n,k=struct.unpack_from('<II',raw,off);assert n%4==0 and off+8+n<=len(raw);chunks.append((k,raw[off+8:off+8+n]));off+=8+n
  assert [k for k,v in chunks]==[0x4e4f534a,0x004e4942]
  self.doc=json.loads(chunks[0][1]);self.bin=chunks[1][1];d=self.doc;assert len(d['buffers'])==1 and 'uri' not in d['buffers'][0];assert 0<=len(self.bin)-d['buffers'][0]['byteLength']<=3
  self.cache={};self.nodes=d['nodes'];self.names=[n.get('name',str(i)) for i,n in enumerate(self.nodes)];self.parents={}
  for i,n in enumerate(self.nodes):
   for ch in n.get('children',[]):assert ch not in self.parents;self.parents[ch]=i
  for i in range(len(self.nodes)):
   seen=set()
   while i in self.parents:assert i not in seen;seen.add(i);i=self.parents[i]
  for i in range(len(d['accessors'])):self.acc(i)
  assert len(d['skins'])==1;self.joints=d['skins'][0]['joints'];assert len(set(self.joints))==72;self.joint_names=[self.names[i] for i in self.joints];assert len(set(self.joint_names))==72
  self.ibm=self.acc(d['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1);assert self.ibm.shape==(72,4,4)
  self.animations=[]
  for a in d.get('animations',[]):
   chs={}
   for c in a['channels']:
    key=(c['target']['node'],c['target']['path']);assert key not in chs and key[1] in ['translation','rotation','scale'];s=a['samplers'][c['sampler']];tt=self.acc(s['input']).ravel();vv=self.acc(s['output']);it=s.get('interpolation','LINEAR');assert it in ['STEP','LINEAR'];assert len(tt)==len(vv) and np.all(np.diff(tt)>0);chs[key]=(tt,vv,it)
   assert set(chs)=={(j,k) for j in self.joints for k in ['translation','rotation','scale']}
   self.animations.append((a.get('name'),chs))
  assert len(self.animations)==2
  self.primitives=[]
  for ni,n in enumerate(self.nodes):
   if 'mesh' not in n:continue
   assert n.get('skin')==0
   for pi,p in enumerate(d['meshes'][n['mesh']]['primitives']):
    a=p['attributes'];pos=self.acc(a['POSITION']);assert all(len(self.acc(ix))==len(pos) for ix in a.values());assert p.get('mode',4)==4
    sets=sorted(int(k[7:]) for k in a if k.startswith('JOINTS_'));assert sets==list(range(len(sets))) and sets==sorted(int(k[8:]) for k in a if k.startswith('WEIGHTS_'))
    j=np.concatenate([self.acc(a['JOINTS_'+str(k)]) for k in sets],1).astype(int);w=np.concatenate([self.acc(a['WEIGHTS_'+str(k)]) for k in sets],1);sw=w.sum(1);assert np.all(w>=0) and np.all(sw>0) and j.min()>=0 and j.max()<72
    ix=self.acc(p['indices']).ravel().astype(int);assert len(ix)%3==0 and ix.min()>=0 and ix.max()<len(pos)
    self.primitives.append({'node':ni,'name':n.get('name'),'primitive':pi,'p':np.c_[pos,np.ones(len(pos))],'j':j,'w':w/sw[:,None],'raw_w':w,'tris':ix.reshape(-1,3),'attributes':a,'material':p.get('material')})
 def acc(self,i):
  if i in self.cache:return self.cache[i]
  a=self.doc['accessors'][i];assert 'sparse' not in a;v=self.doc['bufferViews'][a['bufferView']];assert v.get('buffer',0)==0
  dt=np.dtype({5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]);n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']];vstart=v.get('byteOffset',0);offset=a.get('byteOffset',0);stride=v.get('byteStride',dt.itemsize*n)
  assert vstart>=0 and vstart+v['byteLength']<=self.doc['buffers'][0]['byteLength'];assert stride>=dt.itemsize*n and stride%dt.itemsize==0;assert offset>=0 and (vstart+offset)%dt.itemsize==0;assert a['count']>0 and offset+(a['count']-1)*stride+dt.itemsize*n<=v['byteLength']
  r=np.ndarray((a['count'],n),dtype=dt,buffer=self.bin,offset=vstart+offset,strides=(stride,dt.itemsize)).astype(float)
  if a.get('normalized'):
   ct=a['componentType'];assert ct in [5120,5121,5122,5123];r/={5120:127,5121:255,5122:32767,5123:65535}[ct]
   if ct in [5120,5122]:r=np.maximum(r,-1)
  assert np.isfinite(r).all()
  if 'min' in a:assert np.allclose(r.min(0),a['min'],atol=1e-7,rtol=1e-6)
  if 'max' in a:assert np.allclose(r.max(0),a['max'],atol=1e-7,rtol=1e-6)
  self.cache[i]=r;return r
 def world(self,ai=None,t=0):
  local=[];world={};chs={} if ai is None else self.animations[ai][1]
  def channel(i,k,default):
   if (i,k) not in chs:return self.nodes[i].get(k,default)
   times,v,it=chs[(i,k)]
   if t<=times[0]:return v[0]
   if t>=times[-1]:return v[-1]
   j=int(np.searchsorted(times,t,side='right'))-1
   if it=='STEP':return v[j]
   a=float((t-times[j])/(times[j+1]-times[j]));u=v[j];w=v[j+1]
   if k!='rotation':return (1-a)*u+a*w
   u=u/np.linalg.norm(u);w=w/np.linalg.norm(w);dot=float(np.dot(u,w))
   if dot<0:w=-w;dot=-dot
   if dot>0.9995:q=(1-a)*u+a*w;return q/np.linalg.norm(q)
   theta=math.acos(max(-1,min(1,dot)));return (math.sin((1-a)*theta)*u+math.sin(a*theta)*w)/math.sin(theta)
  for i,n in enumerate(self.nodes):
   if 'matrix' in n:assert not any(j==i for j,k in chs);m=np.array(n['matrix']).reshape(4,4).T
   else:m=trs(*[channel(i,k,d) for k,d in [('translation',[0,0,0]),('rotation',[0,0,0,1]),('scale',[1,1,1])]])
   local.append(m)
  def get(i):
   if i not in world:world[i]=get(self.parents[i])@local[i] if i in self.parents else local[i]
   return world[i]
  return np.array([get(i) for i in range(len(self.nodes))])
 def skin(self,W,pr):
  X=W[self.joints]@self.ibm;out=np.zeros((len(pr['p']),3))
  for k in range(pr['j'].shape[1]):out+=np.einsum('vij,vj->vi',X[pr['j'][:,k],:3],pr['p'])*pr['w'][:,k,None]
  return out

def validate(g,prefix):
 meta=json.loads(Path(str(prefix)+'.json').read_text());z=dict(np.load(str(prefix)+'.npz'));z={k:np.moveaxis(v,-1,0) if meta.get('time_axis_last') and ('_eval' in k or k.endswith('_bones')) else v for k,v in z.items()};assert sha(str(prefix)+'.npz')==meta['capture_npz_sha256'];from compact_reference import reconstruct;z,compact_report=reconstruct(meta,z);bn=list(z['bone_names']);order=[bn.index(n) for n in g.joint_names];assert len(order)==72
 expected_meshes=set(meta['mesh_names']);assert {p['name'] for p in g.primitives}==expected_meshes
 rows=[];maps={};topology={};uvmax=0.;normalmax=0.;weightmax=0.
 for pr in g.primitives:
  mi=meta['mesh_names'].index(pr['name']);p=z[f'p{mi}']@C[:3,:3].T;w=z[f'w{mi}'][:,order];w/=w.sum(1)[:,None];gw=np.zeros((len(pr['p']),72))
  for k in range(pr['j'].shape[1]):np.add.at(gw,(np.arange(len(gw)),pr['j'][:,k]),pr['w'][:,k])
  lookup={}
  for vi,pos in enumerate(p):lookup.setdefault(tuple(pos),[]).append(vi)
  ids=[];amb=0
  for v,pos in enumerate(pr['p'][:,:3]):
   possible=lookup.get(tuple(pos),[]);valid=[vi for vi in possible if abs(w[vi]-gw[v]).max()<1e-7];assert valid,(pr['name'],pr['primitive'],v,pos);ids.append(valid[0]);amb+=len(valid)>1
  ids=np.array(ids);err=float(abs(w[ids]-gw).max());weightmax=max(weightmax,err);maps[(pr['node'],pr['primitive'])]=(mi,ids);topology.setdefault(pr['name'],Counter()).update(facekey(f) for f in ids[pr['tris']])
  material_name=g.doc['materials'][pr['material']].get('name');slots=meta['meshes'][mi]['materials'];assert material_name in slots;matid=slots.index(material_name)
  tri_lookup={}
  for ti,vs in enumerate(z[f'tris{mi}']):tri_lookup.setdefault(facekey(vs),[]).append(ti)
  uv_err=0.;normal_err=0.
  for tri in pr['tris']:
   vids=ids[tri];possible=tri_lookup.get(facekey(vids),[]);possible=[i for i in possible if z[f'tri_mat{mi}'][i]==matid];assert possible,(pr['name'],vids,'triangle material')
   ti=possible[0];native=z[f'tris{mi}'][ti];lis=z[f'tri_loops{mi}'][ti]
   for gv,sv in zip(tri,vids):
    li=lis[list(native).index(sv)]
    if meta['meshes'][mi]['uv_names']:
     uv=z[f'uv{mi}_0'][li].copy();uv[1]=1-uv[1];e=float(abs(g.acc(pr['attributes']['TEXCOORD_0'])[gv]-uv).max());uv_err=max(uv_err,e)
    normal=z[f'loop_normal{mi}'][li]@C[:3,:3].T;n=g.acc(pr['attributes']['NORMAL'])[gv];normal_err=max(normal_err,float(np.linalg.norm(n-normal)))
  assert uv_err<2e-7,(pr['name'],uv_err);assert normal_err<1e-4,(pr['name'],normal_err)
  uvmax=max(uvmax,uv_err);normalmax=max(normalmax,normal_err)
  rows.append({'name':pr['name'],'primitive':pr['primitive'],'vertices':len(ids),'maximum_positive_influences':int(np.count_nonzero(pr['w'],axis=1).max()),'max_normalized_weight_error':err,'ambiguous_equivalent_bind_matches':amb,'material':material_name,'uv_max_error':uv_err,'normal_max_vector_error':normal_err})
 for name,t in topology.items():assert t==Counter(facekey(f) for f in z[f'tris{meta["mesh_names"].index(name)}']),name
 rest=C@z['bone_rest'][order];bind_identity=float(abs(rest@g.ibm-np.eye(4)).max());assert bind_identity<2e-6
 parent_checks={}
 for n in g.joint_names:
  node=g.joints[g.joint_names.index(n)];p=g.parents.get(node);parent=g.names[p] if p is not None else None;native=meta['bone_parents'][n];parent_checks[n]=parent==native if native else p not in g.joints
 assert all(parent_checks.values())
 motion=[]
 for label in ['neutral','motion']:
  wanted=meta['actions'][label]['name'];candidates=[i for i,(n,ch) in enumerate(g.animations) if n==wanted]
  if not candidates:candidates=[i for i,(n,ch) in enumerate(g.animations) if ('neutral' in n.lower() if label=='neutral' else 'articulation' in n.lower() or 'raise' in n.lower())]
  assert len(candidates)==1;(ai,)=candidates;name,ch=g.animations[ai];keycounts=Counter(len(v[0]) for v in ch.values());times=z[label+'_times'];mesherr={n:0. for n in meta['mesh_names']};boneerr=0.;carriererr=0.;endpoints=[]
  for ti,t in enumerate(times):
   W=g.world(ai,float(t));expected=C@z[label+'_bones'][ti,order];err=abs(W[g.joints]-expected);boneerr=max(boneerr,float(err.max()));carriererr=max(carriererr,float(err[[g.joint_names.index('Gear_Elbow_L'),g.joint_names.index('Gear_Elbow_R')]].max()))
   if ti in [0,len(times)-1]:endpoints.append(W)
   for pr in g.primitives:
    mi,ids=maps[(pr['node'],pr['primitive'])];pred=g.skin(W,pr);actual=z[f'{label}_eval{mi}'][ti,ids]@C[:3,:3].T;e=float(np.linalg.norm(pred-actual,axis=1).max());mesherr[pr['name']]=max(mesherr[pr['name']],e)
   if ti%60==0:print('REPLAY',label,ti,len(times),flush=True)
  assert boneerr<2e-5,(label,'bone',boneerr);assert max(mesherr.values())<1e-5,(label,'vertex',mesherr)
  tmin=min(v[0][0] for v in ch.values());tmax=max(v[0][-1] for v in ch.values())
  if label=='neutral':assert tmin==tmax==0 and keycounts=={1:216} and all(v[2]=='STEP' for v in ch.values())
  else:assert abs(tmin)<1e-8 and abs(tmax-3)<1e-8 and keycounts=={181:216} and all(v[2]=='LINEAR' and np.array_equal(v[0],np.linspace(0,3,181).astype(np.float32).astype(float)) for v in ch.values())
  motion.append({'label':label,'name':name,'channels':len(ch),'keys_per_channel':dict(keycounts),'interpolation':sorted(set(v[2] for v in ch.values())),'time_min_max':[float(tmin),float(tmax)],'sample_count':len(times),'max_joint_matrix_error':boneerr,'max_carrier_matrix_error':carriererr,'max_vertex_error_m_by_mesh':mesherr,'endpoint_joint_matrix_delta':float(abs(endpoints[-1][g.joints]-endpoints[0][g.joints]).max())})
 return {'status':'PASS','reference_mode':compact_report if compact_report else 'Direct Blender-evaluated mesh vertices at all sampled times','source_sha256':meta['source_sha256'],'source_capture_sha256':meta['capture_npz_sha256'],'joints':72,'native_parent_names_exact':True,'inverse_bind_max_identity_error':bind_identity,'all_oriented_triangles_and_material_assignments_exact':True,'all_uv_max_error':uvmax,'all_normal_max_vector_error':normalmax,'all_normalized_weight_max_error':weightmax,'total_exported_vertices':sum(len(p['p']) for p in g.primitives),'primitives':rows,'animations':motion}
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--glb',type=Path,required=True);ap.add_argument('--source-capture',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();g=GLB(a.glb);report={'schema':'independent-upper-gear-f-binary-validation/1','glb_sha256':g.sha,'method':'Actual accessor bounds/finiteness checks; independent SLERP/TRS FK; normalized all-influence skinning; all vertices at 361 times plus static neutral using the explicitly reported reference mode; oriented triangles/material IDs and per-corner normals/UVs. No producer imports.','source_parity':validate(g,a.source_capture)};a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS',a.output,flush=True)
if __name__=='__main__':main()
