"""Load every GLB in clean Blender state and compare source bounds/triangles."""
import bpy, json, os, math, hashlib
from mathutils import Vector
ROOT=os.path.dirname(os.path.abspath(__file__))
manifest=json.load(open(os.path.join(ROOT,'manifest.json')))
report={'status':'pass','scope':'Asset integrity and Blender reimport only; no game runtime tests','assets':[],'native':{},'errors':[]}
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'furniture_variants.blend'))
images=[im for im in bpy.data.images if im.name.startswith('F04_Texture_')]
report['native']={'image_count':len(images),'all_images_packed':all(im.packed_file is not None for im in images),'metre_scale':bpy.context.scene.unit_settings.scale_length,'asset_count':sum('asset_id' in o for o in bpy.context.scene.objects)}
if not report['native']['all_images_packed']:report['errors'].append('Unpacked native image')
for row in manifest['assets']:
 bpy.ops.wm.read_factory_settings(use_empty=True)
 path=os.path.join(ROOT,row['file']);bpy.ops.import_scene.gltf(filepath=path)
 meshes=[o for o in bpy.context.scene.objects if o.type=='MESH'];positions=[o.matrix_world@v.co for o in meshes for v in o.data.vertices]
 mn=[min(p[i] for p in positions) for i in range(3)];mx=[max(p[i] for p in positions) for i in range(3)]
 bound_error=max(abs(x-y) for measured,expected in zip((mn,mx),row['source_bounds_z_up']) for x,y in zip(measured,expected))
 tri=0;degenerate=0;nonfinite=0;missing_uv=0
 for ob in meshes:
  me=ob.data;me.calc_loop_triangles();tri+=len(me.loop_triangles)
  if not me.uv_layers:missing_uv+=1
  for v in me.vertices:
   if any(not math.isfinite(q) for q in v.co):nonfinite+=1
  for t in me.loop_triangles:
   a,b,c=[me.vertices[i].co for i in t.vertices]
   if (b-a).cross(c-a).length<1e-12:degenerate+=1
 images=list(bpy.data.images)
 missing_images=[im.name for im in images if min(im.size)==0]
 got={'asset_id':row['asset_id'],'meshes':len(meshes),'triangles':tri,'source_triangles':row['triangles'],'bounds_max_abs_error_m':round(bound_error,9),'degenerate_triangles':degenerate,'nonfinite_vertices':nonfinite,'meshes_missing_uv':missing_uv,'decoded_images':len(images),'missing_images':missing_images,'sha256':hashlib.sha256(open(path,'rb').read()).hexdigest()}
 report['assets'].append(got)
 if tri!=row['triangles'] or bound_error>.0001 or degenerate or nonfinite or missing_uv or missing_images:report['errors'].append(row['asset_id']+' failed geometry/texture round-trip check')
if report['errors']:report['status']='fail'
with open(os.path.join(ROOT,'qa','reimport_report.json'),'w') as f:json.dump(report,f,indent=2)
print('REIMPORT_RESULT',json.dumps({'status':report['status'],'assets':len(report['assets']),'errors':report['errors']}))
if report['errors']:raise RuntimeError(str(report['errors']))
