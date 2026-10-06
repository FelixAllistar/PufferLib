"""BRIAR COURT, batch 15. Original CC0-1.0 art. Blender 4.3+.
Run: blender -b -t 2 --python-exit-code 1 --python build_motel.py
Portable: included geometry_core.py and byte-identical reused_assets/ are all inputs.
"""
import bpy,os,sys,math,json,hashlib,shutil
from math import pi
from mathutils import Vector,Matrix
ROOT=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,ROOT)
import geometry_core as g
box=g.box;rod=g.rod;cyl=g.cyl;patch=g.patch;group=g.group
PALETTE={
 'stucco':((.64,.59,.45),.96,0,'clay'),'repair':((.73,.70,.60),.94,0,'clay'),
 'concrete':((.40,.42,.40),.94,0,'clay'),'concrete_dark':((.23,.26,.26),.97,0,'clay'),
 'teal':((.10,.26,.25),.81,.12,'paint'),'faded_teal':((.21,.37,.34),.88,.06,'paint'),
 'cream':((.77,.75,.63),.82,0,'paint'),'ochre':((.64,.40,.14),.82,.1,'paint'),
 'steel':((.32,.36,.36),.62,.58,'metal'),'galvanized':((.50,.53,.50),.57,.65,'metal'),
 'iron':((.06,.08,.08),.76,.35,'metal'),'rust':((.29,.15,.08),.96,.1,'rust'),
 'rubber':((.03,.04,.041),.94,0,'plain'),'glass':((.095,.16,.17),.23,.1,'plain'),
 'wood':((.36,.23,.13),.83,0,'wood'),'paper':((.83,.80,.67),.95,0,'paper'),
 'cloth':((.48,.43,.30),.97,0,'cloth'),'red':((.47,.14,.08),.83,.1,'paint'),
 'lamp':((.89,.81,.57),.59,0,'plain'),'asphalt':((.13,.15,.15),.98,0,'clay'),
 'carpet':((.20,.27,.25),1,0,'cloth'),'tile':((.63,.67,.60),.9,0,'clay'),
}
# Original rectilinear 5x7 lettering. No font dependency or licensed decal input.
FONT={
'A':['01110','10001','10001','11111','10001','10001','10001'],
'B':['11110','10001','10001','11110','10001','10001','11110'],
'C':['01111','10000','10000','10000','10000','10000','01111'],
'D':['11110','10001','10001','10001','10001','10001','11110'],
'E':['11111','10000','10000','11110','10000','10000','11111'],
'F':['11111','10000','10000','11110','10000','10000','10000'],
'I':['111','010','010','010','010','010','111'],
'K':['10001','10010','10100','11000','10100','10010','10001'],
'L':['10000','10000','10000','10000','10000','10000','11111'],
'M':['10001','11011','10101','10101','10001','10001','10001'],
'N':['10001','11001','11001','10101','10011','10011','10001'],
'O':['01110','10001','10001','10001','10001','10001','01110'],
'R':['11110','10001','10001','11110','10100','10010','10001'],
'S':['01111','10000','10000','01110','00001','00001','11110'],
'T':['11111','00100','00100','00100','00100','00100','00100'],
'U':['10001','10001','10001','10001','10001','10001','01110'],
'V':['10001','10001','10001','10001','10001','01010','00100'],
'Y':['10001','10001','01010','00100','00100','00100','00100'],
'0':['01110','10001','10011','10101','11001','10001','01110'],
'1':['010','110','010','010','010','010','111'],
'2':['01110','10001','00001','00010','00100','01000','11111'],
'3':['11110','00001','00001','01110','00001','00001','11110'],
'4':['10010','10010','10010','11111','00010','00010','00010'],
' ':['000']*7}
def label(txt,c,h,mat='cream'):
 unit=h/7;w=sum(len(FONT[ch][0])+1 for ch in txt)*unit-unit;x=c[0]-w/2
 for ch in txt:
  rows=FONT[ch]
  for j,row in enumerate(rows):
   # merge contiguous pixels into a single face-thin bar
   start=None
   for i,val in enumerate(row+'0'):
    if val=='1' and start is None:start=i
    if val=='0' and start is not None:
     box((x+(start+i)/2*unit,c[1],c[2]+(3-j)*unit),((i-start)*unit,.009,unit*.96),mat,0);start=None
  x+=(len(rows[0])+1)*unit

def wall_piece(x0,x1,z0,z1,mat='stucco',depth=.18):
 if x1>x0 and z1>z0:box(((x0+x1)/2,0,(z0+z1)/2),(x1-x0,depth,z1-z0),mat,.003)
def facade():
 group('stucco_masonry')
 for a,b in [(-2,-1.78),(-.62,-.20),(1.8,2)]:wall_piece(a,b,0,2.8)
 wall_piece(-1.78,-.62,2.24,2.8);wall_piece(-.20,1.8,0,.88);wall_piece(-.20,1.8,2.18,2.8)
 group('base_plinth');wall_piece(-.62,2,0,.12,'concrete_dark',.20);wall_piece(-2,-1.78,0,.12,'concrete_dark',.20)
 group('door_jambs')
 for x in [-1.77,-.63]:box((x,-.01,1.115),(.025,.22,2.23),'cream',.002)
 box((-1.2,-.01,2.235),(1.16,.22,.025),'cream',.001)
 group('drip_cap');box((.8,-.10,2.22),(2.08,.08,.035),'cream',.002)
 group('localized_plinth_repair');box((1.51,-.105,.21),(.37,.006,.14),'repair',0)
 patch((-.42,-.095,1.05),(.10,.20),'concrete','Y',41)

def solid_wall(w=4):
 group('masonry');box((0,0,1.4),(w,.18,2.8),'stucco',.003)
 group('skirt');box((0,-.013,.075),(w,.20,.15),'concrete_dark',.002)
 group('repair');patch((w*.27,-.095,.29),(.18,.11),'repair','Y',42)
def end_wall():solid_wall(6)
def corner():
 group('corner_cap');box((0,0,1.4),(.24,.24,2.8),'cream',.003)
 for z in [.08,2.74]:box((0,0,z),(.27,.27,.08),'concrete',.003)
def floor():
 group('concrete_slab');box((0,0,-.05),(4,6,.10),'concrete',.001)
 group('bedroom_carpet');box((0,-1,.002),(3.82,3.8,.004),'carpet',0)
 group('bath_tile');box((0,2.04,.003),(3.82,1.74,.005),'tile',0)
 for x in [-1.5,-1,-.5,0,.5,1,1.5]:box((x,2.04,.006),(.005,1.73,.001),'concrete',0)
 for y in [1.3,1.8,2.3,2.8]:box((0,y,.006),(3.81,.005,.001),'concrete',0)
def roof():
 group('roof_deck');box((0,0,.075),(4,6.24,.15),'cream',.004)
 group('flat_membrane');box((0,0,.157),(4,6.24,.014),'concrete_dark',0)
 group('edge_fascias')
 for y in [-3.12,3.12]:
  box((0,y,.10),(4,.055,.24),'teal',.003);box((0,y-.028,.145),(4,.009,.065),'ochre',0)
def walkway():
 group('gallery_slab');box((0,-.9,-.06),(4,1.8,.12),'concrete',.003)
 group('edge');box((0,-1.8,-.085),(4,.06,.17),'cream',.002)
 for x in [-1.994,1.994]:box((x,-.9,.001),(.009,1.78,.002),'concrete_dark',0)
def canopy():
 group('sheet');box((0,-.95,.08),(4,2,.16),'cream',.003)
 group('front_fascia');box((0,-1.96,.045),(4,.07,.29),'teal',.003)
 box((0,-2,.115),(4,.015,.062),'ochre',0)
 group('seams')
 for x in [-1.98,-.66,.66,1.98]:box((x,-.95,.169),(.02,1.98,.016),'steel',.001)
def post():
 group('painted_tube');box((0,0,1.215),(.085,.085,2.43),'teal',.005)
 for z in [.015,2.425]:box((0,0,z),(.16,.16,.03),'steel',.002)
 for x in [-.052,.052]:cyl((x,0,.035),.014,.012,'galvanized',8)
def door():
 group('leaf');box((.54,0,1.095),(1.08,.045,2.19),'teal',.004)
 group('raised_panels')
 for z,h in [(.57,.66),(1.5,.75)]:box((.54,-.026,z),(.79,.015,h),'faded_teal',.005)
 group('latch');box((.96,-.041,1.04),(.07,.022,.16),'galvanized',.006);rod((.96,-.049,1.06),(.96,-.10,1.06),.024,'steel',12);rod((.96,-.10,1.06),(.86,-.10,1.06),.015,'steel',12)
 group('kickplate');box((.54,-.028,.12),(.99,.013,.16),'steel',.001)
 group('hinges')
 for z in [.23,1.1,1.92]:cyl((.018,0,z),.015,.10,'galvanized',10)
 group('peephole');rod((.54,-.026,1.6),(.54,-.04,1.6),.014,'iron',12)
 group('handle_wear');patch((.90,-.036,.92),(.09,.14),'cream','Y',57)
def window():
 group('frame')
 for x in [-.98,.98]:box((x,0,.65),(.04,.12,1.30),'cream',.003)
 for z in [.025,1.275]:box((0,0,z),(1.99,.12,.05),'cream',.003)
 box((0,-.02,.65),(.028,.09,1.25),'steel',.002)
 group('opaque_dusty_panes')
 for x in [-.49,.49]:box((x,.015,.65),(.94,.018,1.20),'glass',.001)
 group('curtain_folds')
 for side in [-1,1]:
  for i in range(5):box((side*(.57+i*.07),-.004,.65),(.071,.032+(i%2)*.016,1.18),'cloth',.007)
 group('sill');box((0,-.06,.008),(2.07,.25,.033),'cream',.003)
def railing():
 group('rails')
 for x in [-.95,.95]:box((x,0,.50),(.045,.045,1),'teal',.003)
 for z in [.15,1]:box((0,0,z),(2,.045,.045),'teal',.004)
 for x in [-.7,-.35,0,.35,.7]:box((x,0,.56),(.024,.024,.79),'steel',.002)
 group('feet')
 for x in [-.95,.95]:box((x,0,.012),(.16,.12,.024),'galvanized',.002)
def lobby_facade():
 group('frame_and_header')
 box((0,0,2.60),(4,.18,.40),'teal',.004)
 for x in [-1.97,-.64,.65,1.97]:box((x,0,1.2),(.06,.12,2.4),'cream',.003)
 group('glazed_panels')
 for x in [0,1.31]:box((x,.02,1.22),(1.23,.025,2.29),'glass',.002)
 for z in [.06,.75,2.36]:box((.65,-.007,z),(2.6,.06,.04),'cream',.002)
 group('lettering');label('OFFICE',(0,-.096,2.61),.22)
 # left opening stays empty 1.27 m; a distinct separate door leaf is installed open.
def counter():
 group('cabinet');box((0,0,.51),(2.4,.65,1.02),'wood',.008)
 group('vertical_battens')
 for x in [-1.16+i*.145 for i in range(17)]:box((x,-.333,.5),(.035,.025,.90),'teal',.002)
 group('worktop');box((0,0,1.05),(2.5,.76,.07),'cream',.01)
 group('service_ledgetop');box((-.84,.38,1.07),(.80,.28,.04),'wood',.004)
 group('edge_wear');box((.46,-.39,1.07),(.28,.008,.014),'paper',0)
def sign():
 group('mast')
 for x in [-.68,.68]:box((x,0,1.92),(.105,.12,3.84),'steel',.004)
 group('foundation');box((0,0,.10),(1.85,.70,.20),'concrete',.01)
 group('face_casing');box((0,0,3.29),(3.15,.23,1.63),'teal',.03)
 for z in [2.50,4.08]:box((0,-.13,z),(3.2,.025,.065),'ochre',.004)
 group('name_letters');label('BRIAR',(0,-.13,3.74),.31);label('COURT',(0,-.13,3.30),.31);label('MOTEL',(0,-.13,2.82),.25,'paper')
 group('lower_reader');box((0,-.02,2.10),(2.30,.13,.40),'cream',.01);label('VACANCY',(0,-.092,2.10),.16,'red')
 group('edge_wear');patch((1.36,-.13,3.89),(.18,.08),'cream','Y',33)
def ice():
 group('enamel_shell');box((0,0,.79),(.92,.76,1.58),'cream',.015)
 group('hopper');box((0,-.386,.94),(.67,.024,.48),'steel',.005)
 box((0,-.413,.76),(.71,.06,.10),'iron',.003)
 group('vent_louvres')
 for i in range(9):box((0,-.39,.18+i*.034),(.70,.018,.017),'iron',.001)
 group('label');box((0,-.39,1.40),(.7,.025,.23),'teal',.003);label('ICE',(0,-.409,1.40),.14)
 group('lid');box((0,0,1.60),(.95,.80,.04),'steel',.006)
 group('staining');patch((.36,-.389,.48),(.06,.17),'rust','Y',6)
def vending():
 group('steel_cabinet');box((0,0,.92),(.94,.80,1.84),'teal',.016)
 group('display');box((-.115,-.41,1.14),(.57,.025,1.15),'glass',.005)
 group('product_bars')
 for z in [.78,1.03,1.28,1.53]:
  for x in [-.30,-.11,.08]:box((x,-.43,z),(.14,.06,.14),'ochre' if z<1.1 else 'cream',.004)
  box((-.11,-.463,z-.09),(.56,.025,.026),'steel',.001)
 group('buttons')
 for z in [1.45,1.3,1.15,1.0]:rod((.34,-.42,z),(.34,-.44,z),.022,'cream',10)
 box((.33,-.43,1.65),(.10,.02,.026),'iron',0)
 group('pickup_slot');box((0,-.41,.29),(.65,.024,.23),'iron',.003);box((0,-.44,.175),(.69,.07,.025),'steel',.002)
 group('kickwear');patch((.34,-.415,.10),(.12,.065),'steel','Y',72)
def plaque(text='101'):
 group('plate');box((0,-.014,0),(.32,.028,.17),'teal',.007)
 group('numerals');label(text,(0,-.033,0),.09)
 for x in [-.137,.137]:rod((x,-.03,0),(x,-.038,0),.009,'galvanized',8)
def ac():
 group('cabinet');box((0,0,.26),(.92,.34,.52),'cream',.006)
 group('lower_grille')
 for x in [-.40+i*.047 for i in range(18)]:box((x,-.176,.19),(.015,.019,.24),'iron',.001)
 group('upper_louvres')
 for z in [.35,.395,.44]:box((0,-.184,z),(.82,.045,.015),'steel',.001)
 group('wall_flange');box((0,.18,.26),(1.01,.025,.60),'steel',.003)
 group('drip_stain');patch((.37,-.181,.25),(.05,.16),'rust','Y',35)
def stop():
 group('concrete_body')
 g.add([(-.9,-.12,0),(.9,-.12,0),(.9,.12,0),(-.9,.12,0),(-.83,-.095,.13),(.83,-.095,.13),(.83,.095,.13),(-.83,.095,.13)],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'concrete')
 for x in [-.62,.62]:cyl((x,0,.134),.025,.018,'iron',8)
 group('faded_reflectors')
 for x in [-.67,.67]:box((x,-.119,.064),(.19,.009,.045),'ochre',.002)
def ramp():
 group('shallow_walkway_transition')
 g.add([(-.7,-1.2,-.08),(.7,-1.2,-.08),(.7,0,-.08),(-.7,0,-.08),(-.7,-1.2,-.079),(.7,-1.2,-.079),(.7,0,0),(-.7,0,0)],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'concrete')
 for i in range(8):
  y=-1.1+i*.13;z=-.079+(y+1.2)/1.2*.079
  box((0,y,z+.001),(1.30,.018,.002),'concrete_dark',0)
def service_doorway():
 group('walls');wall_piece(-2,-.63,0,2.8);wall_piece(.63,2,0,2.8);wall_piece(-.63,.63,2.24,2.8)
 group('jamb')
 for x in [-.615,.615]:box((x,0,1.115),(.03,.23,2.23),'cream',.002)
 box((0,0,2.235),(1.26,.23,.03),'cream',.002)
def bath_partition():
 group('painted_partition');wall_piece(-2,-1.78,0,2.8,'cream',.12);wall_piece(-.62,2,0,2.8,'cream',.12);wall_piece(-1.78,-.62,2.24,2.8,'cream',.12)
 group('tile_wainscot');box((.68,.067,.53),(2.64,.012,1.06),'tile',.002)
 for x in [-1.77,-.63]:box((x,0,1.115),(.025,.17,2.23),'wood',.002)
def keyrack():
 group('backboard');box((0,0,.46),(1.08,.10,.92),'wood',.006)
 group('cubby_dividers')
 for x in [-.53,-.27,0,.27,.53]:box((x,-.12,.46),(.02,.20,.92),'wood',.001)
 for z in [.02,.24,.46,.68,.90]:box((0,-.12,z),(1.08,.20,.02),'wood',.001)
 group('key_tags')
 for i in range(4):
  x=-.40+i*.27;box((x,-.185,.54),(.062,.012,.09),'ochre',.003)
  rod((x,-.15,.60),(x,-.19,.60),.01,'steel',8)
BUILDERS=[
 ('room_facade_4m','Room facade / door and window openings',facade),('solid_wall_4m','Four metre rear wall',solid_wall),('end_wall_6m','Six metre room side wall',end_wall),('corner_pier','Cream corner cap',corner),('room_floor_4x6m','Carpet and tile room floor',floor),('flat_roof_4x6m','Flat roof with banded fascia',roof),('gallery_walkway_4m','Exterior walkway slab',walkway),('gallery_canopy_4m','Corrugated gallery canopy',canopy),('canopy_post','Thin gallery support post',post),('room_door_leaf','Panel door / left hinge origin',door),('room_window_insert','Divided window with curtain folds',window),('gallery_railing_2m','Simple gallery railing',railing),('lobby_glazed_facade','Reception glazing and doorway',lobby_facade),('reception_counter','Batten-front reception counter',counter),('roadside_sign','BRIAR COURT roadside sign',sign),('ice_machine','Weathered ice dispenser shell',ice),('vending_machine','Snack vending cabinet shell',vending),('room_number_plaque','Room 101 plaque master',plaque),('through_wall_ac','Sleeved through-wall air conditioner',ac),('parking_wheel_stop','Chipped concrete wheel stop',stop),('gallery_access_ramp','Shallow walkway ramp',ramp),('service_doorway_4m','Centred service doorway wall',service_doorway),('bathroom_partition_4m','Off-centre bathroom partition',bath_partition),('reception_key_cubbies','Sixteen-slot reception key rack',keyrack)]

def anchors(name):
 c=[]
 def add(n,p,v):c.append(g.connection(n,p,v))
 if name in ['room_facade_4m','solid_wall_4m','lobby_glazed_facade','service_doorway_4m','bathroom_partition_4m']:
  add('left',[-2,0,0],[-1,0,0]);add('right',[2,0,0],[1,0,0]);add('roof',[0,0,2.8],[0,0,1])
 if name=='end_wall_6m':add('left',[-3,0,0],[-1,0,0]);add('right',[3,0,0],[1,0,0])
 if name=='room_facade_4m':add('door_hinge',[-1.74,0,0],[0,-1,0]);add('window',[.8,-.018,.88],[0,-1,0])
 if name=='room_door_leaf':add('hinge',[0,0,0],[0,-1,0]);add('latch',[1.08,0,1.04],[1,0,0])
 if name=='room_window_insert':add('opening_origin',[0,0,0],[0,1,0])
 if name=='room_floor_4x6m':
  for n,p,v in [('left',[-2,0,0],[-1,0,0]),('right',[2,0,0],[1,0,0]),('front',[0,-3,0],[0,-1,0]),('back',[0,3,0],[0,1,0])]:add(n,p,v)
 if name in ['gallery_walkway_4m','gallery_canopy_4m']:add('left',[-2,0,0],[-1,0,0]);add('right',[2,0,0],[1,0,0])
 if name=='canopy_post':add('foot',[0,0,0],[0,0,-1]);add('head',[0,0,2.44],[0,0,1])
 if name=='gallery_access_ramp':add('walkway',[0,0,0],[0,1,0]);add('court',[0,-1.2,-.079],[0,-1,0])
 if not c:add('placement_origin',[0,0,0],[0,0,1])
 return c

def reset():g.V=[];g.F=[];g.FM=[];g.TAG=[];g.CURRENT='body'
def build_one(n,title,fn,col):
 reset();fn();ob=g.mesh_from_buffer(n,col);ob['asset_id']='mc15_'+n;ob['title']=title;ob['license']='CC0-1.0';ob['units']='metres';ob['render_only']=True;ob['collider_enabled']=False;ob['colliders_supplied']=False;ob['rigid_body_enabled']=False;ob['source_front']='-Y';ob['connections']=anchors(n);ob['pivot']='Authored modular/attachment datum; bounds may extend below Z=0. Door uses left hinge at Z=0.';ob.asset_mark();ob.asset_data.description=title+'; original CC0 motel art';ob.data.calc_loop_triangles();return ob

def export(file,obs):
 sc=bpy.context.scene
 for key in list(sc.keys()):del sc[key]
 sc['batch']='15_motor_court_motel';sc['license']='CC0-1.0';sc['render_only']=True;sc['colliders_supplied']=False;sc['source_basis']='metres; Z-up, front -Y'
 bpy.ops.object.select_all(action='DESELECT')
 for ob in obs:ob.hide_set(False);ob.hide_viewport=False;ob.select_set(True)
 bpy.context.view_layer.objects.active=obs[0]
 bpy.ops.export_scene.gltf(filepath=os.path.join(ROOT,file),export_format='GLB',use_selection=True,export_yup=True,export_extras=True,export_animations=False,export_cameras=False,export_lights=False)
REUSE={
 'bedframe_single_institutional':('../04_furniture_variants/assets/bedframe_single_institutional.glb','bedframe_single_institutional'),
 'mattress_dirty':('../../house_kit/modules/props/mattress_dirty.glb','mattress_dirty'),
 'desk':('../../house_kit/modules/props/desk.glb','desk'),
 'chair_metal_folding':('../04_furniture_variants/assets/chair_metal_folding.glb','chair_metal_folding'),
 'tv_crt':('../../house_kit/modules/props/tv_crt.glb','tv_crt'),
 'basin_pedestal':('../../house_kit/modules/props/basin_pedestal.glb','basin_pedestal'),
 'toilet_old':('../../house_kit/modules/props/toilet_old.glb','toilet_old'),
 'bathtub_old':('../../house_kit/modules/props/bathtub_old.glb','bathtub_old'),
 'frosted_dome_light':('../05_interior_fixtures/assets/frosted_dome_light.glb','frosted_dome_light'),
 'ribbed_glass_wall_sconce':('../05_interior_fixtures/assets/ribbed_glass_wall_sconce.glb','ribbed_glass_wall_sconce'),
 'front_load_washer':('../11_laundry_basement/assets/front_load_washer.glb','lb11_front_load_washer'),
 'vented_tumble_dryer':('../11_laundry_basement/assets/vented_tumble_dryer.glb','lb11_vented_tumble_dryer'),
 'folded_bedsheet_stack':('../11_laundry_basement/assets/folded_bedsheet_stack.glb','lb11_folded_bedsheet_stack'),
 'rotary_telephone':('../02_household_clutter/modules/rotary_telephone.glb','rotary_telephone'),
 'twin_bell_alarm_clock':('../02_household_clutter/modules/twin_bell_alarm_clock.glb','twin_bell_alarm_clock'),
 'chipped_coffee_mug':('../02_household_clutter/modules/chipped_coffee_mug.glb','chipped_coffee_mug')}
def reuse(col):
 out={};recs=[]
 for name,(src,aid) in REUSE.items():
  dest=os.path.join(ROOT,'reused_assets',name+'.glb')
  if not os.path.exists(dest):shutil.copy2(os.path.normpath(os.path.join(ROOT,src)),dest)
  before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=dest);obs=[o for o in set(bpy.data.objects)-before if o.type=='MESH'];assert len(obs)==1,(name,len(obs));ob=obs[0]
  for c in list(ob.users_collection):c.objects.unlink(ob)
  col.objects.link(ob);ob.name='REUSED_'+name;ob.location=(0,0,0);ob['asset_id']=aid;ob['license']='CC0-1.0';ob['units']='metres';ob['reused_existing_design']=True;ob['render_only']=True;ob['collider_enabled']=False;ob['colliders_supplied']=False;out[name]=ob
  recs.append({'id':aid,'name':name,'source_file_original':src,'file':'reused_assets/'+name+'.glb','sha256':hashlib.sha256(open(dest,'rb').read()).hexdigest(),'counted_as_new_design':False,'license':'CC0-1.0','bounds_source_m':g.bounds(ob)})
 return out,recs
PLACEMENTS=[]
def assemble(by,re,root):
 struct=g.collection('02a_STRUCTURE',root);roofs=g.collection('02b_ROOFS_toggle_cutaway',root);props=g.collection('02c_FIXTURES_FURNISHINGS',root);site=g.collection('02d_SITE_ART',root)
 def put(n,p,rot=0,c=None,r=False,scale=None):
  src=(re if r else by)[n];ob=src.copy();ob.data=src.data;(c or props).objects.link(ob);ob.location=p;ob.rotation_mode='XYZ';ob.rotation_euler=(0,0,rot)
  if scale:ob.scale=scale
  ob.name=('REUSE__' if r else 'INSTANCE__')+n+'__%03d'%len(PLACEMENTS);ob['assembly_instance']=True;ob['render_only']=True;ob['collider_enabled']=False;ob['colliders_supplied']=False
  PLACEMENTS.append({'object':ob.name,'asset_id':REUSE[n][1] if r else 'mc15_'+n,'position_source_m':list(p),'yaw_source_deg':round(math.degrees(rot),6),'scale':list(ob.scale),'reused_existing_design':r,'collection':ob.users_collection[0].name});return ob
 # Five bays: lobby, then four guest rooms. Court lies to the south (-Y).
 for cx in [-10,-6,-2,2,6]:
  put('frosted_dome_light',(cx,2.25,2.8),r=True,c=roofs);put('room_floor_4x6m',(cx,3,0),c=struct);put('flat_roof_4x6m',(cx,3,2.8),c=roofs)
  put('gallery_walkway_4m',(cx,0,0),c=struct);put('gallery_canopy_4m',(cx,0,2.44),c=roofs)
  put('room_facade_4m' if cx!=-10 else 'lobby_glazed_facade',(cx,0,0),c=struct)
  put('solid_wall_4m' if cx!=-10 else 'service_doorway_4m',(cx,6,0),c=struct)
  if cx!=-10:
   put('frosted_dome_light',(cx,5.05,2.8),r=True,c=roofs)
   put('room_door_leaf',(cx-1.74,0,0),math.radians(100))
   put('room_window_insert',(cx+.8,-.018,.88))
   put('through_wall_ac',(cx+.8,-.16,.20))
   num=101+[-6,-2,2,6].index(cx);number=put('room_number_plaque',(cx-.40,-.11,1.92));reset();plaque(str(num));tmp=g.mesh_from_buffer('number_variant_'+str(num),props);number.data=tmp.data;bpy.data.objects.remove(tmp,do_unlink=True);number['number_variant']=num;PLACEMENTS[-1]['number_variant']=num
   put('bathroom_partition_4m',(cx,4,0),c=struct)
   put('ribbed_glass_wall_sconce',(cx-.41,-.12,2.34),r=True)
   put('bedframe_single_institutional',(cx+.83,2.52,.008),r=True)
   # Actual mattress shape is reused and fitted to institutional bed support width.
   put('mattress_dirty',(cx+.83,2.52,.389),r=True,scale=(.68,.98,1))
   desk=put('desk',(cx-1.49,2.45,.008),pi/2,r=True)
   put('chair_metal_folding',(cx-.91,2.45,.008),-pi/2,r=True)
   bb=g.bounds(re['desk']);top=bb[1][2]
   put('tv_crt',(cx-1.51,2.68,top+.012),pi/2,r=True,scale=(.70,.70,.70))
   put('twin_bell_alarm_clock',(cx-1.42,1.94,top+.012),r=True)
   put('folded_bedsheet_stack',(cx+.83,2.50,.658),r=True)
   put('basin_pedestal',(cx-.95,5.57,.008),r=True)
   put('toilet_old',(cx+.12,5.52,.008),r=True)
   put('bathtub_old',(cx+1.18,5.00,.008),pi/2,r=True,scale=(.90,.90,.90))
 # Shared walls at room boundaries have no duplicate coincident partitions.
 for x in [-12,-8,-4,0,4,8]:put('end_wall_6m',(x,3,0),pi/2,c=struct)
 for x in [-12,8]:
  for y in [0,6]:put('corner_pier',(x,y,0),c=struct)
 for x in [-12,-8,-4,0,4,8]:put('canopy_post',(x,-1.77,0),c=struct)
 # Lobby opens to a connected 4 x 4 metre linen/laundry room behind it.
 put('room_door_leaf',(-11.9,0,0),pi/2)
 put('reception_counter',(-9.4,3.63,0));put('frosted_dome_light',(-10,4.9,2.8),r=True,c=roofs)
 put('reception_key_cubbies',(-11.23,5.895,1.25))
 put('rotary_telephone',(-10,3.5,1.093),r=True)
 put('chipped_coffee_mug',(-8.78,3.5,1.093),r=True)
 put('chair_metal_folding',(-9.2,4.67,.008),r=True)
 # Service floor/roof use two 4m bays scaled only in length; transforms declared.
 put('room_floor_4x6m',(-10,8,0),c=struct,scale=(1,2/3,1));put('flat_roof_4x6m',(-10,8,2.8),c=roofs,scale=(1,2/3,1))
 put('solid_wall_4m',(-10,10,0),c=struct)
 put('solid_wall_4m',(-12,8,0),pi/2,c=struct);put('service_doorway_4m',(-8,8,0),pi/2,c=struct)
 put('frosted_dome_light',(-10,8,2.8),r=True,c=roofs);put('front_load_washer',(-11.35,9.41,0),r=True);put('vented_tumble_dryer',(-10.55,9.41,0),r=True)
 put('desk',(-11.48,7.65,0),pi/2,r=True);put('folded_bedsheet_stack',(-11.4,7.8,.77),r=True)
 # Guest conveniences at west lobby gallery; readable unobstructed 1.1m inner aisle.
 put('ice_machine',(-13.0,2,0),-pi/2);put('vending_machine',(-13.0,3.25,0),-pi/2)
 put('gallery_railing_2m',(-13.6,4.5,0))
 put('roadside_sign',(9.7,-8.0,-.08),-.13)
 for x in [-10,-6,-2,2,6]:put('parking_wheel_stop',(x,-4.3,-.08))
 put('gallery_access_ramp',(-10.9,-1.83,0))
 # Original non-library site surfaces; no hidden floor plan copied from references.
 g.stage_box('SITE_asphalt_court',(-1,-4,-.16),(31,27,.16),'asphalt',site)
 g.stage_box('SITE_utility_pad',(-13,2.5,-.065),(2.0,4.4,.13),'concrete',site)
 for x in [-12,-8,-4,0,4,8]:g.stage_box('SITE_parking_stripe',(x,-6.50,-.074),(.075,4.2,.012),'cream',site)
 for i in range(7):g.stage_box('SITE_roof_drain',(8.14,5.8,.20+i*.36),(.10,.09,.36),'steel',props)
 # A newly repainted corner room: material override on instance data, not a new design.
 ob=next(o for o in struct.objects if o.name.startswith('INSTANCE__room_facade') and o.location.x==6);ob.data=ob.data.copy()
 for i,m in enumerate(ob.data.materials):
  if m==g.MATS['stucco']:ob.data.materials[i]=g.MATS['repair']
 next(r for r in PLACEMENTS if r['object']==ob.name)['material_overrides']={'MC15_stucco':'MC15_repair'}
 return struct,roofs,props,site

def setup():
 sc=bpy.context.scene;sc.unit_settings.system='METRIC';sc.unit_settings.scale_length=1;sc.render.engine='CYCLES';sc.cycles.device='CPU';sc.cycles.samples=28;sc.cycles.use_denoising=False;sc.render.threads_mode='FIXED';sc.render.threads=2;sc.render.image_settings.file_format='PNG';sc.render.resolution_percentage=100;sc.view_settings.view_transform='AgX';sc.view_settings.look='AgX - Medium High Contrast';sc.view_settings.exposure=.7
 sc.world=bpy.data.worlds.new('Quiet overcast');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.44,.49,.52,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.65
 sc['batch']='15_motor_court_motel';sc['license']='CC0-1.0';sc['render_only']=True;sc['colliders_supplied']=False;sc['source_basis']='metres; Z-up, front -Y'

def main():
 bpy.ops.wm.read_factory_settings(use_empty=True);g.PALETTE=PALETTE;g.materials();setup()
 src=g.collection('01_NEW_ASSET_LIBRARY');old=g.collection('01b_REUSED_LIBRARY');assembly=g.collection('02_MOTEL_EXAMPLE');pres=g.collection('03_CAMERAS_LIGHTS');records=[];by={}
 for n,title,fn in BUILDERS:
  ob=build_one(n,title,fn,src);by[n]=ob;bpy.context.view_layer.update();bb=g.bounds(ob);dims=[round(bb[1][i]-bb[0][i],6) for i in range(3)];path='assets/'+n+'.glb';export(path,[ob]);records.append({'id':ob['asset_id'],'name':n,'title':title,'file':path,'bounds_m_source_xyz':bb,'dimensions_m_source_xyz':dims,'dimensions_m_gltf_xyz':[dims[0],dims[2],dims[1]],'triangles':len(ob.data.loop_triangles),'vertices_source':len(ob.data.vertices),'material_count':len(ob.data.materials),'editable_vertex_groups':[v.name for v in ob.vertex_groups],'pivot':ob['pivot'],'connections':anchors(n),'sha256':hashlib.sha256(open(os.path.join(ROOT,path),'rb').read()).hexdigest(),'bytes':os.path.getsize(os.path.join(ROOT,path)),'render_only':True,'collider_enabled':False,'colliders_supplied':False})
 re,recs=reuse(old);st,roof,props,site=assemble(by,re,assembly);bpy.context.view_layer.update();export('motel_example.glb',list(assembly.all_objects));export('motel_example_cutaway.glb',[o for o in assembly.all_objects if o.name not in roof.objects])
 manifest={'schema':'motor_court_motel_batch15_v1','batch':'15_motor_court_motel','title':'Briar Court: a modest motor-court motel','asset_count':len(records),'total_triangles':sum(r['triangles'] for r in records),'license':'CC0-1.0','original_artwork':True,'seed':151004,'units':'metres','source_coordinates':'Right-handed Z-up, front -Y','gltf_coordinates':'Right-handed Y-up, front +Z. Source (x,y,z) maps to (x,z,-y).','physics':'Render-only. No colliders, rigid bodies, navmesh or game interaction supplied. Reused original GLBs remain byte-identical; override any legacy collider suggestions at import.','snap_grid_m':4,'materials':'Original packed/embedded 192px base-color textures, opaque metallic/roughness PBR. Window inserts and vending display use opaque dark dusty glass.','assets':records,'reused_existing_assets':recs,'example':{'file':'motel_example.glb','cutaway':'motel_example_cutaway.glb','guest_rooms':4,'room_footprint_m':[4,6],'lobby_footprint_m':[4,6],'service_room_footprint_m':[4,4],'building_main_footprint_m':[20,6],'gallery_depth_m':1.8,'ceiling_height_m':2.8,'main_floor_z_m':0,'courtyard_z_m':-.08,'instance_count':len(PLACEMENTS),'room_door_clear_opening_m':[1.11,2.2225]},'notes':['Original fictional location, sign artwork, layout and geometry. No commercial game maps or real incident layout reproduced.','Room number plaque master reads 101. Alternate example numbers are generated from the same design; not separate counted assets.','Open doors are separate left-hinge meshes. They are static placements, not runtime animations.','All room furnishings are reused by source ID and hashes, not counted as new designs.','Single-storey gallery needs no exterior stairs. Shallow courtyard transition is provided.']}
 json.dump(manifest,open(os.path.join(ROOT,'manifest.json'),'w'),indent=2);json.dump({'schema':'motor_court_assembly_v1','source_basis':'metres, Z-up','instances':PLACEMENTS},open(os.path.join(ROOT,'assembly_manifest.json'),'w'),indent=2)
 json.dump({'license':'CC0-1.0','external_images_used':False,'seed':151004,'textures':[{'file':'textures/'+k+'.png','size_px':[192,192],'role':'sRGB baseColor','source':'Original deterministic generated texture; no photographic source'} for k in PALETTE]},open(os.path.join(ROOT,'textures','provenance.json'),'w'),indent=2)
 src.hide_render=True;old.hide_render=True;src.hide_viewport=True;old.hide_viewport=True
 g.camera(pres,'Motel overview',(24,-31,25),(-2,1,.5),34)
 for im in bpy.data.images:
  if im.source=='FILE' and not im.packed_file:im.pack()
 bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'motor_court_motel.blend'))
 print('MOTEL_BUILT',len(records),manifest['total_triangles'],len(PLACEMENTS))
if __name__=='__main__':main()
