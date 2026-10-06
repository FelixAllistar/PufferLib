"""Independent actual-GLB sampling and all-influence deformation validation.

python validate_fixture.py FIXTURE.glb PARITY_WORK OUTPUT.json
Requires NumPy and SciPy, used only for validation, not by the consumer.
"""
import collections, hashlib, json, struct, sys
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree

glb, work, output = map(Path,sys.argv[1:])
raw=glb.read_bytes()
assert struct.unpack_from('<III',raw)==(0x46546c67,2,len(raw))
size,kind=struct.unpack_from('<II',raw,12); assert kind==0x4e4f534a
doc=json.loads(raw[20:20+size]); binsize,binkind=struct.unpack_from('<II',raw,20+size)
assert binkind==0x004e4942
binary=raw[28+size:28+size+binsize]
def acc(index):
    a=doc['accessors'][index]; v=doc['bufferViews'][a['bufferView']]
    assert not a.get('sparse') and v.get('buffer',0)==0
    dt=np.dtype({5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']])
    width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
    x=np.ndarray((a['count'],width),dtype=dt,buffer=binary,
                 offset=v.get('byteOffset',0)+a.get('byteOffset',0),
                 strides=(v.get('byteStride',dt.itemsize*width),dt.itemsize)).copy()
    if a.get('normalized'): x=x.astype(float)/np.iinfo(dt).max
    assert np.isfinite(x).all()
    return x

times=np.load(work/'sample_times.npy')
source=np.load(work/'source.npz'); reimport=np.load(work/'reimport.npz'); stock=np.load(work/'stock_reimport.npz')
capture=json.loads((work/'capture_report.json').read_text())
assert hashlib.sha256(raw).hexdigest()==capture['glb_sha256']
nodes=doc['nodes']; skin=doc['skins'][0]; joints=skin['joints']
bn=list(source['bone_names']); bi={n:i for i,n in enumerate(bn)}
joint_order=np.array([bi[nodes[i]['name']] for i in joints])
parent={child:i for i,n in enumerate(nodes) for child in n.get('children',[])}
assert len(parent)==sum(len(n.get('children',[])) for n in nodes)
C=np.array([[1,0,0,0],[0,0,-1,0],[0,1,0,0],[0,0,0,1]],float) # glTF -> Blender
def rotation(q):
    q=q/np.linalg.norm(q,axis=-1,keepdims=True)
    x,y,z,w=np.moveaxis(q,-1,0); r=np.empty(q.shape[:-1]+(3,3))
    r[...,0,0]=1-2*(y*y+z*z);r[...,0,1]=2*(x*y-z*w);r[...,0,2]=2*(x*z+y*w)
    r[...,1,0]=2*(x*y+z*w);r[...,1,1]=1-2*(x*x+z*z);r[...,1,2]=2*(y*z-x*w)
    r[...,2,0]=2*(x*z-y*w);r[...,2,1]=2*(y*z+x*w);r[...,2,2]=1-2*(x*x+y*y)
    return r
from gltf_math import GLB
parsed=GLB(glb)
def world_at(animation,ts):return parsed.world_at(animation,ts)
ibm=acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1).astype(float)
rest=world_at(None,np.array([0.]))[0]
world=world_at(doc['animations'][0],times)
deformation=np.empty_like(source['deformation'])
deformation[:,joint_order]=C@world[:,joints]@ibm@C.T
def normalized(w):return w/w.sum(axis=1)[:,None]
def match(p,w,sp,sw):
    dist,idx=cKDTree(sp).query(p,k=min(16,len(sp)))
    err=np.max(abs(w[:,None,:]-sw[idx]),axis=2)
    pick=np.argmin(dist+err,axis=1);ids=idx[np.arange(len(p)),pick]
    return ids,float(np.linalg.norm(p-sp[ids],axis=1).max()),float(abs(w-sw[ids]).max())
def skin_points(p,w,d):
    result=np.zeros((len(d),len(p),3));w=normalized(w)
    for j in range(w.shape[1]):
        v=np.flatnonzero(w[:,j])
        if len(v): result[:,v]+=(np.einsum('nij,vj->nvi',d[:,j,:3,:3],p[v])+d[:,j,None,:3,3])*w[v,j][None,:,None]
    return result
def triangles(indices,canonical):
    rows=[]
    for a,b,c in canonical[indices]:rows.append(min((int(a),int(b),int(c)),(int(b),int(c),int(a)),(int(c),int(a),int(b))))
    return collections.Counter(rows)
report={**capture,'schema':'swat-import-fixture-validation/1','status':'checking',
        'glb_nodes':len(nodes),'glb_joints':len(joints),'animations':[a['name']for a in doc['animations']],
        'interpolation_modes':sorted({s.get('interpolation','LINEAR')for a in doc['animations']for s in a['samplers']}),
        'joint_names_match':set(nodes[j]['name']for j in joints)==set(bn),
        'joint_parents_match':all((nodes[parent[j]]['name']if parent.get(j)in joints else '')==source['parents'][bi[nodes[j]['name']]]for j in joints),
        'inverse_bind_rest_identity_max_element_error':float(abs(rest[joints]@ibm-np.eye(4)).max()),
        'standard_gltf_deformation_max_element_error':float(abs(deformation-source['deformation']).max()),
        'meshes':[]}
for mi,name in enumerate(capture['mesh_names']):
    ni=next(i for i,n in enumerate(nodes)if n.get('name')==name);node=nodes[ni]
    positions=[];weights=[];tris=[];offset=0;normal_error=0;sets=[]
    for primitive in doc['meshes'][node['mesh']]['primitives']:
        a=primitive['attributes'];p=acc(a['POSITION']).astype(float)@C[:3,:3].T
        normals=acc(a['NORMAL']);normal_error=max(normal_error,float(abs(np.linalg.norm(normals,axis=1)-1).max()))
        w=np.zeros((len(p),len(bn)));weightsets=sorted(int(k.split('_')[1])for k in a if k.startswith('WEIGHTS_'))
        sets.append(len(weightsets))
        for g in weightsets:
            js=acc(a[f'JOINTS_{g}']).astype(int);ws=acc(a[f'WEIGHTS_{g}']).astype(float)
            assert js.min()>=0 and js.max()<len(joints) and ws.min()>=0
            for k in range(4):np.add.at(w,(np.arange(len(p)),joint_order[js[:,k]]),ws[:,k])
        positions.append(p);weights.append(w);tris.append(acc(primitive['indices']).ravel().reshape(-1,3)+offset);offset+=len(p)
    p=np.concatenate(positions);w=np.concatenate(weights);gt=np.concatenate(tris)
    sp=source[f'p{mi}'];sw=normalized(source[f'w{mi}']);st=source[f'tris{mi}']
    ip=reimport[f'p{mi}'];iw=normalized(reimport[f'w{mi}'])
    ids,position_error,weight_error=match(p,normalized(w),sp,sw)
    imported_ids,ip_error,iw_error=match(ip,iw,sp,sw)
    # Canonicalize source duplicates with identical rounded bind and weight data.
    features={};canonical=[]
    for v in range(len(sp)):
        key=tuple(np.round(sp[v],7))+tuple(np.round(sw[v],7))
        canonical.append(features.setdefault(key,len(features)))
    canonical=np.array(canonical)
    surface_equal=triangles(gt,canonical[ids])==triangles(st,canonical)
    source_used=set(canonical[st.ravel()]);glb_used=set(canonical[ids[gt.ravel()]])
    row={'name':name,'source_vertices':len(sp),'glb_vertices_with_seams':len(p),
         'source_rendered_vertices':len(set(st.ravel())),'source_loose_vertices':len(sp)-len(set(st.ravel())),
         'source_triangles':len(st),'glb_triangles':len(gt),'oriented_surface_triangles_preserved':surface_equal,
         'rendered_bind_weight_vertices_preserved':source_used==glb_used,
         'bind_position_max_error_m':position_error,'normalized_weight_max_error':weight_error,
         'source_raw_weight_sum_range':[float(source[f'w{mi}'].sum(axis=1).min()),float(source[f'w{mi}'].sum(axis=1).max())],
         'glb_weight_sum_max_error':float(abs(w.sum(axis=1)-1).max()),
         'source_max_influences':int((sw>0).sum(axis=1).max()),'glb_max_influences':int((w>0).sum(axis=1).max()),
         'glb_vertices_over_four_influences':int(((w>0).sum(axis=1)>4).sum()),'weight_set_counts':sets,
         'normal_length_max_error':normal_error,'reimport_bind_error_m':ip_error,'reimport_weight_error':iw_error}
    maxerr=maxierr=maxstockerr=source_eval_err=import_eval_err=0.;lo=np.full(3,np.inf);hi=np.full(3,-np.inf)
    for start in range(0,len(times),16):
        end=min(start+16,len(times));s=skin_points(sp,sw,source['deformation'][start:end]);g=skin_points(p,w,deformation[start:end]);i=skin_points(ip,iw,reimport['deformation'][start:end])
        maxerr=max(maxerr,float(np.linalg.norm(g-s[:,ids],axis=2).max()))
        maxierr=max(maxierr,float(np.linalg.norm(i-s[:,imported_ids],axis=2).max()))
        stp=skin_points(ip,iw,stock['deformation'][start:end]);maxstockerr=max(maxstockerr,float(np.linalg.norm(stp-s[:,imported_ids],axis=2).max()))
        lo=np.minimum(lo,g.min(axis=(0,1)));hi=np.maximum(hi,g.max(axis=(0,1)))
        for ci in capture['control_indices']:
            if start<=ci<end:
                source_eval_err=max(source_eval_err,float(np.linalg.norm(s[ci-start]-source[f'eval_{ci}_{mi}'],axis=1).max()))
                import_eval_err=max(import_eval_err,float(np.linalg.norm(i[ci-start]-reimport[f'eval_{ci}_{mi}'],axis=1).max()))
    row.update(max_vertex_difference_standard_gltf_m=maxerr,max_vertex_difference_reimport_m=maxierr,max_vertex_difference_stock_blender_reimport_m=maxstockerr,
               source_formula_vs_blender_evaluated_m=source_eval_err,reimport_formula_vs_blender_evaluated_m=import_eval_err,
               sampled_bounds_blender_xyz_m=[lo.tolist(),hi.tolist()])
    report['meshes'].append(row)
    output.write_text(json.dumps(report,indent=2))
    print('MESH',json.dumps(row),flush=True)
report['max_vertex_difference_standard_gltf_m']=max(x['max_vertex_difference_standard_gltf_m']for x in report['meshes'])
report['max_vertex_difference_reimport_m']=max(x['max_vertex_difference_reimport_m']for x in report['meshes'])
report['max_vertex_difference_stock_blender_reimport_m']=max(x['max_vertex_difference_stock_blender_reimport_m']for x in report['meshes'])
report['stock_blender_reimport_status']='DIRECT_SINGLE_STEP_KEY_ROUNDTRIP'
report['reimport_method']='Unmodified stock Blender4.3.2 glTF import; no pose adapter'
report['pass_criteria']={'rendered_bind_and_weight_vertices_preserved':True,'oriented_surface_triangles_preserved':True,
                         'maximum_skin_difference_m':0.0001,'note':'Numerical export fidelity gate only'}
report['status']='PASS'if report['joint_names_match']and report['joint_parents_match']and all(x['rendered_bind_weight_vertices_preserved']and x['oriented_surface_triangles_preserved']for x in report['meshes'])and max(report['max_vertex_difference_standard_gltf_m'],report['max_vertex_difference_reimport_m'])<.0001 else 'FAIL'
report['limitations']=['No engine/ozz/GPU playback test','No artistic or full contact-clearance approval','No gameplay timing adaptation','Single static pose only; no duration, interpolation or loop behavior is authored','No complete Khronos glTF validator run','Material appearance and animated normal parity not independently certified']
output.write_text(json.dumps(report,indent=2))
print('VALIDATION',report['status'],report['max_vertex_difference_standard_gltf_m'],report['max_vertex_difference_reimport_m'],flush=True)
