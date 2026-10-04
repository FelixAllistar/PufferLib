"""Independent numerical oracle, using NumPy/SciPy transforms and actual GLB bytes.
Synthetic checks run in CTest. Pass --asset to validate a local private fixture.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import numpy as np
from scipy.spatial.transform import Rotation, Slerp


from character_fixtures import synthetic


def decode(raw):
    magic,version,size=struct.unpack_from('<III',raw)
    assert magic==0x46546C67 and version==2 and size==len(raw)
    length,kind=struct.unpack_from('<II',raw,12); assert kind==0x4E4F534A
    doc=json.loads(raw[20:20+length]); binary=raw[28+length:]
    def access(index):
        a=doc['accessors'][index]; view=doc['bufferViews'][a['bufferView']]
        dtype=np.dtype({5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']]); width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        values=np.ndarray((a['count'],width),dtype=dtype,buffer=binary,offset=view.get('byteOffset',0)+a.get('byteOffset',0),strides=(view.get('byteStride',width*dtype.itemsize),dtype.itemsize)).copy()
        if a.get('normalized'): values=values.astype(float)/np.iinfo(dtype).max
        return values
    return doc,access


def reference(d,access,time):
    nodes=d['nodes']; parents={child:i for i,n in enumerate(nodes) for child in n.get('children',[])}
    values=[{'translation':np.array(n.get('translation',[0,0,0]),float),'rotation':np.array(n.get('rotation',[0,0,0,1]),float),'scale':np.array(n.get('scale',[1,1,1]),float)} for n in nodes]
    if time>=0:
        for channel in d['animations'][0]['channels']:
            s=d['animations'][0]['samplers'][channel['sampler']]; times=access(s['input']).ravel().astype(float); data=access(s['output']).astype(float); path=channel['target']['path']; t=np.clip(time,times[0],times[-1]); mode=s.get('interpolation','LINEAR')
            if mode=='CUBICSPLINE':
                data=data.reshape(len(times),3,-1)
                j=min(max(np.searchsorted(times,t,side='right')-1,0),len(times)-2)
                dt=times[j+1]-times[j]; u=(t-times[j])/dt
                v=(2*u**3-3*u**2+1)*data[j,1]+(u**3-2*u**2+u)*dt*data[j,2]+(-2*u**3+3*u**2)*data[j+1,1]+(u**3-u**2)*dt*data[j+1,0]
                if path=='rotation': v/=np.linalg.norm(v)
            elif mode=='STEP' or len(times)==1: v=data[np.clip(np.searchsorted(times,t,side='right')-1,0,len(times)-1)]
            elif path=='rotation': v=Slerp(times,Rotation.from_quat(data))([t]).as_quat()[0]
            else: v=np.array([np.interp(t,times,data[:,c]) for c in range(data.shape[1])])
            values[channel['target']['node']][path]=v
    world={}
    def transform(i):
        if i in world: return world[i]
        v=values[i]; local=np.eye(4); local[:3,:3]=Rotation.from_quat(v['rotation']).as_matrix() @ np.diag(v['scale']); local[:3,3]=v['translation']
        if 'matrix' in nodes[i]: local=np.array(nodes[i]['matrix']).reshape(4,4).T
        world[i]=transform(parents[i]) @ local if i in parents else local
        return world[i]
    matrices=np.array([transform(i) for i in range(len(nodes))]); meshes=[]
    for ni,node in enumerate(nodes):
        if 'mesh' not in node: continue
        for p in d['meshes'][node['mesh']]['primitives']:
            attrs=p['attributes']; pos=access(attrs['POSITION']).astype(float); normal=access(attrs['NORMAL']).astype(float)
            if 'skin' in node:
                skin=d['skins'][node['skin']]; inv=access(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1).astype(float) if 'inverseBindMatrices' in skin else np.tile(np.eye(4),(len(skin['joints']),1,1))
                palette=matrices[skin['joints']] @ inv
                ids=[]; weights=[]; k=0
                while f'JOINTS_{k}' in attrs: ids.append(access(attrs[f'JOINTS_{k}'])); weights.append(access(attrs[f'WEIGHTS_{k}'])); k+=1
                ids=np.concatenate(ids,axis=1).astype(int); weights=np.concatenate(weights,axis=1).astype(float)
                blended=np.einsum('vi,vijk->vjk',weights,palette[ids])
            else: blended=np.tile(matrices[ni],(len(pos),1,1))
            posed=np.einsum('vij,vj->vi',blended[:,:3,:3],pos)+blended[:,:3,3]
            determinants=np.linalg.det(blended[:,:3,:3]); good=abs(determinants)>1e-14
            normals=np.zeros_like(normal)
            if good.any():
                normals[good]=np.einsum('vji,vj->vi',np.linalg.inv(blended[good,:3,:3]),normal[good]); normals[good]/=np.linalg.norm(normals[good],axis=1)[:,None]
            meshes.append((bool(good.any()),posed,normals))
    return matrices,meshes


def run(probe,raw,times,work,label,limit=32,reject=False):
    asset=work/(label+'.glb'); asset.write_bytes(raw)
    samplefile=work/(label+'.txt'); samplefile.write_text(''.join(f'{t:.17g}\n' for t in times))
    output=work/(label+'.bin'); result=subprocess.run([str(probe),str(asset),str(samplefile),str(output),str(limit)],capture_output=True,text=True)
    if reject:
        assert result.returncode==1,(label,result.stdout,result.stderr); print('PASS reject',label); return
    assert result.returncode==0,(label,result.stdout,result.stderr)
    data=np.fromfile(output,dtype='<f4'); d,access=decode(raw); cursor=0; worst={'matrix':0.,'position':0.,'normal':0.}
    for time in times:
        matrices,meshes=reference(d,access,time); count=len(matrices)*16
        got=data[cursor:cursor+count].reshape(-1,4,4).transpose(0,2,1); cursor+=count
        worst['matrix']=max(worst['matrix'],float(abs(got-matrices).max()))
        for visible,positions,normals in meshes:
            assert data[cursor]==float(visible),(label,time,'visibility'); cursor+=1
            n=positions.size; gp=data[cursor:cursor+n].reshape(-1,3); cursor+=n; gn=data[cursor:cursor+n].reshape(-1,3); cursor+=n
            worst['position']=max(worst['position'],float(abs(gp-positions).max())); worst['normal']=max(worst['normal'],float(abs(gn-normals).max()))
    assert cursor==len(data) and np.isfinite(data).all()
    assert worst['matrix']<.0001 and worst['position']<.0001 and worst['normal']<.002,(label,worst)
    print(result.stdout.strip()); print('PASS numerical parity',label,worst)


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--probe',required=True,type=Path); parser.add_argument('--asset',type=Path); args=parser.parse_args()
    probe=args.probe.resolve()
    with tempfile.TemporaryDirectory(prefix='swat-character-') as root:
        work=Path(root)
        times=[-1,0,.25,.5,.999999,1,1.000001,2,.1] # backward seek and mixed channel modes
        run(probe,synthetic(),times,work,'seven-influence')
        run(probe,synthetic(),times,work,'four-influence',limit=4,reject=True)
        for bad in ['empty','bounds','cycle','joints','pair','duplicate','matrix_channel','external','weights','affine','material','parents','duplicate_child','emissive']:
            run(probe,synthetic(malformed=bad),[0],work,bad,reject=True)
        run(probe,synthetic()[:-1],[0],work,'truncated',reject=True)
        if args.asset:
            raw=args.asset.read_bytes(); d,access=decode(raw); times={-1.,0.,6.}
            for sampler in d['animations'][0]['samplers']:
                keys=access(sampler['input']).ravel(); times.update(float(t) for t in keys)
            # Every authored key, precise transfer-boundary neighborhoods, and
            # off-key times expose interpolation/resampling loss.
            times.update(float(t) for t in np.arange(.013,6,.137))
            for t in [.7,.88,1.05,1.4,1.928125,3.107862609329446,3.65,3.85,4.35,5.8,6]: times.update([t-1e-5,t,t+1e-5])
            run(probe,raw,sorted(times),work,'actual-private-fixture')
            run(probe,raw,[0],work,'actual-four-weight',limit=4,reject=True)

if __name__=='__main__': main()
