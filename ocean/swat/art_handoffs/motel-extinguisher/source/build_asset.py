"""Original wall-bracket fire extinguisher. Blender 4.x. Metres, base Z=0."""
import bpy,bmesh,math,json
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True);s=bpy.context.scene;s.unit_settings.system='METRIC';s.unit_settings.scale_length=1
m=bpy.data.materials.new('Fire_extinguisher_opaque_512_atlas');m.use_nodes=True
bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=.73;bs.inputs['Metallic'].default_value=.12
tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(R/'source/extinguisher_atlas_512.png'));m.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color'])

orm=m.node_tree.nodes.new('ShaderNodeTexImage');orm.image=bpy.data.images.load(str(R/'source/extinguisher_orm_512.png'));orm.image.colorspace_settings.name='Non-Color'
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
 if k==1:
  for v in uv.data:v.uv.x=.52+(v.uv.x-.524)/.452*.075
 o.data.materials.append(m);parts.append(o);return o
def box(name,loc,dims,k,b=.002):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.dimensions=dims;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);return finish(o,name,k,min(b,min(dims)*.20))
def tube(name,points,r,k,N=10):
 # Parallel transported ring frame: manufactured constant-section bent tube.
 ps=[Vector(p)for p in points];vs=[];prev=None
 for i,p in enumerate(ps):
  t=(ps[min(i+1,len(ps)-1)]-ps[max(0,i-1)]).normalized()
  u=Vector((0,1,0));v=t.cross(u).normalized()
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

def lathe(name,profile,k,N=24):
 vs=[(r*math.cos(j*2*math.pi/N),r*math.sin(j*2*math.pi/N),z)for r,z in profile for j in range(N)];fs=[]
 for i in range(len(profile)-1):
  for j in range(N):fs.append((i*N+j,i*N+(j+1)%N,(i+1)*N+(j+1)%N,(i+1)*N+j))
 fs.extend([tuple(reversed(range(N))),tuple((len(profile)-1)*N+j for j in range(N))]);me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);finish(o,name,k)
 for f in o.data.polygons:f.use_smooth=len(f.vertices)==4
 return o
# Rolled base ring supports a drawn steel pressure bottle, domed shoulder and threaded neck.
lathe('Painted_pressure_cylinder',[(.068,.006),(.079,.013),(.085,.027),(.087,.045),(.087,.347),(.086,.367),(.081,.385),(.073,.401),(.060,.415),(.044,.425),(.025,.431),(.023,.441)],0,24)
lathe('Rolled_base_foot',[(.080,0),(.087,0),(.090,.008),(.090,.021),(.086,.026),(.080,.024)],0,24)
lathe('Threaded_valve_collar',[(.026,.429),(.026,.450),(.023,.454)],2,16)
box('Valve_body',(0,0,.466),(.059,.039,.040),2,.006)
# Fixed carrying handle and movable squeeze lever, visible clearance and pivot.
box('Fixed_handle_root',(.018,0,.491),(.067,.018,.012),2,.002)
bar('Fixed_handle_upturn',(.042,0,.491),(.068,0,.515),.014,.018,2)
box('Carry_handle',(.098,0,.516),(.078,.020,.012),2,.003)
bar('Squeeze_lever_root',(-.008,0,.488),(.025,0,.538),.014,.017,0)
box('Squeeze_lever',(.079,0,.540),(.130,.021,.011),0,.003)
# Eight-sided hinge pin and pull ring: no working simulation supplied.
for y in [-.024,.024]:
 bpy.ops.mesh.primitive_cylinder_add(vertices=10,radius=.009,depth=.008,location=(.010,y,.487),rotation=(math.pi/2,0,0));finish(bpy.context.object,'Hinge_pin',2)
bpy.ops.mesh.primitive_torus_add(major_radius=.016,minor_radius=.0025,major_segments=16,minor_segments=6,location=(-.029,-.033,.484),rotation=(math.pi/2,0,0));finish(bpy.context.object,'Safety_pull_ring',2)
bpy.ops.mesh.primitive_cylinder_add(vertices=8,radius=.0025,depth=.069,location=(-.013,0,.484),rotation=(math.pi/2,0,0));finish(bpy.context.object,'Safety_pin_shaft',2)
# Gauge stem, raised metal bezel and atlas-mapped opaque dial (no glass/blend cost).
bpy.ops.mesh.primitive_cylinder_add(vertices=16,radius=.020,depth=.026,location=(0,-.033,.466),rotation=(math.pi/2,0,0));finish(bpy.context.object,'Pressure_gauge_bezel',2)
bpy.ops.mesh.primitive_cylinder_add(vertices=24,radius=.0165,depth=.0014,location=(0,-.047,.466),rotation=(math.pi/2,0,0));g=finish(bpy.context.object,'Printed_pressure_dial',3)
# Cylinder local X,Y disc maps to circular dial region before object rotation.
for f in g.data.polygons:
 for li in f.loop_indices:
  v=g.data.vertices[g.data.loops[li].vertex_index].co;g.data.uv_layers.active.data[li].uv=((384+v.x/.0165*53)/512,1-(128-v.y/.0165*53)/512)
# Fixed hose route with metal ferrules, retaining clip and flattened discharge nozzle.
pts=[(-.030,.006,.475),(-.067,.006,.478),(-.105,.006,.463),(-.120,.006,.429),(-.122,.006,.374),(-.118,.006,.285),(-.115,.006,.212)]
tube('Retained_discharge_hose',pts,.009,1,10)
box('Hose_retaining_clip',(-.096,.005,.282),(.043,.026,.026),0,.003)
tube('Nozzle_ferrule',[(-.115,.006,.224),(-.115,.006,.193)],.012,2,12)
box('Discharge_nozzle',(-.115,.006,.166),(.030,.023,.056),1,.004)
# Wall bracket: upright spine, bottle saddle, base ledge and upper retaining strap.
box('Wall_bracket_spine',(0,.113,.225),(.045,.012,.346),2,.002)
box('Bottom_support_ledge',(0,.081,.038),(.067,.075,.014),2,.001)
for z in [.093,.345]:
 box('Wall_mount_tab',(0,.127,z),(.073,.014,.044),2,.002)
 for x in [-.024,.024]:
  bpy.ops.mesh.primitive_cylinder_add(vertices=8,radius=.006,depth=.004,location=(x,.117,z),rotation=(math.pi/2,0,0));finish(bpy.context.object,'Mount_fastener_head',2)
# Contoured bracket side saddle behind cylinder, deliberately only rear half of bottle.
for side in [-1,1]:
 bar('Cradle_wing',(side*.014,.105,.240),(side*.065,.065,.240),.018,.012,2)
 box('Cradle_rubber_pad',(side*.063,.061,.240),(.018,.012,.055),1,.002)
# Curved adhesive paper label conforms to cylinder with narrow opaque inset.
vs=[];fs=[];N=12
for z in [.126,.312]:
 for i in range(N+1):
  a=-math.pi/2+(-.80+1.60*i/N);vs.append((.08745*math.cos(a),.08745*math.sin(a),z))
for i in range(N):fs.append((i,i+1,N+2+i,N+1+i))
me=bpy.data.meshes.new('Curved_printed_label');me.from_pydata(vs,[],fs);me.update();o=bpy.data.objects.new('Curved_printed_label',me);bpy.context.collection.objects.link(o);finish(o,o.name,3)
for f in o.data.polygons:
 f.use_smooth=True
 for li in f.loop_indices:
  ix=o.data.loops[li].vertex_index;i=ix%(N+1);top=ix//(N+1);o.data.uv_layers.active.data[li].uv=((268+232*i/N)/512,1-(501-232*top)/512)

# Keep original named components editable and hidden; single selected runtime mesh.
edit=bpy.data.collections.new('Editable_original_components');s.collection.children.link(edit)
for o in parts:
 cp=o.copy();cp.data=o.data.copy();edit.objects.link(cp)
edit.hide_viewport=True;edit.hide_render=True
bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();o=bpy.context.object;o.name='motel_fire_extinguisher'
s.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
q=o.modifiers.new('Runtime_triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=q.name)
bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
tex.image.pack();orm.image.pack();bpy.ops.wm.save_as_mainfile(filepath=str(R/'source/fire_extinguisher.blend'))
bpy.ops.export_scene.gltf(filepath=str(R/'runtime/motel_fire_extinguisher.glb'),export_format='GLB',use_selection=True,export_yup=True,export_tangents=True)
print('TRIANGLES',len(o.data.polygons))
