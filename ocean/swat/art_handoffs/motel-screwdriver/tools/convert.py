"""Run with Blender 4.3+: blender -b --python tools/convert.py -- /path/to/package
Rebuilds source-scale/editable main-only scenes and GLB from immutable original ZIP.
No mesh/UV editing, welding, decimation, normal-map fabrication or shader tuning.
"""
import bpy,sys,zipfile,tempfile,json,hashlib,struct,math
from pathlib import Path
from mathutils import Vector,Matrix
root=Path(sys.argv[sys.argv.index('--')+1]).resolve() if '--' in sys.argv else Path(__file__).resolve().parents[1]
for d in ['runtime','editable','qa']: (root/d).mkdir(exist_ok=True)
def digest(x):return hashlib.sha256(x).hexdigest()
def mesh_record(o):
 m=o.data;m.calc_loop_triangles();return {'vertices':len(m.vertices),'polygons':len(m.polygons),'triangles':len(m.loop_triangles),'uv_layers':[u.name for u in m.uv_layers], 'local_position_f64_sha256':digest(b''.join(struct.pack('<3d',*v.co) for v in m.vertices)),'loop_uv_f64_sha256':digest(b''.join(struct.pack('<2d',*u.uv) for u in m.uv_layers.active.data)),'polygon_index_sha256':digest(json.dumps([list(p.vertices) for p in m.polygons],separators=(',',':')).encode()),'matrix_world':[list(row) for row in o.matrix_world]}
def bounds(o):
 p=[o.matrix_world@v.co for v in o.data.vertices];lo=Vector([min(v[i] for v in p) for i in range(3)]);hi=Vector([max(v[i] for v in p) for i in range(3)]);return lo,hi
bpy.ops.wm.read_factory_settings(use_empty=True)
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp)
 with zipfile.ZipFile(root/'source/Tools_Pack_original.zip') as z: (t/'fbx.zip').write_bytes(z.read('Tools Pack FBX.zip'))
 with zipfile.ZipFile(t/'fbx.zip') as z: (t/'source.fbx').write_bytes(z.read('Tools Pack FBX.fbx'))
 bpy.ops.import_scene.fbx(filepath=str(t/'source.fbx'))
 o=bpy.data.objects['Screwdriver']
 for other in list(bpy.data.objects):
  if other!=o:bpy.data.objects.remove(other,do_unlink=True)
 for unused_mesh in list(bpy.data.meshes):
  if unused_mesh.users==0:bpy.data.meshes.remove(unused_mesh)
 before=mesh_record(o);lo,hi=bounds(o)
 mat=bpy.data.materials.new('Original screwdriver maps');mat.use_nodes=True;bs=mat.node_tree.nodes.get('Principled BSDF');mapping=[]
 for role,socket in [('diffuse','Base Color'),('roughness','Roughness'),('metallic','Metallic')]:
  path=root/'textures'/f'screwdriver_{role}.png';im=bpy.data.images.load(str(path));im.colorspace_settings.name='sRGB' if role=='diffuse' else 'Non-Color';node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=im;node.interpolation='Linear';node.extension='REPEAT';mat.node_tree.links.new(node.outputs['Color'],bs.inputs[socket]);im.pack();mapping.append({'source':str(path.relative_to(root)),'socket':socket,'color_space':im.colorspace_settings.name,'resolution':list(im.size),'sha256':digest(path.read_bytes()),'sampling':'linear, repeat; original UVMap'})
 o.data.materials.clear();o.data.materials.append(mat)
 bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=1
 bpy.ops.wm.save_as_mainfile(filepath=str(root/'editable/screwdriver_original_scale.blend'))
 # Preserve mesh local positions, indices and UVs; transform only.
 f=.25/(hi.y-lo.y);center=Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z));o.matrix_world=Matrix.Scale(f,4)@Matrix.Translation(-center)@o.matrix_world
 bpy.context.view_layer.update();after=mesh_record(o);nlo,nhi=bounds(o)
 for k in ['local_position_f64_sha256','loop_uv_f64_sha256','polygon_index_sha256']:assert before[k]==after[k],k
 o.name='wood_handle_flathead_screwdriver_MAIN';bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
 bpy.ops.wm.save_as_mainfile(filepath=str(root/'editable/screwdriver_250mm_MAIN.blend'))
 bpy.ops.export_scene.gltf(filepath=str(root/'runtime/wood_handle_flathead_screwdriver_250mm_MAIN.glb'),export_format='GLB',use_selection=True,export_yup=True,export_apply=False,export_image_format='AUTO',export_materials='EXPORT',export_texcoords=True,export_normals=True,export_tangents=False)
 report={'blender':bpy.app.version_string,'object_count':1,'source_geometry':before,'runtime_editable_geometry':after,'unchanged_local_mesh_and_uv':True,'source_bounds_blender_xyz':{'min':list(lo),'max':list(hi),'dimensions':list(hi-lo)},'normalization':{'uniform_factor':f,'length_metres':.25,'basis':'Editorial normalization, NOT verified physical size','pivot':'Source orientation retained; bottom-centered world AABB. Blender Y length; glTF Z length; glTF Y up.'},'normalized_bounds_blender_xyz':{'min':list(nlo),'max':list(nhi),'dimensions':list(nhi-nlo)},'texture_mapping':mapping,'shader_defaults':'Blender 4.3 Principled BSDF defaults except linked base color, roughness and metallic. No retuning. No normal input.','export_note':'glTF exporter packs roughness in G and metallic in B; this derived runtime image is separate from immutable original maps.'}
 (root/'qa/conversion_report.json').write_text(json.dumps(report,indent=2))
