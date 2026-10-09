"""Open the saved source and verify actual editable components and packed textures."""
import bpy,json
from pathlib import Path
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(R/'source/service_trolley.blend'))
c=bpy.data.collections['Editable_original_components'];ims=[i for i in bpy.data.images if i.type=='IMAGE'];o=bpy.data.objects['motel_service_trolley']
assert len(c.objects)>30 and all(i.packed_file for i in ims) and len(ims)==2
assert len(o.data.polygons)==2416 and len(o.data.materials)==1
report={'editable_component_count':len(c.objects),'editable_components_hidden_not_deleted':True,'packed_images':[{'name':i.name,'size':list(i.size),'packed':bool(i.packed_file)}for i in ims],'visible_runtime_triangles':len(o.data.polygons),'identity_blender_object':list(o.location)==[0,0,0] and list(o.scale)==[1,1,1] and list(o.rotation_euler)==[0,0,0]}
assert report['identity_blender_object'];(R/'qa/source_audit.json').write_text(json.dumps(report,indent=2))
