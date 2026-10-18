"""BC_SEDAN01 original anonymous compact. Blender 4.3.2 deterministic mesh recipe.
Design coordinates: +X forward, +Y left, +Z up. Converted once before saving
into project Blender axes (+X left, -Y front, +Z up), and exporter converts
to standard glTF (+X left, +Y up, +Z front). All distances in metres.
"""
import bpy, bmesh, math, json, os, sys, hashlib
from pathlib import Path
from mathutils import Vector
from math import sin,cos,pi,sqrt
R=Path(os.environ.get('SEDAN_OUT',Path(__file__).resolve().parent.parent)); SRC=Path(__file__).resolve().parent
for n in ['runtime','source','qa','previews','docs']:(R/n).mkdir(parents=True,exist_ok=True)
MAPS=R/'source/maps'
if not (MAPS/'paint_baseColor.png').exists():MAPS=SRC/'maps'
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1.0
parts=[];groups={};metadata={}

def material(name,color,rough,metal=0,base=None,orm=None):
 m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1);p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
 if base:
  t=m.node_tree.nodes.new('ShaderNodeTexImage');t.image=bpy.data.images.load(str(MAPS/base),check_existing=True);m.node_tree.links.new(t.outputs['Color'],p.inputs['Base Color'])
 if orm:
  t=m.node_tree.nodes.new('ShaderNodeTexImage');t.image=bpy.data.images.load(str(MAPS/orm),check_existing=True);t.image.colorspace_settings.name='Non-Color';sep=m.node_tree.nodes.new('ShaderNodeSeparateColor');m.node_tree.links.new(t.outputs['Color'],sep.inputs['Color']);m.node_tree.links.new(sep.outputs['Green'],p.inputs['Roughness']);m.node_tree.links.new(sep.outputs['Blue'],p.inputs['Metallic'])
 m.surface_render_method='DITHERED';m.use_backface_culling=True
 return m
paint=material('BC_SEDAN_Paint_SageGrey',(0.16,.21,.19),.33,.43,'paint_baseColor.png','paint_ORM.png')
rubber=material('BC_SEDAN_Rubber_Trim',(.012,.014,.013),.79,0,'rubber_baseColor.png')
glass=material('BC_SEDAN_Opaque_Glass',(.036,.060,.068),.20,.20)
detail=material('BC_SEDAN_Detail_Atlas',(.4,.42,.42),.4,0,'detail_baseColor.png','detail_ORM.png')

def add_mesh(name,verts,faces,mat,group='body',tile=None,smooth=True,uvs=None):
 me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);me.materials.append(mat)
 bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
 if bm.calc_volume(signed=True)<-1e-12:bmesh.ops.reverse_faces(bm,faces=list(bm.faces))
 bm.to_mesh(me);bm.free()
 # Per-face box UVs are intentional for low-contrast paint; custom tire UVs supplied below.
 uv=me.uv_layers.new(name='UVMap');lo=[min(v[k] for v in verts) for k in range(3)];hi=[max(v[k] for v in verts) for k in range(3)]
 for poly in me.polygons:
  poly.use_smooth=smooth
  drop=max(range(3),key=lambda k:abs(poly.normal[k]));axes=[k for k in range(3) if k!=drop]
  for li in poly.loop_indices:
   co=me.vertices[me.loops[li].vertex_index].co
   q=[max(0,min(1,(co[a]-lo[a])/max(hi[a]-lo[a],1e-6))) for a in axes]
   if mat==rubber:q=[.05+.12*q[0],.02+.12*q[1]]
   if tile is not None:q=[(tile%4+.06+.88*q[0])/4,(tile//4+.06+.88*q[1])/4]
   uv.data[li].uv=q
 me.set_sharp_from_angle(angle=math.radians(60))
 if uvs:
  # Called after normal recalc using vertex->UV except the cylindrical seam.
  for poly in me.polygons:
   vals=[uvs[me.loops[li].vertex_index] for li in poly.loop_indices];cross=max(q[0] for q in vals)-min(q[0] for q in vals)>.5
   for li,q in zip(poly.loop_indices,vals):uv.data[li].uv=(q[0]+(1 if cross and q[0]<.5 else 0),q[1])
 parts.append(o);groups[o.name]=group
 return o

def interp(x,points):
 if x<=points[0][0]:return points[0][1]
 for (a,v),(b,w) in zip(points,points[1:]):
  if x<=b:return v+(w-v)*(x-a)/(b-a)
 return points[-1][1]
widths=[(-2.15,.84),(-2.08,.845),(-1.92,.85),(-1.28,.875),(-.50,.864),(.5,.863),(1.28,.875),(1.91,.845),(2.10,.845),(2.15,.84)]
tops=[(-2.15,.84),(-2.05,.85),(-1.70,.88),(-1.27,.935),(-.85,.956),(.70,.945),(1.10,.913),(1.65,.852),(2.04,.841),(2.15,.84)]
def width(x):return interp(x,widths)
def top(x):return interp(x,tops)
def archbottom(x):
 z=.245
 for wc in [-1.28,1.28]:
  dx=abs(x-wc)
  if dx<=.362+1e-7:z=max(z,.315+sqrt(max(0,.362**2-dx**2)))
 return z
xs=set([-2.15,-2.13,-2.10,-2.08,-2.05,-2.0,-1.95,-1.92,-1.7,-.85,-.5,.0,.5,.7,1.1,1.65,1.91,2.0,2.04,2.08,2.10,2.13,2.15])
for wc in [-1.28,1.28]:
 for j in range(17):xs.add(round(wc+.362*cos(pi*j/16),7))
 xs.add(wc-.363);xs.add(wc+.363)
xs=sorted(xs)
verts=[]
for x in xs:
 w=width(x);t=top(x);b=archbottom(x);upper=t-.12
 # Positive side from roof center, around shoulder, into wheel well and floor.
 half=[(0,t),(.58*w,t-.004),(.84*w,t-.020),(.966*w,t-.059),(w,upper),(.998*w,(upper+b)/2),(.973*w,b),(.607,b),(.54,.235),(0,.235)]
 ring=half+ [(-y,z) for y,z in half[-2:0:-1]]
 verts.extend([(x-math.copysign((.19*(abs(y)/.84)**3+.04*max(0,min(1,(.63-z)/.10)))*max(0,(abs(x)-1.75)/.40),x),y,z) for y,z in ring])
nr=len(ring);faces=[]
for i in range(len(xs)-1):
 for j in range(nr):faces.append((i*nr+j,i*nr+(j+1)%nr,(i+1)*nr+(j+1)%nr,(i+1)*nr+j))
for ri,x in [(0,xs[0]),(len(xs)-1,xs[-1])]:
 center=len(verts);verts.append((x-math.copysign(.04,x),0,.50))
 for j in range(nr):faces.append((center,ri*nr+j,ri*nr+(j+1)%nr))

body=add_mesh('Coachwork_continuous_arch_cut_shell',verts,faces,paint)
# Continuous longitudinal/perimeter UVs keep the fast-changing inner arch walls valid.
uv=body.data.uv_layers.active
for p in body.data.polygons:
 iscap=any(i>=len(xs)*nr for i in p.vertices)
 js=[i%nr for i in p.vertices if i<len(xs)*nr];wrap=bool(js) and max(js)-min(js)>nr/2
 for li in p.loop_indices:
  vi=body.data.loops[li].vertex_index;co=body.data.vertices[vi].co
  if iscap:q=((co.y+.90)/1.80,(co.z-.20)/.80)
  else:
   ri,j=divmod(vi,nr);q=((xs[ri]+2.15)/4.30,j/nr+(1 if wrap and j==0 else 0))
  uv.data[li].uv=q


# Round-ended solid strips swept along paths; details use restrained cross-sections.
def tube(name,path,r,mat,group='body',tile=None,sides=6):
 closed=(Vector(path[0])-Vector(path[-1])).length<1e-8
 path=list(path[:-1] if closed else path);vv=[];last_t=None;last_u=None
 for i,p in enumerate(path):
  left=path[(i-1)%len(path)] if closed else path[max(0,i-1)]
  right=path[(i+1)%len(path)] if closed else path[min(i+1,len(path)-1)]
  tangent=(Vector(right)-Vector(left)).normalized()
  if last_t is None:
   ref=Vector((0,0,1)) if abs(tangent.z)<.9 else Vector((0,1,0));u=tangent.cross(ref).normalized()
  else:u=last_t.rotation_difference(tangent)@last_u;u=(u-tangent*u.dot(tangent)).normalized()
  v=tangent.cross(u).normalized();last_t=tangent;last_u=u
  for j in range(sides):vv.append(tuple(Vector(p)+r*(cos(2*pi*j/sides)*u+sin(2*pi*j/sides)*v)))
 ff=[]
 for i in range(len(path) if closed else len(path)-1):
  ni=(i+1)%len(path)
  for j in range(sides):ff.append((i*sides+j,i*sides+(j+1)%sides,ni*sides+(j+1)%sides,ni*sides+j))
 if not closed:ff.extend([tuple(range(sides-1,-1,-1)),tuple((len(path)-1)*sides+j for j in range(sides))])
 return add_mesh(name,vv,ff,mat,group,tile,smooth=False)

def box(name,loc,size,mat,group='body',tile=None,bevel=.01):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=name;o.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if bevel:
  m=o.modifiers.new('Editable_edge_radii','BEVEL');m.width=min(bevel,min(size)*.22);m.segments=1;bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=m.name)
 verts=[tuple(o.matrix_world@v.co) for v in o.data.vertices];faces=[tuple(p.vertices) for p in o.data.polygons];bpy.data.objects.remove(o,do_unlink=True)
 return add_mesh(name,verts,faces,mat,group,tile,False)

# Actual raised lips follow true openings, including downturned ends.
for side in [-1,1]:
 for tag,wc in [('rear',-1.28),('front',1.28)]:
  path=[(wc+.363,side*(width(wc+.363)*.975+.002),.25)]
  for j in range(21):
   a=pi*j/20;x=wc+.365*cos(a);z=.315+.365*sin(a);path.append((x,side*(width(x)*.975+.004),z))
  path.append((wc-.363,side*(width(wc-.363)*.975+.002),.25))
  tube(f'{tag}_arch_lip_{side}',path,.012,paint,sides=4)
 # Rocker sill and small bumper side rub strip, deliberately not crossing arch holes.
 tube(f'Rocker_sill_{side}',[(-.90,side*.846,.275),(-.70,side*.85,.266),(.70,side*.85,.266),(.90,side*.846,.275)],.022,paint,sides=6)

# Cabin shell: broad curved roof, sloping screen and rear glass; no interior.
def side_y(z):return .795-.148*(z-.88)/.51
# Side opaque metal sheets (the inset opaque glass overlays are a static-exterior treatment).
for side in [-1,1]:
 poly=[(-1.34,side*.795,.88),(1.06,side*.795,.88),(.36,side*.647,1.392),(-.64,side*.647,1.392)]
 off=[(x,y-side*.012,z) for x,y,z in poly]
 add_mesh(f'Cabin_side_frame_{side}',poly+off,[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],paint)

# Smooth closed patch helper, backs offset into cabin. Independent topology remains editable.
def patch(name,fn,nu,nv,offset,mat,group='body',tile=None):
 vv=[fn(i/nu,j/nv) for j in range(nv+1) for i in range(nu+1)];N=len(vv);vv+= [tuple(Vector(p)+Vector(offset)) for p in vv]
 ff=[]
 for j in range(nv):
  for i in range(nu):
   a=j*(nu+1)+i;b=a+1;c=b+nu+1;d=a+nu+1;ff.extend([(a,b,c,d),(d+N,c+N,b+N,a+N)])
 boundary=list(range(nu+1))+[j*(nu+1)+nu for j in range(1,nv+1)]+[nv*(nu+1)+i for i in range(nu-1,-1,-1)]+[j*(nu+1) for j in range(nv-1,0,-1)]
 for a,b in zip(boundary,boundary[1:]+boundary[:1]):ff.append((a,b,b+N,a+N))
 return add_mesh(name,vv,ff,mat,group,tile)

def roof(u,v):
 x=-.641+1.002*u; y=(v*2-1)*.649;z=1.39+.060*(1-(y/.649)**2)+.004*sin(pi*u)
 return(x,y,z)
patch('Roof_crown',roof,6,8,(0,0,-.018),paint)
def screen(u,v):
 y=(u*2-1)*(.796-.148*v);x=1.06-.70*v+.025*(1-(u*2-1)**2)*sin(pi*v);z=.88+.510*v+(1-(u*2-1)**2)*(.045+.015*v)
 return(x,y,z)
def rear_screen(u,v):
 y=(u*2-1)*(.796-.148*v);x=-1.34+.70*v-.016*(1-(u*2-1)**2)*sin(pi*v);z=.88+.510*v+(1-(u*2-1)**2)*(.045+.015*v)
 return(x,y,z)
patch('Windshield_frame_base',screen,8,5,(-.012,0,-.006),paint)
patch('Rear_screen_frame_base',rear_screen,8,5,(.012,0,-.006),paint)
# Opaque, sealed glass sits 2.5 mm above each coachwork backing surface.
patch('Windshield_opaque',lambda u,v:tuple(Vector(screen(.030+.94*u,.05+.90*v))+Vector((.0025,0,.0025))),8,5,(-.004,0,-.003),glass,'windows')
patch('Rear_screen_opaque',lambda u,v:tuple(Vector(rear_screen(.045+.91*u,.065+.87*v))+Vector((-.0025,0,.0025))),8,5,(.004,0,-.003),glass,'windows')

# Front and rear side lites, with narrow black weather seals and visible body A/B/C pillars.
def side_window(name,points,side):
 vv=[(x,side*(side_y(z)+.0035),z) for x,z in points]
 back=[(x,y-side*.004,z) for x,y,z in vv];N=len(vv)
 o=add_mesh(name,vv+back,[tuple(range(N)),tuple(range(2*N-1,N-1,-1))]+[(i,(i+1)%N,(i+1)%N+N,i+N) for i in range(N)],glass,'windows',smooth=False)
 tube(name+'_weatherseal',vv+[vv[0]],.007,rubber,sides=4)
for side in [-1,1]:
 side_window(f'Front_side_window_{side}',[(.946,.953),(.334,1.357),(-.187,1.357),(-.186,.953)],side)
 side_window(f'Rear_side_window_{side}',[(-.278,.953),(-.277,1.357),(-.608,1.357),(-1.212,.953)],side)
 # B-pillar dark satin applique, physically continuous pillar remains behind.
 pts=[(-.253,side*(side_y(.950)+.004),.950),(-.213,side*(side_y(.950)+.004),.950),(-.215,side*(side_y(1.360)+.004),1.360),(-.251,side*(side_y(1.360)+.004),1.360)]
 add_mesh(f'B_pillar_applique_{side}',pts+[(x,y-side*.003,z) for x,y,z in pts],[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],rubber)
 # Beltline seal follows side glass base and a restrained chrome-free drip rail.
 tube(f'Beltline_seal_{side}',[(-1.22,side*(side_y(.945)+.004),.945),(-.8,side*(side_y(.945)+.004),.945),(.4,side*(side_y(.945)+.004),.945),(.96,side*(side_y(.945)+.004),.945)],.006,rubber,sides=4)

# Surface coordinates for panel seams: coachwork side above arch opening.
def sy(x,z):
 w=width(x);t=top(x);b=archbottom(x);upper=t-.12
 if z>=t-.059:
  return w*interp(z,[(t-.059,.966),(t-.02,.84),(t,.0)])
 if z>=upper:return w*interp(z,[(upper,1),(t-.059,.966)])
 if z>=b:return w*interp(z,[(b,.973),((upper+b)/2,.998),(upper,1)])
 return .973*w
from mathutils.bvhtree import BVHTree
contact_meshes=[body,bpy.data.objects['Cabin_side_frame_1']]
contact_bvhs=[BVHTree.FromPolygons([v.co for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons]) for o in contact_meshes]
body_bvh=contact_bvhs[0]
def shut_y(x,z):
 hits=[tree.ray_cast(Vector((x,2,z)),Vector((0,-1,0)),4)[0] for tree in contact_bvhs]
 ys=[h.y for h in hits if h is not None]
 return (max(ys) if ys else sy(x,z))+.0015
for side in [-1,1]:
 for tag,pathxz in [
  ('shared_B_pillar',[(-.225,.953),(-.225,.93),(-.225,.91),(-.225,.89),(-.225,.86),(-.225,.80),(-.23,.56),(-.24,.315)]),
  ('front_door',[(-.241,.315),(.76,.315),(.84,.40),(.90,.53),(.925,.70),(1.005,.85),(1.015,.89),(.985,.93),(.946,.953)]),
  ('rear_door',[(-.239,.315),(-.73,.315),(-.85,.37),(-.90,.55),(-1.02,.67),(-1.22,.75),(-1.30,.89),(-1.27,.92),(-1.212,.953)])]:
  dense=[]
  for (x0,z0),(x1,z1) in zip(pathxz,pathxz[1:]):
   steps=max(1,math.ceil(math.hypot(x1-x0,z1-z0)/(.035 if min(z0,z1)>.82 else .10)))
   dense.extend([(x0+(x1-x0)*k/steps,z0+(z1-z0)*k/steps) for k in range(steps)])
  dense.append(pathxz[-1]);path=[(x,side*shut_y(x,z),z) for x,z in dense]
  tube(f'{tag}_panel_gap_{side}',path,.0028,rubber,sides=4)
 # Compact pull handles with dark inset and shaped painted cap.
 for tag,x in [('front',-.055),('rear',-.775)]:
  y=side*(sy(x,.835)+.003)
  box(f'{tag}_handle_recess_{side}',(x,y,.835),(.173,.012,.043),rubber,bevel=.012)
  box(f'{tag}_handle_{side}',(x,y+side*.013,.840),(.135,.025,.022),detail,tile=8,bevel=.009)
 # Fuel flap only right side, seam conforms to rear fender.
 if side==-1:
  corners=[(-1.80,.760),(-1.64,.760),(-1.64,.85),(-1.80,.85),(-1.80,.760)];path=[]
  for (x0,z0),(x1,z1) in zip(corners,corners[1:]):
   for k in range(4):
    x=x0+(x1-x0)*k/4;z=z0+(z1-z0)*k/4;path.append((x,side*shut_y(x,z),z))
  path.append(path[0])
  tube('Fuel_filler_seam',path,.0027,rubber,sides=4)

# Hood and boot shut lines conform to top skin.
for side in [-1,1]:
 path=[]
 for x in [1.04,1.20,1.4,1.65,1.85,1.98]:
  y=side*width(x)*.78;hit=body_bvh.ray_cast(Vector((x,y,3)),Vector((0,0,-1)),4)[0];z=hit.z if hit else top(x)-.016;path.append((x,y,z+.0015))
 tube(f'Hood_side_shutline_{side}',path,.003,rubber,sides=4)
 path=[]
 for x in [-1.40,-1.60,-1.80,-1.98]:
  y=side*width(x)*.78;hit=body_bvh.ray_cast(Vector((x,y,3)),Vector((0,0,-1)),4)[0];path.append((x,y,(hit.z if hit else top(x)-.014)+.0015))
 tube(f'Boot_side_shutline_{side}',path,.003,rubber,sides=4)
for name,x in [('Hood_front',1.98),('Boot_rear',-1.98)]:
 path=[]
 for yy in [-.78,-.4,0,.4,.78]:
  y=yy*width(x);hit=body_bvh.ray_cast(Vector((x,y,3)),Vector((0,0,-1)),4)[0];path.append((x,y,(hit.z if hit else top(x)-.022*yy*yy)+.0015))
 tube(name+'_shutline',path,.003,rubber,sides=4)

# Bumpers are tapered closed lofts that wrap to the corners, not plain cubes.
def bumper(name,front):
 vv=[];s=1 if front else -1
 for i in range(17):
  y=-.83+1.66*i/16;f=abs(y/.83);x=s*(2.135-.19*f**3)
  for dx,z in [(-.05,.34),(.006,.365),(.015,.50),(-.018,.555),(-.13,.56),(-.15,.34)]:vv.append((x+s*dx,y,z))
 ff=[]
 for i in range(16):
  for j in range(6):ff.append((i*6+j,i*6+(j+1)%6,(i+1)*6+(j+1)%6,(i+1)*6+j))
 ff.extend([tuple(range(5,-1,-1)),tuple(16*6+j for j in range(6))]);return add_mesh(name,vv,ff,paint)
bumper('Front_wrap_bumper',True);bumper('Rear_wrap_bumper',False)
for s,tag in [(1,'Front'),(-1,'Rear')]:
 path=[]
 for j in range(13):
  y=-.81+1.62*j/12;x=s*(2.155-.19*abs(y/.83)**3);path.append((x,y,.481))
 tube(tag+'_bumper_rubrail',path,.023,rubber,sides=4)
 seam=[(s*(2.151-.19*(abs(y)/.84)**3),y,.563) for y in [-.81,-.72,-.6,-.4,-.2,0,.2,.4,.6,.72,.81]]
 tube(tag+'_bumper_body_separation',seam,.0035,rubber,sides=4)
 # Lower air intake/rear recess and modest blank plates.
 box(tag+'_plate_surround',(s*2.145,0,.613),(.018,.405,.128),rubber,bevel=.01)
 box(tag+'_blank_registration_panel',(s*2.158,0,.613),(.012,.343,.088),detail,tile=5,bevel=.004)
 if s==1:
  box('Lower_intake_dark',(2.158,0,.399),(.021,.70,.065),rubber,bevel=.016)
  # narrow, unbranded grille between lamps, with three plausible horizontal slats
  box('Upper_grille_recess',(2.165,0,.755),(.016,.675,.108),rubber,bevel=.013)
  for z in [.722,.75,.778]:box('Unbranded_grille_slat_'+str(z),(2.178,0,z),(.012,.64,.011),detail,tile=4,bevel=.003)

# Lens volumes have sloping outer edges, flush with swept front/rear fascia.
def lens(name,front,side,tile,z0,z1,y0,y1):
 s=1 if front else -1
 # Backing depth forms a real enclosed lens with lens-like edge radii profile.
 pts=[]
 for y,z in [(y0,z0),(y1,z0+.013),(y1,z1-.015),(y0,z1)]:
  yy=side*y;xx=s*(2.161-.19*(y/.84)**3);pts.append((xx,yy,z))
 back=[(x-s*.024,y,z) for x,y,z in pts]
 add_mesh(name,pts+back,[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],detail,tile=tile,smooth=False)
 tube(name+'_gasket',pts+[pts[0]],.008,rubber,sides=4)
for side in [-1,1]:
 lens(f'Headlamp_{side}',True,side,1,.674,.796,.374,.705)
 lens(f'Front_indicator_{side}',True,side,2,.674,.773,.722,.794)
 lens(f'Rear_tail_lamp_{side}',False,side,3,.674,.788,.345,.790)
 lens(f'Rear_reverse_lens_{side}',False,side,7,.691,.758,.358,.460)
 # Small side marker on the front fender.
 box(f'Side_marker_{side}',(1.69,side*(sy(1.69,.735)+.004),.735),(.085,.012,.029),detail,tile=2,bevel=.009)

# Door-mounted mirrors: closed tapered ellipsoid-like housings, no logo.
for side in [-1,1]:
 tube(f'Mirror_stalk_{side}',[(.82,side*.79,1.005),(.78,side*.87,1.018)],.025,rubber,sides=6)
 # A bevelled wedge housing is more slender than a cube, with a rear-facing mirror inset.
 vv=[(.68,side*.86,.996),(.84,side*.86,.996),(.87,side*.98,1.005),(.66,side*.98,1.005),(.69,side*.86,1.075),(.83,side*.86,1.075),(.85,side*.97,1.063),(.67,side*.97,1.063)]
 add_mesh(f'Mirror_housing_{side}',vv,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],paint)
 # Viewable reflective face on aft end of housing; opaque metallic surface.
 patch(f'Mirror_face_{side}',lambda u,v,s=side:(.662+.012*v,s*(.875+.086*u),1.012+.043*v),2,1,(.003,0,0),detail,tile=6)

# Tires and restrained pressed-steel wheel covers. All four have true ground contact.
wheel_centers=[]
for x,tag in [(1.28,'Front'),(-1.28,'Rear')]:
 for side,label in [(1,'Left'),(-1,'Right')]:
  yc=side*.765;zc=.315;group=f'wheel_{tag.lower()}_{label.lower()}';wheel_centers.append({'name':group,'design_center':[x,yc,zc],'radius':.315,'width':.20})
  profile=[(-.10,.210),(-.10,.263),(-.088,.301),(-.066,.315),(.066,.315),(.088,.301),(.10,.263),(.10,.210)]
  vv=[];uv=[];N=40
  for j,(dy,r) in enumerate(profile):
   for i in range(N):
    a=2*pi*i/N;vv.append((x+r*cos(a),yc+dy,max(0,zc+r*sin(a))));uv.append((i/N,j/(len(profile)-1)))
  ff=[]
  for j in range(len(profile)):
   for i in range(N):ff.append((j*N+i,j*N+(i+1)%N,((j+1)%len(profile))*N+(i+1)%N,((j+1)%len(profile))*N+i))
  add_mesh(group+'_tire',vv,ff,rubber,group,uvs=uv)
  # Metal wheel dish at both outer and inner sides. Vent recesses are dark material islands.
  for face_side in [-1,1]:
   outer=face_side==side
   if not outer:
    tube(group+'_inner_steel_dish',[(x,yc+face_side*.080,zc),(x,yc+face_side*.087,zc)],.208,detail,group,tile=4,sides=32)
    continue
   levels=[(.0,.098),(.055,.103),(.115,.100),(.153,.089),(.187,.085),(.211,.098)]
   vv=[];nf=32
   # Avoid collapsed center fan by use one center vertex, then five complete loops.
   vv=[(x,yc+face_side*levels[0][1],zc)]
   for r,depth in levels[1:]:
    for i in range(nf):
     a=2*pi*i/nf;dep=depth
     if .14<r<.20 and i%6 in [1,2,3,4]:dep-=.007
     vv.append((x+r*cos(a),yc+face_side*dep,zc+r*sin(a)))
   ff=[(0,1+i,1+(i+1)%nf) for i in range(nf)]
   for j in range(len(levels)-2):
    for i in range(nf):ff.append((1+j*nf+i,1+j*nf+(i+1)%nf,1+(j+1)*nf+(i+1)%nf,1+(j+1)*nf+i))
   # Closed flat backing uses one fan rather than duplicating invisible dish topology.
   backstart=len(vv)
   for i in range(nf):
    a=2*pi*i/nf;vv.append((x+.211*cos(a),yc+face_side*.076,zc+.211*sin(a)))
   bc=len(vv);vv.append((x,yc+face_side*.076,zc))
   for i in range(nf):
    a=1+4*nf+i;b=1+4*nf+(i+1)%nf;c=backstart+(i+1)%nf;d=backstart+i
    ff.append((a,b,c,d));ff.append((bc,d,c))
   o=add_mesh(group+('_outer' if outer else '_inner')+'_steel_cover',vv,ff,detail,group,tile=0)
   # Recess fields occupy wheel's annular slots, rendered by same atlas material.
   if outer:
    for slot in range(8):
     a=2*pi*(slot+.19)/8;b=2*pi*(slot+.80)/8;pts=[]
     for r,ang in [(.151,a),(.188,a),(.188,b),(.151,b)]:pts.append((x+r*cos(ang),yc+face_side*.083,zc+r*sin(ang)))
     bk=[(a,b-face_side*.003,c) for a,b,c in pts]
     add_mesh(group+'_vent_'+str(slot),pts+bk,[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],detail,group,tile=4,smooth=False)
    # Four actual tiny lug heads on each pressed wheel cover.
    for lug in range(4):
     a=2*pi*lug/4+pi/4
     tube(group+'_lug_'+str(lug),[(x+.080*cos(a),yc+side*.104,zc+.080*sin(a)),(x+.080*cos(a),yc+side*.111,zc+.080*sin(a))],.013,detail,group,tile=8,sides=6)

# Modest exterior undertray and axle hints; no simulation or mechanical fidelity claim.
box('Static_undertray',(0,0,.239),(2.0,1.13,.035),rubber,bevel=.025)
for x,tag in [(-1.28,'Rear'),(1.28,'Front')]:
 tube(tag+'_static_axle_hint',[(x,-.65,.315),(x,.65,.315)],.035,detail,tile=4,sides=8)
# Front wipers rest on opaque glass and are simple restrained geometry.
for a,b in [(-.55,-.08),(.12,.57)]:
 p0=Vector(screen((a+.795)/1.59,.085));p1=Vector(screen((b+.795)/1.59,.13));p0+=Vector((.008,0,.008));p1+=Vector((.008,0,.008));tube('Parked_wiper_'+str(a),[p0,p1],.007,rubber,sides=4)

# One root, project axis conversion baked into mesh data. Keep named source solids editable.
root=bpy.data.objects.new('BC_SEDAN01',None);bpy.context.collection.objects.link(root)
root['asset_id']='BC_SEDAN01';root['revision']='r01';root['scope']='Original static exterior, opaque glazing. No interior, rig, driving, collision or damage implementation.';root['front_axis']='Blender -Y; glTF +Z';root['unit']='metres';root['pivot']='Ground center (0,0,0)'
for source_order,o in enumerate(parts):
 o['source_order']=source_order
 for v in o.data.vertices:v.co=(v.co.y,-v.co.x,v.co.z)
 o.data.update();o.parent=root;o['runtime_group']=groups[o.name]
 # Triangulation left non-destructive in editable source.
 m=o.modifiers.new('Export_triangulation','TRIANGULATE');m.keep_custom_normals=True
for im in bpy.data.images:
 if im.source=='FILE':im.pack()
for w in wheel_centers:
 x,y,z=w['design_center'];w['blender_center']=[y,-x,z];w['gltf_center']=[y,z,x];del w['design_center']
root['wheel_centers_json']=json.dumps(wheel_centers)
# A neutral source viewport; no review stage shipped with asset.
bpy.ops.object.select_all(action='DESELECT');root.select_set(True);bpy.context.view_layer.objects.active=root
bpy.ops.wm.save_as_mainfile(filepath=str(R/'source/BC_SEDAN01_editable.blend'))
# Collapse source solids into seven logical static runtime groups on a temporary copy.
source_stats={'source_mesh_objects':len(parts),'wheel_centers':wheel_centers,'blender_version':bpy.app.version_string,'groups':{},'materials':[m.name for m in [paint,rubber,glass,detail]]}
group_objects={g:[o for o in parts if groups[o.name]==g] for g in sorted(set(groups.values()))}
for g,obs in group_objects.items():
 source_stats['groups'][g]=len(obs)
 bpy.ops.object.select_all(action='DESELECT')
 for o in obs:o.select_set(True)
 bpy.context.view_layer.objects.active=obs[0];bpy.ops.object.convert(target='MESH');bpy.ops.object.join();obj=bpy.context.object;obj.name='BC_SEDAN01_'+g;obj.data.name=obj.name;obj['role']=g
 # Shared ground-centered origins preserve easy engine import; wheel center metadata is descriptive.
 obj.location=(0,0,0)
bpy.ops.object.select_all(action='DESELECT');root.select_set(True)
for o in root.children:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(R/'runtime/BC_SEDAN01_parked.glb'),export_format='GLB',use_selection=True,export_apply=True,export_yup=True,export_materials='EXPORT',export_extras=True,export_cameras=False,export_lights=False,export_animations=False,export_normals=True,export_texcoords=True)
source_stats['runtime_sha256']=hashlib.sha256((R/'runtime/BC_SEDAN01_parked.glb').read_bytes()).hexdigest();(R/'qa/builder_inventory.json').write_text(json.dumps(source_stats,indent=2))
print('SEDAN_BUILD_DONE',json.dumps(source_stats))
