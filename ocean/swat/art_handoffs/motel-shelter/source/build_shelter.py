"""Original roadside shelter art. Blender 4.3.2. No outside geometry or add-ons.
Run: blender -b --python source/build_shelter.py
Optional SHELTER_OUT=/absolute/fresh/output; input maps remain relative to recipe.
"""
import bpy, bmesh, math, json, os, hashlib
from pathlib import Path
from mathutils import Vector

SRC=Path(__file__).resolve().parent
OUT=Path(os.environ.get('SHELTER_OUT', SRC.parent))
for sub in ['source','runtime','qa']: (OUT/sub).mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
parts=[]; anchors=[]
root=bpy.data.objects.new('BC_SHELTER01_ROOT',None);bpy.context.collection.objects.link(root)
root['asset_id']='BC_SHELTER01';root['units']='metres'
root['scope']='Static environment art. No transit designation, placement, collision, navigation, support/cover logic or native performance acceptance.'
root['front']='Blender -Y; standard glTF +Z after export'
root['origin']='center between four grade-level baseplate centers; Blender Z=0'

def image(name,linear=False):
    im=bpy.data.images.load(str(SRC/'maps'/name),check_existing=True)
    if linear:im.colorspace_settings.name='Non-Color'
    return im
def material(name,tex,metal,rough,orm=None,roughmap=None):
    m=bpy.data.materials.new(name);m.use_nodes=True;m.use_backface_culling=True
    m.diffuse_color=(.27,.30,.27,1)
    p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
    t=m.node_tree.nodes.new('ShaderNodeTexImage');t.image=image(tex);t.interpolation='Linear'
    m.node_tree.links.new(t.outputs['Color'],p.inputs['Base Color'])
    if orm:
        t=m.node_tree.nodes.new('ShaderNodeTexImage');t.image=image(orm,True)
        sep=m.node_tree.nodes.new('ShaderNodeSeparateColor');sep.mode='RGB'
        m.node_tree.links.new(t.outputs['Color'],sep.inputs[0]);m.node_tree.links.new(sep.outputs['Green'],p.inputs['Roughness']);m.node_tree.links.new(sep.outputs['Blue'],p.inputs['Metallic'])
    if roughmap:
        t=m.node_tree.nodes.new('ShaderNodeTexImage');t.image=image(roughmap,True)
        m.node_tree.links.new(t.outputs['Color'],p.inputs['Roughness'])
    return m
paint=material('BC_SHELTER01_WeatheredGreenSteel','shelter_paint_atlas.png',.02,.84,orm='shelter_paint_orm.png')
steel=material('BC_SHELTER01_GalvanizedRoofAndFixings','steel.png',.78,.66)
wood=material('BC_SHELTER01_WeatheredTimber','shelter_timber_atlas.png',0,.85,roughmap='shelter_timber_roughness.png')

def assign_uv(o,style='frame',cell=0):
    me=o.data;uv=me.uv_layers.active or me.uv_layers.new(name='UVMap')
    lo=[min(v.co[i] for v in me.vertices) for i in range(3)]; hi=[max(v.co[i] for v in me.vertices) for i in range(3)]
    for p in me.polygons:
        ax=max(range(3),key=lambda i:abs(p.normal[i]));u,v=([1,2],[0,2],[0,1])[ax]
        for li in p.loop_indices:
            c=me.vertices[me.loops[li].vertex_index].co
            a=(c[u]-lo[u])/max(hi[u]-lo[u],1e-8);b=(c[v]-lo[v])/max(hi[v]-lo[v],1e-8)
            if style=='timber':
                # Long dimension is local X, regardless of plank's later rotation.
                a=(c.x-lo[0])/max(hi[0]-lo[0],1e-8)
                vv=1 if ax==2 else 2
                b=(c[vv]-lo[vv])/max(hi[vv]-lo[vv],1e-8)
                if ax==0: a=(c.y-lo[1])/max(hi[1]-lo[1],1e-8)
                uv.data[li].uv=(.006+a*.988,1-(cell+1)/8+.009+b*.107)
            elif style in ['panel','frame','base']:
                q=cell if style=='panel' else (3 if style=='base' else 2)
                if style=='panel' and ax==1:
                    a=(c.x-lo[0])/max(hi[0]-lo[0],1e-8);b=(c.z-lo[2])/max(hi[2]-lo[2],1e-8)
                uv.data[li].uv=((q%2)*.5+.008+a*.484,(1-q//2)*.5+.008+b*.484)
            else:uv.data[li].uv=(c[u]*1.4+.17,c[v]*1.4+.37)

def register(o,m,style='frame',cell=0):
    o.data.materials.append(m);o.parent=root;parts.append(o)
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
    assign_uv(o,style if m==paint or m==wood else 'steel',cell)
    return o
def mesh(n,vs,fs,m=paint,style='frame',cell=0):
    me=bpy.data.meshes.new(n);me.from_pydata(vs,[],fs);me.update()
    o=bpy.data.objects.new(n,me);bpy.context.collection.objects.link(o)
    return register(o,m,style,cell)
def box(n,loc,dim,m=paint,bev=.003,seg=1,style='frame',cell=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=n;o.dimensions=dim
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bev:
        mod=o.modifiers.new('Small fabricated edge radius','BEVEL');mod.width=bev;mod.segments=seg
        bpy.ops.object.modifier_apply(modifier=mod.name)
    o.select_set(False);return register(o,m,style,cell)
def beam(n,a,b,w,d,m=paint,bev=.003,seg=1):
    a,b=Vector(a),Vector(b);o=box(n,(a+b)/2,(w,d,(b-a).length),m,bev,seg)
    o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();return o
def cylinder(n,a,b,r,m=steel,N=8):
    a,b=Vector(a),Vector(b)
    bpy.ops.mesh.primitive_cylinder_add(vertices=N,radius=r,depth=(b-a).length,location=(a+b)/2)
    o=bpy.context.object;o.name=n;o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();o.select_set(False)
    return register(o,m,'steel')
def prism(n,poly,off,m=paint,style='frame',cell=0):
    count=len(poly);vs=list(poly)+[tuple(Vector(p)+Vector(off)) for p in poly]
    fs=[tuple(range(count-1,-1,-1)),tuple(range(count,count*2))]+[(i,(i+1)%count,(i+1)%count+count,i+count) for i in range(count)]
    return mesh(n,vs,fs,m,style,cell)
def anchor(n,pos,purpose):
    o=bpy.data.objects.new('ANCHOR_'+n,None);bpy.context.collection.objects.link(o);o.parent=root;o.location=pos
    o.empty_display_size=.07;o['purpose']=purpose;o['coordinate_basis']='Node transform uses normal exporter basis conversion; documented coordinates are explicitly separated.';anchors.append(o)

def roofz(y):return 2.36-.10*(y+.80)
# Four hollow-section-looking posts with capped ends hidden inside seated rails.
for sx,labelx in [(-1,'Left'),(1,'Right')]:
    for sy,labely in [(-1,'Front'),(1,'Rear')]:
        x,y=sx*1.38,sy*.58;n=labelx+'_'+labely
        box('Baseplate_'+n,(x,y,.011),(.205,.205,.022),paint,.006,1,'base')
        top=roofz(y)-.100
        box('Post_'+n,(x,y,(.021+top)/2),(.09,.09,top-.021),paint,.004,1)
        # Fillet-like collar, shoulder and four visible fastener heads seated on plate.
        box('PostWeldCollar_'+n,(x,y,.030),(.108,.108,.020),paint,.006,1,'base')
        for dx in [-.073,.073]:
            for dy in [-.073,.073]:
                cylinder('AnchorWasher_'+n+'_'+str(dx)+'_'+str(dy),(x+dx,y+dy,.021),(x+dx,y+dy,.025),.014,steel,12)
                cylinder('AnchorNut_'+n+'_'+str(dx)+'_'+str(dy),(x+dx,y+dy,.024),(x+dx,y+dy,.036),.010,steel,6)
        anchor('Foot_'+n,(x,y,0),'Descriptive baseplate ground-contact center. No gameplay/collision semantics.')

# Side roof rails follow the actual slope. Front/rear rails and three purlins meet roof underside.
for x,side in [(-1.38,'Left'),(1.38,'Right')]:
    beam('RoofSideRail_'+side,(x,-.63,roofz(-.63)-.054),(x,.63,roofz(.63)-.054),.096,.10)
for y,tag in [(-.58,'Front'),(.58,'Rear')]:
    box('RoofCrossRail_'+tag,(0,y,roofz(y)-.054),(2.77,.09,.10),paint,.004,1)
for y,tag in [(-.27,'A'),(.14,'B')]:
    box('RoofPurlin_'+tag,(0,y,roofz(y)-.024),(2.77,.04,.040),steel,.002)

# Short knee braces express the joined frame rather than a pile of disconnected boxes.
for x,side in [(-1.38,'Left'),(1.38,'Right')]:
    for y,tag in [(-.58,'Front'),(.58,'Rear')]:
        inward=-1 if x>0 else 1
        beam('KneeAcross_'+side+'_'+tag,(x,y,roofz(y)-.36),(x+inward*.28,y,roofz(y)-.105),.035,.035,paint,.002)
    # Side-facing gusset pair ties sloping rail to the upright; hand-sized plate, no floating bolts.
    xx=x+(-.047 if x<0 else .041)
    for y,tag,sign in [(-.58,'Front',1),(.58,'Rear',-1)]:
        poly=[(xx,y-.045*sign,roofz(y)-.095),(xx,y+.20*sign,roofz(y)-.115),(xx,y-.045*sign,roofz(y)-.28)]
        prism('TopGusset_'+side+'_'+tag,poly,(.006,0,0),steel)

# Rear opaque steel screen and its rails. Air gap below is intentional for grade clearance.
for z,tag,h in [(.23,'Lower',.06),(1.12,'Middle',.035),(2.03,'Upper',.055)]:
    box('RearRail_'+tag,(0,.58,z),(2.76,.072,h),paint,.003)
box('RearCenterMullion',(0,.592,1.1225),(.068,.080,1.815),paint,.003)
for a,b,tag,q in [(-1.335,-.029,'Left',0),(.029,1.335,'Right',1)]:
    # Shallow folded ribs are part of a continuous solid screen, not bars/open lattice.
    profile=[(a,0)]
    for j in range(5):
        c=a+.14+j*(b-a-.28)/4
        profile +=[(c-.032,0),(c-.021,.014),(c+.021,.014),(c+.032,0)]
    profile +=[(b,0),(b,.008)]
    for x,d in reversed(profile[:-2]):profile.append((x,d+.008))
    vs=[(x,.622+d,.255) for x,d in profile]+[(x,.622+d,2.01) for x,d in profile]
    nn=len(profile);fs=[tuple(range(nn-1,-1,-1)),tuple(range(nn,2*nn))]+[(i,(i+1)%nn,(i+1)%nn+nn,i+nn) for i in range(nn)]
    mesh('RearScreen_'+tag+'_FoldedOpaque',vs,fs,paint,'panel',q)
    # Screen clips are clamped to both face and rails; rivets show purposeful attachment.
    for x in [a+.04,b-.04]:
        for z in [.285,1.12,1.99]:
            box('PanelClip_'+tag+'_'+str(x)+'_'+str(z),(x,.617,z),(.040,.020,.050),paint,.001)
            cylinder('PanelRivet_'+tag+'_'+str(x)+'_'+str(z),(x,.608,z),(x,.602,z),.006,steel,6)

# Fixed bench: four 105 mm seat slats, top at 470 mm, depth 442.5 mm, back gently reclined.
for j,y in enumerate([.02625,.13875,.25125,.36375]):
    o=box('BenchSeatSlat_%02d'%(j+1),(0,y,.448),(2.40,.105,.044),wood,.009,2,'timber',j)
for j,z in enumerate([.72,.855,.990]):
    y=.435+(z-.65)*.1763
    o=box('BenchBackSlat_%02d'%(j+1),(0,y,z),(2.40,.038,.115),wood,.006,2,'timber',j+4)
    o.rotation_euler.x=math.radians(-10)

# Cantilever brackets meet the rear structural rail and support each seat slat.
box('BenchRearSupportRail',(0,.552,.403),(2.70,.060,.054),paint,.003)
for j,x in enumerate([-1.03,0,1.03]):
    n='%02d'%(j+1)
    beam('BenchSeatBearer_'+n,(x,-.031,.403),(x,.576,.403),.042,.046,paint,.003)
    beam('BenchBrace_'+n,(x,-.010,.379),(x,.557,.250),.030,.032,paint,.002)
    beam('BenchBackUpright_'+n,(x,.424,.39),(x,.543,1.065),.033,.036,paint,.002)
    # Mounting pads express steel-to-frame contact at the rear rail, no independent legs.
    box('BenchBracketMount_'+n,(x,.549,.275),(.085,.022,.090),paint,.003)
    for k,y in enumerate([.02625,.13875,.25125,.36375]):
        cylinder('SeatBolt_'+n+'_'+str(k),(x,y,.468),(x,y,.474),.006,steel,8)
    for k,z in enumerate([.72,.855,.990]):
        y=.435+(z-.65)*.1763
        cylinder('BackBolt_'+n+'_'+str(k),(x,y-.018,z),(x,y-.023,z),.006,steel,8)

# A pair of unintrusive rounded end arm rails lends a credible resting/standing affordance.
# They are not collision/navigation proxies and do not imply accessibility compliance.
for x,tag in [(-1.19,'Left'),(1.19,'Right')]:
    beam('ArmFrontSupport_'+tag,(x,-.020,.405),(x,-.020,.655),.025,.030,paint,.007,2)
    beam('ArmRearSupport_'+tag,(x,.430,.440),(x,.430,.655),.025,.030,paint,.007,2)
    box('ArmTop_'+tag,(x,.205,.663),(.042,.50,.040),paint,.012,2)

# Corrugated roof, real thickness, visible fold silhouette and continuous slope to rear gutter.
xx=[-1.60]
for i in range(22):
    c=-1.51+i*3.02/21
    xx += [c-.026,c-.015,c+.015,c+.026]
xx+=[1.60];xx=sorted(set(xx))
def ribz(x):
    return max([.019*max(0,min(1,(.026-abs(x-(-1.51+i*3.02/21)))/.011)) for i in range(22)])
vs=[]
for layer in [0,-.005]:
    for y in [-.80,.72]:
        vs +=[(x,y,roofz(y)+ribz(x)+layer) for x in xx]
nx=len(xx);fs=[]
for i in range(nx-1):
    fs.extend([(i,i+1,nx+i+1,nx+i),(2*nx+i,3*nx+i,3*nx+i+1,2*nx+i+1),
               (i,2*nx+i,2*nx+i+1,i+1),(nx+i,nx+i+1,3*nx+i+1,3*nx+i)])
fs += [(0,nx,3*nx,2*nx),(nx-1,3*nx-1,4*nx-1,2*nx-1)]
mesh('Roof_ClosedCorrugatedSheet',vs,fs,steel,'steel')
# Folded front drip and narrow side verge caps. All have physical thickness.
prism('Roof_FrontDrip',[(-1.61,-.812,2.365),(-1.61,-.812,2.310),(-1.61,-.802,2.310),(-1.61,-.802,2.355),(-1.61,-.770,2.355),(-1.61,-.770,2.365)],(3.22,0,0),steel)
for x,tag,sx in [(-1.604,'Left',1),(1.604,'Right',-1)]:
    poly=[(x,-.804,roofz(-.804)+.004),(x+.035*sx,-.804,roofz(-.804)+.004),(x+.035*sx,-.804,roofz(-.804)-.002),(x+.006*sx,-.804,roofz(-.804)-.002),(x+.006*sx,-.804,roofz(-.804)-.052),(x,-.804,roofz(-.804)-.052)]
    prism('Roof_Verge_'+tag,poly,(0,1.528,-.1528),steel)
# Open topped U gutter, capped ends, then a true through-hole for the downpipe.
yz=[(.710,2.220),(.710,2.147),(.830,2.147),(.830,2.220),(.824,2.220),(.824,2.153),(.716,2.153),(.716,2.220)]
g=prism('Gutter_OpenTrough',[(-1.603,y,z) for y,z in yz],(3.206,0,0),steel)
for x,tag in [(-1.602,'Left'),(1.602,'Right')]:
    box('Gutter_EndCap_'+tag,(x,.770,2.1835),(.006,.124,.079),steel,.001)
bpy.ops.mesh.primitive_cylinder_add(vertices=16,radius=.024,depth=.05,location=(1.43,.77,2.15));cut=bpy.context.object
bpy.context.view_layer.objects.active=g
mod=g.modifiers.new('Actual drain opening','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cut
bpy.ops.object.modifier_apply(modifier=mod.name);bpy.data.objects.remove(cut,do_unlink=True);assign_uv(g,'steel')

def tube(n,path,r=.028,t=.004,N=12):
    points=[Vector(p) for p in path];verts=[]
    # Tube rings follow a planar bent path in YZ, with X as one radial axis.
    for rr in [r,r-t]:
        for i,p in enumerate(points):
            direction=(points[min(i+1,len(points)-1)]-points[max(0,i-1)]).normalized()
            u=Vector((1,0,0));v=direction.cross(u).normalized()
            verts +=[tuple(p+rr*(math.cos(2*math.pi*k/N)*u+math.sin(2*math.pi*k/N)*v)) for k in range(N)]
    L=len(points)*N;faces=[]
    for j in range(len(points)-1):
        for k in range(N):
            a=j*N+k;b=j*N+(k+1)%N
            faces +=[(a,b,b+N,a+N),(L+a,L+a+N,L+b+N,L+b)]
    for k in range(N):
        kk=(k+1)%N;end=(len(points)-1)*N
        faces +=[(k,L+k,L+kk,kk),(end+k,end+kk,L+end+kk,L+end+k)]
    ob=mesh(n,verts,faces,steel,'steel');
    return ob
tube('Downpipe_HollowBentOutlet',[(1.43,.77,2.154),(1.43,.77,2.05),(1.43,.77,.25),(1.43,.79,.19),(1.43,.835,.15),(1.43,.86,.125)],.024,.003,12)
tube('Downpipe_TopSocket',[(1.43,.77,2.10),(1.43,.77,2.151)],.030,.006,12)
for z,tag in [(.52,'Lower'),(1.48,'Upper')]:
    tube('PipeClamp_'+tag,[(1.43,.77,z-.012),(1.43,.77,z+.012)],.029,.004,12)
    beam('PipeClampStandoff_'+tag,(1.425,.624,z),(1.43,.745,z),.018,.018,steel,.001)
    box('PipeClampMount_'+tag,(1.425,.625,z),(.030,.012,.063),steel,.002)

# Small roof screws above supported crossmembers; every fixing rests on the roof surface.
for x in [-1.37,-.65,.065,.78,1.37]:
    for y in [-.58,.58]:
        z=roofz(y)+ribz(x)
        cylinder('RoofScrew_'+str(x)+'_'+str(y),(x,y,z-.001),(x,y,z+.005),.007,steel,8)

anchor('SeatCenter',(0,.195,.470),'Descriptive seat-top center only; engine owns occupancy/interaction.')
anchor('RoofFrontCenter',(0,-.80,2.36),'Descriptive roof sheet low surface at front edge.')
anchor('RoofRearCenter',(0,.72,roofz(.72)),'Descriptive drainage edge; no water simulation supplied.')
anchor('DrainOutlet',(1.43,.86,.125),'Descriptive open outlet center; no particle/water system supplied.')

# Freeze mesh transforms at the common placement origin; keep source parts individually editable.
for o in parts:
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
    mod=o.modifiers.new('Export triangulation','TRIANGULATE');mod.quad_method='FIXED';mod.ngon_method='BEAUTY'
    o['static_art']=True
    o.select_set(False)
bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=1
for im in bpy.data.images:
    if im.source=='FILE':im.pack();im.filepath='//maps/'+Path(im.filepath).name
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source'/'BC_SHELTER01_editable.blend'))

# Inventory precedes runtime grouping so the original closed solids stay inspectable.
rows=[];dep=bpy.context.evaluated_depsgraph_get()
for o in parts:
    ev=o.evaluated_get(dep);me=ev.to_mesh();me.calc_loop_triangles();bm=bmesh.new();bm.from_mesh(o.data)
    points=[o.matrix_world@v.co for v in o.data.vertices]
    rows.append({'name':o.name,'triangles':len(me.loop_triangles),'vertices':len(me.vertices),'material':o.data.materials[0].name,'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),'signed_volume_m3':bm.calc_volume(signed=True),'bounds_blender_xyz_m':[[min(p[i] for p in points) for i in range(3)],[max(p[i] for p in points) for i in range(3)]]})
    bm.free();ev.to_mesh_clear()
points=[o.matrix_world@v.co for o in parts for v in o.data.vertices]
lo=[min(p[i] for p in points) for i in range(3)];hi=[max(p[i] for p in points) for i in range(3)]
inventory={'asset_id':'BC_SHELTER01','builder':'Original deterministic recipe, Blender '+bpy.app.version_string,'parts':rows,'source_mesh_parts':len(parts),'triangles':sum(r['triangles'] for r in rows),'bounds_blender_xyz_m':[lo,hi],'dimensions_blender_xyz_m':[hi[i]-lo[i] for i in range(3)],'materials':3,'packed_images':len([im for im in bpy.data.images if im.packed_file]),'anchors_blender_xyz_m':{o.name:list(o.location) for o in anchors},'scope':'Builder checks only; independent audit is separate.'}
(OUT/'qa'/'builder_inventory.json').write_text(json.dumps(inventory,indent=2))

runtime=[]
for m,tag in [(paint,'FrameAndScreen'),(steel,'RoofAndHardware'),(wood,'BenchTimber')]:
    subset=[o for o in parts if o.data.materials[0]==m];copies=[]
    bpy.ops.object.select_all(action='DESELECT')
    for o in subset:
        c=o.copy();c.data=o.data.copy();bpy.context.collection.objects.link(c);c.select_set(True);copies.append(c)
    bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();o=bpy.context.object;o.name='BC_SHELTER01_'+tag;o['source_parts']=[p.name for p in subset];runtime.append(o)
bpy.ops.object.select_all(action='DESELECT');root.select_set(True)
for o in runtime+anchors:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/'runtime'/'BC_SHELTER01_static.glb'),export_format='GLB',use_selection=True,export_yup=True,export_texcoords=True,export_normals=True,export_materials='EXPORT',export_apply=True,export_extras=True)
print('SHELTER_INVENTORY',json.dumps({k:v for k,v in inventory.items() if k!='parts'}))
