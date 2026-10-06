"""Extract immutable Rifle 7 rest geometry and bind its existing PBR images.

blender -b --python export_rifle.py -- SOURCE.blend TEXTURE_DIRECTORY OUTPUT_DIRECTORY
No save is made to SOURCE.blend. See recipe.json for the reversible rigid matrix.
"""
import bpy,hashlib,json,math,sys
import numpy as np
from pathlib import Path
from mathutils import Matrix,Vector

source,textures,out=map(Path,sys.argv[sys.argv.index('--')+1:]);out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before=sha(source);assert before=='19d79d3eab5a0974ce0c1edc8ace6a2b27327f6df56a71bb85f533c63522628f'
bpy.ops.wm.open_mainfile(filepath=str(source.resolve()))
pad=Vector((.00129691231995821,.44999998807907104,.02265022322535515))
# Native local forward -Y, up +Z -> Blender +X forward,+Z up.
# glTF exporter subsequently maps Blender (x,y,z) -> (x,z,-y).
native_to_blender=Matrix.Rotation(math.pi/2,4,'Z')@Matrix.Translation(-pad)
native_to_engine=Matrix(((0,-1,0,pad.y),(0,0,1,-pad.z),(-1,0,0,pad.x),(0,0,0,1)))
copies=[];measurements=[]
for original,newname in [('Rifle 7','Rifle7_RigidBody'),('Removed magazine','Rifle7_SeatedMagazine')]:
    old=bpy.data.objects[original];old.data.calc_loop_triangles()
    vertices=np.array([v.co for v in old.data.vertices]);triangles=np.array([list(t.vertices)for t in old.data.loop_triangles])
    measurements.append({'object':original,'export_node':newname,'vertices':len(vertices),'triangles':len(triangles),
                         'native_bounds':[vertices.min(0).tolist(),vertices.max(0).tolist()]})
    np.savez_compressed(out/(newname+'_source.npz'),vertices=vertices,triangles=triangles,
                        loop_vertex_indices=np.array([l.vertex_index for l in old.data.loops]),
                        triangle_loop_indices=np.array([list(t.loops)for t in old.data.loop_triangles]),
                        corner_normals=np.array([n.vector[:]for n in old.data.corner_normals]),
                        uv=np.array([l.uv[:]for l in old.data.uv_layers.active.data]))
    # Use precisely the source's rendered triangles, keeping each corner's UV
    # and normal. This avoids the exporter's n-gon tangent-generation failure.
    mesh=bpy.data.meshes.new(newname+'_triangulated')
    mesh.from_pydata([native_to_blender@Vector(v)for v in vertices],[],triangles.tolist());mesh.update()
    uv=mesh.uv_layers.new(name='UVMap');corner_normals=[]
    for target,tri in zip(mesh.polygons,old.data.loop_triangles):
        target.use_smooth=old.data.polygons[tri.polygon_index].use_smooth
        for target_loop,source_loop in zip(target.loop_indices,tri.loops):
            uv.data[target_loop].uv=old.data.uv_layers.active.data[source_loop].uv
            corner_normals.append(native_to_blender.to_3x3()@old.data.corner_normals[source_loop].vector)
    mesh.normals_split_custom_set(corner_normals)
    obj=bpy.data.objects.new(newname,mesh);bpy.context.scene.collection.objects.link(obj);copies.append(obj)
# Remove all copied-in scene objects, actions, rigs and cameras except these rigid meshes.
for obj in list(bpy.data.objects):
    if obj not in copies:bpy.data.objects.remove(obj,do_unlink=True)
for act in list(bpy.data.actions):bpy.data.actions.remove(act)
material=bpy.data.materials.new('Rifle7_OriginalPBR');material.use_nodes=True
nodes=material.node_tree.nodes;links=material.node_tree.links;bsdf=nodes.get('Principled BSDF')
inputs=[]
for role in ('BaseColor','Metallic','Roughness','Normal'):
    path=textures/('7_uv_checker_material_uv_grid_4096x4096_'+role+'.png')
    image=bpy.data.images.load(str(path.resolve()),check_existing=False)
    image.colorspace_settings.name='sRGB'if role=='BaseColor'else 'Non-Color'
    tex=nodes.new('ShaderNodeTexImage');tex.name='Original '+role;tex.image=image
    if role=='BaseColor':links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
    elif role=='Normal':
        normal=nodes.new('ShaderNodeNormalMap');normal.inputs['Strength'].default_value=1
        links.new(tex.outputs['Color'],normal.inputs['Color']);links.new(normal.outputs['Normal'],bsdf.inputs['Normal'])
    else:
        separate=nodes.new('ShaderNodeSeparateColor');links.new(tex.outputs['Color'],separate.inputs['Color'])
        links.new(separate.outputs['Red'],bsdf.inputs[role])
    inputs.append({'role':role,'file':path.name,'sha256':sha(path),'bytes':path.stat().st_size,'size':list(image.size),'colorspace':image.colorspace_settings.name})
for obj in copies:
    obj.data.materials.clear();obj.data.materials.append(material)
    for p in obj.data.polygons:p.material_index=0
    obj.select_set(True)
bpy.context.view_layer.objects.active=copies[0]
settings={'export_format':'GLB','use_selection':True,'export_animations':False,'export_skins':False,
          'export_cameras':False,'export_lights':False,'export_yup':True,'export_materials':'EXPORT',
          'export_image_format':'AUTO','export_texcoords':True,'export_normals':True,'export_tangents':True}
path=out/'rifle7_rigid_textured.glb';bpy.ops.export_scene.gltf(filepath=str(path.resolve()),**settings)
report={'schema':'swat-rigid-rifle-recipe/1','source_file':source.name,'source_sha256':before,
        'source_geometry':'Existing game-scale Rifle 7 mesh/rest and one seated magazine; no pose evaluation, character skin or new scaling',
        'original_upload_fbx_sha256':'6e4be0a056f2934ae74513a262472abc30475c532932b7c233775e476bdb83f8',
        'blender_version':bpy.app.version_string,'units':'meters',
        'engine_axes':{'forward':'+X','up':'+Y','right':'+Z'},
        'origin':'Measured center of the actual stock rear pad bounding box',
        'native_stock_center':list(pad),'native_to_export_engine_row_major':[list(r)for r in native_to_engine],
        'export_engine_to_native_row_major':[list(r)for r in native_to_engine.inverted()],
        'scale_applied_during_extraction':1.0,'geometry':measurements,'texture_inputs':inputs,
        'topology_note':'Source loop triangles made explicit; per-corner UV and normals retained for tangent generation',
        'height_map':'Source height exists but is not used; no displacement or mesh changes applied',
        'normal_map_convention':'Supplied map used unchanged with standard glTF tangent-space normal interpretation; source convention not separately documented',
        'settings':settings,'export_file':path.name,'export_bytes':path.stat().st_size,'export_sha256':sha(path),
        'source_unchanged':sha(source)==before,'validation':'pending'}
assert report['source_unchanged']
(out/'recipe.json').write_text(json.dumps(report,indent=2));print('RIGID_RIFLE_EXPORTED',json.dumps(report),flush=True)
