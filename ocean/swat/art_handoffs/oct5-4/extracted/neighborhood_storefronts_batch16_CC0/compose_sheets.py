"""Labelled contact sheet and location-view sheet from actual Blender renders."""
from pathlib import Path
import json,sys
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parent
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
def f(n):return ImageFont.truetype(font,n)
m=json.loads((ROOT/'manifest.json').read_text())
w=400;h=422;pad=26;header=110
im=Image.new('RGB',(4*w+pad*2,6*h+header+pad),(21,27,31));d=ImageDraw.Draw(im)
d.text((pad,24),'MORROW BLOCK / MODULAR STOREFRONTS',font=f(32),fill='#e9e4d8');d.text((pad,72),'24 original modules  |  metre scale  |  separate doors, openings and shop fittings',font=f(20),fill='#99adb0')
for i,a in enumerate(m['assets']):
 tile=Image.open(ROOT/'previews'/('%02d_%s.png'%(i+1,a['name']))).convert('RGB').resize((380,342));x=pad+(i%4)*w;y=header+(i//4)*h;im.paste(tile,(x,y));d.text((x,y+349),f"{i+1:02d}  {a['title']}",font=f(17),fill='#e9e4d8');dims=a['dimensions_m_source_xyz'];d.text((x,y+380),' × '.join(f'{v:.2f}' for v in dims)+' m  /  '+str(a['triangles'])+' tris',font=f(15),fill='#99adb0')
im.save(ROOT/'storefronts_contact_sheet.jpg',quality=93)
if '--contact-only' in sys.argv:raise SystemExit(0)
views=[('01_street_front.png','01 / STREET FRONT · eye height 1.72 m'),('02_laundromat.png','02 / LAUNDROMAT · eye height 1.65 m'),('03_pawn_repair.png','03 / PAWN & REPAIR · eye height 1.65 m'),('04_shared_service.png','04 / SHARED SERVICE · eye height 1.65 m')]
im=Image.new('RGB',(1652,1330),(21,27,31));d=ImageDraw.Draw(im);d.text((26,20),'MORROW BLOCK / PLAYER-HEIGHT LOCATION VIEWS',font=f(29),fill='#e9e4d8');d.text((26,65),'Original two-unit plan · subdued materials · reused laundry, household and workshop props',font=f(18),fill='#99adb0')
for i,(file,title) in enumerate(views):
 x=26+(i%2)*813;y=110+(i//2)*610;im.paste(Image.open(ROOT/file).convert('RGB').resize((787,551)),(x,y));d.text((x,y+563),title,font=f(19),fill='#e9e4d8')
im.save(ROOT/'storefronts_location_views.jpg',quality=94)
print('SHEETS_COMPLETE')
