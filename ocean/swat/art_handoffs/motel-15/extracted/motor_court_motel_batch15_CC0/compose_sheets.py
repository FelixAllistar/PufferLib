from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json,hashlib
ROOT=Path(__file__).resolve().parent;m=json.loads((ROOT/'manifest.json').read_text())
FONT=ImageFont.load_default(size=20);SMALL=ImageFont.load_default(size=16);TITLE=ImageFont.load_default(size=34)
W=1920;COLS=6;CW=320;RH=371;TOP=118
sheet=Image.new('RGB',(W,TOP+4*RH+44),(24,35,36));d=ImageDraw.Draw(sheet)
d.text((28,22),'BRIAR COURT | MOTOR-COURT MOTEL',font=TITLE,fill=(232,225,203));d.text((30,71),'24 ORIGINAL CC0 MODULES  /  METRES  /  13,452 TRIANGLES  /  STATIC ART',font=FONT,fill=(163,188,177))
for i,a in enumerate(m['assets']):
 x=(i%COLS)*CW;y=TOP+(i//COLS)*RH;im=Image.open(ROOT/'previews'/('%02d_%s.png'%(i+1,a['name']))).convert('RGB').resize((310,310),Image.Resampling.LANCZOS);sheet.paste(im,(x+5,y));d.text((x+12,y+317),'%02d  %s'%(i+1,a['name'].replace('_',' ')),font=SMALL,fill=(230,225,209));dims=a['dimensions_m_source_xyz'];d.text((x+12,y+341),'%.2f x %.2f x %.2f m  |  %s tris'%(*dims,a['triangles']),font=SMALL,fill=(153,179,169))
d.text((28,sheet.height-32),'Individually framed views; tile scale varies. Reused furnishings are excluded from this new-design count.',font=SMALL,fill=(180,190,181));sheet.save(ROOT/'motel_contact_sheet.jpg',quality=93,subsampling=0)
views=[('motel_courtyard.png','01  Court and repeated room fronts'),('motel_cutaway.png','02  Roofless arrangement: rooms, reception, laundry'),('motel_guest_room.png','03  Guest room: separate door, reused furniture'),('motel_reception.png','04  Reception and connected service space')]
out=Image.new('RGB',(1920,1524),(24,35,36));d=ImageDraw.Draw(out);d.text((28,20),'BRIAR COURT | LOCATION VIEWS',font=TITLE,fill=(232,225,203))
for i,(f,lbl) in enumerate(views):
 x=(i%2)*960;y=90+(i//2)*710;im=Image.open(ROOT/f).convert('RGB').resize((944,674),Image.Resampling.LANCZOS);out.paste(im,(x+8,y));d.text((x+16,y+681),lbl,font=SMALL,fill=(206,213,199))
out.save(ROOT/'motel_location_views.jpg',quality=93,subsampling=0)
recs=[]
for p in sorted(list((ROOT/'previews').glob('*.png'))+[ROOT/f for f,_ in views]+[ROOT/'motel_contact_sheet.jpg',ROOT/'motel_location_views.jpg']):
 with Image.open(p) as im:im.verify()
 with Image.open(p) as im:recs.append({'file':p.relative_to(ROOT).as_posix(),'size_px':list(im.size),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(ROOT/'qa/presentation_report.json').write_text(json.dumps({'status':'pass','images':recs,'scope':'PNG/JPEG decoding, image dimensions and content hashes. Visual review is recorded separately.'},indent=2));print('Sheets composed and',len(recs),'images decoded')
