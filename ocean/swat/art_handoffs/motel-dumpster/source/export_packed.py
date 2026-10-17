"""Export the packed editable .blend without loose texture files.
blender -b source/BC_DUMP01_editable.blend --python source/export_packed.py -- /tmp/packed.glb
Consolidates source parts as the runtime does. Equivalent content, byte identity is not promised.
"""
import bpy,sys
root=bpy.data.objects['BC_DUMP01_ROOT'];parts=[o for o in bpy.data.objects if o.type=='MESH'];runtime=[]
bpy.ops.object.select_all(action='DESELECT')
for group in ['Body','Lid_Left','Lid_Right']:
 subset=[o for o in parts if (o.name.startswith(group+'_') if group!='Body' else not o.name.startswith(('Lid_Left_','Lid_Right_')))]
 copies=[]
 for o in subset:
  c=o.copy();c.data=o.data.copy();bpy.context.collection.objects.link(c);copies.append(c);c.select_set(True)
 bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();joined=bpy.context.object;joined.name='BC_DUMP01_'+group;joined['source_parts']=[o.name for o in subset];runtime.append(joined);joined.select_set(False)
root.select_set(True)
for o in runtime:o.select_set(True)
out=sys.argv[sys.argv.index('--')+1]
bpy.ops.export_scene.gltf(filepath=out,export_format='GLB',use_selection=True,export_yup=True,export_texcoords=True,export_normals=True,export_materials='EXPORT',export_apply=True,export_extras=True)
