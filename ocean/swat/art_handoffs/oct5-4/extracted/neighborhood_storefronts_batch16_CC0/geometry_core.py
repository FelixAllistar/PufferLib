"""Batch 16 / Neighborhood Storefronts geometry support. Original CC0-1.0 artwork. Blender 4.3+.
Shared original geometry and texture functions for build_storefronts.py.
Included locally so regeneration does not depend on earlier batch scripts.
"""
import bpy, bmesh, math, random, json, os, sys, hashlib
import numpy as np
from math import sin, cos, pi
from mathutils import Vector, Matrix
ROOT=os.path.dirname(os.path.abspath(__file__))
for p in ['assets','textures','previews']:os.makedirs(os.path.join(ROOT,p),exist_ok=True)
R=random.Random(161005)
TILES={}; MATS={}; V=[]; F=[]; FM=[]; TAG=[]; CURRENT='body'
def texture(key,color,kind,size=192):
 rng=np.random.default_rng(161005+sum((i+1)*ord(c) for i,c in enumerate(key)))
 y,x=np.mgrid[0:size,0:size].astype(float)/size
 n=rng.normal(0,.024,(size,size)); cloud=np.zeros_like(x)
 for i in range(24):
  cx,cy=rng.uniform(-.2,1.2,2); s=rng.uniform(.025,.23)
  cloud+=rng.uniform(-.16,.07)*np.exp(-((x-cx)**2+(y-cy)**2)/(s*s))
 grain=1+n+cloud*.18
 if kind=='wood':
  grain+=.11*np.sin(x*142+np.sin(y*12)*2.2)+.038*np.sin(x*371+np.sin(y*33))
  for i in range(18):
   cx,cy=rng.random(2);grain-=rng.uniform(.15,.35)*np.exp(-((x-cx)**2/.00001+(y-cy)**2/.025))
 elif kind in ['metal','paint','plastic']:
  for i in range(160):
   cx,cy=rng.random(2);sx=rng.uniform(.001,.005);sy=rng.uniform(.001,.018)
   grain-=rng.uniform(.12,.45)*np.exp(-((x-cx)**2/(sx*sx)+(y-cy)**2/(sy*sy)))
 elif kind=='cloth':grain+=.055*np.sin(x*size*pi)+.035*np.sin(y*size*pi)
 elif kind=='rust':grain+=.12*np.sin(x*65)*np.sin(y*39)
 elif kind in ['clay','earth']:grain+=rng.normal(0,.042,(size,size))
 rgba=np.ones((size,size,4),dtype=np.float32)
 for c in range(3):rgba[:,:,c]=np.clip(color[c]*grain,.004,.92)
 im=bpy.data.images.new('NS16_'+key,width=size,height=size);im.pixels.foreach_set(rgba.ravel())
 im.filepath_raw=os.path.join(ROOT,'textures',key+'.png');im.file_format='PNG';im.save();im.pack()
 return im

def materials():
 for key,(col,rough,metal,kind) in PALETTE.items():
  m=bpy.data.materials.new('NS16_'+key);m.use_nodes=True;m.diffuse_color=(*col,1)
  bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(1,1,1,1)
  bs.inputs['Roughness'].default_value=rough;bs.inputs['Metallic'].default_value=metal
  tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=texture(key,col,kind);tex.interpolation='Linear'
  m.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color']);MATS[key]=m

def group(s):
 global CURRENT;CURRENT=s

def add(vs,fs,mat,rot=None,c=(0,0,0)):
 start=len(V);c=Vector(c)
 V.extend([tuple((rot@Vector(v) if rot else Vector(v))+c) for v in vs]);F.extend([tuple(start+i for i in f) for f in fs]);FM.extend([mat]*len(fs));TAG.extend([CURRENT]*len(fs))

def box(c,d,mat='iron',bevel=.008,rot=None):
 if isinstance(rot,(float,int)):rot=Matrix.Rotation(rot,3,'Z')
 bm=bmesh.new();bmesh.ops.create_cube(bm,size=1)
 for v in bm.verts:v.co.x*=d[0];v.co.y*=d[1];v.co.z*=d[2]
 if bevel:bmesh.ops.bevel(bm,geom=list(bm.edges),offset=min(bevel,min(d)*.2),segments=1,affect='EDGES')
 bm.verts.ensure_lookup_table();bm.verts.index_update();add([tuple(v.co) for v in bm.verts],[tuple(v.index for v in f.verts) for f in bm.faces],mat,rot,c);bm.free()

def rod(a,b,r,mat='iron',n=12,r2=None):
 a=Vector(a);b=Vector(b);d=b-a
 if d.length<.000001:return
 rr=r if r2 is None else r2
 vv=[(r*cos(2*pi*i/n),r*sin(2*pi*i/n),0) for i in range(n)]+[(rr*cos(2*pi*i/n),rr*sin(2*pi*i/n),d.length) for i in range(n)]
 ff=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,n+(i+1)%n,n+i) for i in range(n)]
 add(vv,ff,mat,d.to_track_quat('Z','Y').to_matrix(),a)

def cyl(c,r,h,mat='iron',n=24):rod((c[0],c[1],c[2]-h/2),(c[0],c[1],c[2]+h/2),r,mat,n)

def tube(points,r,mat='rubber',n=8,closed=False):
 points=[Vector(p) for p in points]; vv=[]; prev=None
 for i,p in enumerate(points):
  tangent=(points[min(i+1,len(points)-1)]-points[max(0,i-1)]).normalized()
  helper=Vector((0,0,1)) if abs(tangent.z)<.9 else Vector((0,1,0))
  u=(prev-tangent*prev.dot(tangent)) if prev is not None else tangent.cross(helper)
  if u.length<1e-6:u=tangent.cross(helper)
  u.normalize();w=tangent.cross(u).normalized();prev=u.copy()
  for j in range(n):vv.append(tuple(p+r*(cos(j*2*pi/n)*u+sin(j*2*pi/n)*w)))
 ff=[]
 for i in range(len(points)-1):
  for j in range(n):ff.append((i*n+j,i*n+(j+1)%n,(i+1)*n+(j+1)%n,(i+1)*n+j))
 ff += [tuple(reversed(range(n))),tuple((len(points)-1)*n+j for j in range(n))]
 add(vv,ff,mat)

def torus(c,major,minor,mat='steel',axis='Z',n=28,k=6):
 rot={'Z':None,'X':Matrix.Rotation(pi/2,3,'Y'),'Y':Matrix.Rotation(pi/2,3,'X')}[axis]
 vv=[((major+minor*cos(j*2*pi/k))*cos(i*2*pi/n),(major+minor*cos(j*2*pi/k))*sin(i*2*pi/n),minor*sin(j*2*pi/k)) for i in range(n) for j in range(k)]
 ff=[(i*k+j,((i+1)%n)*k+j,((i+1)%n)*k+(j+1)%k,i*k+(j+1)%k) for i in range(n) for j in range(k)]
 add(vv,ff,mat,rot,c)

def rr(w,d,z,bevel=.045):
 a=w/2;b=d/2;q=min(bevel,a*.4,b*.4)
 return [(-a+q,-b,z),(a-q,-b,z),(a,-b+q,z),(a,b-q,z),(a-q,b,z),(-a+q,b,z),(-a,b-q,z),(-a,-b+q,z)]

def loft(rings,mat,cap_bottom=True,cap_top=False):
 n=len(rings[0]);vv=sum(rings,[]);ff=[]
 for j in range(len(rings)-1):
  for i in range(n):ff.append((j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i))
 if cap_bottom:ff.append(tuple(reversed(range(n))))
 if cap_top:ff.append(tuple(range((len(rings)-1)*n,len(rings)*n)))
 add(vv,ff,mat)

def open_box(w,d,h,mat='plastic_blue',thick=.02):
 loft([rr(w*.85,d*.85,0),rr(w,d,h),rr(w-2*thick,d-2*thick,h),rr(w*.85-2*thick,d*.85-2*thick,.035)],mat,True,True)

def bowl_profile(profile,mat,n=32,irregular=0):
 rings=[]
 for r,z in profile:rings.append([((r+irregular*sin(i*2*pi/n*7))*cos(i*2*pi/n),(r+irregular*sin(i*2*pi/n*7))*sin(i*2*pi/n),z) for i in range(n)])
 loft(rings,mat,True,True)

def bolts(c,dx,dz,mat='steel_light'):
 x,y,z=c
 for xx in [-dx/2,dx/2]:
  for zz in [-dz/2,dz/2]:rod((x+xx,y,z+zz),(x+xx,y-.004,z+zz),.009,mat,8)

def patch(c,d,mat='rust',axis='Y',seed=1):
 rng=random.Random(seed);n=7;vs=[(0,0,0)]+[(cos(i*2*pi/n)*d[0]*rng.uniform(.6,1),sin(i*2*pi/n)*d[1]*rng.uniform(.6,1),0) for i in range(n)]
 rot=Matrix.Rotation(pi/2,3,'X') if axis=='Y' else None
 add(vs,[(0,i+1,(i+1)%n+1) for i in range(n)],mat,rot,c)

def wheel(c,r=.1,width=.05):
 x,y,z=c;rod((x-width/2,y,z),(x+width/2,y,z),r,'rubber',24)
 for xx in [x-width/2-.001,x+width/2+.001]:
  rod((xx-.002,y,z),(xx+.002,y,z),r*.52,'steel',16);rod((xx-.004,y,z),(xx+.004,y,z),r*.17,'rust',10)


def collection(name,parent=None):
 c=bpy.data.collections.new(name);(parent or bpy.context.scene.collection).children.link(c);return c

def bounds(ob):
 vv=[ob.matrix_world@Vector(v.co) for v in ob.data.vertices]
 return [[min(v[i] for v in vv) for i in range(3)],[max(v[i] for v in vv) for i in range(3)]]

def mesh_from_buffer(name,col):
 mesh=bpy.data.meshes.new(name+'_mesh');mesh.from_pydata(V,[],F);mesh.update();ob=bpy.data.objects.new(name,mesh);col.objects.link(ob)
 used=list(dict.fromkeys(FM))
 for m in used:mesh.materials.append(MATS[m])
 for p,m in zip(mesh.polygons,FM):p.material_index=used.index(m)
 bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free();mesh.update()
 uv=mesh.uv_layers.new(name='UVMap')
 for p in mesh.polygons:
  axis=max(range(3),key=lambda i:abs(p.normal[i]));axes=[i for i in range(3) if i!=axis]
  for li in p.loop_indices:
   v=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=(v[axes[0]]/TILES.get(used[p.material_index],(1,1))[0],v[axes[1]]/TILES.get(used[p.material_index],(1,1))[1])
 for tag in sorted(set(TAG)):
  vg=ob.vertex_groups.new(name=tag);inds=set()
  for face,t in zip(F,TAG):
   if t==tag:inds.update(face)
  vg.add(list(inds),1,'REPLACE')
 return ob

def connection(name,p,n):
 return {'name':name,'position_source_m':p,'outward_source':n,'position_gltf_m':[p[0],p[2],-p[1]],'outward_gltf':[n[0],n[2],-n[1]]}


def light(col,name,loc,target,power,size,color):
 d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='DISK';d.size=size;d.color=color;ob=bpy.data.objects.new(name,d);col.objects.link(ob);ob.location=loc;ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler();return ob

def camera(col,name,loc,target,ortho=None,lens=35):
 d=bpy.data.cameras.new(name);d.clip_end=300;d.lens=lens;ob=bpy.data.objects.new(name,d);col.objects.link(ob);ob.location=loc;ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler()
 if ortho:d.type='ORTHO';d.ortho_scale=ortho
 bpy.context.scene.camera=ob;return ob

def stage_box(name,c,d,mat,col):
 global V,F,FM,TAG,CURRENT
 V=[];F=[];FM=[];TAG=[];CURRENT='presentation';box(c,d,mat,.002);ob=mesh_from_buffer(name,col)
 ob['license']='CC0-1.0';ob['units']='metres';ob['render_only']=True;ob['collider_enabled']=False;ob['colliders_supplied']=False;ob['presentation_only']=True;return ob

