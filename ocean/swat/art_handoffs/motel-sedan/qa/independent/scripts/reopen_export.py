"""Reopen exact copied packed .blend while all referenced external maps are absent."""
import bpy,sys,json,hashlib,os
from pathlib import Path
args=sys.argv[sys.argv.index('--')+1:];out=Path(args[0]);report={'source':bpy.data.filepath,'source_sha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),'images':[]}
for im in bpy.data.images:
 if im.source=='FILE':
  pp=Path(bpy.path.abspath(im.filepath));a={'name':im.name,'external_path':str(pp),'external_exists':pp.exists(),'packed':bool(im.packed_file),'size':list(im.size),'pixels_count':len(im.pixels),'has_data':im.has_data,'packed_sha256':hashlib.sha256(im.packed_file.data).hexdigest()if im.packed_file else None};report['images'].append(a)
  assert not pp.exists(),f'External image unexpectedly exists: {pp}'
  assert im.packed_file and im.has_data and len(im.pixels)==512*512*4
report['all_missing_external_maps_loaded_from_pack']=True
root=bpy.data.objects['BC_SEDAN01'];groups={}
for o in bpy.data.objects:
 if o.type=='MESH':groups.setdefault(o['runtime_group'],[]).append(o)
for g,obs in sorted(groups.items()):
 bpy.ops.object.select_all(action='DESELECT')
 for o in obs:o.select_set(True)
 bpy.context.view_layer.objects.active=obs[0];bpy.ops.object.convert(target='MESH');bpy.ops.object.join();o=bpy.context.object;o.name='BC_SEDAN01_'+g;o.data.name=o.name;o['role']=g;o.location=(0,0,0)
bpy.ops.object.select_all(action='DESELECT');root.select_set(True)
for o in root.children:o.select_set(True)
p=out/'packed_reexport.glb'
bpy.ops.export_scene.gltf(filepath=str(p),export_format='GLB',use_selection=True,export_apply=True,export_yup=True,export_materials='EXPORT',export_extras=True,export_cameras=False,export_lights=False,export_animations=False,export_normals=True,export_texcoords=True)
report['export_sha256']=hashlib.sha256(p.read_bytes()).hexdigest();(out/'packed_reopen_report.json').write_text(json.dumps(report,indent=2));print('INDEPENDENT_PACKED_REOPEN_OK')
