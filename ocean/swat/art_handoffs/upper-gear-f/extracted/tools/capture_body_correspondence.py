"""Capture exact retained original body IDs, polygon groups and UV corners read-only."""
import bpy,hashlib,json,sys
from pathlib import Path
import numpy as np
source,out=map(Path,sys.argv[sys.argv.index('--')+1:]);h=hashlib.sha256(source.read_bytes()).hexdigest();assert h=='4271f2279bc3cbbcb46594157752b2ac0ca052628c24b4c8629a82c6771a3fc0'
bpy.ops.wm.open_mainfile(filepath=str(source.resolve()));o=bpy.data.objects['SWAT_Wearer'];m=o.data;m.calc_loop_triangles();v=m.attributes['native_vertex_id'];f=m.attributes['native_polygon_id'];assert v.domain=='POINT'and f.domain=='FACE'
data={'F_vertex_to_original_N_vertex':np.array([x.value for x in v.data],dtype=np.int32),'F_polygon_to_original_N_polygon':np.array([x.value for x in f.data],dtype=np.int32),'F_polygon_material_slot':np.array([x.material_index for x in m.polygons],dtype=np.int32),'F_loop_vertex':np.array([x.vertex_index for x in m.loops],dtype=np.int32),'F_triangle_vertices':np.array([t.vertices[:]for t in m.loop_triangles],dtype=np.int32),'F_triangle_polygon':np.array([t.polygon_index for t in m.loop_triangles],dtype=np.int32),'F_triangle_loop_indices':np.array([t.loops[:]for t in m.loop_triangles],dtype=np.int32)}
uvs=[]
for i,uv in enumerate(m.uv_layers):data[f'F_loop_UV_{i}']=np.array([x.uv[:]for x in uv.data]);uvs.append(uv.name)
np.savez_compressed(out,**data)
report={'source_F_sha256':h,'mapping_npz_sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'source_object':o.name,'original_source_vertices':24850,'original_source_polygons':25026,'F_vertices':len(m.vertices),'F_polygons':len(m.polygons),'F_triangles':len(m.loop_triangles),'source_material_slots':[x.name for x in m.materials],'UV_layers':uvs,'active_UV_index':m.uv_layers.active_index,'source_unchanged':hashlib.sha256(source.read_bytes()).hexdigest()==h,'semantics':'Full originalN→compactedF correspondence. Source material slots and loop UVs are exact retained originals; glTF flips V according to the export convention. Exported seam vertices are mapped separately.'}
out.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');assert report['source_unchanged'];print(json.dumps(report,indent=2))
