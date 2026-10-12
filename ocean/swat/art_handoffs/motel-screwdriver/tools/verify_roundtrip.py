"""Blender: --python tools/verify_roundtrip.py -- /package. Read-only geometry verification."""
import bpy,sys,json,hashlib,struct,collections,itertools
import numpy as np
from pathlib import Path
root=Path(sys.argv[sys.argv.index('--')+1]).resolve()
def record():
 out=[];count=0;pts=[]
 for o in bpy.context.scene.objects:
  if o.type!='MESH':continue
  count+=1;m=o.data;m.calc_loop_triangles();uv=m.uv_layers.active.data
  for v in m.vertices:pts.append(o.matrix_world@v.co)
  for tri in m.loop_triangles:
   corners=[]
   for vi,li in zip(tri.vertices,tri.loops):
    p=o.matrix_world@m.vertices[vi].co;u=uv[li].uv;corners.append(tuple(float(v) for v in (*p,*u)))
   out.append(tuple(sorted(corners)))
 return {'triangles':sorted(out),'mesh_object_count':count,'bounds':[[min(p[i] for p in pts),max(p[i] for p in pts)] for i in range(3)]}
bpy.ops.wm.open_mainfile(filepath=str(root/'editable/screwdriver_250mm_MAIN.blend'));a=record();bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(root/'runtime/wood_handle_flathead_screwdriver_250mm_MAIN.glb'));b=record()
aa=np.array(a['triangles']);bb=np.array(b['triangles']);ac=aa[:,:,:3].mean(axis=1);bc=bb[:,:,:3].mean(axis=1);used=set();miss=[];maxpos=0.;maxuv=0.
for i,t in enumerate(aa):
 ids=np.where(np.max(np.abs(bc-ac[i]),axis=1)<2e-6)[0];found=False
 for j in ids:
  if int(j) in used:continue
  for perm in itertools.permutations(range(3)):
   delta=np.abs(t-bb[j,list(perm)]);dp=float(delta[:,:3].max());du=float(delta[:,3:].max())
   if dp<2e-6 and du<2e-6:
    used.add(int(j));maxpos=max(maxpos,dp);maxuv=max(maxuv,du);found=True;break
  if found:break
 if not found:miss.append(i)
r={'comparison':'One-to-one triangle-corner position+UV match, all corner permutations, per-component tolerance 2e-6. No rounding-grid comparison; measured float32 conversion errors reported.','source_mesh_objects':a['mesh_object_count'],'reimport_mesh_objects':b['mesh_object_count'],'source_triangles':len(aa),'reimport_triangles':len(bb),'unmatched_source_triangles':len(miss),'unmatched_reimport_triangles':len(bb)-len(used),'geometry_uv_roundtrip_pass':not miss and len(used)==len(bb),'maximum_position_component_error_metres':maxpos,'maximum_uv_component_error':maxuv,'bounds_source':a['bounds'],'bounds_reimport':b['bounds']};(root/'qa/roundtrip_report.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2));assert r['geometry_uv_roundtrip_pass']
