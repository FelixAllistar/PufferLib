"""Dense glTF accessor, CUBICSPLINE sampler and analytic endpoint FK helpers.
NumPy only. This module reads GLB files and never rewrites them.
"""
import json,struct
import numpy as np

def qrot(q):
 q=q/np.linalg.norm(q,axis=-1,keepdims=True);x,y,z,w=np.moveaxis(q,-1,0);r=np.empty(q.shape[:-1]+(3,3));r[...,0,0]=1-2*(y*y+z*z);r[...,0,1]=2*(x*y-z*w);r[...,0,2]=2*(x*z+y*w);r[...,1,0]=2*(x*y+z*w);r[...,1,1]=1-2*(x*x+z*z);r[...,1,2]=2*(y*z-x*w);r[...,2,0]=2*(x*z-y*w);r[...,2,1]=2*(y*z+x*w);r[...,2,2]=1-2*(x*x+y*y);return r

def qmul(a,b):
 ax,ay,az,aw=a;bx,by,bz,bw=b;return np.array([aw*bx+ax*bw+ay*bz-az*by,aw*by-ax*bz+ay*bw+az*bx,aw*bz+ax*by-ay*bx+az*bw,aw*bw-ax*bx-ay*by-az*bz])
def skew(w):return np.array([[0,-w[2],w[1]],[w[2],0,-w[0]],[-w[1],w[0],0]])
def angular_velocity(M,D):
 A=M[:3,:3];AD=D[:3,:3];u,s,vh=np.linalg.svd(A)
 if s.min()<1e-10:return None
 R=u@vh;S=R.T@A;K=R.T@AD-AD.T@R;lam,V=np.linalg.eigh(S);Omega=V@((V.T@K@V)/(lam[:,None]+lam[None,:]))@V.T;W=R@Omega@R.T;return np.array([W[2,1],W[0,2],W[1,0]])

class GLB:
 def __init__(self,path):
  self.raw=path.read_bytes();magic,version,size=struct.unpack_from('<III',self.raw);assert (magic,version,size)==(0x46546c67,2,len(self.raw));n,kind=struct.unpack_from('<II',self.raw,12);assert kind==0x4e4f534a;self.doc=json.loads(self.raw[20:20+n]);bn,bk=struct.unpack_from('<II',self.raw,20+n);assert bk==0x004e4942;self.binary=self.raw[28+n:28+n+bn];self.nodes=self.doc['nodes'];self.parents={c:i for i,n in enumerate(self.nodes)for c in n.get('children',[])};assert len(self.parents)==sum(len(n.get('children',[]))for n in self.nodes)
 def acc(self,index):
  a=self.doc['accessors'][index];v=self.doc['bufferViews'][a['bufferView']];assert not a.get('sparse')and v.get('buffer',0)==0;dt=np.dtype({5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']]);width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']];x=np.ndarray((a['count'],width),dtype=dt,buffer=self.binary,offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',dt.itemsize*width),dt.itemsize)).copy()
  if a.get('normalized'):x=x.astype(float)/np.iinfo(dt).max
  assert np.isfinite(x).all();return x
 def world_at(self,animation,times):
  ts=np.asarray(times,float);tr=np.broadcast_to(np.array([n.get('translation',[0,0,0])for n in self.nodes]),(len(ts),len(self.nodes),3)).copy();ro=np.broadcast_to(np.array([n.get('rotation',[0,0,0,1])for n in self.nodes]),(len(ts),len(self.nodes),4)).copy();sc=np.broadcast_to(np.array([n.get('scale',[1,1,1])for n in self.nodes]),(len(ts),len(self.nodes),3)).copy()
  for ch in (animation or {}).get('channels',[]):
   s=animation['samplers'][ch['sampler']];t=self.acc(s['input']).ravel().astype(float);v=self.acc(s['output']).astype(float);mode=s.get('interpolation','LINEAR');prop=ch['target']['path'];idx=ch['target']['node'];
   if len(t)==1:
    assert mode in ['STEP','LINEAR'] and len(v)==1
    sample=np.broadcast_to(v[0],(len(ts),len(v[0])))
    if prop=='rotation':ro[:,idx]=sample/np.linalg.norm(sample,axis=1,keepdims=True)
    elif prop=='translation':tr[:,idx]=sample
    elif prop=='scale':sc[:,idx]=sample
    else:raise ValueError(prop)
    continue
   j=np.clip(np.searchsorted(t,ts,side='right')-1,0,len(t)-2);dt=(t[j+1]-t[j])[:,None];u=np.clip((ts-t[j])[:,None]/dt,0,1)
   if mode=='CUBICSPLINE':
    assert len(v)==3*len(t)and len(t)>=2;v=v.reshape(len(t),3,-1);sample=(2*u**3-3*u**2+1)*v[j,1]+dt*(u**3-2*u**2+u)*v[j,2]+(-2*u**3+3*u**2)*v[j+1,1]+dt*(u**3-u**2)*v[j+1,0]
   elif mode=='STEP':sample=v[np.clip(np.searchsorted(t,ts,side='right')-1,0,len(t)-1)]
   elif mode=='LINEAR'and prop=='rotation':
    a=v[j]/np.linalg.norm(v[j],axis=1,keepdims=True);b=v[j+1]/np.linalg.norm(v[j+1],axis=1,keepdims=True);dot=np.sum(a*b,axis=1,keepdims=True);b=np.where(dot<0,-b,b);theta=np.arccos(np.clip(abs(dot),0,1));den=np.sin(theta);small=den<1e-6;den=np.where(small,1,den);sample=np.where(small,a*(1-u)+b*u,np.sin((1-u)*theta)/den*a+np.sin(u*theta)/den*b)
   else:
    assert mode=='LINEAR';sample=v[j]*(1-u)+v[j+1]*u
   if prop=='rotation':ro[:,idx]=sample/np.linalg.norm(sample,axis=1,keepdims=True)
   elif prop=='translation':tr[:,idx]=sample
   elif prop=='scale':sc[:,idx]=sample
   else:raise ValueError(prop)
  local=np.zeros((len(ts),len(self.nodes),4,4));local[...,:3,:3]=qrot(ro)*sc[...,None,:];local[...,:3,3]=tr;local[...,3,3]=1
  for i,n in enumerate(self.nodes):
   if 'matrix'in n:local[:,i]=np.array(n['matrix']).reshape(4,4).T
  world=np.empty_like(local);done=set()
  def visit(i):
   if i in done:return
   if i in self.parents:visit(self.parents[i]);world[:,i]=world[:,self.parents[i]]@local[:,i]
   else:world[:,i]=local[:,i]
   done.add(i)
  for i in range(len(self.nodes)):visit(i)
  return world
 def endpoint_fk(self,animation,end):
  vals=[{k:np.array(n.get(k,default),float)for k,default in [('translation',[0,0,0]),('rotation',[0,0,0,1]),('scale',[1,1,1])]}for n in self.nodes];dots=[{k:np.zeros_like(v)for k,v in row.items()}for row in vals]
  for ch in animation['channels']:
   s=animation['samplers'][ch['sampler']];t=self.acc(s['input']).ravel().astype(float);v=self.acc(s['output']).astype(float);prop=ch['target']['path'];node=ch['target']['node'];mode=s.get('interpolation','LINEAR');i=-1 if end else 0
   assert mode=='CUBICSPLINE','Endpoint derivative helper intentionally requires explicit cubic channels'
   v=v.reshape(len(t),3,-1);vals[node][prop]=v[i,1];dots[node][prop]=v[i,0 if end else 2]
  M=[];D=[]
  for v,d in zip(vals,dots):
   raw=v['rotation'];norm=np.linalg.norm(raw);q=raw/norm;qd=(d['rotation']-q*(q@d['rotation']))/norm;R=qrot(q);omega=2*qmul(qd,np.array([-q[0],-q[1],-q[2],q[3]]))[:3];RD=skew(omega)@R;m=np.eye(4);m[:3,:3]=R@np.diag(v['scale']);m[:3,3]=v['translation'];md=np.zeros((4,4));md[:3,:3]=RD@np.diag(v['scale'])+R@np.diag(d['scale']);md[:3,3]=d['translation'];M.append(m);D.append(md)
  M=np.array(M);D=np.array(D);W=np.empty_like(M);WD=np.empty_like(D);done=set()
  def visit(i):
   if i in done:return
   if i in self.parents:
    p=self.parents[i];visit(p);W[i]=W[p]@M[i];WD[i]=WD[p]@M[i]+W[p]@D[i]
   else:W[i]=M[i];WD[i]=D[i]
   done.add(i)
  for i in range(len(self.nodes)):visit(i)
  return W,WD
