import bpy,json
from pathlib import Path
R=Path(__file__).resolve().parents[1];bpy.ops.wm.open_mainfile(filepath=str(R/'source/reception_noticeboard.blend'))
editable=bpy.data.collections['Editable_original_components'];ims=[im for im in bpy.data.images if im.type=='IMAGE'];out={'editable_component_count':len(editable.objects),'editable_collection_hidden':editable.hide_render,'packed_images':[{'name':im.name,'size':list(im.size),'packed':im.packed_file is not None}for im in ims],'runtime_mesh_triangles':len(bpy.data.objects['motel_reception_noticeboard'].data.polygons),'source_units':'metres','source_coordinate_system':'Z-up front -Y, export converts to glTF Y-up front +Z'}
assert len(editable.objects)==13;assert len(ims)==2 and all(im.packed_file for im in ims);assert out['runtime_mesh_triangles']==1416
(R/'qa/source_audit.json').write_text(json.dumps(out,indent=2));print(out)
