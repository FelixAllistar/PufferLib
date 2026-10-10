import bpy,json,math
from pathlib import Path
R=Path(__file__).resolve().parents[1];bpy.ops.wm.open_mainfile(filepath=str(R/'source/fire_extinguisher.blend'))
o=bpy.data.objects['motel_fire_extinguisher'];s=bpy.context.scene
r={'packed_images':[{ 'name':i.name,'packed':bool(i.packed_file),'size':list(i.size)}for i in bpy.data.images if i.type=='IMAGE'],'editable_component_count':len(bpy.data.collections['Editable_original_components'].objects),'runtime_mesh_triangles':len(o.data.polygons),'metres':s.unit_settings.scale_length==1,'runtime_identity':all(abs(o.matrix_world[i][j]-(1 if i==j else 0))<1e-7 for i in range(4)for j in range(4)),'editable_collection_hidden_render':bpy.data.collections['Editable_original_components'].hide_render,'material_count':len(o.data.materials),'finite_vertices':all(math.isfinite(c)for v in o.data.vertices for c in v.co),'no_runtime_modifiers':len(o.modifiers)==0}
assert all(i['packed'] and i['size']==[512,512] for i in r['packed_images']);assert r['runtime_identity'] and r['metres'] and r['finite_vertices'] and r['no_runtime_modifiers'];(R/'qa/source_audit.json').write_text(json.dumps(r,indent=2))
