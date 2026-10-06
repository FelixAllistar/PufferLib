"""Expand algebraic LBS from complete captured Blender joint poses, preserving direct controls separately.
Input z arrays already have time first. No glTF accessors, inverse binds or producer code are used.
"""
import numpy as np

def reconstruct(meta,z):
 if not meta.get('direct_evaluated_control_indices'):return z,None
 direct={k:v for k,v in z.items() if '_eval' in k};rest_inv=np.linalg.inv(z['bone_rest']);report={'mode':'all-time algebraic skin from independently captured Blender joint matrices, bind positions and normalized native weights','dense_direct_evaluated_arrays_included':False,'direct_control_times_s':meta['direct_evaluated_control_times_s'],'direct_control_checks':[]};z=dict(z)
 for label in ['neutral','motion']:
  times=z[label+'_times'];X=z[label+'_bones'].astype(float)@rest_inv;ci=meta['direct_evaluated_control_indices'][label]
  for mi,name in enumerate(meta['mesh_names']):
   p=np.c_[z[f'p{mi}'],np.ones(len(z[f'p{mi}']))];w=z[f'w{mi}'].copy();w/=w.sum(1)[:,None];width=int(np.count_nonzero(w,axis=1).max());j=np.zeros((len(w),width),int);ww=np.zeros((len(w),width))
   for vi,row in enumerate(w):
    ix=np.flatnonzero(row);j[vi,:len(ix)]=ix;ww[vi,:len(ix)]=row[ix]
   poses=np.empty((len(times),len(p),3),dtype=np.float64)
   for ti in range(len(times)):
    out=np.zeros((len(p),3))
    for k in range(width):out+=np.einsum('vij,vj->vi',X[ti,j[:,k],:3],p)*ww[:,k,None]
    poses[ti]=out
   actual=direct[f'{label}_eval{mi}'];e=np.linalg.norm(poses[ci]-actual,axis=2);err=float(e.max());assert err<1e-5,(label,name,err)
   report['direct_control_checks'].append({'label':label,'name':name,'control_count':len(ci),'max_algebraic_to_direct_evaluated_error_m':err})
   z[f'{label}_eval{mi}']=poses
 return z,report
