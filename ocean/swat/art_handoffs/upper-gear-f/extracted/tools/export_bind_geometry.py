"""Read the pinned F source and export only its selected character bind geometry.
Usage: blender -b --python export_bind_geometry.py -- SOURCE.blend OUT.glb
The source file is never saved. Animation is added separately from evaluated transforms.
"""
import bpy,hashlib,json,sys
from pathlib import Path
source,out=map(Path,sys.argv[sys.argv.index('--')+1:])
h=hashlib.sha256(source.read_bytes()).hexdigest()
assert h=='4271f2279bc3cbbcb46594157752b2ac0ca052628c24b4c8629a82c6771a3fc0'
bpy.ops.wm.open_mainfile(filepath=str(source.resolve()))
rig=bpy.data.objects['SWAT_Mixamo_Rig'];assert len(rig.data.bones)==72
rig.animation_data.action=bpy.data.actions['Neutral Carry / Anatomical Gear F']
bpy.context.scene.frame_set(0)
meshes=[o for o in bpy.data.objects if o.type=='MESH' and any(m.type=='ARMATURE'and m.object==rig for m in o.modifiers)]
assert len(meshes)==9 and not any(o.name=='Rebuild Review Floor'for o in meshes)
for o in list(bpy.context.selected_objects):o.select_set(False)
for o in [rig,*meshes]:
 assert not o.hide_render
 o.hide_set(False);o.select_set(True)
bpy.context.view_layer.objects.active=rig
options=dict(filepath=str(out.resolve()),export_format='GLB',use_selection=True,
 export_yup=True,export_apply=False,export_animations=False,export_current_frame=False,
 export_rest_position_armature=True,export_skins=True,export_all_influences=True,
 export_influence_nb=8,export_def_bones=False,export_texcoords=True,export_normals=True,
 export_tangents=False,export_materials='EXPORT',export_cameras=False,export_lights=False,
 export_attributes=False,export_extras=False)
bpy.ops.export_scene.gltf(**options)
assert hashlib.sha256(source.read_bytes()).hexdigest()==h
report={'source_sha256':h,'blender':bpy.app.version_string,'bind_geometry_glb_sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'mesh_names':[o.name for o in meshes],'excluded_scene_objects':[o.name for o in bpy.data.objects if o not in [rig,*meshes]],'export_options':options,'scope':'Fresh geometry/skin/material export for the72-bone derivative; no source save, animation resampling or default-pose bake'}
out.with_suffix('.export.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
