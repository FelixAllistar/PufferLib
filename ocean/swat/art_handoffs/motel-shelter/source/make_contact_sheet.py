"""QA-only derived previews. No retouching; resize and label actual-export renders."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json,hashlib
R=Path(__file__).resolve().parent.parent
receipt=json.loads((R/'qa/render_binding.json').read_text())
runtime=R/'runtime/BC_SHELTER01_static.glb'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert receipt['runtime_sha256']==sha(runtime),'Preview receipt belongs to an older runtime'
views={v['name']:v for v in receipt['views']}
names=['front_threequarter','rear_threequarter','eye_level','front','bench_join_detail','ground_contact','roof_drain_detail','rear']
for n in names:assert sha(R/views[n]['file'])==views[n]['sha256']
W,H=1400,2432
sheet=Image.new('RGB',(W,H),(27,34,32));d=ImageDraw.Draw(sheet)
fontpath='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
try:
    title=ImageFont.truetype(fontpath,28);label=ImageFont.truetype(fontpath,20);small=ImageFont.truetype(fontpath,16)
except OSError:title=label=small=ImageFont.load_default()
d.text((24,15),'BC_SHELTER01 · roadside waiting shelter · r01',font=title,fill=(230,232,224))
d.text((24,54),'Actual runtime import · 3.220 × 1.689 × 2.379 m · 7,868 triangles · static art candidate',font=small,fill=(178,190,179))
bindings=[]
for i,n in enumerate(names):
    im=Image.open(R/views[n]['file']).convert('RGB');im=im.resize((700,550),Image.Resampling.LANCZOS)
    x=(i%2)*700;y=88+(i//2)*582;sheet.paste(im,(x,y));d.text((x+12,y+554),n.replace('_',' '),font=label,fill=(225,228,220))
    bindings.append({'source':views[n]['file'],'source_sha256':views[n]['sha256'],'cell':i})
out=R/'previews/contact_sheet.jpg';sheet.save(out,quality=93,subsampling=0)
hero=R/'previews/hero.jpg';Image.open(R/views['front_threequarter']['file']).convert('RGB').save(hero,quality=94,subsampling=0)
(R/'qa/contact_sheet_binding.json').write_text(json.dumps({'runtime_sha256':sha(runtime),'method':'Resized original rendered pixels into a labeled sheet; no retouching. Hero is a JPEG encoding of the front_threequarter render.','contact_sheet':{'file':'previews/contact_sheet.jpg','sha256':sha(out),'cells':bindings},'hero':{'file':'previews/hero.jpg','sha256':sha(hero),'source':views['front_threequarter']['file'],'source_sha256':views['front_threequarter']['sha256']}},indent=2))
