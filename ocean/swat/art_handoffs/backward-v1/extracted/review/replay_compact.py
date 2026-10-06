"""Python+NumPy independent complete skin replay using portable Blender references."""
import argparse,json,hashlib
from pathlib import Path
import numpy as np
from replay_unit import GLB,C
def skin(D,p,w):
    P=np.c_[p,np.ones(len(p))];out=np.zeros((len(D),len(p),3))
    for j in range(w.shape[1]):
        keep=w[:,j]>0
        if keep.any():out[:,keep]+=np.einsum('tij,vj->tvi',D[:,j,:3,:],P[keep])*w[keep,j,None]
    return out
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--package',required=True,type=Path);ap.add_argument('--captures',required=True,type=Path);ap.add_argument('--output',required=True,type=Path);args=ap.parse_args();p=args.captures;m=json.loads((p/'capture_manifest.json').read_text())
    for name,row in m['files'].items():assert (p/name).stat().st_size==row['bytes'] and hashlib.sha256((p/name).read_bytes()).hexdigest()==row['sha256'],name
    s=np.load(p/'source.npz');r=np.load(p/'reimport.npz');maps=np.load(p/'vertex_correspondence.npz');g=GLB(args.package/'walk_backward_shared_ready_n_c1_loop_a.glb');assert g.sha==m['reimport']['glb_sha256']
    times=s['times'];controls=s['control_indices'];assert np.array_equal(times,r['times']);names=list(s['bone_names']);order=[names.index(n) for n in g.joint_names];W,_,_=g.world(times);SD=s['bone_world']@np.linalg.inv(s['bone_rest']);rows=[]
    for pr in g.primitives:
        mi=m['source']['mesh_names'].index(pr['name']);ids=maps[f'{mi}_{pr["primitive"]}'];offset=sum(len(pr0['p']) for pr0 in g.primitives if pr0['name']==pr['name'] and pr0['primitive']<pr['primitive']);ri=np.arange(offset,offset+len(ids));sw=s[f'w{mi}'][ids];sw/=sw.sum(1)[:,None];rw=r[f'w{mi}'][ri];rw/=rw.sum(1)[:,None];errs={k:0. for k in ['byte_to_source','byte_to_cubic_reimport','source_to_cubic_reimport','source_to_stock_reimport','source_actual_control','cubic_reimport_actual_control','stock_actual_control']}
        for start in range(0,len(times),12):
            stop=min(start+12,len(times));src=skin(SD[start:stop],s[f'p{mi}'][ids],sw);cu=skin(r['cubic_deformation'][start:stop],r[f'p{mi}'][ri],rw);stock=skin(r['stock_deformation'][start:stop],r[f'p{mi}'][ri],rw);pred=g.skin(W[start:stop],pr)@C[:3,:3]
            for key,a,b in [('byte_to_source',pred,src),('byte_to_cubic_reimport',pred,cu),('source_to_cubic_reimport',src,cu),('source_to_stock_reimport',src,stock)]:errs[key]=max(errs[key],float(np.linalg.norm(a-b,axis=2).max()))
            for ci,index in enumerate(controls):
                if start<=index<stop:
                    for key,predicted,reference in [('source_actual_control',src,s[f'eval{mi}'][ci,ids]),('cubic_reimport_actual_control',cu,r[f'cubic_eval{mi}'][ci,ri]),('stock_actual_control',stock,r[f'stock_eval{mi}'][ci,ri])]:errs[key]=max(errs[key],float(np.linalg.norm(predicted[index-start]-reference,axis=1).max()))
        assert max(v for k,v in errs.items() if k!='source_to_stock_reimport')<2e-5,(pr['name'],errs)
        rows.append({'name':pr['name'],'primitive':pr['primitive'],'vertices':len(ids),'maximum_errors_m':errs})
    report={'status':'PASS','glb_sha256':g.sha,'matrix_sample_count':len(times),'direct_full_surface_control_count':len(controls),'total_exported_vertices':sum(x['vertices'] for x in rows),'method':m['method'],'meshes':rows,'qualification':'Stock Blender import discards cubic tangent fidelity. Only the explicit byte-evaluated cubic-aware imported skin passes the cubic animation parity claim.'}
    args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
