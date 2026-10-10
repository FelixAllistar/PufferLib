"""Original CC0-1.0 furniture variants. Blender 4.3+, metres, -Y front.
Run: blender -b -t 2 --python-exit-code 1 --python build_furniture.py
Geometry and texture artwork are original. No external asset dependencies.
"""
import bpy, bmesh, math, os, json, random, struct, zlib, sys
import numpy as np
from mathutils import Vector, Matrix
from math import sin, cos, pi
ROOT=os.path.dirname(os.path.abspath(__file__))
for folder in ('assets','textures','previews','qa'):os.makedirs(os.path.join(ROOT,folder),exist_ok=True)
MATS={}; V=[]; F=[]; MI=[]
PALETTE={
 'wool_slate':((.34,.39,.38),.96,'fabric',0.,'fabric'),
 'wool_olive':((.41,.42,.32),.97,'fabric',0.,'fabric'),
 'wool_faded':((.51,.51,.43),.96,'fabric',0.,'fabric'),
 'vinyl_tobacco':((.33,.245,.18),.78,'vinyl',0.,'fabric'),
 'wood_walnut':((.33,.244,.16),.8,'wood',0.,'wood'),
 'wood_honey':((.50,.386,.24),.81,'wood',0.,'wood'),
 'wood_endgrain':((.44,.335,.218),.89,'wood',0.,'wood'),
 'wood_dark':((.22,.177,.129),.89,'wood',0.,'wood'),
 'paint_putty':((.58,.57,.47),.84,'paint',0.,'wood'),
 'metal_sage':((.39,.43,.35),.72,'paint',.12,'steel'),
 'metal_cream':((.63,.61,.49),.66,'paint',.13,'steel'),
 'steel':((.38,.39,.36),.56,'metal',.66,'steel'),
 'iron':((.19,.20,.185),.73,'metal',.5,'steel'),
 'rust':((.37,.24,.14),.94,'rust',.0,'steel'),
 'cane':((.55,.456,.31),.94,'cane',0.,'wood'),
 'canvas':((.48,.47,.385),.97,'fabric',0.,'fabric'),
 'foam':((.60,.47,.26),1.,'foam',0.,'fabric'),
 'rubber':((.11,.12,.11),.9,'plain',0.,'rubber'),
 'seam':((.235,.25,.219),.95,'plain',0.,'fabric'),
 'brass':((.43,.36,.23),.58,'metal',.64,'steel'),
}

def png(path,a):
 h,w,_=a.shape
 raw=b''.join(b'\x00'+a[y].tobytes() for y in range(h))
 def chunk(k,d):return struct.pack('!I',len(d))+k+d+struct.pack('!I',zlib.crc32(k+d)&0xffffffff)
 with open(path,'wb') as f:f.write(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))

def make_materials():
 for key,(color,rough,kind,metal,semantic) in PALETTE.items():
  m=bpy.data.materials.new('F04_'+key);m.use_nodes=True
  p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
  m.diffuse_color=(*color,1);m['surface_semantic']=semantic;m['license']='CC0-1.0';m['physics_binding']='suggestion_only; preserve authoritative gameplay material'
  if semantic in ('wood','steel'):m['physics_material_id_suggestion']={'wood':2,'steel':4}[semantic]
  if kind=='plain':p.inputs['Base Color'].default_value=(*[v**2.2 for v in color],1)
  else:
   n=512 if kind in ('fabric','wood','vinyl','cane') else 256
   rng=np.random.default_rng(sum((i+1)*ord(c) for i,c in enumerate(key))+4204)
   y,x=np.mgrid[0:n,0:n].astype(float)/n
   value=1+rng.normal(0,.025,(n,n))
   for i in range(22):
    a,b=rng.random(2);r=rng.uniform(.03,.23)
    value+=np.exp(-((x-a)**2+(y-b)**2)/r**2)*rng.uniform(-.16,.1)
   if kind=='wood':
    wave=x*245+1.2*np.sin(y*12)+.55*np.sin(y*33)
    value+=.10*np.sin(wave)+.035*np.sin(wave*3.7)
    for i in range(18):
     xx,yy=rng.random(2);value-=np.exp(-((x-xx)**2/.000015+(y-yy)**2/.01))*rng.uniform(.1,.35)
   elif kind=='fabric':
    value+=.045*np.sin(x*n*pi)+.055*np.cos(y*n*pi)
    value+=.025*np.sin(x*141)*np.sin(y*157)
   elif kind=='vinyl':
    value+=.028*np.sin(x*987+np.sin(y*422)*4)*np.sin(y*843)
    for i in range(34):
     xx,yy=rng.random(2);value-=.10*np.exp(-((x-xx+.012*np.sin(y*92))**2/.000008+(y-yy)**2/.0004))
   elif kind=='cane':value+=.14*np.sin(x*320)+.025*np.sin(y*71)
   elif kind in ('paint','metal','rust'):
    for i in range(100):
     xx,yy=rng.random(2);r=rng.uniform(.001,.008);value-=rng.uniform(.1,.35)*np.exp(-((x-xx)**2+(y-yy)**2)/r**2)
   a=np.stack([np.clip(v*value,0,1) for v in color],axis=-1)
   path=os.path.join(ROOT,'textures',key+'.png');png(path,(a*255).astype('uint8'))
   im=bpy.data.images.load(path);im.name='F04_Texture_'+key;im.colorspace_settings.name='sRGB';im.pack()
   tx=m.node_tree.nodes.new('ShaderNodeTexImage');tx.image=im;tx.extension='REPEAT';tx.interpolation='Linear'
   m.node_tree.links.new(tx.outputs['Color'],p.inputs['Base Color']);p.inputs['Base Color'].default_value=(1,1,1,1)
  MATS[key]=m

def add(v,f,mat,rot=None,center=(0,0,0)):
 s=len(V);c=Vector(center)
 if rot is None:V.extend([tuple(Vector(q)+c) for q in v])
 else:V.extend([tuple(rot@Vector(q)+c) for q in v])
 F.extend([tuple(s+i for i in face) for face in f]);MI.extend([mat]*len(f))

def box(c,d,m='wood_walnut',bevel=.009,rot=None,segments=1):
 if isinstance(rot,(int,float)):rot=Matrix.Rotation(rot,3,'Z')
 bm=bmesh.new();bmesh.ops.create_cube(bm,size=1)
 for v in bm.verts:v.co.x*=d[0];v.co.y*=d[1];v.co.z*=d[2]
 if bevel:bmesh.ops.bevel(bm,geom=list(bm.edges),offset=min(bevel,min(d)*.45),segments=segments,affect='EDGES',profile=.5)
 bm.verts.ensure_lookup_table();bm.verts.index_update()
 add([tuple(v.co) for v in bm.verts],[tuple(v.index for v in f.verts) for f in bm.faces],m,rot,c);bm.free()

def rod(a,b,r,m='iron',n=10,r2=None):
 a,b=Vector(a),Vector(b);d=b-a
 if d.length<1e-7:return
 q=d.to_track_quat('Z','Y').to_matrix();rr=r if r2 is None else r2
 vv=[(rad*cos(2*pi*i/n),rad*sin(2*pi*i/n),z) for z,rad in ((0,r),(d.length,rr)) for i in range(n)]
 ff=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
 add(vv,ff,m,q,a)

def line(points,r=.005,m='seam',n=6):
 for a,b in zip(points,points[1:]):rod(a,b,r,m,n)

def ellipse_ring(c,rx,ry,r,m='cane',n=40,k=6):
 vv=[((rx+r*cos(2*pi*j/k))*cos(2*pi*i/n),(ry+r*cos(2*pi*j/k))*sin(2*pi*i/n),r*sin(2*pi*j/k)) for i in range(n) for j in range(k)]
 ff=[(i*k+j,((i+1)%n)*k+j,((i+1)%n)*k+(j+1)%k,i*k+(j+1)%k) for i in range(n) for j in range(k)]
 add(vv,ff,m,None,c)

def sphere(c,d,m='brass',n=12,k=6):
 vv=[(d[0]/2*sin(pi*j/k)*cos(2*pi*i/n),d[1]/2*sin(pi*j/k)*sin(2*pi*i/n),d[2]/2*cos(pi*j/k)) for j in range(k+1) for i in range(n)]
 ff=[(j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i) for j in range(k) for i in range(n)]
 add(vv,ff,m,None,c)

def transform_since(start,rot,center=(0,0,0)):
 c=Vector(center)
 for i in range(start,len(V)):V[i]=tuple(rot@Vector(V[i])+c)

def rounded_path(w,d,z,r):
 pts=[]
 for x,y,a in ((w/2-r,d/2-r,0),(-w/2+r,d/2-r,pi/2),(-w/2+r,-d/2+r,pi),(w/2-r,-d/2+r,3*pi/2)):
  for i in range(5):
   t=a+i*pi/8;pts.append((x+r*cos(t),y+r*sin(t),z))
 return pts+[pts[0]]

def piped(c,d,m='wool_slate',rotation=None,bevel=.06):
 start=len(V);box((0,0,0),d,m,bevel,segments=3)
 line(rounded_path(d[0]-.035,d[1]-.035,d[2]*.30,min(d[0],d[1])*.12),.0038,'seam',6)
 transform_since(start,rotation or Matrix.Identity(3),c)

def patch(c,size,m='foam',rot=None,seed=17):
 rng=random.Random(seed);n=10;verts=[(0,0,0)]+[(cos(i*2*pi/n)*size[0]*rng.uniform(.65,1),sin(i*2*pi/n)*size[1]*rng.uniform(.65,1),0) for i in range(n)]
 add(verts,[(0,i+1,(i+1)%n+1) for i in range(n)],m,rot,c)

def handle(x,y,z,w=.13,m='brass'):
 for xx in (x-w/2,x+w/2):rod((xx,y+.013,z),(xx,y-.025,z),.009,m,8)
 rod((x-w/2,y-.025,z),(x+w/2,y-.025,z),.011,m,10)

def turned_leg(x,y,h=.38):
 levels=[(0,.025),(.035,.035),(.07,.027),(.13,.021),(.22,.03),(h-.04,.042),(h,.038)]
 for (z,r),(zz,rr) in zip(levels,levels[1:]):rod((x,y,z),(x,y,zz),r,'wood_walnut',10,rr)

def wingback():
 for x in (-.32,.32):
  for y in (-.29,.28):turned_leg(x,y,.24)
 box((0,0,.30),(.81,.76,.21),'wool_slate',.075,segments=3)
 piped((0,-.095,.452),(.58,.62,.17),'wool_faded')
 # Graceful tapered back shell, inner high back and distinct forward wings.
 box((0,.29,.89),(.66,.21,.94),'wool_slate',.095,rot=Matrix.Rotation(math.radians(-6),3,'X'),segments=3)
 piped((0,.164,.91),(.51,.16,.76),'wool_slate',Matrix.Rotation(math.radians(-6),3,'X'),.065)
 for x in (-.365,.365):
  box((x,-.025,.63),(.17,.75,.30),'wool_slate',.073,segments=3)
  piped((x,-.02,.79),(.20,.71,.09),'wool_faded',bevel=.035)
  box((x*.88,.15,1.09),(.16,.33,.49),'wool_slate',.07,rot=Matrix.Rotation((-.13 if x<0 else .13),3,'Y'),segments=3)
 for x in (-.13,.13):
  for z in (.82,1.04):
   sphere((x,.071,z),(.028,.012,.028),'seam',10,4)
   line([(x-.07,.074,z-.09),(x,.059,z),(x+.07,.074,z+.09)],.0022,'seam',5)
 patch((.13,-.16,.539),(.075,.028),'seam');patch((.13,-.16,.540),(.047,.016),'foam',seed=29)
 for i in range(6):line([(.055+i*.022,-.187,.542),(.063+i*.022,-.166,.542)],.002,'wool_faded',5)

def recliner():
 for x in (-.34,.34):
  for y in (-.25,.3):box((x,y,.07),(.10,.13,.14),'rubber',.015)
 box((0,.015,.27),(.82,.77,.24),'vinyl_tobacco',.065,segments=3)
 piped((0,-.03,.472),(.59,.64,.22),'vinyl_tobacco',bevel=.085)
 for x in (-.39,.39):
  box((x,.015,.515),(.23,.87,.49),'vinyl_tobacco',.08,segments=3)
  piped((x,-.015,.78),(.25,.78,.16),'vinyl_tobacco',bevel=.07)
 rot=Matrix.Rotation(math.radians(-14),3,'X')
 box((0,.38,.9),(.78,.27,.83),'vinyl_tobacco',.095,rot,3)
 for z,y,h in ((.80,.237,.30),(1.13,.32,.31)):
  piped((0,y,z),(.62,.22,h),'vinyl_tobacco',rot,.09)
  line([(-.19,y-.112,z),(.19,y-.112,z)],.0028,'seam',5)
 # Extended static footrest with readable scissor support below.
 for x in (-.245,.245):
  rod((x,-.32,.23),(x,-.89,.43),.016,'iron',8)
  rod((x,-.31,.43),(x,-.86,.27),.016,'steel',8)
  sphere((x,-.58,.335),(.043,.025,.043),'brass',8,4)
 piped((0,-.91,.43),(.66,.43,.14),'vinyl_tobacco',Matrix.Rotation(math.radians(10),3,'X'),.055)
 rod((.519,.13,.49),(.562,.13,.49),.017,'iron',10)
 rod((.562,.13,.49),(.562,.18,.64),.016,'wood_walnut',10)
 sphere((.562,.18,.65),(.038,.071,.06),'wood_walnut',10,5)
 patch((-.12,-.12,.584),(.11,.027),'wood_honey',seed=15)
 for i in range(8):line([(-.30+i*.075,-.352,.535),(-.28+i*.075,-.355,.57)],.0018,'seam',5)

def folding_chair():
 # Front/rear tubular legs cross visibly at the sides, with formed seat/back.
 for x in (-.231,.231):
  line([(x,-.285,.026),(x,-.19,.395),(x,.08,.55),(x,.21,.85)],.0175,'metal_sage',10)
  line([(x,.30,.026),(x,.17,.33),(x,-.12,.49)],.016,'metal_sage',10)
  rod((x-.008,-.044,.407),(x+.008,-.044,.407),.032,'steel',12)
  for y in (-.285,.30):rod((x,y,.0),(x,y,.041),.023,'rubber',10)
  line([(x,-.19,.36),(x,.19,.27)],.008,'steel',8)
 rod((-.231,.255,.15),(.231,.255,.15),.012,'metal_sage',10)
 box((0,-.026,.472),(.456,.425,.045),'metal_sage',.019,segments=2)
 box((0,-.028,.502),(.408,.363,.018),'wool_olive',.008,segments=2)
 rot=Matrix.Rotation(math.radians(-10),3,'X')
 box((0,.19,.766),(.478,.061,.222),'metal_sage',.028,rot,2)
 box((0,.153,.768),(.406,.019,.15),'metal_cream',.008,rot,2)
 for x in (-.177,.177):rod((x,.133,.771),(x,.12,.771),.008,'steel',8)
 for x,y in ((-.19,-.17),(.13,.13),(.195,-.04)):
  patch((x,y,.497),(.021,.009),'rust',seed=int(abs(x)*500))

def spindle_chair():
 # Splayed legs, saddle seat, spindle bow back: distinct from kit ladderback.
 for x in (-.17,.17):
  for y in (-.16,.16):rod((x*1.25,y*1.3,.018),(x,y,.425),.021,'wood_walnut',10,.032)
 box((0,-.006,.449),(.484,.45,.068),'wood_honey',.032,segments=3)
 for y in (-.18,.18):rod((-.197,y,.19),(.197,y,.19),.011,'wood_walnut',10)
 rod((0,-.18,.19),(0,.18,.19),.012,'wood_walnut',10)
 pts=[]
 for i in range(25):
  a=pi*i/24;pts.append((.226*cos(a),.208+.049*sin(a),.50+.46*sin(a)))
 line(pts,.022,'wood_walnut',10)
 for i,x in enumerate((-.165,-.11,-.055,0,.055,.11,.165)):
  top=.50+.43*math.sqrt(1-(x/.228)**2)
  rod((x,.173,.478),(x,.248,top),.0095,'wood_honey',8)
  rod((x,.20,.635),(x,.223,.735),.013,'wood_walnut',10,.0095)
 for x in (-.21,.21):rod((x,.185,.46),(x,.21,.59),.018,'wood_walnut',10)
 for i in range(5):line([(-.12+i*.035,-.125,.485),(-.095+i*.035,-.11,.485)],.001,'wood_dark',4)

def card_table():
 # Slim padded top and folding underframe rather than a normal dining table.
 box((0,0,.729),(.89,.89,.052),'metal_cream',.016,segments=2)
 box((0,0,.759),(.84,.84,.011),'wool_olive',.004,segments=2)
 for x in (-.35,.35):
  for y in (-.35,.35):
   a=(x*1.07,y*1.07,.024);b=(x,y,.70)
   rod(a,b,.014,'metal_sage',10)
   rod((a[0],a[1],0),(a[0],a[1],.043),.019,'rubber',10)
   box((x,y,.689),(.07,.05,.044),'steel',.007)
  rod((x,-.35,.245),(x,.35,.245),.009,'metal_sage',8)
  for y in (-.35,.35):rod((x,y,.46),(x,y*.36,.681),.0065,'steel',8)
 for y in (-.35,.35):rod((-.35,y,.675),(.35,y,.675),.012,'metal_sage',8)
 patch((-.31,.285,.766),(.031,.017),'canvas',seed=27)
 for x,y in ((-.447,-.447),(-.447,.447),(.447,-.447),(.447,.447)):
  box((x*.984,y*.984,.731),(.04,.04,.048),'rubber',.007)

def low_dresser():
 for x in (-.57,.57):
  for y in (-.22,.22):box((x,y,.085),(.082,.075,.17),'wood_dark',.009)
 box((0,.273,.469),(1.32,.026,.65),'wood_dark',.003)
 for x in (-.645,.645):box((x,0,.469),(.038,.55,.65),'wood_walnut',.005)
 for z in (.17,.785):box((0,0,z),(1.36,.60,.052),'wood_walnut',.01)
 box((0,0,.815),(1.41,.64,.045),'wood_honey',.009)
 for x in (-.325,.325):
  for j,z in enumerate((.29,.493,.696)):
   ajar=(x>0 and j==2);y=-.385 if ajar else -.291
   # Dark drawer cavity and actual box visible behind the slightly opened upper drawer.
   box((x,y+.215,z-.072),(.59,.40,.012),'wood_dark',.002)
   if ajar:
    for xx in (x-.279,x+.279):box((xx,y+.20,z),(.018,.40,.16),'wood_endgrain',.003)
   box((x,y,z),(.627,.046,.186),'paint_putty' if j==0 else 'wood_walnut',.008)
   box((x,y-.026,z),(.535,.012,.11),'wood_honey',.003)
   handle(x,y-.04,z,.16)
 for x in (-.645,0,.645):box((x,-.274,.48),(.026,.034,.59),'wood_dark',.002)
 for z in (.395,.60):box((0,-.281,z),(1.30,.029,.015),'wood_dark',.001)
 for i in range(7):
  x=-.58+i*.175;line([(x,-.273,.839),(x+.06,-.245,.839)],.0017,'wood_dark',4)
 patch((-.44,-.2,.839),(.08,.055),'wood_endgrain',seed=311)

def wardrobe():
 for x in (-.48,.48):
  for y in (-.21,.21):box((x,y,.076),(.11,.115,.15),'wood_walnut',.014)
 for x in (-.555,.555):box((x,0,1.025),(.044,.60,1.82),'wood_walnut',.007)
 box((0,.296,1.025),(1.08,.027,1.82),'wood_dark',.004)
 for z in (.16,1.90):box((0,0,z),(1.17,.65,.067),'wood_walnut',.009)
 box((0,0,1.951),(1.22,.69,.055),'wood_honey',.011)
 box((0,0,.204),(1.08,.55,.021),'wood_honey',.003)
 box((0,0,1.64),(1.08,.55,.025),'wood_honey',.003)
 rod((-.52,.055,1.51),(.52,.055,1.51),.013,'steel',10)
 # One door open by 23 degrees, authoring remains static; cavity is fully modelled.
 for x in (-.275,.275):
  start=len(V);xx=x
  box((xx,-.327,1.025),(.532,.047,1.65),'wood_walnut',.006)
  for z,h in ((.57,.62),(1.33,.70)):
   box((xx,-.356,z),(.411,.026,h),'wood_honey',.009)
   for xxx in (xx-.207,xx+.207):box((xxx,-.37,z),(.021,.013,h+.026),'wood_dark',.003)
   for zz in (z-h/2,z+h/2):box((xx,-.37,zz),(.433,.013,.021),'wood_dark',.003)
  hx=xx+(.184 if x<0 else -.184);handle(hx,-.373,.99,.047,'brass')
  for z in (.48,1.48):box((xx+(-.254 if x<0 else .254),-.356,z),(.029,.031,.071),'brass',.004)
  if x>0:
   hinge=Vector((.548,-.327,0));rot=Matrix.Rotation(math.radians(23),3,'Z')
   for i in range(start,len(V)):V[i]=tuple(rot@(Vector(V[i])-hinge)+hinge)
 # Two simple bent-wire hangers and a folded linen stack behind the opened panel.
 for x in (-.13,.13):
  line([(x,0,1.51),(x+.04,0,1.55),(x+.065,0,1.52),(x+.052,0,1.47),(x-.15,0,1.32),(x+.20,0,1.32),(x+.052,0,1.47)],.0035,'steel',5)
 box((.30,-.03,.25),(.32,.38,.071),'canvas',.013,segments=2)
 box((.28,-.015,.307),(.30,.36,.045),'wool_olive',.014,segments=2)

def shoe_rack():
 for x in (-.40,.40):
  for y in (-.16,.16):rod((x,y,0),(x,y,.455),.019,'metal_sage',10)
  line([(x,-.16,.455),(x,-.16,.51),(x,.16,.51),(x,.16,.455)],.016,'metal_sage',8)
 for z in (.12,.36):
  for y in (-.158,.158):rod((-.40,y,z),(.40,y,z),.012,'metal_sage',8)
  for i in range(7):box((0,-.15+i*.05,z+.014),(.82,.033,.028),'wood_honey',.004)
 # One worn pair of loafers, fused as dressing; separated loose parts in source mesh.
 for x in (-.14,.03):
  box((x,-.005,.402),(.123,.283,.025),'rubber',.02,segments=2)
  box((x,-.021,.439),(.115,.256,.071),'vinyl_tobacco',.029,segments=3)
  box((x,.065,.478),(.066,.070,.009),'rubber',.013,segments=2)
  line([(x-.043,-.091,.472),(x-.04,-.033,.477),(x+.04,-.033,.477),(x+.043,-.091,.472)],.0025,'wood_honey',5)


def hamper():
 # Real open tapered wicker hamper; liner sits below rim, opening is not painted.
 rx,ry=.245,.19
 for i in range(36):
  a=2*pi*i/36
  line([(.82*rx*cos(a),.82*ry*sin(a),.025),(rx*cos(a),ry*sin(a),.58)],.009,'cane',6)
 for z in [0.035+i*.041 for i in range(14)]:
  k=.82+.18*z/.58;ellipse_ring((0,0,z),rx*k,ry*k,.011,'cane',36,5)
 for z in (.025,.583,.603):ellipse_ring((0,0,z),rx*(.83 if z<.1 else 1),ry*(.83 if z<.1 else 1),.014,'wood_honey',36,6)
 # Inner canvas bag side wall and recessed base.
 vv=[]
 for z,scale in ((.06,.79),(.56,.96)):
  vv += [(rx*scale*cos(2*pi*i/36),ry*scale*sin(2*pi*i/36),z) for i in range(36)]
 ff=[tuple(reversed(range(36)))]+[(i,(i+1)%36,(i+1)%36+36,i+36) for i in range(36)]
 add(vv,ff,'canvas')
 for x in (-.252,.252):
  line([(x,0,.49),(x*1.07,0,.56),(x*1.09,0,.66),(x,0,.68),(x,0,.603)],.012,'wood_honey',8)
 # Loosely folded fabric slopes visibly into the open interior, not a solid plug.
 start=len(V);piped((-.02,-.04,.515),(.30,.27,.072),'wool_faded',bevel=.025)
 box((.105,-.16,.552),(.11,.075,.235),'wool_olive',.017,rot=Matrix.Rotation(math.radians(-17),3,'X'),segments=2)
 for i in range(4):line([(.06+i*.024,-.2,.466),(.06+i*.024,-.195,.622)],.0025,'seam',5)


def hospital_bedframe():
 w=.99;l=2.04
 for x in (-w/2,w/2):
  for y in (-l/2,l/2):
   top=1.07 if y>0 else .77
   rod((x,y,.072),(x,y,top),.026,'metal_cream',12)
   sphere((x,y,top),(.058,.058,.058),'metal_cream',12,5)
   # Static small casters ground the institutional silhouette.
   rod((x-.021,y,.052),(x+.021,y,.052),.048,'rubber',12)
   box((x,y,.098),(.045,.077,.026),'steel',.004)
  box((x,0,.365),(.043,l,.068),'iron',.005)
 for y in (-l/2,l/2):
  top=.735 if y<0 else 1.035
  rod((-w/2,y,top),(w/2,y,top),.024,'metal_cream',12)
  rod((-w/2,y,.40),(w/2,y,.40),.022,'metal_cream',10)
  for x in (-.33,-.165,0,.165,.33):rod((x,y,.40),(x,y,top),.010,'metal_cream',8)
  # Small overpainted welded joints and restrained exposed rust.
  for x in (-w/2,w/2):box((x,y-.017,.40),(.062,.025,.041),'rust',.004)
 for y in (-.82,-.55,-.28,0,.28,.55,.82):box((0,y,.368),(.94,.031,.026),'steel',.003)
 for x in [-.43+i*.086 for i in range(11)]:rod((x,-.96,.393),(x,.96,.393),.0038,'steel',5)
 for y in [-.93+i*.093 for i in range(21)]:rod((-.455,y,.394),(.455,y,.394),.0034,'steel',5)
 for x in (-.33,.33):box((x,0,.335),(.052,.083,.036),'steel',.004)


def shelving():
 w=1.16;d=.46
 # Angle-stock posts use true L sections, with repeated bolt head details.
 for x in (-w/2,w/2):
  for y in (-d/2,d/2):
   box((x,y,.91),(.044,.013,1.82),'metal_sage',.002)
   box((x+(.015 if x<0 else -.015),y+(.015 if y<0 else -.015),.91),(.013,.044,1.82),'metal_sage',.002)
   box((x,y,.012),(.075,.063,.024),'rubber',.004)
 for z in (.13,.53,.93,1.33,1.74):
  box((0,0,z),(1.16,.46,.022),'metal_sage',.003)
  for y in (-.226,.226):box((0,y,z-.023),(1.16,.018,.044),'metal_sage',.002)
  for x in (-.577,.577):
   for y in (-.239,.239):rod((x,y-.003,z-.02),(x,y-.012,z-.02),.008,'steel',8)
  patch((-.39,-.15,z+.012),(.068,.012),'rust',seed=int(z*1000))
 for a,b in (((-.56,.235,.18),(.56,.235,1.69)),((.56,.238,.18),(-.56,.238,1.69))):rod(a,b,.0065,'steel',8)
 # One deliberate mismatched replacement wooden shelf.
 box((0,0,.952),(1.105,.40,.024),'wood_honey',.003)
 for y in (-.145,.145):line([(-.50,y,.966),(.48,y+.016,.966)],.001,'wood_dark',4)

def credenza():
 for x in (-.62,.62):
  for y in (-.18,.18):rod((x*1.1,y*1.16,.015),(x,y,.25),.025,'wood_walnut',10,.041)
 box((0,0,.292),(1.49,.46,.083),'wood_walnut',.01)
 box((0,.224,.513),(1.46,.025,.45),'wood_dark',.004)
 for x in (-.723,.723):box((x,0,.513),(.04,.46,.45),'wood_walnut',.006)
 box((0,0,.760),(1.54,.50,.055),'wood_honey',.012)
 box((.25,0,.514),(.024,.42,.42),'wood_walnut',.004)
 box((.481,0,.506),(.437,.42,.022),'wood_honey',.003)
 # Tambour slats across left 2/3; open right shelving is geometric.
 for i in range(35):box((-.682+i*.026,-.241,.513),(.023,.026,.412),'wood_honey' if i%4 else 'wood_walnut',.004)
 box((.213,-.263,.513),(.028,.025,.38),'wood_walnut',.004)
 handle(.167,-.274,.522,.055,'brass')
 # Dark sliding tracks rather than hinged-door trim.
 for z in (.305,.726):box((0,-.225,z),(1.42,.047,.01),'wood_dark',.001)
 for i in range(4):line([(-.55+i*.14,-.10,.789),(-.51+i*.14,-.078,.789)],.0015,'wood_dark',4)

BUILDERS={
 'wingback_armchair_worn':(wingback,'Worn wingback armchair','High winged back, buttoned wool insert, turned feet and stitched seat tear.'),
 'recliner_vinyl_extended':(recliner,'Extended vinyl recliner','Segmented tobacco vinyl, static extended footrest, scissor linkages and release lever.'),
 'chair_metal_folding':(folding_chair,'Metal folding chair','Open crossed tubular frame, pivot bolts, formed backrest and thin padded seat.'),
 'chair_dining_spindle':(spindle_chair,'Spindle dining chair','Bow back, seven turned spindles, splayed legs and saddle-profile seat.'),
 'table_folding_card':(card_table,'Folding card table','Square fabric playing surface, corner caps and slim braced folding underframe.'),
 'dresser_low_six_drawer':(low_dresser,'Low six-drawer dresser','Mismatched painted lower fronts, recessed panels, brass handles and one ajar drawer.'),
 'wardrobe_panel_ajar':(wardrobe,'Tall panel wardrobe','Raised wood panels, one static 23-degree open door, interior shelf, rail and hangers.'),
 'shoe_rack_slatted':(shoe_rack,'Slatted shoe rack','Tubular sage frame, two wooden shelves and one pair of worn loafers.'),
 'laundry_hamper_woven':(hamper,'Woven laundry hamper','Open tapered basket, individual wicker ribs, canvas liner and draped cloth.'),
 'bedframe_single_institutional':(hospital_bedframe,'Single institutional bedframe','Bare cream steel frame with welded uprights, spring-wire deck and static casters.'),
 'shelving_steel_repaired':(shelving,'Repaired steel shelving','Open angle-stock shelving, bolt details, rear X braces and one replacement wood shelf.'),
 'credenza_tambour':(credenza,'Tambour-door credenza','Long low sideboard, vertical timber tambour slats, tapered legs and exposed right cubby.'),
}

def build_asset(name):
 global V,F,MI
 V=[];F=[];MI=[];BUILDERS[name][0]()
 ground_shift=-min(q[2] for q in V)
 V=[(q[0],q[1],q[2]+ground_shift) for q in V]
 mesh=bpy.data.meshes.new('F04_MESH_'+name);mesh.from_pydata(V,[],F);mesh.update()
 # Assign source materials before topology cleanup.
 keys=list(dict.fromkeys(MI))
 for k in keys:mesh.materials.append(MATS[k])
 for p,k in zip(mesh.polygons,MI):p.material_index=keys.index(k)
 bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.0000001);bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=.0000001)
 bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free();mesh.update()
 uv=mesh.uv_layers.new(name='UVMap')
 for p in mesh.polygons:
  n=p.normal;axis=max(range(3),key=lambda i:abs(n[i]));axes=(1,2) if axis==0 else ((0,2) if axis==1 else (0,1))
  for li in p.loop_indices:
   co=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=(co[axes[0]]*1.85,co[axes[1]]*1.85)
  p.use_smooth=False
 mesh.calc_loop_triangles()
 ob=bpy.data.objects.new('F04_'+name,mesh);bpy.context.scene.collection.objects.link(ob)
 mn=[min(v.co[i] for v in mesh.vertices) for i in range(3)];mx=[max(v.co[i] for v in mesh.vertices) for i in range(3)]
 ob['asset_id']=name;ob['license']='CC0-1.0';ob['units']='metres';ob['canonical_front']='-Y (Blender); +Z (glTF)'
 ob['collision_footprint_xy']=[round(mn[0],4),round(mn[1],4),round(mx[0],4),round(mx[1],4)]
 ob['collision_recommendation']='simple_box_optional; render_only_by_default';ob['runtime_binding']='not_implemented';ob['pivot_policy']='authored body-reference XY, lowest vertex Z=0; not full-AABB centred'
 ob['source_batch']='04_furniture_variants';ob['design_notes']=BUILDERS[name][2]
 ob.asset_mark();ob.asset_data.description=BUILDERS[name][2];ob.asset_data.author='Original CC0 furniture library'
 for tag in ('furniture','weathered','environment','CC0'):ob.asset_data.tags.new(tag)
 return ob,{'asset_id':name,'label':BUILDERS[name][1],'description':BUILDERS[name][2], 'file':'assets/'+name+'.glb','units':'metres','source_bounds_z_up':[mn,mx],'dimensions_m_xyz_blender':[round(mx[i]-mn[i],4) for i in range(3)],'triangles':len(mesh.loop_triangles),'vertices':len(mesh.vertices),'material_count':len(keys),'materials':['F04_'+k for k in keys],'surface_semantics':sorted(set(PALETTE[k][4] for k in keys)),'canonical_front':'Blender -Y, glTF +Z','pivot':'authored furniture-body XY reference, not full-AABB centred; lowest vertex at Z=0','authored_ground_translation_z_m':ground_shift,'collision_footprint_xy':[round(mn[0],4),round(mn[1],4),round(mx[0],4),round(mx[1],4)],'optional_coarse_aabb_y_up':{'min':[mn[0],mn[2],-mx[1]],'max':[mx[0],mx[2],-mn[1]],'warning':'Suggestion only. Includes decorative protrusions and static open components. Never auto-enable colliders.'},'collision_policy':'render_only_by_default','license':'CC0-1.0'}

def main():
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
 scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1;scene['license']='CC0-1.0';scene['batch']='04_furniture_variants';scene['front']='-Y';scene['runtime_integration']='asset-side only'
 make_materials();rows=[]
 for i,name in enumerate(BUILDERS):
  ob,row=build_asset(name);bpy.ops.object.select_all(action='DESELECT');ob.select_set(True);bpy.context.view_layer.objects.active=ob
  bpy.ops.export_scene.gltf(filepath=os.path.join(ROOT,row['file']),export_format='GLB',use_selection=True,export_yup=True,export_animations=False,export_extras=True,export_materials='EXPORT')
  row['file_bytes']=os.path.getsize(os.path.join(ROOT,row['file']));rows.append(row)
  ob.location=((i%4)*2.7,(i//4)*3.2,0)
  collection=bpy.data.collections.new('ASSET_'+name);scene.collection.children.link(collection)
  for c in list(ob.users_collection):c.objects.unlink(ob)
  collection.objects.link(ob)
  ob['catalog_location_only']=True
 manifest={'schema':'environment_furniture_batch_v1','version':'1.0.0','batch':'04_furniture_variants','title':'Secondhand rooms: furniture variants','license':'CC0-1.0','author':'Original procedural geometry and texture artwork','source_units':'metres','source_coordinate_system':'right-handed Z-up, -Y front','glTF_coordinate_system':'right-handed Y-up, +Z front; exporter maps (x,y,z) to (x,z,-y)','metadata_coordinate_warning':'collision_footprint_xy remains Blender-local XY inside GLB extras. Arbitrary extras are not transformed by the exporter.','pivot_policy':'Authored furniture-body XY reference, not full-AABB centred. X/Y asymmetry preserves open components and mechanisms. Each lowest vertex is normalized to Z=0.','double_sided_materials':True,'asset_count':len(rows),'total_triangles':sum(x['triangles'] for x in rows),'physics_material_suggestions':{'wood':2,'steel':4,'fabric':'No exact cloth ID; choose intentional gameplay mapping, do not infer carpet=9 automatically.','rubber':'No exact ID in inspected target.'},'engine_material_notice':'Semantic suggestions only. Preserve authoritative runtime material IDs. No collider, runtime adapter, animation, destruction, LOD or navigation binding is implemented.','texture_policy':'Original 256/512px PNG base-color textures; external rebuild copies, packed in blend and embedded in each GLB. Regular metallic/roughness values. No procedural shader dependency.','assets':rows}
 with open(os.path.join(ROOT,'manifest.json'),'w') as f:json.dump(manifest,f,indent=2)
 # Give the editable source a useful coloured overview when opened interactively.
 for screen in bpy.data.screens:
  for area in screen.areas:
   if area.type=='VIEW_3D':
    space=area.spaces.active;space.shading.type='SOLID';space.shading.color_type='MATERIAL';space.shading.show_cavity=True
    space.region_3d.view_location=(4.05,3.2,.55);space.region_3d.view_distance=14
    space.region_3d.view_rotation=Vector((.7,-1.3,.9)).to_track_quat('Z','Y')
 bpy.ops.object.select_all(action='DESELECT')
 # Preserve modest native file size and one editable mesh per asset, with loose parts.
 bpy.context.preferences.filepaths.save_version=0
 bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'furniture_variants.blend'),compress=True)
 print('FURNITURE_RESULT',json.dumps({'assets':len(rows),'triangles':manifest['total_triangles'],'native':'furniture_variants.blend'}))

if __name__=='__main__':main()
