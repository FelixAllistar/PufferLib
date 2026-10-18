"""Re-export a relocated packed editable source without its maps folder.
blender -b --python source/export_packed.py -- /path/BC_SHELTER01_editable.blend /path/output.glb
No external image reload. Equivalent surfaces/PBR, not binary identity, is the contract.
"""
import bpy, sys
from pathlib import Path
args=sys.argv[sys.argv.index('--')+1:]
assert len(args)==2, 'Supply packed .blend and output GLB after --'
bpy.ops.wm.open_mainfile(filepath=str(Path(args[0]).resolve()))
root=bpy.data.objects.get('BC_SHELTER01_ROOT');assert root
parts=[o for o in bpy.data.objects if o.type=='MESH'];anchors=[o for o in bpy.data.objects if o.name.startswith('ANCHOR_')]
assert len(parts)==160, 'Expected the original editable source; refusing an unexpected scene'
for im in bpy.data.images:
    if im.source=='FILE':assert im.packed_file, 'Image is not packed: '+im.name
runtime=[]
for material,tag in [('BC_SHELTER01_WeatheredGreenSteel','FrameAndScreen'),('BC_SHELTER01_GalvanizedRoofAndFixings','RoofAndHardware'),('BC_SHELTER01_WeatheredTimber','BenchTimber')]:
    subset=[o for o in parts if o.data.materials[0].name==material];assert subset
    bpy.ops.object.select_all(action='DESELECT');copies=[]
    for o in subset:
        c=o.copy();c.data=o.data.copy();bpy.context.collection.objects.link(c);c.select_set(True);copies.append(c)
    bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();o=bpy.context.object;o.name='BC_SHELTER01_'+tag;o['source_parts']=[p.name for p in subset];runtime.append(o)
bpy.ops.object.select_all(action='DESELECT');root.select_set(True)
for o in runtime+anchors:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(Path(args[1]).resolve()),export_format='GLB',use_selection=True,export_yup=True,export_texcoords=True,export_normals=True,export_materials='EXPORT',export_apply=True,export_extras=True)
