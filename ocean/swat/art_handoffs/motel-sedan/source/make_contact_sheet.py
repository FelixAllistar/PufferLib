"""Assemble unretouched runtime renders into a labeled QA contact sheet."""
from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
import json,hashlib,os
R=Path(os.environ.get('SEDAN_OUT',Path(__file__).resolve().parent.parent));P=R/'previews'
views=[('front_threequarter','Front three-quarter'),('rear_threequarter','Rear three-quarter'),('side','Side'),('front','Front'),('rear','Rear'),('eye_level','Eye level'),('underside','Underside'),('wheel_contact','Wheel / ground contact'),('top','Top')]
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf';f=ImageFont.truetype(font,18);h=ImageFont.truetype(font,25)
W=1800;cw=600;ch=455;im=Image.new('RGB',(W,70+ch*3),(29,34,35));d=ImageDraw.Draw(im);d.text((22,14),'BC_SEDAN01  |  Original static compact sedan  |  Actual runtime export',fill=(234,237,232),font=h)
bind=json.loads((R/'qa/render_binding.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();records=[]
for i,(name,label) in enumerate(views):
 p=P/(name+'.png');b=next(v for v in bind['views'] if v['name']==name);assert sha(p)==b['sha256']
 img=Image.open(p).convert('RGB');img.thumbnail((600,420));x=(i%3)*cw;y=70+(i//3)*ch;im.paste(img,(x+(cw-img.width)//2,y));d.text((x+16,y+425),label,fill=(228,233,230),font=f);records.append({'file':'previews/'+name+'.png','sha256':sha(p)})
p=P/'contact_sheet.jpg';im.save(p,quality=92,subsampling=0)
(R/'qa/contact_sheet_binding.json').write_text(json.dumps({'runtime_sha256':bind['runtime_sha256'],'input_views':records,'contact_sheet':'previews/contact_sheet.jpg','sha256':sha(p),'method':'Resize and label only, with no retouching or generated embellishment'},indent=2))
