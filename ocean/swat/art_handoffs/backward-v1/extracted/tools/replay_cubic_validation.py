"""Independent byte-level cubic, normalized-quaternion FK and full-influence skin audit.
Does not import converter code or writer-side arrays. Matrices are float64 numerical evaluation.
"""
import json, struct, hashlib, re, argparse
from pathlib import Path
import numpy as np
C=np.array([[1.,0,0,0],[0,0,1,0],[0,-1,0,0],[0,0,0,1.]])

def rot_and_dot(q,dq):
    """XYZW, normalize polynomial before applying it; exact normalization derivative."""
    n=np.linalg.norm(q,axis=-1,keepdims=True)
    u=q/n; v=(dq-u*np.sum(u*dq,axis=-1,keepdims=True))/n
    x,y,z,w=np.moveaxis(u,-1,0); a,b,c,d=np.moveaxis(v,-1,0)
    R=np.empty(q.shape[:-1]+(3,3)); D=np.empty_like(R)
    R[...,0,0]=1-2*(y*y+z*z); R[...,0,1]=2*(x*y-z*w); R[...,0,2]=2*(x*z+y*w)
    R[...,1,0]=2*(x*y+z*w); R[...,1,1]=1-2*(x*x+z*z); R[...,1,2]=2*(y*z-x*w)
    R[...,2,0]=2*(x*z-y*w); R[...,2,1]=2*(y*z+x*w); R[...,2,2]=1-2*(x*x+y*y)
    D[...,0,0]=-4*(y*b+z*c); D[...,0,1]=2*(a*y+x*b-c*w-z*d); D[...,0,2]=2*(a*z+x*c+b*w+y*d)
    D[...,1,0]=2*(a*y+x*b+c*w+z*d); D[...,1,1]=-4*(x*a+z*c); D[...,1,2]=2*(b*z+y*c-a*w-x*d)
    D[...,2,0]=2*(a*z+x*c-b*w-y*d); D[...,2,1]=2*(b*z+y*c+a*w+x*d); D[...,2,2]=-4*(x*a+y*b)
    return R,D,n[...,0]

def trs(p,q,s,dp,dq,ds):
    R,D,n=rot_and_dot(q,dq)
    M=np.zeros(p.shape[:-1]+(4,4)); V=np.zeros_like(M)
    M[...,:3,:3]=R*s[...,None,:]; M[...,:3,3]=p; M[...,3,3]=1
    V[...,:3,:3]=D*s[...,None,:]+R*ds[...,None,:]; V[...,:3,3]=dp
    return M,V

def polar(M,D=None):
    U,s,Vh=np.linalg.svd(M[...,:3,:3]); R=U@Vh
    if np.any(np.linalg.det(R)<0): raise ValueError('Negative determinant in polar rotation')
    if D is None: return R
    if np.min(s)<1e-10: return R,None
    A=np.swapaxes(R,-1,-2)@D[...,:3,:3]; K=A-np.swapaxes(A,-1,-2)
    V=np.swapaxes(Vh,-1,-2); Kp=Vh@K@V
    Om=V@(Kp/(s[...,None,:]+s[...,:,None]))@Vh
    W=R@Om@np.swapaxes(R,-1,-2)
    return R,np.stack([W[...,2,1],W[...,0,2],W[...,1,0]],axis=-1)

def angular_distance(R,S):
    A=R@np.swapaxes(S,-1,-2)
    vee=np.stack([A[...,2,1]-A[...,1,2],A[...,0,2]-A[...,2,0],A[...,1,0]-A[...,0,1]],axis=-1)*.5
    return np.arctan2(np.linalg.norm(vee,axis=-1),(np.trace(A,axis1=-2,axis2=-1)-1)*.5)

def angular_step(R0,R1,dt):
    A=R1@R0.T
    vee=np.array([A[2,1]-A[1,2],A[0,2]-A[2,0],A[1,0]-A[0,1]])*.5
    sn=np.linalg.norm(vee); th=np.arctan2(sn,(np.trace(A)-1)*.5)
    return vee*(th/sn if sn>1e-15 else 1)/dt

class GLB:
    def __init__(self,path):
        self.path=path; b=path.read_bytes(); self.sha=hashlib.sha256(b).hexdigest()
        magic,ver,length=struct.unpack_from('<III',b)
        assert magic==0x46546C67 and ver==2 and length==len(b)
        chunks={}; off=12
        while off<len(b):
            n,kind=struct.unpack_from('<II',b,off); chunks[kind]=b[off+8:off+8+n];off+=8+n
        self.doc=json.loads(chunks[0x4E4F534A]);self.bin=chunks[0x004E4942];self.cache={}
        self.nodes=self.doc['nodes'];self.names=[x.get('name',str(i)) for i,x in enumerate(self.nodes)]
        self.parents={c:i for i,x in enumerate(self.nodes) for c in x.get('children',[])}
        self.joints=self.doc['skins'][0]['joints'];self.joint_names=[self.names[i] for i in self.joints]
        self.channels={};norms=[]
        assert len(self.doc['animations'])==1
        for ch in self.doc['animations'][0]['channels']:
            sm=self.doc['animations'][0]['samplers'][ch['sampler']];key=(ch['target']['node'],ch['target']['path'])
            assert key not in self.channels and sm['interpolation']=='CUBICSPLINE'
            tin=self.acc(sm['input']).ravel(); val=self.acc(sm['output'])
            ai=self.doc['accessors'][sm['input']];ao=self.doc['accessors'][sm['output']]
            assert ai['componentType']==5126 and ai['type']=='SCALAR' and ao['componentType']==5126
            assert ao['type']==('VEC4' if key[1]=='rotation' else 'VEC3')
            assert len(tin)>=2 and len(val)==len(tin)*3 and np.all(np.isfinite(val)) and np.all(np.diff(tin)>0) and tin[0]==0 and tin[-1]==1
            val=val.reshape(len(tin),3,-1);self.channels[key]=(tin,val)
            if key[1]=='rotation': norms.extend(np.abs(np.linalg.norm(val[:,1],axis=1)-1))
        assert set(self.channels)=={(j,p) for j in self.joints for p in ['translation','rotation','scale']}
        self.validation={'joint_count':len(self.joints),'channel_count':len(self.channels),'samplers_all_cubic':True,'duration_s':1,'key_quaternion_norm_residual_max':float(max(norms))}
        self.primitives=[]
        for ni,node in enumerate(self.nodes):
            if 'mesh' not in node:continue
            for pi,pr in enumerate(self.doc['meshes'][node['mesh']]['primitives']):
                a=pr['attributes'];p=self.acc(a['POSITION']);js=[];ws=[]
                for k in range(10):
                    if 'JOINTS_'+str(k) not in a:continue
                    js.append(self.acc(a['JOINTS_'+str(k)]).astype(int));ws.append(self.acc(a['WEIGHTS_'+str(k)]))
                j=np.concatenate(js,axis=1);w=np.concatenate(ws,axis=1);s=w.sum(axis=1)
                assert np.all(np.isfinite(w)) and np.all(w>=0) and np.all(s>0) and np.all((j>=0)&(j<len(self.joints)))
                self.primitives.append({'node':ni,'name':node['name'],'primitive':pi,'p':np.column_stack([p,np.ones(len(p))]),'j':j,'w':w/s[:,None], 'raw_weight_sum_error':float(np.max(np.abs(s-1))), 'max_influences':int(np.max(np.count_nonzero(w,axis=1)))})
        self.ibm=self.acc(self.doc['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
    def acc(self,i):
        if i in self.cache:return self.cache[i]
        a=self.doc['accessors'][i];assert 'sparse' not in a
        v=self.doc['bufferViews'][a['bufferView']];assert v.get('buffer',0)==0
        ct=a['componentType'];dt=np.dtype({5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}[ct]);n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        off=v.get('byteOffset',0)+a.get('byteOffset',0); stride=v.get('byteStride',dt.itemsize*n)
        r=np.ndarray((a['count'],n),dtype=dt,buffer=self.bin,offset=off,strides=(stride,dt.itemsize)).astype(float)
        if a.get('normalized'):
            if ct==5121:r/=255
            elif ct==5123:r/=65535
            elif ct==5120:r=np.maximum(r/127,-1)
            elif ct==5122:r=np.maximum(r/32767,-1)
        self.cache[i]=r;return r
    def sample(self,key,t):
        ti,v=self.channels[key]; ix=np.clip(np.searchsorted(ti,t,side='right')-1,0,len(ti)-2)
        h=ti[ix+1]-ti[ix];u=(t-ti[ix])/h; h=h[:,None];u=u[:,None]
        p=v[ix,1];q=v[ix+1,1];a=v[ix,2];b=v[ix+1,0]
        out=(2*u**3-3*u**2+1)*p+(u**3-2*u**2+u)*h*a+(-2*u**3+3*u**2)*q+(u**3-u**2)*h*b
        deriv=(6*u*u-6*u)/h*p+(3*u*u-4*u+1)*a+(-6*u*u+6*u)/h*q+(3*u*u-2*u)*b
        return out,deriv
    def world(self,t):
        t=np.atleast_1d(t);L=[];D=[];minnorm=1.
        for i,node in enumerate(self.nodes):
            vals={};ders={}
            for path,default in [('translation',[0,0,0]),('rotation',[0,0,0,1]),('scale',[1,1,1])]:
                if (i,path) in self.channels:vals[path],ders[path]=self.sample((i,path),t)
                else: vals[path]=np.tile(node.get(path,default),(len(t),1));ders[path]=np.zeros_like(vals[path])
            if 'matrix' in node:
                assert all((i,p) not in self.channels for p in vals)
                m=np.tile(np.array(node['matrix']).reshape(4,4).T,(len(t),1,1));d=np.zeros_like(m)
            else:m,d=trs(vals['translation'],vals['rotation'],vals['scale'],ders['translation'],ders['rotation'],ders['scale'])
            minnorm=min(minnorm,float(np.min(np.linalg.norm(vals['rotation'],axis=1))));L.append(m);D.append(d)
        W={};WD={}
        def get(i):
            if i in W:return W[i],WD[i]
            if i in self.parents:
                p,v=get(self.parents[i]);W[i]=p@L[i];WD[i]=v@L[i]+p@D[i]
            else:W[i]=L[i];WD[i]=D[i]
            return W[i],WD[i]
        for i in range(len(self.nodes)):get(i)
        return np.stack([W[j] for j in self.joints],axis=1),np.stack([WD[j] for j in self.joints],axis=1),minnorm
    def skin(self,W,pr):
        X=W@self.ibm[None];out=np.zeros((len(W),len(pr['p']),3))
        for k in range(pr['j'].shape[1]):
            weights=pr['w'][:,k];mask=weights>0
            if not mask.any():continue
            y=np.einsum('tvab,vb->tva',X[:,pr['j'][mask,k],:3,:],pr['p'][mask], optimize=True)
            out[:,mask]+=y*weights[mask][None,:,None]
        return out

class Native:
    def __init__(self,path):
        d=json.load(open(path));self.doc=d;self.names=[b['name'] for b in d['bones']];self.bones=d['bones'];self.curves={};self.fps=d['fps']
        for c in d['curves']:
            assert all(k[6] in ['BEZIER','LINEAR'] for k in c['keys'])
            name,prop=re.match(r'pose.bones\["(.*)"\]\.(.*)',c['data_path']).groups()
            self.curves[(name,prop,c['component'])]=(np.array([k[:6] for k in c['keys']]),np.array([k[6]=='BEZIER' for k in c['keys']]))
        self.rest={b['name']:np.array(b['rest_world_native']) for b in self.bones}
        self.local={b['name']:(np.linalg.inv(self.rest[b['parent']])@self.rest[b['name']] if b['parent'] else self.rest[b['name']]) for b in self.bones}
    def curve(self,key,t):
        k,bez=self.curves[key];frame=t*self.fps;i=np.clip(np.searchsorted(k[:,0],frame,side='right')-1,0,len(k)-2)
        p=k[i];q=k[i+1];h=q[:,0]-p[:,0];z=(frame-p[:,0])/h
        y=p[:,1]+z*(q[:,1]-p[:,1]);dy=(q[:,1]-p[:,1])/h
        mask=bez[i]
        if mask.any():
            a=p[mask];b=q[mask];f=frame[mask];u=z[mask].copy()
            for _ in range(12):
                v=1-u;x=v**3*a[:,0]+3*v*v*u*a[:,4]+3*v*u*u*b[:,2]+u**3*b[:,0]
                dx=3*(v*v*(a[:,4]-a[:,0])+2*v*u*(b[:,2]-a[:,4])+u*u*(b[:,0]-b[:,2]))
                u=np.clip(u-(x-f)/dx,0,1)
            v=1-u; y[mask]=v**3*a[:,1]+3*v*v*u*a[:,5]+3*v*u*u*b[:,3]+u**3*b[:,1]
            dyd=3*(v*v*(a[:,5]-a[:,1])+2*v*u*(b[:,3]-a[:,5])+u*u*(b[:,1]-b[:,3]))
            dx=3*(v*v*(a[:,4]-a[:,0])+2*v*u*(b[:,2]-a[:,4])+u*u*(b[:,0]-b[:,2]))
            dy[mask]=dyd/dx
        return y,dy*self.fps
    def world(self,t):
        t=np.atleast_1d(t);W={};D={}
        for b in self.bones:
            name=b['name'];v=[];dv=[]
            for prop,count in [('location',3),('rotation_quaternion',4),('scale',3)]:
                pair=[self.curve((name,prop,i),t) for i in range(count)]
                v.append(np.stack([x[0] for x in pair],axis=-1));dv.append(np.stack([x[1] for x in pair],axis=-1))
            v[1]=v[1][:,[1,2,3,0]];dv[1]=dv[1][:,[1,2,3,0]]
            B,DB=trs(*v,*dv);L=self.local[name]@B;DL=self.local[name]@DB
            if b['parent']:
                pa=b['parent'];W[name]=W[pa]@L;D[name]=D[pa]@L+W[pa]@DL
            else:W[name]=C@L;D[name]=C@DL
        return np.stack([W[n] for n in self.names],axis=1),np.stack([D[n] for n in self.names],axis=1)

def stats(v):
    v=np.asarray(v);return {'max':float(np.max(v)),'rms':float(np.sqrt(np.mean(v*v))),'p95':float(np.percentile(v,95))}

def endpoint_report(g,native,source_analytic):
    W,D,mn=g.world([0,1]);NW,ND=native.world([0,1]);order=[native.names.index(n) for n in g.joint_names];NW=NW[:,order];ND=ND[:,order]
    ref=json.load(open(source_analytic)); refs={b['bone']:b for b in ref['bones']};bones=[]
    for i,name in enumerate(g.joint_names):
        rr={'bone':name,'endpoint_pose_matrix_abs_gap':float(np.max(np.abs(W[1,i]-W[0,i]))),'endpoint_position_gap_m':float(np.linalg.norm(W[1,i,:3,3]-W[0,i,:3,3])), 'linear_velocity_gap_m_s':float(np.linalg.norm(D[1,i,:3,3]-D[0,i,:3,3])),'outgoing_linear_m_s':D[0,i,:3,3].tolist(),'incoming_linear_m_s':D[1,i,:3,3].tolist(),'native_pose_max_matrix_abs_error':float(np.max(np.abs(W[:,i]-NW[:,i]))),'native_linear_velocity_max_error_m_s':float(np.max(np.linalg.norm(D[:,i,:3,3]-ND[:,i,:3,3],axis=1))),'analytic_report_linear_velocity_max_error_m_s':float(max(np.linalg.norm(D[j,i,:3,3]-C[:3,:3]@refs[name][side+'_linear_m_s']) for j,side in enumerate(['outgoing','incoming'])))}
        if name=='Prop_Magazine_B':rr.update({'angular_undefined_reason':'intentionally collapsed zero-scale bone'})
        else:
            R,om=polar(W[:,i],D[:,i]);NR,nom=polar(NW[:,i],ND[:,i]);rr.update({'endpoint_rotation_gap_deg':float(np.degrees(angular_distance(R[1],R[0]))),'angular_velocity_gap_deg_s':float(np.degrees(np.linalg.norm(om[1]-om[0]))),'outgoing_angular_deg_s':np.degrees(om[0]).tolist(),'incoming_angular_deg_s':np.degrees(om[1]).tolist(),'native_angular_velocity_max_error_deg_s':float(np.degrees(np.max(np.linalg.norm(om-nom,axis=1)))),'analytic_report_angular_velocity_max_error_deg_s':float(max(np.linalg.norm(np.degrees(om[j])-C[:3,:3]@refs[name][side+'_angular_deg_s']) for j,side in enumerate(['outgoing','incoming'])))})
        bones.append(rr)
    geo=[]
    for pr in g.primitives:
        P=g.skin(W,pr);V=g.skin(D,pr)
        rr={k:pr[k] for k in ['name','primitive','max_influences','raw_weight_sum_error']};rr.update({'vertices':len(pr['p']),'endpoint_position_gap_m':stats(np.linalg.norm(P[1]-P[0],axis=1)), 'velocity_gap_m_s':stats(np.linalg.norm(V[1]-V[0],axis=1))})
        if pr['name']=='Rebuilt SWAT full body':
            for side in ['Left','Right']:
                jointset=[j for j,n in enumerate(g.joint_names) if n in ['mixamorig:'+side+'Foot','mixamorig:'+side+'ToeBase','mixamorig:'+side+'Toe_End']]
                mask=np.sum(pr['w']*np.isin(pr['j'],jointset),axis=1)>.5
                if mask.any():rr[side.lower()+'_foot']={'vertices':int(mask.sum()),'velocity_gap_m_s':stats(np.linalg.norm(V[1,mask]-V[0,mask],axis=1))}
        geo.append(rr)
    return {'sha256':g.sha,'validation':g.validation,'worst_linear':max(bones,key=lambda x:x['linear_velocity_gap_m_s']),'worst_angular':max((x for x in bones if 'angular_velocity_gap_deg_s' in x),key=lambda x:x['angular_velocity_gap_deg_s']),'native_pose_max_matrix_abs_error':max(x['native_pose_max_matrix_abs_error'] for x in bones),'native_linear_velocity_max_error_m_s':max(x['native_linear_velocity_max_error_m_s'] for x in bones),'native_angular_velocity_max_error_deg_s':max(x.get('native_angular_velocity_max_error_deg_s',0) for x in bones),'bones':bones,'skinned_geometry':geo}

def convergence(g):
    eps=sorted(set([1/60,1/240,1/480,1/960,1e-2,1e-3,1e-4,1e-5,1e-6,1e-7,1e-8]),reverse=True);t=np.array([0,1]+[x for e in eps for x in [e,1-e]])
    W,D,_=g.world(t);P=[g.skin(W,pr) for pr in g.primitives];VD=[g.skin(D[:2],pr) for pr in g.primitives];R={i:polar(W[:,i]) for i,n in enumerate(g.joint_names) if n!='Prop_Magazine_B'};rows=[]
    for k,e in enumerate(eps):
        ia=2+k*2;ib=ia+1;v0=(W[ia,:,:3,3]-W[0,:,:3,3])/e;v1=(W[1,:,:3,3]-W[ib,:,:3,3])/e
        a0=np.array([angular_step(r[0],r[ia],e) for r in R.values()]);a1=np.array([angular_step(r[ib],r[1],e) for r in R.values()])
        row={'epsilon_s':e,'measure_kind':'one-sided finite chord difference (not analytic derivative continuity)','joint_linear_gap_max_m_s':float(np.max(np.linalg.norm(v1-v0,axis=1))),'joint_linear_derivative_error_max_m_s':float(max(np.max(np.linalg.norm(v0-D[0,:,:3,3],axis=1)),np.max(np.linalg.norm(v1-D[1,:,:3,3],axis=1)))),'joint_angular_gap_max_deg_s':float(np.degrees(np.max(np.linalg.norm(a1-a0,axis=1)))),'skinned':[]}
        for pr,p,d in zip(g.primitives,P,VD):
            a=(p[ia]-p[0])/e;b=(p[1]-p[ib])/e
            row['skinned'].append({'name':pr['name'],'primitive':pr['primitive'],'velocity_gap_max_m_s':float(np.max(np.linalg.norm(b-a,axis=1))),'analytic_derivative_error_max_m_s':float(max(np.max(np.linalg.norm(a-d[0],axis=1)),np.max(np.linalg.norm(b-d[1],axis=1))))})
        rows.append(row)
    return rows

def main():
    ap=argparse.ArgumentParser(description='Independently replay a unit-key CUBICSPLINE GLB from actual bytes. Requires Python 3 + numpy only.')
    ap.add_argument('--glb',required=True,type=Path)
    ap.add_argument('--source-curves',required=True,type=Path)
    ap.add_argument('--source-analytic-fk',required=True,type=Path)
    ap.add_argument('--output-dir',required=True,type=Path)
    args=ap.parse_args();args.output_dir.mkdir(parents=True,exist_ok=True)
    g=GLB(args.glb);native=Native(args.source_curves);source_analytic=json.load(open(args.source_analytic_fk))
    assert native.doc['source_sha256']==source_analytic['sha256'],'Source provenance mismatch'
    assert native.doc['action']==source_analytic['action']==g.doc['animations'][0]['name'],'Action mismatch'
    assert g.validation['key_quaternion_norm_residual_max']<1e-6,'Expected strict unit-key candidate'
    report={'schema':'independent-unit-glb-validation/1','glb':str(args.glb),'source_sha256':native.doc['source_sha256'],'source_curves_sha256':hashlib.sha256(args.source_curves.read_bytes()).hexdigest(),'source_analytic_fk_sha256':hashlib.sha256(args.source_analytic_fk.read_bytes()).hexdigest(),'method':'Actual binary float64 Hermite with per-second tangents; normalized quaternion differential; full scale-aware FK and polar angular derivative; every stored skin influence normalized across all influence sets. Finite chords are separate from analytic endpoint residuals. Native skin comparison uses the same GLB mesh and inverse binds to isolate animation; direct Blender skin fidelity belongs to a separate native capture.'}
    report.update(endpoint_report(g,native,args.source_analytic_fk))
    NW,ND=native.world([0,1]);order=[native.names.index(n) for n in g.joint_names];NW=NW[:,order];ND=ND[:,order];W,D,_=g.world([0,1])
    for pr,row in zip(g.primitives,report['skinned_geometry']):
        sv=g.skin(ND,pr);v=g.skin(D,pr)
        row['native_source_velocity_gap_m_s']=stats(np.linalg.norm(sv[1]-sv[0],axis=-1));row['native_velocity_error_m_s']=stats(np.linalg.norm(v-sv,axis=-1))
    (args.output_dir/'endpoint_report.json').write_text(json.dumps(report,indent=2))
    report['finite_epsilon_convergence']=convergence(g)
    knots=np.unique(np.concatenate([k[:,0]/native.fps for k,_ in native.curves.values()]));times=np.unique(np.concatenate([knots]+[knots[:-1]+(knots[1:]-knots[:-1])*f for f in [.25,.5,.75]]+[np.array([.125,.875])]))
    NW,ND=native.world(times);NW=NW[:,order];ND=ND[:,order];W,D,minnorm=g.world(times);valid=[i for i,n in enumerate(g.joint_names) if n!='Prop_Magazine_B'];middle=(times>=.125)&(times<=.875)
    report['interior']={'sample_count':len(times),'middle75_sample_count':int(middle.sum()),'min_interpolated_quaternion_norm':minnorm,'all_world_matrix_abs_error':float(np.max(np.abs(W-NW))),'middle75_world_matrix_abs_error':float(np.max(np.abs(W[middle]-NW[middle]))),'all_world_position_error_m':stats(np.linalg.norm(W[:,:,:3,3]-NW[:,:,:3,3],axis=-1)),'all_world_orientation_error_deg':stats(np.degrees(angular_distance(polar(W[:,valid]),polar(NW[:,valid])))),'middle75_world_orientation_error_deg':stats(np.degrees(angular_distance(polar(W[middle][:,valid]),polar(NW[middle][:,valid])))),'skinned_geometry':[]}
    offknots=~np.isin(times,knots)&middle
    om=np.stack([polar(W[offknots,i],D[offknots,i])[1] for i in valid],axis=1);nom=np.stack([polar(NW[offknots,i],ND[offknots,i])[1] for i in valid],axis=1)
    report['middle75_interior_derivatives']={'samples':int(offknots.sum()),'joint_linear_velocity_error_m_s':stats(np.linalg.norm(D[offknots,:,:3,3]-ND[offknots,:,:3,3],axis=-1)),'joint_angular_velocity_error_deg_s':stats(np.degrees(np.linalg.norm(om-nom,axis=-1)))}
    for pr in g.primitives:
        errors=[];miderrors=[]
        for start in range(0,len(times),24):
            stop=min(start+24,len(times));p=g.skin(W[start:stop],pr);q=g.skin(NW[start:stop],pr);er=np.linalg.norm(p-q,axis=-1);errors.append(er.ravel());miderrors.append(er[middle[start:stop]].ravel())
        report['interior']['skinned_geometry'].append({'name':pr['name'],'primitive':pr['primitive'],'vertices':len(pr['p']),'max_influences':pr['max_influences'],'all_position_error_m':stats(np.concatenate(errors)),'middle75_position_error_m':stats(np.concatenate(miderrors))})
    report['sample_times_s']=times.tolist();(args.output_dir/'binary_unit_validation.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'glb_sha256':g.sha,'joint_count':len(g.joints),'analytic_endpoint_joint_linear_gap_max_m_s':report['worst_linear']['linear_velocity_gap_m_s'],'analytic_endpoint_joint_angular_gap_max_deg_s':report['worst_angular']['angular_velocity_gap_deg_s'],'all_joint_endpoint_pose_matrix_gap_max':max(x['endpoint_pose_matrix_abs_gap'] for x in report['bones']),'report':str(args.output_dir/'binary_unit_validation.json')},indent=2))
if __name__=='__main__':main()
