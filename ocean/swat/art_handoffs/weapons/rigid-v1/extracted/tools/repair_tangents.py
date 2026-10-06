"""Orthonormalize generated tangent frames; leave geometry, UVs and images intact."""
import hashlib,json,struct
from pathlib import Path
import numpy as np

def repair(path):
    path=Path(path);raw=bytearray(path.read_bytes());before=hashlib.sha256(raw).hexdigest()
    length=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+length]);binstart=28+length;rows=[]
    def accessor(index):
        a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']];w={'VEC3':3,'VEC4':4}[a['type']]
        assert a['componentType']==5126
        return np.ndarray((a['count'],w),'<f4',buffer=raw,offset=binstart+v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',4*w),4))
    for mesh in doc['meshes']:
        for pr in mesh['primitives']:
            attrs=pr['attributes'];n=accessor(attrs['NORMAL']).astype(float);t=accessor(attrs['TANGENT']);old=t[:,:3].astype(float)
            n/=np.linalg.norm(n,axis=1,keepdims=True);dot=np.sum(old*n,axis=1);lengths=np.linalg.norm(old,axis=1)
            mask=(abs(dot)>1e-6)|(abs(lengths-1)>1e-6);strict=(abs(dot)>1e-4)|(abs(lengths-1)>1e-4)
            fixed=old[mask]-n[mask]*dot[mask,None];ln=np.linalg.norm(fixed,axis=1);fallback=ln<1e-6
            for i in np.flatnonzero(fallback):
                normal=n[mask][i];axis=np.eye(3)[np.argmin(abs(normal))];fixed[i]=np.cross(axis,normal)
            if len(fixed):fixed/=np.linalg.norm(fixed,axis=1,keepdims=True);t[mask,:3]=fixed.astype('<f4')
            rows.append({'mesh':mesh.get('name'),'vertices':len(old),'reorthonormalized':int(mask.sum()),
                         'original_invalid_at_1e_4':int(strict.sum()),'degenerate_direction_fallbacks':int(fallback.sum()),
                         'fallback_rule':'Cross normal with its least-parallel coordinate axis; preserve source tangent handedness'})
    path.write_bytes(raw)
    return {'input_sha256':before,'output_sha256':hashlib.sha256(raw).hexdigest(),'meshes':rows,
            'scope':'Only TANGENT xyz bytes changed. Positions, indices, UVs, normals, images, materials and tangent handedness remain unchanged.'}

if __name__=='__main__':
    import sys
    out=Path(sys.argv[1]);reports=[]
    for filename,recipe,bindings in [('rifle7_rigid_textured.glb','recipe.json','rifle_bindings.json'),('sidearm50_rigid_source_scale.glb','sidearm_recipe.json','sidearm_bindings.json')]:
        result=repair(out/filename);result['file']=filename;reports.append(result)
        r=json.loads((out/recipe).read_text());r['export_sha256']=result['output_sha256'];r['tangent_postprocess']=result;(out/recipe).write_text(json.dumps(r,indent=2))
        if(out/bindings).exists():
            b=json.loads((out/bindings).read_text());b['asset_sha256']=result['output_sha256'];(out/bindings).write_text(json.dumps(b,indent=2))
    (out/'tangent_validation_fix.json').write_text(json.dumps(reports,indent=2));print(json.dumps(reports,indent=2))
