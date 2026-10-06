"""Export one pinned native pose as 210 single STEP-key glTF TRS channels.
Usage: python convert_static.py BASELINE.glb source_curves.json OUTPUT.glb
Static geometry/binds stay unchanged. No temporal duration is authored.
"""
import collections,copy,hashlib,json,math,struct,sys
from pathlib import Path
import numpy as np
base,source_path,out=map(Path,sys.argv[1:4]);policy='unit'
raw=base.read_bytes();assert hashlib.sha256(raw).hexdigest()=='e44a396d19686b172184e241ed42e52291c96a3e27422770da899d18b5912435'
n=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+n]);oldbin=raw[28+n:];source=json.loads(source_path.read_text());assert source['source_sha256']=='b316df0d4c4af329dffe5f5330c502da14d17c0d3af77eecd257262a435445d9'
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

assert source['frame_range']==[0,0] and source['curve_count']==700 and source['key_count']==700
curves={(d['data_path'],d['component']):d for d in source['curves']}
assert all(len(d['keys'])==1 and d['keys'][0][0]==0 and d['extrapolation']=='CONSTANT' for d in curves.values())
names={n['name']:i for i,n in enumerate(doc['nodes'])};animation={'name':source['action'],'channels':[],'samplers':[]};rows=[];rawnormmax=0.
for bone in source['bones']:
 name=bone['name'];ni=names[name];node=doc['nodes'][ni];baseq=np.array(node.get('rotation',[0,0,0,1]),float)[[3,0,1,2]];baseq/=np.linalg.norm(baseq);R=quatR(baseq);S=np.array(node.get('scale',[1,1,1]));L=R@np.diag(S);T=np.array(node.get('translation',[0,0,0]))
 for prop,width,target in [('location',3,'translation'),('rotation_quaternion',4,'rotation'),('scale',3,'scale')]:
  path=f'pose.bones["{name}"].{prop}';value=np.array([curves[path,i]['keys'][0][1] for i in range(width)])
  if target=='translation':value=L@value+T
  elif target=='scale':value*=S
  else:
   norm=np.linalg.norm(value);rawnormmax=max(rawnormmax,abs(norm-1));value=quatmul(baseq,value/norm)[[1,2,3,0]]
  ii=add_accessor(np.array([0.]),'SCALAR',True);oi=add_accessor(value[None,:],'VEC'+str(width));si=len(animation['samplers']);animation['samplers'].append({'input':ii,'output':oi,'interpolation':'STEP'});animation['channels'].append({'sampler':si,'target':{'node':ni,'path':target}})
  rows.append({'node':ni,'name':name,'path':target,'keys':1,'time_s':0,'interpolation':'STEP','static_value':value.tolist(),'source_scalar_interpolation':[curves[path,i]['keys'][0][-1] for i in range(width)]})
doc['animations']=[animation];doc['buffers']=[{'byteLength':len(blob)}];doc['asset']['generator']='SWAT private static-pose review converter; pinned geometry/binds from Blender4.3.2 fixture';doc['asset']['extras']={'source_sha256':source['source_sha256'],'source_action':source['action'],'authored_duration_s':None,'stored_animation_span_s':0,'static_pose':True,'static_sampling':'Sample every TRS channel at t=0; hold pose according to consumer state. No authored time cycle.'}
js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*((-len(js))%4);blob+=b'\0'*((-len(blob))%4);data=struct.pack('<III',0x46546c67,2,12+8+len(js)+8+len(blob))+struct.pack('<II',len(js),0x4e4f534a)+js+struct.pack('<II',len(blob),0x004e4942)+blob;out.write_bytes(data)
report={'source_sha256':source['source_sha256'],'source_curve_count':700,'source_key_count':700,'source_interpolation_counts':source['interpolation_counts'],'output':out.name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'channels':rows,'raw_quaternion_norm_max_deviation':float(rawnormmax),'authored_duration_s':None,'stored_animation_span_s':0,'normalization':'Source and exported key quaternions normalized for physical rotation; no tangent or temporal motion exists. Float32 rest/frame composition errors are measured independently.','static_geometry_template_sha256':hashlib.sha256(raw).hexdigest()}
out.with_suffix('.conversion.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k!='channels'},indent=2))
