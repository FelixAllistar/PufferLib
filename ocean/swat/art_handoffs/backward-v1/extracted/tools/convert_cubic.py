"""Convert pinned native scalar curves to glTF cubic TRS using fixed rest frames.
Usage: python convert_cubic.py BASELINE.glb source_curves.json OUTPUT.glb raw|unit
Geometry/binds are copied unchanged. Native scalar data are not modified.
"""
import collections,copy,hashlib,json,math,struct,sys
from pathlib import Path
import numpy as np
base,source_path,out=map(Path,sys.argv[1:4]);policy=sys.argv[4];assert policy in ['raw','unit']
raw=base.read_bytes();assert hashlib.sha256(raw).hexdigest()=='edcb227870a0483d06c99e89df491e24f6eb22c879e35833e597f3d6a196b899'
n=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+n]);oldbin=raw[28+n:];source=json.loads(source_path.read_text());assert source['source_sha256']=='91cc7919e4eaba63a8e6ddd658448bb7be0d2ac406738d22c9a0641d22e135e5'
# Repack only geometry and inverse-bind accessors; discard the baseline animation buffers.
used=set()
for mesh in doc['meshes']:
 for pr in mesh['primitives']:
  used.update(pr['attributes'].values());used.add(pr['indices'])
for skin in doc['skins']:used.add(skin['inverseBindMatrices'])
used=sorted(used);amap={a:i for i,a in enumerate(used)};views=sorted({doc['accessors'][a]['bufferView']for a in used});vmap={v:i for i,v in enumerate(views)};blob=bytearray();newviews=[]
for v in views:
 view=copy.deepcopy(doc['bufferViews'][v]);start=view.get('byteOffset',0);payload=oldbin[start:start+view['byteLength']];blob.extend(b'\0'*((-len(blob))%4));view['byteOffset']=len(blob);blob.extend(payload);newviews.append(view)
newacc=[]
for a in used:
 ac=copy.deepcopy(doc['accessors'][a]);assert not ac.get('sparse');ac['bufferView']=vmap[ac['bufferView']];newacc.append(ac)
for mesh in doc['meshes']:
 for pr in mesh['primitives']:
  pr['attributes']={k:amap[v]for k,v in pr['attributes'].items()};pr['indices']=amap[pr['indices']]
for skin in doc['skins']:skin['inverseBindMatrices']=amap[skin['inverseBindMatrices']]
doc['accessors']=newacc;doc['bufferViews']=newviews;doc['animations']=[]

def add_accessor(a,kind,time=False):
 a=np.asarray(a,dtype='<f4');blob.extend(b'\0'*((-len(blob))%4));v=len(doc['bufferViews']);doc['bufferViews'].append({'buffer':0,'byteOffset':len(blob),'byteLength':a.nbytes});blob.extend(a.tobytes());x={'bufferView':v,'componentType':5126,'count':len(a),'type':kind}
 if time:x.update(min=[float(a.min())],max=[float(a.max())])
 i=len(doc['accessors']);doc['accessors'].append(x);return i

def quatmul(a,b):
 w,x,y,z=np.asarray(a);bw,bx,by,bz=np.moveaxis(b,-1,0)
 return np.stack([w*bw-x*bx-y*by-z*bz,w*bx+x*bw+y*bz-z*by,w*by-x*bz+y*bw+z*bx,w*bz+x*by-y*bx+z*bw],axis=-1)
def quatR(q):
 w,x,y,z=q/np.linalg.norm(q);return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
class Curve:
 def __init__(self,d):self.a=np.array([k[:6]for k in d['keys']],float);self.mode=[k[-1]for k in d['keys']]
 def eval(self,t,side):
  a=self.a;exact=np.flatnonzero(a[:,0]==t)
  if len(exact):
   k=int(exact[0]);v=a[k,1];j=k-1 if side=='in' else k;j=max(0,min(j,len(a)-2));mode=self.mode[j]
   if mode=='LINEAR':s=(a[j+1,1]-a[j,1])/(a[j+1,0]-a[j,0])
   elif mode=='CONSTANT':s=0.
   else:
    assert mode=='BEZIER'
    co=a[k,:2];h=a[k,2:4]if side=='in'else a[k,4:6];s=(h[1]-co[1])/(h[0]-co[0])
   return v,s*30
  j=int(np.clip(np.searchsorted(a[:,0],t,side='right')-1,0,len(a)-2));left,right=a[j],a[j+1];mode=self.mode[j];u=(t-left[0])/(right[0]-left[0])
  if mode=='LINEAR':return left[1]+u*(right[1]-left[1]),(right[1]-left[1])*30/(right[0]-left[0])
  if mode=='CONSTANT':return left[1],0.
  assert mode=='BEZIER';p=np.array([left[:2],left[4:6],right[2:4],right[:2]])
  lo,hi=0.,1.
  for _ in range(50):
   q=(1-u)**3*p[0]+3*(1-u)**2*u*p[1]+3*(1-u)*u*u*p[2]+u**3*p[3]
   if q[0]<t:lo=u
   else:hi=u
   u=(lo+hi)/2
  q=(1-u)**3*p[0]+3*(1-u)**2*u*p[1]+3*(1-u)*u*u*p[2]+u**3*p[3];d=3*(1-u)**2*(p[1]-p[0])+6*(1-u)*u*(p[2]-p[1])+3*u*u*(p[3]-p[2]);return q[1],d[1]/d[0]*30
curves={(d['data_path'],d['component']):Curve(d)for d in source['curves']};names={n['name']:i for i,n in enumerate(doc['nodes'])};animation={'name':source['action'],'channels':[],'samplers':[]};rows=[];rawnormmax=0.
for bone in source['bones']:
 name=bone['name'];ni=names[name];node=doc['nodes'][ni];baseq=np.array(node.get('rotation',[0,0,0,1]),float)[[3,0,1,2]];baseq/=np.linalg.norm(baseq);R=quatR(baseq);S=np.array(node.get('scale',[1,1,1]));L=R@np.diag(S);T=np.array(node.get('translation',[0,0,0]))
 for prop,width,target in [('location',3,'translation'),('rotation_quaternion',4,'rotation'),('scale',3,'scale')]:
  path=f'pose.bones["{name}"].{prop}';fc=[curves[path,i]for i in range(width)];frames=np.array(sorted({float(t)for f in fc for t in f.a[:,0]}));ts=(frames/30).astype('<f4');assert np.all(np.diff(ts)>0)
  values=np.array([[f.eval(t,'out')[0]for f in fc]for t in frames]);din=np.array([[f.eval(t,'in')[1]for f in fc]for t in frames]);dout=np.array([[f.eval(t,'out')[1]for f in fc]for t in frames])
  if target=='translation':values=values@L.T+T;din=din@L.T;dout=dout@L.T
  elif target=='scale':values*=S;din*=S;dout*=S
  else:
   raw_norm=np.linalg.norm(values,axis=1,keepdims=True);rawnormmax=max(rawnormmax,float(abs(raw_norm-1).max()))
   if policy=='unit':
    q=values/raw_norm;din=(din-q*np.sum(q*din,axis=1,keepdims=True))/raw_norm;dout=(dout-q*np.sum(q*dout,axis=1,keepdims=True))/raw_norm;values=q
   values=quatmul(baseq,values)[:,[1,2,3,0]];din=quatmul(baseq,din)[:,[1,2,3,0]];dout=quatmul(baseq,dout)[:,[1,2,3,0]]
  din[0]=0;dout[-1]=0
  output=np.stack([din,values,dout],axis=1).reshape(-1,width).astype('<f4');ii=add_accessor(ts,'SCALAR',True);oi=add_accessor(output,'VEC'+str(width));si=len(animation['samplers']);animation['samplers'].append({'input':ii,'output':oi,'interpolation':'CUBICSPLINE'});animation['channels'].append({'sampler':si,'target':{'node':ni,'path':target}})
  rows.append({'node':ni,'name':name,'path':target,'native_component_key_counts':[len(f.a)for f in fc],'gltf_key_count':len(ts),'native_segment_modes':dict(collections.Counter(m for f in fc for m in f.mode[:-1])),'export_interpolation':'CUBICSPLINE','max_time_quantization_s':float(abs(ts.astype(float)-frames/30).max()),'key_quaternion_norm_residual':float(abs(np.linalg.norm(output.reshape(-1,3,width)[:,1],axis=1)-1).max())if target=='rotation'else None})
doc['animations']=[animation];doc['buffers']=[{'byteLength':len(blob)}];doc['asset']['generator']='SWAT private cubic-preserving review converter; static geometry from pinned Blender4.3.2 fixture';doc['asset']['extras']={'source_sha256':source['source_sha256'],'source_action':source['action'],'conversion_policy':policy,'normalization':'Quaternion spline output must be normalized including key evaluation; raw and unit probes are distinct'}
js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*((-len(js))%4);blob+=b'\0'*((-len(blob))%4);data=struct.pack('<III',0x46546c67,2,12+8+len(js)+8+len(blob))+struct.pack('<II',len(js),0x4e4f534a)+js+struct.pack('<II',len(blob),0x004e4942)+blob;out.write_bytes(data)
report={'source_sha256':source['source_sha256'],'source_curve_count':source['curve_count'],'source_key_count':source['key_count'],'source_interpolation_counts':source['interpolation_counts'],'output':out.name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'policy':policy,'source_raw_quaternion_key_norm_max_deviation':rawnormmax,'source_fps':30,'duration_s':1,'channels':rows,'conversion_limit':'Stored native Bezier time handles are only approximately affine; endpoint slope Hermite and float32 output introduce measured numerical error. Unit policy additionally uses normalized key quaternions and projected derivatives, preserving physical key derivatives but not algebraically identical interior polynomials. Rest frames contain float32 near-rigid error. No exact-lossless or runtime approval claim.'}
out.with_suffix('.conversion.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k!='channels'},indent=2))
