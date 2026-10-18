"""Export packed editable source without external texture paths.
blender -b source/BC_SEDAN01_editable.blend --python source/export_packed.py -- --output /tmp/packed.glb
"""
import bpy,sys,argparse
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);args=ap.parse_args(sys.argv[sys.argv.index('--')+1:])
root=bpy.data.objects['BC_SEDAN01'];parts=sorted([o for o in root.children if o.type=='MESH'],key=lambda o:o['source_order'])
groups={g:[o for o in parts if o['runtime_group']==g] for g in sorted(set(o['runtime_group'] for o in parts))}
assert all(im.packed_file for im in bpy.data.images if im.source=='FILE'),'All runtime images must be packed'
for g,obs in groups.items():
 bpy.ops.object.select_all(action='DESELECT')
 for o in obs:o.select_set(True)
 bpy.context.view_layer.objects.active=obs[0];bpy.ops.object.convert(target='MESH');bpy.ops.object.join();o=bpy.context.object;o.name='BC_SEDAN01_'+g;o.data.name=o.name;o['role']=g;o.location=(0,0,0)
bpy.ops.object.select_all(action='DESELECT');root.select_set(True)
for o in root.children:o.select_set(True)
Path(args.output).parent.mkdir(parents=True,exist_ok=True)
bpy.ops.export_scene.gltf(filepath=args.output,export_format='GLB',use_selection=True,export_apply=True,export_yup=True,export_materials='EXPORT',export_extras=True,export_cameras=False,export_lights=False,export_animations=False,export_normals=True,export_texcoords=True)
print('PACKED_EXPORT_DONE',args.output)
