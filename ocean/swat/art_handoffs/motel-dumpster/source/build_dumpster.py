"""BC_DUMP01 original static motel refuse-container art. Blender 4.3.2.
Usage: blender -b --python build_dumpster.py. DUMP_OUT redirects output root.
Inputs: relative source/maps only; no scene, mesh, add-on, or network dependencies.
"""
import bpy,bmesh,math,json,os,hashlib
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parent
OUT=Path(os.environ.get('DUMP_OUT',ROOT.parent));(OUT/'runtime').mkdir(parents=True,exist_ok=True);(OUT/'source').mkdir(exist_ok=True);(OUT/'qa').mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
for m in list(bpy.data.materials):bpy.data.materials.remove(m)
parts=[]
root=bpy.data.objects.new('BC_DUMP01_ROOT',None);bpy.context.collection.objects.link(root)
root['asset_id']='BC_DUMP01';root['status']='Static closed-lid art candidate; native acceptance OPEN';root['units']='metres';root['front']='Blender -Y / glTF +Z';root['pivot']='ground footprint center';root['collision']='none supplied'
def mat(n,tex,metal,rough):
 m=bpy.data.materials.new(n);m.diffuse_color=(.25,.3,.25,1);m.use_nodes=True;m.use_backface_culling=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
 t=m.node_tree.nodes.new('ShaderNodeTexImage');t.image=bpy.data.images.load(str(ROOT/'maps'/tex),check_existing=True);t.interpolation='Linear';m.node_tree.links.new(t.outputs['Color'],p.inputs['Base Color']);return m
paint=mat('BC_DUMP01_WeatheredPaint','dumpster_paint_atlas.png',.05,.83)
lidmat=mat('BC_DUMP01_MoldedLid','rubber.png',0,.88)
steel=mat('BC_DUMP01_Hardware','steel.png',.62,.63)
label=mat('BC_DUMP01_FictionalLabel','dumpster_label_atlas.png',0,.88)
def mesh(n,vs,fs,m,uv_func=None):
 me=bpy.data.meshes.new(n);me.from_pydata(vs,[],fs);me.update();ob=bpy.data.objects.new(n,me);bpy.context.collection.objects.link(ob);ob.data.materials.append(m);ob.parent=root;parts.append(ob)
 bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free()
 uv=me.uv_layers.new(name='UVMap')
 for p in me.polygons:
  ax=max(range(3),key=lambda i:abs(p.normal[i]));u,v=([1,2],[0,2],[0,1])[ax]
  for li in p.loop_indices:
   co=me.vertices[me.loops[li].vertex_index].co
   uv.data[li].uv=uv_func(co,p) if uv_func else (co[u]*.7+.2,co[v]*.7+.2)
  loopids=list(p.loop_indices); coords=[uv.data[li].uv.copy() for li in loopids]
  area=abs(sum(coords[i].x*coords[(i+1)%len(coords)].y-coords[(i+1)%len(coords)].x*coords[i].y for i in range(len(coords))))
  if (not uv_func and m==paint) or area<1e-10:
   cs=[me.vertices[me.loops[li].vertex_index].co for li in loopids]
   ulo=min(c[u] for c in cs);uhi=max(c[u] for c in cs);vlo=min(c[v] for c in cs);vhi=max(c[v] for c in cs)
   for li,co in zip(loopids,cs):uv.data[li].uv=(.10+.29*(co[u]-ulo)/max(uhi-ulo,1e-9),.60+.29*(co[v]-vlo)/max(vhi-vlo,1e-9))
 return ob
def box(n,loc,dim,m=paint,b=.006):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=n;o.dimensions=dim;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if b:
  mod=o.modifiers.new('Fabricated edge bevel','BEVEL');mod.width=b;mod.segments=1;bpy.ops.object.modifier_apply(modifier=mod.name)
 o.data.materials.append(m);o.parent=root;parts.append(o)
 bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.cube_project(cube_size=.75);bpy.ops.object.mode_set(mode='OBJECT')
 if m==paint:
  uv=o.data.uv_layers.active
  for p in o.data.polygons:
   ax=max(range(3),key=lambda i:abs(p.normal[i]));u,v=([1,2],[0,2],[0,1])[ax];lis=list(p.loop_indices);cs=[o.data.vertices[o.data.loops[li].vertex_index].co for li in lis];lo=[min(c[i] for c in cs) for i in range(3)];hi=[max(c[i] for c in cs) for i in range(3)]
   for li,c in zip(lis,cs):uv.data[li].uv=(.014+.472*(c[u]-lo[u])/max(hi[u]-lo[u],1e-9),.514+.472*(c[v]-lo[v])/max(hi[v]-lo[v],1e-9))
 o.select_set(False);return o

def cylinder(n,a,b,r,m=steel,N=12):
 a,b=Vector(a),Vector(b);bpy.ops.mesh.primitive_cylinder_add(vertices=N,radius=r,depth=(b-a).length,location=(a+b)/2);o=bpy.context.object;o.name=n;o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();o.data.materials.append(m);o.parent=root;parts.append(o);return o

def beam(n,a,b,width,depth,m=paint,bev=.004):
 a,b=Vector(a),Vector(b);o=box(n,(a+b)/2,(width,depth,(b-a).length),m,bev);o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();return o

def prism(n,poly,offset,m=paint,uv=None):
 vs=list(poly)+[tuple(Vector(v)+Vector(offset)) for v in poly];N=len(poly);fs=[tuple(range(N-1,-1,-1)),tuple(range(N,N*2))]+[(i,(i+1)%N,(i+1)%N+N,i+N) for i in range(N)];return mesh(n,vs,fs,m,uv)

def paneluv(q,u,v):
 return ((q%2)*.5+.015+u*.47,(1-q//2)*.5+.015+v*.47)
# Closed manifold sheet shell panels. Small localized front dent, not random vertex jitter.
xs=[-1,-.66,-.33,0,.33,.66,1]; ts=[0,.19,.42,.67,1];vs=[]
for t in ts:
 for u in xs:
  x=u*(.92+.13*t);y=-.54-.14*t;z=.20+1.04*t
  dent=.025*max(0,1-abs(u-.50)/.22)*max(0,1-abs(t-.42)/.32)
  vs.append((x,y+dent,z))
N=len(vs);vs+= [(x,y+.017,z) for x,y,z in vs];fs=[];nx=len(xs);ny=len(ts)
for j in range(ny-1):
 for i in range(nx-1):
  a=j*nx+i;fs.extend([(a,a+1,a+1+nx,a+nx),(a+N,a+nx+N,a+nx+1+N,a+1+N)])
perim=list(range(nx))+[j*nx+nx-1 for j in range(1,ny)]+list(range((ny-1)*nx+nx-2,(ny-1)*nx-1,-1))+[j*nx for j in range(ny-2,0,-1)]
for i,a in enumerate(perim):b=perim[(i+1)%len(perim)];fs.append((a,b,b+N,a+N))
mesh('Body_FrontDentedPanel',vs,fs,paint,lambda c,p:paneluv(0,(c.x+1.05)/2.10,(c.z-.20)/1.04))
prism('Body_RearPanel',[(-.92,.54,.20),(.92,.54,.20),(1.05,.68,1.52),(-1.05,.68,1.52)],(0,-.017,0),paint,lambda c,p:paneluv(1,(c.x+1.05)/2.1,(c.z-.20)/1.32))
for s,n in [(-1,'Left'),(1,'Right')]:
 prism('Body_'+n+'TaperedPanel',[(s*.92,-.54,.20),(s*.92,.54,.20),(s*1.05,.68,1.52),(s*1.05,-.68,1.24)],(-s*.017,0,0),paint,lambda c,p,q=2+(s==1):paneluv(q,(c.y+.68)/1.36,(c.z-.20)/1.32))
box('Body_WeldedFloor',(0,0,.207),(1.84,1.08,.030),paint,.004)
# Distinct reinforcing hems, rolled top rim and pressed vertical stiffeners.
beam('Rim_Front',(-1.07,-.68,1.25),(1.07,-.68,1.25),.058,.057,paint,.010)
beam('Rim_Rear',(-1.07,.68,1.53),(1.07,.68,1.53),.058,.057,paint,.010)
for s,n in [(-1,'Left'),(1,'Right')]:
 beam('Rim_'+n,(s*1.05,-.68,1.25),(s*1.05,.68,1.53),.058,.052,paint,.009)
 beam('Corner_'+n+'Front',(s*.92,-.549,.23),(s*1.05,-.689,1.23),.056,.025,paint,.004)
 beam('Corner_'+n+'Rear',(s*.92,.549,.23),(s*1.05,.689,1.51),.056,.025,paint,.004)
 beam('LowerHem_'+n,(s*.93,-.54,.24),(s*.93,.54,.24),.045,.055,paint,.004)
beam('LowerHem_Front',(-.94,-.549,.24),(.94,-.549,.24),.048,.045,paint,.004)
beam('LowerHem_Rear',(-.94,.549,.24),(.94,.549,.24),.048,.045,paint,.004)
# U-section pressed stiffeners: slanted polygonal section with flattened shoulders.
for k,x in enumerate([-.72,-.25,.25,.72]):
 z0=.31;z1=1.17;y0=-.54-.14*(z0-.2)/1.04-.014;y1=-.54-.14*(z1-.2)/1.04-.014
 sec=[(-.049,0),(-.032,-.026),(.032,-.026),(.049,0),(.038,.008),(-.038,.008)]
 v=[(x+u,y+w,z) for y,z in [(y0,z0),(y1,z1)] for u,w in sec];n=len(sec);f=[tuple(range(n-1,-1,-1)),tuple(range(n,n*2))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
 mesh('Front_PressedRib_%02d'%(k+1),v,f,paint)
# Ground contact via broad skid rails, hollow underbody remains visible.
for s,n in [(-1,'Left'),(1,'Right')]:
 # Tapered boat-ended skid, closed extruded six-point profile.
 poly=[(s*.70-.065,-.61,0),(s*.70-.065,.61,0),(s*.70-.065,.61,.11),(s*.70-.065,.48,.20),(s*.70-.065,-.48,.20),(s*.70-.065,-.61,.11)]
 prism('Skid_'+n,poly,(.13,0,0),paint)
# Open rectangular fork sleeves: thick wall ring ends and continuous inner passage.
def pocket(s,n):
 xc=s*1.13;zc=.85;outer=[(-.11,-.115),(.11,-.115),(.11,.115),(-.11,.115)];inner=[(-.089,-.094),(.089,-.094),(.089,.094),(-.089,.094)]
 vv=[]
 for y in [-.56,.56]:
  for loop in [outer,inner]:
   vv +=[(xc+x,y,zc+z) for x,z in loop]
 ff=[]
 for i in range(4):
  j=(i+1)%4;ff +=[(i,j,8+j,8+i),(4+i,12+i,12+j,4+j),(i,4+i,4+j,j),(8+i,8+j,12+j,12+i)]
 ob=mesh('ForkPocket_'+n+'_OpenTube',vv,ff,paint)
 # Scraped leading edges are local steel end collars, open aperture retained.
 for y,tag in [(-.565,'Front'),(.565,'Rear')]:
  for a,b in [((xc-.10,y,zc-.105),(xc+.10,y,zc-.105)),((xc-.10,y,zc+.105),(xc+.10,y,zc+.105))]:beam('ForkLip_'+n+'_'+tag,a,b,.012,.012,steel,.002)
 for y,tag in [(-.37,'Front'),(.37,'Rear')]:
  prism('ForkGusset_'+n+'_'+tag,[(s*1.005,y,.72),(s*1.225,y,.72),(s*1.015,y,.52)],(0,.032,0),paint)
for s,n in [(-1,'Left'),(1,'Right')]:pocket(s,n)
# Two molded lid shells, each rib is part of one connected closed mesh.
def lid(xmin,xmax,n):
 xx=[xmin,xmin+.025]
 for i in range(6):
  c=xmin+.100+i*(xmax-xmin-.20)/5
  xx += [c-.045,c-.026,c+.026,c+.045]
 xx +=[xmax-.025,xmax];xx=sorted(set(xx));yy=[-.725,-.665,.655,.725];vv=[]
 for j,y in enumerate(yy):
  for x in xx:
   h=0
   for i in range(6):
    c=xmin+.100+i*(xmax-xmin-.20)/5;d=abs(x-c)
    h=max(h,.028*max(0,min(1,(.045-d)/.019)))
   z=1.292+(y+.725)/1.45*.298+h*([0,1,1,0][j]);vv.append((x,y,z))
 nx=len(xx);ny=len(yy);ff=[]
 for j in range(ny-1):
  for i in range(nx-1):a=j*nx+i;ff.append((a,a+1,a+nx+1,a+nx))
 pp=list(range(nx))+[j*nx+nx-1 for j in range(1,ny)]+list(range((ny-1)*nx+nx-2,(ny-1)*nx-1,-1))+[j*nx for j in range(ny-2,0,-1)]
 ni=len(vv)
 for k in pp:
  x,y,z=vv[k];vv.append((x,y,1.292+(y+.725)/1.45*.298-.040))
 ff.append(tuple(range(ni,ni+len(pp))))
 for i,k in enumerate(pp):ff.append((k,pp[(i+1)%len(pp)],ni+(i+1)%len(pp),ni+i))
 o=mesh('Lid_'+n+'_CorrugatedShell',vv,ff,lidmat);o['state']='closed static';o['hinge_axis_blender']=[1,0,0];o['hinge_center_blender']=[(xmin+xmax)/2,.70,1.56]
 # Molded grab loop at front is open underneath and grounded in the lip.
 xc=(xmin+xmax)/2
 for s in [-1,1]:box('Lid_'+n+'_HandleFoot_'+str(s),(xc+s*.115,-.719,1.318),(.032,.055,.060),lidmat,.007)
 beam('Lid_'+n+'_HandleGrip',(xc-.13,-.746,1.343),(xc+.13,-.746,1.343),.027,.036,lidmat,.007)
 return o
lid(-1.095,-.009,'Left');lid(.009,1.095,'Right')
# Hinge barrels and supporting welded tabs sit visibly against rear rim.
for k,x in enumerate([-.88,-.20,.20,.88]):
 box('Hinge_%02d_MountTab'%k,(x,.705,1.49),(.075,.022,.12),paint,.003)
 cylinder('Hinge_%02d_Barrel'%k,(x-.052,.714,1.565),(x+.052,.714,1.565),.025,steel,12)
 cylinder('Hinge_%02d_PinEnd'%k,(x-.059,.714,1.565),(x+.059,.714,1.565),.012,steel,8)
# Two simple rear reinforcement straps.
for x in [-.52,.52]:beam('RearStrap_'+str(x),(x,.57,.41),(x,.691,1.40),.048,.025,paint,.004)
# Front original fictional placard, seated on the sloping panel with an intentional thin label thickness.
z0=.79;z1=1.05;x0=-.185;x1=.185
p=[(x0,-.54-.14*(z0-.20)/1.04-.002,z0),(x1,-.54-.14*(z0-.20)/1.04-.002,z0),(x1,-.54-.14*(z1-.20)/1.04-.002,z1),(x0,-.54-.14*(z1-.20)/1.04-.002,z1)]
prism('Label_BriarCourtRefuse',p,(0,.002,0),label,lambda c,p:((c.x-x0)/(x1-x0),.5+(c.z-z0)/(z1-z0)*.5))
# Small caution decals on side top corners; physically thick and opaque, no alpha sorting.
for s,n in [(-1,'Left'),(1,'Right')]:
 z0=.98;z1=1.12;y0=-.52;y1=-.34
 # body x reaches 1.05 at top, side skin x does not vary linearly with z for sloped top; use same side surface y/z slope.
 def side_x(y,z):
  # side wall plane: x=.92+.13*((z-.20)/(1.18+.28*y/1.36)); small outward label offset
  # exact side quad vertices are noncoplanar; labels are placed on fork housing instead below.
  return s*1.243
 p=[(side_x(0,0),y0,.785),(side_x(0,0),y1,.785),(side_x(0,0),y1,.915),(side_x(0,0),y0,.915)]
 prism('Label_ForkCaution_'+n,p,(-s*.002,0,0),label,lambda c,p: (.50+(c.y-y0)/(y1-y0)*.49,.01+(c.z-.785)/.130*.48))
# Drain plug face in lower rear; decorative static closure only.
cylinder('Rear_DrainBoss',(0,.555,.31),(0,.582,.31),.036,paint,12)
cylinder('Rear_DrainHex',(0,.582,.31),(0,.591,.31),.023,steel,6)
# Freeze transforms while preserving editable named solids. Parent/root identity; no negative scale.
for o in parts:
 bpy.context.view_layer.objects.active=o;o.select_set(True);bpy.ops.object.transform_apply(location=True,rotation=True,scale=True);o.select_set(False)
 bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
 # Triangulation modifier remains editable in source, exporter evaluates it.
 mod=o.modifiers.new('Runtime triangulation','TRIANGULATE');mod.quad_method='FIXED';mod.ngon_method='BEAUTY'
 o['render_only']=True
bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=1.0
for im in bpy.data.images:
 if im.source=='FILE':im.pack();im.filepath='//maps/'+Path(im.filepath).name
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source'/'BC_DUMP01_editable.blend'))
# Runtime: three static owners, five material primitives. Source remains separately editable.
bpy.ops.object.select_all(action='DESELECT')
runtime=[]
for group in ['Body','Lid_Left','Lid_Right']:
 subset=[o for o in parts if (o.name.startswith(group+'_') if group!='Body' else not o.name.startswith(('Lid_Left_','Lid_Right_')))]
 copies=[]
 for o in subset:
  c=o.copy();c.data=o.data.copy();bpy.context.collection.objects.link(c);copies.append(c);c.select_set(True)
 bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();joined=bpy.context.object;joined.name='BC_DUMP01_'+group;joined['source_parts']=[o.name for o in subset];runtime.append(joined);joined.select_set(False)
root.select_set(True)
for o in runtime:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/'runtime'/'BC_DUMP01_closed.glb'),export_format='GLB',use_selection=True,export_yup=True,export_texcoords=True,export_normals=True,export_materials='EXPORT',export_apply=True,export_extras=True)
# Builder geometry inventory, not an independent certification.
bounds=[o.matrix_world@v.co for o in parts for v in o.data.vertices];lo=[min(v[i] for v in bounds) for i in range(3)];hi=[max(v[i] for v in bounds) for i in range(3)];rows=[]
for o in parts:
 me=o.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh();me.calc_loop_triangles();bm=bmesh.new();bm.from_mesh(o.data)
 rows.append({'node':o.name,'triangles':len(me.loop_triangles),'vertices':len(me.vertices),'material':o.data.materials[0].name,'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),'signed_volume':bm.calc_volume(signed=True),'uv_layers':len(me.uv_layers)});bm.free();o.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh_clear()
report={'asset':'BC_DUMP01','revision':'r01','mesh_nodes':len(parts),'triangles':sum(p['triangles'] for p in rows),'bounds_blender_xyz_m':[lo,hi],'dimensions_blender_xyz_m':[hi[i]-lo[i] for i in range(3)],'root_pivot':[0,0,0],'materials':4,'embedded_images':4,'parts':rows,'runtime_mesh_nodes':3,'runtime_material_primitives':5,'status':'Static offline candidate; no native engine acceptance or performance claims'}
(OUT/'qa'/'builder_inventory.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='parts'}))
