"""Morrow Block, batch16. Original CC0 modular geometry and original fictional signs.
Run blender -b -t 2 --python-exit-code 1 --python build_storefronts.py
Portable inputs: geometry_core.py, lettering.py, inputs/, reused_assets/.
"""
import bpy, os, sys, math, json, hashlib, struct
from math import pi
from mathutils import Vector, Matrix
ROOT=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,ROOT)
import geometry_core as g
from lettering import FONT
box=g.box;rod=g.rod;cyl=g.cyl;group=g.group
PALETTE={
 'brick':((.38,.32,.265),.94,0,'brick'), 'mortar':((.44,.425,.38),.97,0,'plain'),
 'teal':((.105,.255,.255),.73,.15,'plain'), 'oxblood':((.26,.105,.07),.77,.12,'plain'),
 'ivory':((.78,.745,.625),.81,0,'plain'), 'steel':((.34,.39,.39),.58,.63,'plain'),
 'iron':((.052,.069,.073),.75,.30,'plain'), 'rubber':((.025,.030,.032),.96,0,'plain'),
 'glass':((.24,.35,.36),.21,.08,'plain'), 'concrete':((.43,.44,.40),.97,0,'plain'),
 'asphalt':((.105,.12,.125),.99,0,'plain'), 'paper':((.85,.82,.68),.96,0,'plain'),
 'linen':((.48,.56,.52),.97,0,'plain'), 'floor_tile':((.66,.69,.61),.90,0,'tile'),
 'ochre':((.59,.38,.13),.86,0,'plain'), 'lamp':((.83,.88,.82),.6,0,'plain'),
}
def hashfile(path):return hashlib.sha256(open(path,'rb').read()).hexdigest()
def materials():
 import numpy as np
 # New CC0 tiny base-color textures are deliberately quiet, not edge-wear noise.
 for key,(col,rough,metal,kind) in PALETTE.items():
  size=256;yy,xx=np.mgrid[0:size,0:size];rng=np.random.default_rng(161005+sum(map(ord,key)))
  noise=rng.normal(0,.007,(size,size));rgb=np.asarray(col)[None,None,:]*(1+noise[:,:,None]);rgb=np.clip(rgb,0,1)
  if kind=='brick':
   row=(yy//(size//3));offset=(row%2)*size//4;seam=(yy%(size//3)<5)|((xx+offset)%(size//2)<5)
   rgb[seam]=(.39,.38,.335)
   for r in range(3):
    for c in range(3):
     mask=(row==r)&(((xx+(r%2)*size//4)//(size//2))==c)&~seam;rgb[mask]*=(.94+.027*((r*3+c)%5))
  elif kind=='tile':
   seam=(xx<2)|(yy<2);rgb[seam]=(.42,.445,.405)
  rgba=np.concatenate([rgb,np.ones((size,size,1))],axis=2).astype('float32')
  im=bpy.data.images.new('NS16_'+key,width=size,height=size);im.pixels.foreach_set(rgba.ravel());im.filepath_raw=os.path.join(ROOT,'textures',key+'.png');im.file_format='PNG';im.save();im.pack()
  mat=bpy.data.materials.new('NS16_'+key);mat.use_nodes=True;mat.diffuse_color=(*col,.24 if key=='glass' else 1)
  bs=mat.node_tree.nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=rough;bs.inputs['Metallic'].default_value=metal
  tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=im;mat.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color'])
  if key=='glass':
   bs.inputs['Alpha'].default_value=.20;mat.surface_render_method='DITHERED';mat.use_transparency_overlap=False
  if key=='lamp':bs.inputs['Emission Color'].default_value=(*col,1);bs.inputs['Emission Strength'].default_value=.4
  g.MATS[key]=mat;g.TILES[key]=(1,.25) if key=='brick' else ((.5,.5) if key=='floor_tile' else (1,1))
 fam=json.load(open(os.path.join(ROOT,'inputs/materials_v1/material_manifest.json')))
 for rec in fam['materials']:
  key=rec['id'];mat=bpy.data.materials.new('NS16_'+key);mat.use_nodes=True;bs=mat.node_tree.nodes.get('Principled BSDF')
  for channel,target in [('basecolor','Base Color'),('roughness','Roughness'),('normal',None)]:
   im=bpy.data.images.load(os.path.join(ROOT,'inputs/materials_v1',key+'_'+channel+'.png'),check_existing=True)
   im.colorspace_settings.name='sRGB' if channel=='basecolor' else 'Non-Color';im.pack();tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=im;tex.extension='REPEAT'
   if target:mat.node_tree.links.new(tex.outputs['Color'],bs.inputs[target])
   else:
    norm=mat.node_tree.nodes.new('ShaderNodeNormalMap');norm.inputs['Strength'].default_value=1;mat.node_tree.links.new(tex.outputs['Color'],norm.inputs['Color']);mat.node_tree.links.new(norm.outputs['Normal'],bs.inputs['Normal'])
  g.MATS[key]=mat;g.TILES[key]=tuple(rec['tile_metres'])

def label(txt,c,h,mat='ivory'):
 unit=h/7;w=sum(len(FONT[ch][0])+1 for ch in txt)*unit-unit;x=c[0]-w/2
 for ch in txt:
  for j,row in enumerate(FONT[ch]):
   start=None
   for i,v in enumerate(row+'0'):
    if v=='1' and start is None:start=i
    if v=='0' and start is not None:box((x+(start+i)/2*unit,c[1],c[2]+(3-j)*unit),((i-start)*unit,.006,unit*.94),mat,0);start=None
  x+=(len(FONT[ch][0])+1)*unit

def wall(w=2,mat='plaster_painted',depth=.2):
 group('wall_core');box((0,0,1.6),(w,depth,3.2),mat,.003)
 group('skirting');box((0,-depth/2-.008,.065),(w,.016,.13),'door_paint',.002)
def masonry():
 wall()
 group('exterior_brick_skin');box((0,-.108,1.66),(2,.016,3.08),'brick',.001)
 group('stone_plinth');box((0,-.135,.075),(2,.07,.15),'concrete',.002)
def pier():
 group('brick_pier');box((0,0,1.575),(.28,.34,3.15),'brick',.004)
 group('base_cap');box((0,0,.095),(.32,.37,.19),'concrete',.005)
 group('capital');box((0,0,3.155),(.34,.39,.09),'ivory',.003)
def window():
 group('frame')
 for x in [-.967,.967]:box((x,0,1.335),(.066,.13,2.67),'teal',.004)
 for z in [.045,.43,2.635]:box((0,0,z),(1.868,.13,.07),'teal',.003)
 group('kick_panel');box((0,0,.235),(1.87,.045,.32),'teal',.002)
 group('clear_glazing');box((0,.008,1.53),(1.866,.014,2.12),'glass',.001)
 group('slender_mullion');box((.36,-.012,1.54),(.028,.11,2.17),'teal',.002)
 group('deep_sill');box((0,-.065,.45),(2,.28,.035),'steel',.002)
def entry_frame():
 group('jambs')
 for x in [-.97,-.64,.64,.97]:box((x,0,1.29),(.06,.15,2.58),'teal',.003)
 group('sidelights')
 for x in [-.805,.805]:box((x,.014,1.33),(.27,.014,2.54),'glass',.001)
 group('header');box((0,0,2.615),(2,.15,.07),'teal',.003)
 group('threshold');box((0,0,.009),(1.25,.25,.018),'steel',.002)
 for x in [-.8,.8]:box((x,-.02,.15),(.28,.055,.26),'teal',.001)
def glass_door():
 group('leaf_frame')
 for x in [.028,1.172]:box((x,0,1.275),(.056,.055,2.55),'teal',.003)
 for z in [.035,.26,2.515]:box((.6,0,z),(1.088,.055,.07),'teal',.003)
 group('glass');box((.6,.007,1.385),(1.09,.014,2.18),'glass',.001)
 group('kickplate');box((.6,-.004,.15),(1.09,.055,.16),'steel',.003)
 group('pushbar');rod((.20,-.06,1.07),(1.07,-.06,1.07),.014,'steel',12)
 for x in [.20,1.07]:rod((x,-.022,1.07),(x,-.062,1.07),.014,'steel',12)
 group('hinge')
 for z in [.20,1.30,2.35]:cyl((.011,0,z),.014,.09,'steel',10)
 group('handle');rod((1.08,.065,.90),(1.08,.065,1.22),.014,'steel',12)
 for z in [.90,1.22]:rod((1.08,.02,z),(1.08,.065,z),.014,'steel',12)
def transom():
 group('transom_frame')
 for x in [-.9725,.9725]:box((x,0,.275),(.055,.13,.55),'teal',.002)
 box((0,0,.275),(.055,.13,.45),'teal',.002)
 for z in [.025,.525]:box((0,0,z),(1.89,.13,.05),'teal',.002)
 group('glass')
 for x in [-.485,.485]:box((x,.008,.275),(.91,.014,.45),'glass',.001)
def shutter():
 group('galvanized_slats')
 for i in range(36):
  z=.035+i*.072;box((0,0,z),(1.86,.048,.066),'steel',.003)
 group('bottomrail');box((0,-.005,.025),(1.9,.075,.05),'iron',.003)
 group('recess_handle');box((0,-.045,.77),(.22,.024,.065),'iron',.003)
 for x in [-.135,.135]:box((x,-.04,.77),(.035,.03,.11),'steel',.003)
def shutter_frame():
 group('guide_tracks')
 for x in [-.963,.963]:box((x,0,1.36),(.075,.115,2.72),'iron',.003)
 group('roller_box');box((0,0,2.82),(2.08,.34,.24),'steel',.013)
 group('end_covers')
 for x in [-1.04,1.04]:box((x,0,2.82),(.025,.37,.27),'iron',.004)
def awning():
 group('stretched_canvas');g.add([(-1,0,.16),(1,0,.16),(1,-1,-.13),(-1,-1,-.13),(-1,0,.12),(1,0,.12),(1,-1,-.17),(-1,-1,-.17)],[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],'linen')
 group('valance');box((0,-1.006,-.25),(2,.027,.20),'teal',.001)
 group('sewn_stripes')
 for x in [-.75,-.25,.25,.75]:g.add([(x-.052,-.003,.162),(x+.052,-.003,.162),(x+.052,-.987,-.123),(x-.052,-.987,-.123)],[(0,1,2,3)],'ivory')
 group('brackets')
 for x in [-.9,.9]:rod((x,0,-.35),(x,-.95,-.17),.015,'iron',8);rod((x,0,.12),(x,-.96,-.15),.015,'iron',8)
def fascia():
 group('fascia_panel');box((0,0,.425),(2,.23,.85),'plaster_painted',.004)
 group('projecting_cap');box((0,-.04,.86),(2,.37,.055),'concrete',.004)
 group('lower_band');box((0,-.15,.046),(2,.065,.092),'teal',.003)
def laundry_sign():
 group('enamel_casing');box((0,0,0),(5.25,.09,.59),'teal',.008)
 group('raised_type');label('MORROW WASH',(0,-.05,.085),.24);label('SELF SERVICE',(0,-.051,-.17),.085,'paper')
 for x in [-2.49,2.49]:
  for z in [-.20,.20]:rod((x,-.048,z),(x,-.058,z),.010,'steel',8)
def pawn_sign():
 group('painted_signboard');box((0,0,0),(5.25,.085,.59),'oxblood',.008)
 group('raised_type');label('ALDER PAWN',(0,-.048,.086),.24);label('BUY SELL REPAIR',(0,-.049,-.17),.088,'paper')
 for x in [-2.48,2.48]:
  for z in [-.20,.20]:rod((x,-.045,z),(x,-.056,z),.010,'steel',8)
def display_case():
 group('base');box((0,0,.23),(1.8,.70,.46),'door_paint',.007)
 group('glass_case')
 for x in [-.86,.86]:
  for y in [-.31,.31]:box((x,y,.73),(.035,.035,.56),'steel',.002)
 for z in [.47,.985]:
  for y in [-.31,.31]:box((0,y,z),(1.77,.025,.025),'steel',.001)
 for y in [-.324,.324]:box((0,y,.73),(1.70,.009,.48),'glass',.001)
 for x in [-.882,.882]:box((x,0,.73),(.009,.60,.48),'glass',.001)
 box((0,0,1.005),(1.82,.72,.016),'glass',.001)
 group('display_shelf');box((0,0,.485),(1.69,.59,.02),'linen',.002)
 group('feet')
 for x in [-.74,.74]:
  for y in [-.24,.24]:box((x,y,.035),(.11,.11,.07),'rubber',.005)
 group('lock');rod((.62,.332,.28),(.62,.343,.28),.016,'steel',10)
def counter():
 group('cabinet');box((0,0,.51),(2,.70,1.02),'door_paint',.007)
 group('inset_front');box((0,-.362,.52),(1.77,.027,.74),'framing_pine',.003)
 group('top');box((0,0,1.05),(2.10,.82,.07),'door_wood',.007)
 group('kick');box((0,-.367,.075),(1.90,.035,.10),'iron',.002)
 group('back_drawers')
 for x in [-.5,.5]:
  for z in [.40,.72]:
   box((x,.359,z),(.89,.025,.26),'door_paint',.002);rod((x-.1,.39,z),(x+.1,.39,z),.011,'steel',10)
def payment():
 group('steel_cabinet');box((0,0,.70),(.61,.43,1.40),'teal',.012)
 group('top');box((0,0,1.415),(.64,.46,.03),'steel',.004)
 group('control_panel');box((0,-.22,1.14),(.42,.025,.37),'ivory',.005)
 group('bill_slot');box((0,-.239,1.16),(.24,.019,.027),'iron',.001)
 group('small_screen');box((0,-.24,1.27),(.24,.016,.08),'iron',.003)
 group('return_cup');box((0,-.224,.64),(.30,.023,.21),'iron',.003);box((0,-.28,.555),(.31,.12,.035),'steel',.003)
 group('labels');label('CHANGE',(0,-.237,.91),.067,'ivory');label('1 5',(0,-.252,1.065),.045,'teal')
 group('lock');rod((.24,-.22,.93),(.24,-.236,.93),.012,'steel',10)
 group('foot');box((0,0,.025),(.65,.46,.05),'iron',.003)
def rules():
 group('backplate');box((0,0,0),(.62,.04,.80),'ivory',.005)
 group('lettering');label('WASH',(0,-.025,.24),.095,'teal');label('DRY FOLD',(0,-.025,.09),.058,'teal');label('OPEN 7-9',(0,-.025,-.08),.052,'iron');label('NO DYE',(0,-.025,-.24),.063,'iron')
 for x in [-.27,.27]:
  for z in [-.35,.35]:rod((x,-.022,z),(x,-.028,z),.009,'steel',8)
def partition():wall(2,'plaster_painted',.12)
def doorway(service=False):
 mat='brick' if service else 'plaster_painted';dep=.20 if service else .12
 group('wall')
 for x in [-.8375,.8375]:box((x,0,1.6),(.325,dep,3.2),mat,.002)
 box((0,0,2.82),(1.35,dep,.76),mat,.002)
 group('jamb')
 for x in [-.645,.645]:box((x,0,1.21),(.05,dep+.04,2.42),'door_paint',.002)
 box((0,0,2.43),(1.34,dep+.04,.04),'door_paint',.002)
 if service:
  group('dripcap');box((0,-.14,2.48),(1.47,.14,.04),'steel',.003)
def stock_door():
 group('leaf');box((.60,0,1.195),(1.2,.042,2.39),'door_paint',.004)
 group('kickplate');box((.60,-.027,.18),(1.11,.010,.27),'steel',.001)
 group('handle');rod((1.095,-.022,1.04),(1.095,-.083,1.04),.021,'steel',12);rod((1.095,-.083,1.04),(.99,-.083,1.04),.012,'steel',12)
 group('hinges')
 for z in [.22,1.21,2.2]:cyl((.01,0,z),.014,.08,'steel',8)
def rear_door():
 stock_door()
 group('vent')
 box((.6,-.028,.69),(.73,.017,.40),'iron',.002)
 for i in range(7):box((.6,-.043,.53+i*.046),(.72,.033,.023),'steel',.001)
 group('pushplate');box((.21,.025,1.08),(.14,.012,.27),'steel',.002)
def hatch():
 group('masonry')
 box((0,0,.525),(2,.20,1.05),'plaster_painted',.003);box((0,0,2.83),(2,.20,.74),'plaster_painted',.003)
 for x in [-.80,.80]:box((x,0,1.755),(.40,.20,1.41),'plaster_painted',.003)
 group('hatch_frame')
 for x in [-.60,.60]:box((x,-.035,1.755),(.04,.28,1.41),'door_paint',.003)
 for z in [1.07,2.43]:box((0,-.035,z),(1.24,.28,.04),'door_paint',.003)
 group('closed_hatch');box((0,0,1.755),(1.16,.043,1.29),'door_paint',.003)
 group('two_leaf_seam');box((0,-.027,1.755),(.012,.007,1.28),'iron',0)
 group('ledge');box((0,-.25,1.07),(1.40,.55,.045),'door_wood',.004)
def floor():
 group('slab');box((0,0,-.078),(2,2,.14),'concrete',0)
 group('floor_finish');box((0,0,-.004),(2,2,.008),'floor_tile',0)
def roof():
 group('roof_slab');box((0,0,.07),(2,2,.14),'plaster_painted',0)
 group('membrane');box((0,0,.148),(2,2,.016),'iron',0)
BUILDERS=[('masonry_pier','Brick pier / 3200 mm',pier),('glazed_display_bay_2m','Glazed storefront bay',window),('masonry_wall_2m','Brick-faced wall bay',masonry),('storefront_entry_frame_2m','Entry frame and sidelights',entry_frame),('storefront_glass_door','Glass door / left hinge',glass_door),('transom_insert_2m','Divided transom insert',transom),('roller_shutter_2m','Separate closed shutter',shutter),('shutter_tracks_2m','Tracks and roller box',shutter_frame),('striped_awning_2m','Cloth awning and brackets',awning),('fascia_cornice_2m','Upper fascia and cornice',fascia),('morrow_wash_sign','MORROW WASH sign',laundry_sign),('alder_pawn_sign','ALDER PAWN sign',pawn_sign),('glazed_display_case','Glazed display cabinet',display_case),('service_counter_2m','Repair service counter',counter),('laundry_change_machine','Laundry payment fixture',payment),('laundry_rules_sign','Wash and opening rules sign',rules),('stockroom_partition_2m','Plain stockroom partition',partition),('stockroom_doorway_2m','Stockroom doorway bay',doorway),('stockroom_door_leaf','Painted rear door leaf',stock_door),('rear_service_frame_2m','Exterior service doorway',lambda:doorway(True)),('rear_service_door','Vented service door leaf',rear_door),('service_hatch_2m','Closed stock service hatch',hatch),('floor_slab_2m','Metric tiled floor slab',floor),('roof_slab_2m','Roof/ceiling slab',roof)]
def reset():g.V=[];g.F=[];g.FM=[];g.TAG=[];g.CURRENT='body'
def anchors(name):
 cs=[]
 def a(n,p,v):cs.append(g.connection(n,p,v))
 if name.endswith('_2m') and name not in ['service_counter_2m']:
  a('left',[-1,0,0],[-1,0,0]);a('right',[1,0,0],[1,0,0])
 if name in ['glazed_display_bay_2m','storefront_entry_frame_2m']:a('transom',[0,0,2.65],[0,0,1])
 if name=='storefront_entry_frame_2m':a('door_hinge',[-.6,0,.025],[0,-1,0])
 if name in ['stockroom_doorway_2m','rear_service_frame_2m']:a('door_hinge',[-.6,0,.01],[0,-1,0])
 if name.endswith('door') or name.endswith('door_leaf'):a('hinge',[0,0,0],[0,-1,0])
 if name in ['floor_slab_2m','roof_slab_2m']:a('front',[0,-1,0],[0,-1,0]);a('back',[0,1,0],[0,1,0])
 if name=='fascia_cornice_2m':a('sign_face',[0,-.166,.425],[0,-1,0])
 a('placement_origin',[0,0,0],[0,0,1])
 return cs

def export(file,obs):
 bpy.ops.object.select_all(action='DESELECT')
 for ob in obs:ob.hide_set(False);ob.hide_viewport=False;ob.select_set(True)
 bpy.context.view_layer.objects.active=obs[0]
 # Tangents require triangles/quads. Temporary modifiers affect export only;
 # native polygon topology and editable component vertex groups are preserved.
 mods=[]
 for ob in obs:
  if ob.type=='MESH':
   mod=ob.modifiers.new('TEMP_EXPORT_TRIANGULATE','TRIANGULATE');mod.keep_custom_normals=True;mods.append((ob,mod))
 try:bpy.ops.export_scene.gltf(filepath=os.path.join(ROOT,file),export_format='GLB',use_selection=True,export_yup=True,export_apply=True,export_tangents=True,export_extras=True,export_animations=False,export_cameras=False,export_lights=False)
 finally:
  for ob,mod in mods:ob.modifiers.remove(mod)
 # A tangent is optional without a normal map. Blender can emit zero tangents on
 # sub-millimetre decorative bevels. Omit that unused attribute, not geometry.
 # The BIN chunk is kept byte-for-byte; normal-mapped material tangents remain.
 path=os.path.join(ROOT,file);raw=open(path,'rb').read();size,kind=struct.unpack_from('<II',raw,12);assert kind==0x4e4f534a
 doc=json.loads(raw[20:20+size]);removed=0
 for mesh in doc.get('meshes',[]):
  for prim in mesh.get('primitives',[]):
   mat=doc.get('materials',[])[prim['material']] if 'material' in prim else {}
   if 'normalTexture' not in mat and 'TANGENT' in prim['attributes']:del prim['attributes']['TANGENT'];removed+=1
 if removed:
  js=json.dumps(doc,separators=(',',':'),ensure_ascii=False).encode();js+=b' '*((-len(js))%4);tail=raw[20+size:]
  open(path,'wb').write(struct.pack('<III',0x46546c67,2,20+len(js)+len(tail))+struct.pack('<II',len(js),0x4e4f534a)+js+tail)


def reuse(col):
 catalog=json.load(open(os.path.join(ROOT,'reused_assets/reuse_catalog.json')))
 print('REUSE_CATALOG_TYPE',type(catalog),list(catalog)[:4]);rows=catalog['assets'] if isinstance(catalog,dict) and 'assets' in catalog else catalog
 out={}
 for rec in rows:
  name=rec['name'];dest=os.path.join(ROOT,'reused_assets',name+'.glb');before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=dest);obs=[o for o in set(bpy.data.objects)-before if o.type=='MESH'];assert len(obs)==1,(name,len(obs));ob=obs[0]
  for c in list(ob.users_collection):c.objects.unlink(ob)
  col.objects.link(ob);ob.name='REUSED_'+name;ob.location=(0,0,0);ob['asset_id']=rec['id'];ob['source_id']=rec['id'];ob['source_sha256']=hashfile(dest);ob['reused_existing_design']=True;ob['render_only']=True;ob['collider_enabled']=False;ob['colliders_supplied']=False;ob['units']='metres';out[name]=ob
 return out,rows
PLACEMENTS=[];SUPPORTS=[];CONNECTIONS=[]
def assemble(by,re,col):
 st=g.collection('02a_STRUCTURE',col);ro=g.collection('02b_ROOFS_toggle_cutaway',col);pr=g.collection('02c_FURNISHINGS',col);site=g.collection('02d_SITE',col)
 def put(n,p,yaw=0,c=None,r=False,scale=None,overrides=None):
  src=(re if r else by)[n];ob=src.copy();ob.data=src.data;(c or pr).objects.link(ob)
  # Explicit mode avoids imported quaternion rotation ignoring a later Euler change.
  ob.rotation_mode='XYZ';ob.rotation_euler=(0,0,yaw);ob.location=p
  if scale:ob.scale=scale
  ob.name=('REUSE__' if r else 'INSTANCE__')+n+'__%03d'%len(PLACEMENTS);ob['assembly_instance']=True
  if overrides:
   ob.data=ob.data.copy()
   for i,m in enumerate(ob.data.materials):
    if m.name in overrides:ob.data.materials[i]=g.MATS[overrides[m.name]]
  PLACEMENTS.append({'object':ob.name,'asset_id':ob['asset_id'],'position_source_m':list(p),'yaw_source_deg':math.degrees(yaw),'scale':list(ob.scale),'source_rotation_mode':src.rotation_mode,'instance_rotation_mode':'XYZ','reused_existing_design':r,'collection':ob.users_collection[0].name,'material_overrides':overrides or {}});return ob
 def supported(n,xy,surf,z=None,yaw=0):
  src=re[n];bb=g.bounds(src);top=z if z is not None else max((surf.matrix_world@v.co).z for v in surf.data.vertices)
  ob=put(n,(xy[0],xy[1],top-bb[0][2]+.002),yaw,r=True);SUPPORTS.append({'object':ob.name,'support':surf.name,'tolerance_m':.012});return ob
 for x in [-5,-3,-1,1,3,5]:
  for y in [1,3,5,7,9]:
   put('floor_slab_2m',(x,y,0),c=st,overrides={'NS16_floor_tile':'floor_pine'} if x>0 and y<6 else None);put('roof_slab_2m',(x,y,3.2),c=ro)
  typ='storefront_entry_frame_2m' if x in [-3,3] else 'glazed_display_bay_2m'
  f=put(typ,(x,0,0),c=st);t=put('transom_insert_2m',(x,0,2.65),c=st);CONNECTIONS.append({'a':f.name,'socket_a':'transom','b':t.name,'socket_b':'placement_origin'})
  if x in [-3,3]:
   d=put('storefront_glass_door',(x-.6,0,.025),pi/2);CONNECTIONS.append({'a':f.name,'socket_a':'door_hinge','b':d.name,'socket_b':'hinge'})
  put('fascia_cornice_2m',(x,0,3.2),c=st)
  if x<0:put('striped_awning_2m',(x,-.16,2.91),c=st)
  if x>0:put('shutter_tracks_2m',(x,-.19,0),c=st)
  typ='stockroom_doorway_2m' if x in [-3,3] else 'stockroom_partition_2m'
  f=put(typ,(x,6,0),c=st)
  if x in [-3,3]:
   d=put('stockroom_door_leaf',(x-.6,6,.01),pi/2);CONNECTIONS.append({'a':f.name,'socket_a':'door_hinge','b':d.name,'socket_b':'hinge'})
  typ='rear_service_frame_2m' if x==-3 else ('service_hatch_2m' if x==3 else 'masonry_wall_2m')
  f=put(typ,(x,10,0),pi,c=st)
  if x==-3:
   d=put('rear_service_door',(x+.6,10,.01),pi/2);CONNECTIONS.append({'a':f.name,'socket_a':'door_hinge','b':d.name,'socket_b':'hinge'})
 for x in [-3,3]:
  for y in [2.1,4.7]:put('frosted_dome_light',(x,y,3.2),c=ro,r=True)
 for x in [-4,0,4]:put('frosted_dome_light',(x,8.5,3.2),c=ro,r=True)
 # Perimeter two-metre bays, no duplicate walls at seams.
 for y in [1,3,5,7,9]:
  put('masonry_wall_2m',(-6,y,0),-pi/2,c=st);put('masonry_wall_2m',(6,y,0),pi/2,c=st)
 for y in [1,3,5]:put('stockroom_partition_2m',(0,y,0),pi/2,c=st)
 for x in [-6,0,6]:put('masonry_pier',(x,-.04,0),c=st)
 put('morrow_wash_sign',(-3,-.177,3.625));put('alder_pawn_sign',(3,-.177,3.625))
 # One shut display bay makes the closing pawn tenancy visibly distinct.
 put('roller_shutter_2m',(5,-.21,0))
 put('laundry_rules_sign',(-5.84,.86,1.79),pi/2);put('laundry_change_machine',(-.57,4.82,0),-pi/2)
 for y in [1.7,2.73,3.76,4.79]:put('front_load_washer',(-5.42,y,0),pi/2,r=True)
 for y in [1.8,2.95,4.1]:put('vented_tumble_dryer',(-.6,y,0),-pi/2,r=True)
 # Reused compact folding table in an intentional side nook, leaving central access clear.
 table=put('table_folding_card',(-4.4,.87,0),r=True);bpy.context.view_layer.update();supported('folded_bedsheet_stack',(-4.42,.88),table)
 put('laundry_hamper_woven',(-4.6,5.22,0),r=True)
 case=put('glazed_display_case',(1.48,2.1,0),pi/2)
 # Display objects rest on the inner shelf, not on an imagined cabinet top.
 bpy.context.view_layer.update();supported('twin_bell_alarm_clock',(1.47,1.73),case,z=.495,yaw=pi/2);supported('portable_am_fm_radio',(1.47,2.30),case,z=.495,yaw=pi/2)
 count=put('service_counter_2m',(4.37,4.68,0));bpy.context.view_layer.update();supported('two_slot_toaster',(4.53,4.67),count)
 put('shelving_steel_repaired',(5.5,2.50,0),-pi/2,r=True)
 # Shared back-of-house, with clear cross circulation in front of the work line.
 sink=put('deep_utility_sink',(-5.25,9.48,0),r=True)
 bench=put('workbench_pegboard',(1.5,9.48,0),r=True);bpy.context.view_layer.update()
 # Workbench z datum is a known actual tabletop, verified by triangle support QA.
 supported('corded_power_drill',(1.18,9.34),bench,z=.8815);supported('combination_pliers',(1.79,9.25),bench,z=.8815)
 put('shelving_steel_repaired',(4.97,9.40,0),r=True)
 put('handled_detergent_jug',(-4.38,9.68,0),r=True)
 # Street/apron are presentation site art, original and render-only.
 g.stage_box('SITE_sidewalk',(0,-1.25,-.075),(15,2.5,.15),'concrete',site)
 g.stage_box('SITE_rear_apron',(0,11.0,-.07),(15,2,.14),'concrete',site)
 g.stage_box('SITE_street',(0,-5.0,-.175),(28,5,.15),'asphalt',site)
 for x in [-6,-3,0,3,6]:g.stage_box('SITE_sidewalk_joint',(x,-1.25,.0007),(.009,2.50,.0014),'mortar',site)
 return st,ro,pr,site

def setup():
 sc=bpy.context.scene;sc.unit_settings.system='METRIC';sc.unit_settings.scale_length=1;sc.render.engine='CYCLES';sc.cycles.device='CPU';sc.cycles.samples=32;sc.cycles.use_denoising=False;sc.render.threads_mode='FIXED';sc.render.threads=2;sc.render.image_settings.file_format='PNG';sc.render.resolution_percentage=100;sc.view_settings.view_transform='AgX';sc.view_settings.look='AgX - Medium High Contrast';sc.view_settings.exposure=.5
 sc.world=bpy.data.worlds.new('Overcast neighborhood');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.55,.61,.66,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.5
 sc['batch']='16_neighborhood_storefronts';sc['license']='CC0-1.0 original models; input source provenance retained';sc['render_only']=True;sc['colliders_supplied']=False;sc['source_basis']='metres Z-up front -Y'
def main():
 bpy.ops.wm.read_factory_settings(use_empty=True);materials();setup();src=g.collection('01_NEW_ASSET_LIBRARY');old=g.collection('01b_REUSED_LIBRARY');ass=g.collection('02_STOREFRONTS_EXAMPLE');pres=g.collection('03_CAMERAS_LIGHTS');by={};records=[]
 for name,title,fn in BUILDERS:
  reset();fn();ob=g.mesh_from_buffer(name,src);ob['asset_id']='ns16_'+name;ob['license']='CC0-1.0';ob['units']='metres';ob['render_only']=True;ob['collider_enabled']=False;ob['colliders_supplied']=False;ob['connections']=anchors(name);ob.asset_mark();ob.asset_data.description=title+'; original CC0 neighborhood storefront module';by[name]=ob
  bpy.context.view_layer.update();bb=g.bounds(ob);ob.data.calc_loop_triangles();path='assets/'+name+'.glb';export(path,[ob]);records.append({'id':ob['asset_id'],'name':name,'title':title,'file':path,'bounds_m_source_xyz':bb,'dimensions_m_source_xyz':[bb[1][i]-bb[0][i] for i in range(3)],'triangles':len(ob.data.loop_triangles),'vertices_source':len(ob.data.vertices),'material_count':len(ob.data.materials),'editable_vertex_groups':[v.name for v in ob.vertex_groups],'pivot':'Left hinge floor datum for door leaves; attachment origin for signs/transom/awnings; floor-centred bay origin for wall and floor/roof pieces. See exact sockets.','connections':anchors(name),'sha256':hashfile(os.path.join(ROOT,path)),'bytes':os.path.getsize(os.path.join(ROOT,path)),'render_only':True,'collider_enabled':False,'colliders_supplied':False})
 re,recs=reuse(old);st,roof,pr,site=assemble(by,re,ass);bpy.context.view_layer.update()
 # Prior GLB scene extras are untrusted for this new scene; reset ownership metadata.
 for k in list(bpy.context.scene.keys()):del bpy.context.scene[k]
 bpy.context.scene['batch']='16_neighborhood_storefronts';bpy.context.scene['license']='CC0-1.0';bpy.context.scene['render_only']=True;bpy.context.scene['colliders_supplied']=False;bpy.context.scene['source_basis']='metres Z-up front -Y'
 for r in PLACEMENTS:r['matrix_world_source_row_major']=[list(v) for v in bpy.data.objects[r['object']].matrix_world]
 export('storefronts_example.glb',list(ass.all_objects));export('storefronts_example_cutaway.glb',[o for o in ass.all_objects if o.name not in roof.objects])
 manifest={'schema':'neighborhood_storefronts_batch16_v1','title':'Morrow Block: laundromat and pawn/repair storefronts','batch':'16_neighborhood_storefronts','license':'CC0-1.0 original models, sign artwork, original procedural maps and verified existing CC0 texture derivatives; source provenance retained','asset_count':len(records),'total_triangles':sum(r['triangles'] for r in records),'units':'metres','source_coordinates':'Right-handed Z-up, front -Y','gltf_coordinates':'Right-handed Y-up, front +Z; source (x,y,z) maps to (x,z,-y)','physics':'Render-only; no colliders, rigid bodies, navmesh or runtime interaction. Reused source bytes preserved, override any legacy collider suggestion at consumer import.','snap_grid_m':2,'material_uv_contract':{'projection':'Per-face dominant-axis metric projection; vertical faces use X/Z or Y/Z, floors X/Y. U/V divided by declared tile metres; no random per-cell offsets.','wood_grain':'+V follows vertical length on wall/door/support faces and +Y on horizontal floors. Source normal UV tangent basis exported by Blender.','tiles_m':g.TILES,'normal':'Tangent-space OpenGL +Y, linear, strength=1; roughness Non-Color; basecolor sRGB','glass':'Alpha blended 0.20 geometry, no optical refraction runtime requirement','source_family':'inputs/materials_v1/material_manifest.json'},'assets':records,'reused_existing_assets':recs,'example':{'file':'storefronts_example.glb','cutaway':'storefronts_example_cutaway.glb','footprint_m':[12,10],'public_units':2,'public_unit_m':[6,6],'shared_rear_service_m':[12,4],'ceiling_height_m':3.2,'floor_z_m':0,'entry_clear_width_m':1.22,'entry_clear_height_m':2.58,'stockroom_clear_width_m':1.24,'stockroom_clear_height_m':2.41,'instance_count':len(PLACEMENTS)},'limitations':['No building-code, collision, AI navigation or engine-renderer compliance claimed.','Doors are separate editable hinged meshes, example shown statically open.','Original fictional businesses and layout. No copied commercial map, real business name or incident.','Muted material family v1 reused unchanged; provisional generated plaster excluded.','No loader, physics, engine runtime, original batch or prior asset edited.']}
 json.dump(manifest,open(os.path.join(ROOT,'manifest.json'),'w'),indent=2);json.dump({'schema':'storefront_assembly_v1','instances':PLACEMENTS,'connections':CONNECTIONS,'supported_props':SUPPORTS},open(os.path.join(ROOT,'assembly_manifest.json'),'w'),indent=2)
 src.hide_render=True;old.hide_render=True;src.hide_viewport=True;old.hide_viewport=True;g.camera(pres,'Overview',(16,-20,18),(0,4,0),22)
 for im in bpy.data.images:
  if im.source=='FILE' and not im.packed_file:im.pack()
 bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'neighborhood_storefronts.blend'));print('STOREFRONTS_BUILT',len(records),manifest['total_triangles'],len(PLACEMENTS))
if __name__=='__main__':main()
