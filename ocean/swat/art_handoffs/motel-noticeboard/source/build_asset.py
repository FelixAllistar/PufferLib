"""Original unplaced noticeboard. Blender Z-up/-Y front; GLB Y-up/+Z front."""
import bpy,bmesh,math,json
from pathlib import Path
R=Path(__file__).resolve().parents[1];bpy.ops.wm.read_factory_settings(use_empty=True);s=bpy.context.scene;s.unit_settings.system='METRIC';s.unit_settings.scale_length=1
m=bpy.data.materials.new('Noticeboard_one_opaque_atlas');m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');n=m.node_tree.nodes.new('ShaderNodeTexImage');n.image=bpy.data.images.load(str(R/'source/noticeboard_basecolor_1024.png'));m.node_tree.links.new(n.outputs['Color'],p.inputs['Base Color']);q=m.node_tree.nodes.new('ShaderNodeTexImage');q.image=bpy.data.images.load(str(R/'source/noticeboard_orm_1024.png'));q.image.colorspace_settings.name='Non-Color';sp=m.node_tree.nodes.new('ShaderNodeSeparateColor');m.node_tree.links.new(q.outputs['Color'],sp.inputs['Color']);m.node_tree.links.new(sp.outputs['Green'],p.inputs['Roughness']);m.node_tree.links.new(sp.outputs['Blue'],p.inputs['Metallic'])
rects={'cork':(4,4,508,508),'wood':(516,4,1020,120),'wood_lower':(516,132,1020,252),'back':(516,260,1020,380),'pin':(516,388,1020,508),'main':(3,515,637,1021),'quiet':(643,515,1021,829),'coffee':(643,835,1021,1021)};parts=[]
def finish(o,name,region,bevel=0):
 o.name=name;bpy.context.view_layer.objects.active=o
 if bevel:
  b=o.modifiers.new('Soft_handworn_edges','BEVEL');b.width=bevel;b.segments=2;bpy.ops.object.modifier_apply(modifier=b.name)
 bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
 uv=o.data.uv_layers.new(name='UVMap') if not o.data.uv_layers else o.data.uv_layers.active
 lo=[min(v.co[i]for v in o.data.vertices)for i in range(3)];hi=[max(v.co[i]for v in o.data.vertices)for i in range(3)];x0,y0,x1,y1=rects[region]
 for f in o.data.polygons:
  axes=[0,2] if abs(f.normal.y)>.7 else sorted(range(3),key=lambda i:abs(f.normal[i]))[:2]
  if region.startswith('wood'): axes=sorted(axes,key=lambda a:hi[a]-lo[a],reverse=True)
  for li in f.loop_indices:
   v=o.data.vertices[o.data.loops[li].vertex_index].co;u,w=[(v[a]-lo[a])/max(1e-8,hi[a]-lo[a]) for a in axes];uv.data[li].uv=((x0+u*(x1-x0))/1024,1-(y1-w*(y1-y0))/1024)
 o.data.materials.append(m);parts.append(o);return o
def mesh(name,v,f,r,b=0):
 me=bpy.data.meshes.new(name);me.from_pydata(v,[],f);me.update();o=bpy.data.objects.new(name,me);s.collection.objects.link(o);return finish(o,name,r,b)
def box(name,loc,dims,r,b):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.dimensions=dims;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);return finish(o,name,r,b)
box('Backing_6mm',(0,-.0045,0),(.690,.006,.470),'back',.001)
box('Cork_insert_18mm',(0,-.015,0),(.658,.018,.438),'cork',.0005)
# Four mitred timber members; inner border overlaps the insert by 1 mm.
out=[(-.36,-.25),(.36,-.25),(.36,.25),(-.36,.25)];inn=[(-.327,-.217),(.327,-.217),(.327,.217),(-.327,.217)]
for i in range(4):
 j=(i+1)%4;shape=[out[i],out[j],inn[j],inn[i]];v=[(x,y,z)for y in [0,-.032]for x,z in shape];f=[(0,3,2,1),(4,5,6,7)]+[(k,(k+1)%4,(k+1)%4+4,k+4)for k in range(4)];mesh('Mitred_frame_'+str(i),v,f,'wood_lower' if i==0 else 'wood',.0012)
# Gently bowed real two-sided 0.28 mm sheets, with curl mainly at unpinned bottom corners.
anchors=[]
def paper(name,x,z,w,h,angle,region):
 v=[];N=4;ang=math.radians(angle)
 for side in range(2):
  for j in range(N+1):
   for i in range(N+1):
    a=i/N;b=j/N;xx=(a-.5)*w;zz=(b-.5)*h;curl=.0004+.0027*((1-b)**3)*(abs(a-.5)*2)**3
    v.append((x+xx*math.cos(ang)-zz*math.sin(ang),-.0244-curl+side*.00028,z+xx*math.sin(ang)+zz*math.cos(ang)))
 f=[];L=(N+1)**2
 for side in range(2):
  for j in range(N):
   for i in range(N):
    k=side*L+j*(N+1)+i;f.append((k,k+1,k+N+2,k+N+1))
 rim=list(range(N+1))+[j*(N+1)+N for j in range(1,N+1)]+[N*(N+1)+i for i in range(N-1,-1,-1)]+[j*(N+1)for j in range(N-1,0,-1)]
 for a,b in zip(rim,rim[1:]+rim[:1]):f.append((a,b,b+L,a+L))
 o=mesh(name,v,f,region)
 # Parametric paper UVs preserve the printed layout despite mesh curl and rotation.
 uv=o.data.uv_layers.active;x0,y0,x1,y1=rects[region]
 for po in o.data.polygons:
  for li in po.loop_indices:
   k=o.data.loops[li].vertex_index%L;a=(k%(N+1))/N;b=(k//(N+1))/N;uv.data[li].uv=((x0+a*(x1-x0))/1024,1-(y1-b*(y1-y0))/1024)
 for a in ([-.34,.34] if region=='main' else [0]):
  xx=a*w;zz=h*.43;anchors.append((x+xx*math.cos(ang)-zz*math.sin(ang),z+xx*math.sin(ang)+zz*math.cos(ang)))
paper('Guest_information_sheet',-.132,.015,.355,.284,-1.2,'main')
paper('Quiet_hours_sheet',.188,.075,.214,.1783,2.2,'quiet')
paper('Coffee_card',.187,-.134,.213,.1065,-2.8,'coffee')
# Rounded low-profile burgundy drawing pins, heads resting on the paper.
for i,(x,z) in enumerate(anchors):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=12,ring_count=6,radius=1,location=(x,-.027,z));o=bpy.context.object;o.scale=(.0042,.0024,.0042);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);finish(o,'Burgundy_pin_'+str(i),'pin')
# Two small dark rear hanger plates as source mounting reference; no wall or collision.
# Mount points remain metadata only; no embedded duplicate hardware.
editable=bpy.data.collections.new('Editable_original_components');s.collection.children.link(editable)
for o in parts:
 cp=o.copy();cp.data=o.data.copy();editable.objects.link(cp)
editable.hide_viewport=True;editable.hide_render=True;bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();o=bpy.context.object;o.name='motel_reception_noticeboard';s.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');bpy.ops.object.transform_apply(location=True,rotation=True,scale=True);t=o.modifiers.new('Explicit_runtime_triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=t.name)
for im in [n.image,q.image]:im.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(R/'source/reception_noticeboard.blend'))
bpy.ops.export_scene.gltf(filepath=str(R/'runtime/motel_reception_noticeboard.glb'),export_format='GLB',use_selection=True,export_yup=True,export_tangents=True)
(R/'qa/mounting.json').write_text(json.dumps({'coordinate_system':'GLB metres Y-up front +Z','origin':'frame rear wall-contact plane center','wall_plane_Z':0,'mount_anchor_centers_m':[[-.25,.234,0],[.25,.234,0]],'mount_spacing_m':.5,'frame_section_m':[.033,.032],'backing_thickness_m':.006,'backing_rear_recess_m':.0015,'cork_thickness_m':.018,'paper_thickness_m':.00028,'pin_centers_XY_m':anchors,'support':'Reference only. Engine owns mounting, support, damage and collision. Source unplaced.'},indent=2))
# Preserve identical geometry while making exporter triangle ordering reproducible.
import runpy
runpy.run_path(str(R/'source/canonicalize_glb.py'))
