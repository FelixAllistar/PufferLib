"""Original compact junction/conduit source art; Blender 4.3+. Z-up source, Y-up GLB."""
import bpy, math, json, bmesh
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
sc=bpy.context.scene;sc.unit_settings.system='METRIC';sc.unit_settings.scale_length=1
m=bpy.data.materials.new('Junction_1K_opaque_zinc_atlas');m.use_nodes=True
ns=m.node_tree.nodes;lk=m.node_tree.links;p=ns.get('Principled BSDF')
for typ,cs in [('basecolor','sRGB'),('orm','Non-Color'),('normal','Non-Color')]:
 n=ns.new('ShaderNodeTexImage');n.image=bpy.data.images.load(str(R/'source/textures'/f'junction_{typ}.png'));n.image.colorspace_settings.name=cs
 if typ=='basecolor':lk.new(n.outputs['Color'],p.inputs['Base Color'])
 elif typ=='orm':
  s=ns.new('ShaderNodeSeparateColor');lk.new(n.outputs['Color'],s.inputs[0]);lk.new(s.outputs['Green'],p.inputs['Roughness']);lk.new(s.outputs['Blue'],p.inputs['Metallic'])
 else:
  s=ns.new('ShaderNodeNormalMap');lk.new(n.outputs['Color'],s.inputs['Color']);lk.new(s.outputs['Normal'],p.inputs['Normal'])
parts=[]
def finish(o,name,k=0,bev=0):
 o.name=name;bpy.context.view_layer.objects.active=o
 if bev:
  q=o.modifiers.new('Rounded_tooling_edges','BEVEL');q.width=bev;q.segments=1;bpy.ops.object.modifier_apply(modifier=q.name)
 bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
 for old in list(o.data.uv_layers):o.data.uv_layers.remove(old)
 uv=o.data.uv_layers.new(name='UVMap');lo=[min(v.co[i] for v in o.data.vertices) for i in range(3)];hi=[max(v.co[i] for v in o.data.vertices) for i in range(3)]
 for f in o.data.polygons:
  ax=sorted(range(3),key=lambda i:abs(f.normal[i]))[:2]
  for li in f.loop_indices:
   v=o.data.vertices[o.data.loops[li].vertex_index].co;a=[(v[i]-lo[i])/max(1e-8,hi[i]-lo[i]) for i in ax]
   uv.data[li].uv=((k%2)*.5+.008+.484*a[0],1-(k//2)*.5-.008-.484*a[1])
 o.data.materials.clear();o.data.materials.append(m);parts.append(o);return o
def box(name,loc,dim,k=0,bev=.0005):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.dimensions=dim;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);return finish(o,name,k,bev)
def mesh(name,vs,fs,k=0):
 me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);return finish(o,name,k)
def ring(name,center,outer,inner,depth,k=1,n=12,axis='Z'):
 vs=[]
 for z,r in [(-depth/2,outer),(depth/2,outer),(-depth/2,inner),(depth/2,inner)]:
  for i in range(n):
   a=i*math.tau/n;v=(r*math.cos(a),r*math.sin(a),z)
   if axis=='Y':v=(v[0],v[2],v[1])
   vs.append(tuple(center[j]+v[j] for j in range(3)))
 fs=[]
 for i in range(n):
  j=(i+1)%n;fs += [(i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),(i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)]
 return mesh(name,vs,fs,k)
def screw(name,x,y,z,r=.003):
 # Recessed real straight slot, with dark slot floor inside two raised metal cheeks.
 bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=r,depth=.0014,location=(x,y,z),rotation=(math.pi/2,0,0));o=bpy.context.object;bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 bpy.ops.mesh.primitive_cube_add(size=1,location=(x,y-.0005,z));cut=bpy.context.object;cut.dimensions=(r*2.3,.0015,.0008);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 bpy.context.view_layer.objects.active=o;q=o.modifiers.new('Machined_screw_slot','BOOLEAN');q.object=cut;q.operation='DIFFERENCE';bpy.ops.object.modifier_apply(modifier=q.name);bpy.data.objects.remove(cut,do_unlink=True);finish(o,name,3)
# Sealed cover encloses an actual thin-wall housing, not a filled cuboid.
box('Cast_back',(0,-.0015,0),(.104,.003,.104),0,.001)
for x in [-.0505,.0505]:box('Cast_side',(x,-.027,0),(.003,.051,.104),0,.001)
for z in [-.0505,.0505]:
 o=box('Cast_end_with_conduit_port',(0,-.027,z),(.098,.051,.003),0,0)
 bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=.0105,depth=.01,location=(0,-.027,z));cut=bpy.context.object;bpy.context.view_layer.objects.active=o
 q=o.modifiers.new('Real_conduit_port','BOOLEAN');q.object=cut;q.operation='DIFFERENCE';bpy.ops.object.modifier_apply(modifier=q.name);bpy.data.objects.remove(cut,do_unlink=True)
 # Refresh surface UV after hole operation.
 parts.remove(o);finish(o,'Cast_end_with_conduit_port',0)
# Seal is an open perimeter, hidden only by removable-looking closed cover.
for x in [-.049,.049]:box('Cover_gasket_side',(x,-.053,0),(.002,.0015,.100),2,0)
for z in [-.049,.049]:box('Cover_gasket_end',(0,-.053,z),(.096,.0015,.002),2,0)
box('Bevelled_cover',(0,-.055,0),(.108,.0025,.108),0,.002)
for x,z in [(-.040,.040),(.040,-.040)]:screw('Cover_slotted_screw',x,-.0573,z,.0035)
# Two short open-ended conduit stubs. Metal wall thickness, no hidden caps.
for name,za,zb in [('Upper',.046,.355),('Lower',-.225,-.046)]:
 ring(name+'_EMT_hollow_conduit',(0,-.027,(za+zb)/2),.009,.0078,zb-za,1)
 z=.063 if name=='Upper' else -.063
 ring(name+'_cast_connector',(0,-.027,z),.013,.0091,.026,0)
 ring(name+'_hex_locknut',(0,-.027,.050 if name=='Upper' else -.050),.016,.0092,.005,3,6)
 # Connector set screw faces outward.
 screw(name+'_connector_set_screw',0,-.041,z,.003)
# Two real thin bent saddles wrap conduit, meet wall through lateral feet.
for z in [.260,-.160]:
 xs=[-.026,-.017,-.013,-.011,-.007,0,.007,.011,.013,.017,.026]
 ys=[0,0,-.008,-.025,-.03265,-.036,-.03265,-.025,-.008,0,0]
 vs=[]
 for dz in [-.007,.007]:
  for off in [0,-.0012]:vs.extend([(x,y+off,z+dz) for x,y in zip(xs,ys)])
 n=len(xs);fs=[]
 for i in range(n-1):fs +=[(i,i+1,2*n+i+1,2*n+i),(n+i,3*n+i,3*n+i+1,n+i+1),(i,n+i,n+i+1,i+1),(2*n+i,2*n+i+1,3*n+i+1,3*n+i)]
 fs +=[(0,2*n,3*n,n),(n-1,2*n-1,4*n-1,3*n-1)]
 mesh('Bent_conduit_saddle',vs,fs,1)
 for x in [-.022,.022]:screw('Saddle_wall_screw',x,-.003,z,.0028)
# Bake transforms; unified single material and primitive with object origin at wall/box centre.
bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();o=bpy.context.object;o.name='motel_surface_junction_candidate';sc.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
# Every physical part is retained, no collision proxy or invisible fill geometry.
tri=o.modifiers.new('Runtime_triangulation','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=tri.name)
bm=bmesh.new();bm.from_mesh(o.data)
bad=[f for f in bm.faces if f.calc_area()<1e-14]
bmesh.ops.delete(bm,geom=bad,context='FACES_ONLY')
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
for im in bpy.data.images:
 if im.source=='FILE':im.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(R/'source/junction.blend'))
bpy.ops.export_scene.gltf(filepath=str(R/'runtime/motel_surface_junction.glb'),export_format='GLB',use_selection=True,export_yup=True,export_tangents=True)
# Fresh import is the preview subject, so render cannot silently use source-only detail.
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(R/'runtime/motel_surface_junction.glb'))
subject=[o for o in sc.objects if o.type=='MESH'];vs=[o.matrix_world@v.co for o in subject for v in o.data.vertices];lo=[min(v[i] for v in vs) for i in range(3)];hi=[max(v[i] for v in vs) for i in range(3)]
(R/'qa/reimport.json').write_text(json.dumps({'source_blender_bounds':{'min':lo,'max':hi},'reimport_meshes':len(subject),'render_subject':'actual exported GLB, freshly imported'},indent=2))
# Neutral product lighting. Staging is preview-only and absent from the GLB/source.
sc.render.engine='CYCLES';sc.cycles.samples=48;sc.cycles.use_denoising=False;sc.render.resolution_x=1100;sc.render.resolution_y=1100;sc.render.resolution_percentage=100
sc.world.color=(.25,.25,.25);sc.view_settings.view_transform='AgX';sc.view_settings.exposure=-.65
def area(loc,power,size):
 bpy.ops.object.light_add(type='AREA',location=loc);a=bpy.context.object;a.data.energy=power;a.data.shape='DISK';a.data.size=size;a.rotation_euler=(Vector((0,-.025,.07))-a.location).to_track_quat('-Z','Y').to_euler()
area((-.6,-.7,.8),55,.65);area((.5,-.4,.1),30,.45);area((0,.3,.5),40,.3)
bpy.ops.object.camera_add();cam=bpy.context.object;sc.camera=cam;cam.data.type='ORTHO';cam.data.lens=50
for name,loc,target,scale in [('full',( .40,-.85,.40),(0,-.025,.065),.68),('front',(0,-1,.065),(0,-.025,.065),.66),('detail',(.20,-.5,.18),(0,-.025,.014),.22),('open_end',(.10,-.28,.49),(0,-.027,.33),.16),('rear',(.3,.7,.25),(0,-.01,.07),.66)]:
 cam.location=loc;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=scale;sc.render.filepath=str(R/'preview'/f'{name}.png');bpy.ops.render.render(write_still=True)
