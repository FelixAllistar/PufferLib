"""Verify final inspectable raster files and native camera/view metadata."""
from pathlib import Path
import bpy,json,hashlib
ROOT=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'neighborhood_storefronts.blend'))
sc=bpy.context.scene;errors=[];rows=[]
files=[f'{i:02d}_{n}.png' for i,n in [(1,'street_front'),(2,'laundromat'),(3,'pawn_repair'),(4,'shared_service'),(5,'cutaway')]]
for name in files:
 p=ROOT/name
 if not p.is_file():errors.append('Missing '+name);continue
 im=bpy.data.images.load(str(p),check_existing=False);w,h=im.size[:];px=list(im.pixels)[::4];finite=all(__import__('math').isfinite(v) for v in px);spread=max(px)-min(px) if px else 0;okay=(w,h)==(1120,784) and finite and spread>.1
 rows.append({'file':name,'size_px':[w,h],'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'finite_pixels':finite,'red_channel_range':spread,'status':'pass' if okay else 'fail'});bpy.data.images.remove(im)
 if not okay:errors.append(name+' bad render')
cameras=[]
for ob in bpy.data.collections['03_CAMERAS_LIGHTS'].objects:
 if ob.type!='CAMERA':continue
 cameras.append({'name':ob.name,'position_source_m':list(ob.location),'type':ob.data.type,'lens_mm':ob.data.lens,'orthographic_scale_m':ob.data.ortho_scale if ob.data.type=='ORTHO' else None})
expected={f'{i:02d}_{n}':height for i,n,height in [(1,'street_front',1.72),(2,'laundromat',1.65),(3,'pawn_repair',1.65),(4,'shared_service',1.65)]}
for name,z in expected.items():
 ob=bpy.data.objects.get(name)
 if not ob or abs(ob.location.z-z)>1e-5:errors.append(name+' incorrect eye height')
if sc.render.engine!='CYCLES' or sc.render.threads!=2 or sc.cycles.device!='CPU':errors.append('CPU/two-thread render settings mismatch')
report={'status':'pass' if not errors else 'fail','errors':errors,'renderer':'Blender 4.3.2 / Cycles CPU','cpu_threads':2,'final_resolution':[1120,784],'samples_max':80,'adaptive_threshold':.03,'denoising':False,'reason_no_denoising':'Installed Blender build has no OpenImageDenoise support; final renders use unfiltered actual samples.','view_transform':sc.view_settings.view_transform,'look':sc.view_settings.look,'exposure':sc.view_settings.exposure,'new_asset_preview_count':len(list((ROOT/'previews').glob('*.png'))),'cameras':cameras,'renders':rows,'visual_review':'qa/visual_review.md','scope':'Verifies intended camera heights and actual nonempty geometry renders. Visual inspection is recorded separately; no engine screenshots or runtime validation claimed.'}
(ROOT/'qa/presentation_report.json').write_text(json.dumps(report,indent=2));print(report['status'],errors)
if errors:raise RuntimeError('; '.join(errors))
