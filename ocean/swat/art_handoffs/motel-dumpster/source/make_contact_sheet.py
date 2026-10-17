from pathlib import Path
from PIL import Image, ImageOps, ImageDraw, ImageFont
import hashlib,json
R=Path(__file__).resolve().parent.parent;V=R/'previews_final'
views=[('front_threequarter','FRONT / THREE-QUARTER'),('rear_threequarter','REAR / HINGES'),('front','FRONT / FLOOR CONTACT'),('right','RIGHT / FORK SLEEVE'),('top','TOP / CLOSED LIDS'),('underside','UNDERSIDE / SKIDS')]
W,H=720,600;canvas=Image.new('RGB',(W*2,H*3+120),(39,44,41));d=ImageDraw.Draw(canvas);f=ImageFont.load_default(size=25);small=ImageFont.load_default(size=18)
d.text((24,18),'BRIAR COURT  /  BC_DUMP01',font=f,fill=(230,232,220));d.text((24,55),'Closed-lid commercial dumpster  |  2,908 triangles  |  Static art candidate',font=small,fill=(190,198,184))
for i,(n,label) in enumerate(views):
 im=Image.open(V/(n+'.png')).convert('RGB');im=ImageOps.contain(im,(W,H-40),Image.Resampling.LANCZOS);x=(i%2)*W+(W-im.width)//2;y=90+(i//2)*H;canvas.paste(im,(x,y));d.text(((i%2)*W+18,y+H-30),label,font=small,fill=(220,225,211))
canvas.save(V/'contact_sheet.jpg',quality=92,subsampling=0)
Image.open(V/'front_threequarter.png').convert('RGB').save(V/'hero.jpg',quality=94,subsampling=0)
receipt={'method':'Contact sheet and JPEG hero are resized/reencoded derivatives of final PNG renders, with no generated replacement geometry.','runtime_sha256':hashlib.sha256((R/'runtime/BC_DUMP01_closed.glb').read_bytes()).hexdigest(),'inputs':{n+'.png':hashlib.sha256((V/(n+'.png')).read_bytes()).hexdigest() for n,_ in views},'outputs':{n:hashlib.sha256((V/n).read_bytes()).hexdigest() for n in ['contact_sheet.jpg','hero.jpg']}}
(R/'qa/contact_sheet_binding.json').write_text(json.dumps(receipt,indent=2))
