"""Original two-shelf steel service trolley. Blender 4.x. Metres, ground Z=0."""
import bpy,bmesh,math,json
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True);s=bpy.context.scene;s.unit_settings.system='METRIC';s.unit_settings.scale_length=1
m=bpy.data.materials.new('Service_trolley_opaque_512_atlas');m.use_nodes=True
bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=.73;bs.inputs['Metallic'].default_value=.12
tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(R/'source/trolley_atlas_512.png'));m.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color'])

orm=m.node_tree.nodes.new('ShaderNodeTexImage');orm.image=bpy.data.images.load(str(R/'source/trolley_orm_512.png'));orm.image.colorspace_settings.name='Non-Color'
sep=m.node_tree.nodes.new('ShaderNodeSeparateColor');m.node_tree.links.new(orm.outputs['Color'],sep.inputs['Color']);m.node_tree.links.new(sep.outputs['Green'],bs.inputs['Roughness']);m.node_tree.links.new(sep.outputs['Blue'],bs.inputs['Metallic'])
parts=[]
def finish(o,name,k,b=0):
 o.name=name;bpy.context.view_layer.objects.active=o
 if b:
  q=o.modifiers.new('Small_formed_edge','BEVEL');q.width=b;q.segments=1;bpy.ops.object.modifier_apply(modifier=q.name)
 bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
 uv=o.data.uv_layers.get('UVMap') or o.data.uv_layers.new(name='UVMap');lo=[min(v.co[i]for v in o.data.vertices)for i in range(3)];hi=[max(v.co[i]for v in o.data.vertices)for i in range(3)]
 for f in o.data.polygons:
  ax=sorted(range(3),key=lambda j:abs(f.normal[j]))[:2]
  for li in f.loop_indices:
   v=o.data.vertices[o.data.loops[li].vertex_index].co;t=[(v[j]-lo[j])/max(hi[j]-lo[j],1e-8) for j in ax];uv.data[li].uv=(k%2*.5+.024+t[0]*.452,1-k//2*.5-.024-t[1]*.452)
 o.data.materials.append(m);parts.append(o);return o
def box(name,loc,dims,k,b=.002):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.dimensions=dims;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);return finish(o,name,k,min(b,min(dims)*.20))
def tube(name,points,r,k,N=10):
 # Parallel transported ring frame: manufactured constant-section bent tube.
 ps=[Vector(p)for p in points];vs=[];prev=None
 for i,p in enumerate(ps):
  t=(ps[min(i+1,len(ps)-1)]-ps[max(0,i-1)]).normalized()
  u=Vector((1,0,0));v=t.cross(u).normalized()
  if prev is not None and u.dot(prev)<0:u=-u;v=-v
  prev=u
  for j in range(N):a=2*math.pi*j/N;vs.append(p+r*(u*math.cos(a)+v*math.sin(a)))
 fs=[]
 for i in range(len(ps)-1):
  for j in range(N):fs.append((i*N+j,i*N+(j+1)%N,(i+1)*N+(j+1)%N,(i+1)*N+j))
 fs.extend([tuple(reversed(range(N))),tuple((len(ps)-1)*N+j for j in range(N))])
 me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);finish(o,name,k)
 # Smooth tube sides, flat end caps, preserving actual bend shape.
 for f in o.data.polygons:f.use_smooth=len(f.vertices)==4
 return o
def bar(name,a,b,w,d,k):
 a=Vector(a);b=Vector(b);o=box(name,(a+b)/2,(w,d,(b-a).length),k,.001);o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();return o
def bolt(name,x,y,z):
 bpy.ops.mesh.primitive_cylinder_add(vertices=8,radius=.012,depth=.010,location=(x,y,z),rotation=(0,math.pi/2,0));finish(bpy.context.object,name,2)
# Grounded, empty utility trolley. 0.78 m tray width; 0.44 m depth.
# Two bent tubular end frames carry both shelves and act as handles.
for sign in [-1,1]:
 x=sign*.410
 pts=[(x,-.230,.145),(x,-.230,.850)]
 for i in range(1,5):
  a=math.pi-i*math.pi/8;pts.append((x,-.175+.055*math.cos(a),.850+.055*math.sin(a)))
 pts.append((x,.175,.905))
 for i in range(1,5):
  a=math.pi/2-i*math.pi/8;pts.append((x,.175+.055*math.cos(a),.850+.055*math.sin(a)))
 pts.append((x,.230,.145))
 tube('Bent_end_frame',pts,.014,0,10)
 tube('Push_handle_sleeve',[(x,-.115,.905),(x,.115,.905)],.019,1,12)
# Stamped steel trays with turned-up lips; visible rolled edge above each wall.
for label,z in [('Lower',.235),('Upper',.705)]:
 box(label+'_tray_pan',(0,0,z),(.790,.430,.018),0,0)
 for sy in [-1,1]:
  box(label+'_raised_long_wall',(0,sy*.219,z+.025),(.790,.014,.058),0,0)
  box(label+'_rolled_long_lip',(0,sy*.221,z+.057),(.802,.020,.010),0,.002)
 for sx in [-1,1]:
  box(label+'_raised_end_wall',(sx*.393,0,z+.025),(.014,.430,.058),0,0)
  box(label+'_rolled_end_lip',(sx*.394,0,z+.057),(.018,.445,.010),0,.002)
 # Washable textured liner set within the shelf floor, leaves exposed steel drainage margin.
 box(label+'_non_slip_liner',(0,0,z+.012),(.746,.386,.004),3,0)
 # Shelf underside channels continue to welded mounting saddles on frame.
 for sy in [-1,1]:
  box(label+'_support_cross_channel',(0,sy*.18,z-.023),(.840,.024,.028),0,0)
# Caster stems, mounting plates, vertical fork cheeks and rubber tires.
# Wheel centers at z=.064 and radius .064 give four measured ground contacts.
for ix in [-1,1]:
 for iy in [-1,1]:
  x=ix*.410;y=iy*.230
  box('Caster_fork_bridge',(x,y,.134),(.068,.075,.015),2,0)
  tube('Caster_swivel_bearing',[(x,y,.139),(x,y,.170)],.027,2,12)
  for side in [-1,1]:box('Pressed_caster_fork',(x+side*.029,y,.098),(.008,.055,.072),2,0)
  bpy.ops.mesh.primitive_cylinder_add(vertices=16,radius=.064,depth=.044,location=(x,y,.064),rotation=(0,math.pi/2,0));o=finish(bpy.context.object,'Rubber_caster_tire',1,0)
  for p in o.data.polygons:p.use_smooth=len(p.vertices)==4
  for side in [-1,1]:
   bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=.027,depth=.004,location=(x+side*.024,y,.064),rotation=(0,math.pi/2,0));finish(bpy.context.object,'Recessed_wheel_hub',2)
   bpy.ops.mesh.primitive_cylinder_add(vertices=8,radius=.009,depth=.007,location=(x+side*.036,y,.064),rotation=(0,math.pi/2,0));finish(bpy.context.object,'Wheel_axle_head',2)
# Rubber corner bumpers cushion door jamb contact and tie the silhouette together.
for sx in [-1,1]:
 for sy in [-1,1]:box('Corner_bumper',(sx*.411,sy*.231,.719),(.038,.040,.054),1,.005)
# Keep original named components editable and hidden; single selected runtime mesh.
edit=bpy.data.collections.new('Editable_original_components');s.collection.children.link(edit)
for o in parts:
 cp=o.copy();cp.data=o.data.copy();edit.objects.link(cp)
edit.hide_viewport=True;edit.hide_render=True
bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();o=bpy.context.object;o.name='motel_service_trolley'
s.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
q=o.modifiers.new('Runtime_triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=q.name)
bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
tex.image.pack();orm.image.pack();bpy.ops.wm.save_as_mainfile(filepath=str(R/'source/service_trolley.blend'))
bpy.ops.export_scene.gltf(filepath=str(R/'runtime/motel_service_trolley.glb'),export_format='GLB',use_selection=True,export_yup=True,export_tangents=True)
print('TRIANGLES',len(o.data.polygons))
