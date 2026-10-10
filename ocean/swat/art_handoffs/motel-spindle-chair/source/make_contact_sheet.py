"""Compose the actual Blender-rendered previews with readable catalogue labels."""
from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parent
manifest=json.loads((ROOT/'manifest.json').read_text())
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf';bold='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
W=2640;M=48;G=24;CW=618;CH=738
out=Image.new('RGB',(W,2540),(39,47,43));d=ImageDraw.Draw(out)
f1=ImageFont.truetype(bold,60);f2=ImageFont.truetype(font,26);fl=ImageFont.truetype(bold,26);fs=ImageFont.truetype(font,22)
d.text((M,37),'SECONDHAND ROOMS',font=f1,fill=(232,226,207))
d.text((M,119),'BATCH 04 / 12 FURNITURE VARIANTS / ORIGINAL CC0 ART',font=f2,fill=(173,185,166))
for i,a in enumerate(manifest['assets']):
 x=M+(i%4)*(CW+G);y=188+(i//4)*(CH+G)
 d.rounded_rectangle((x,y,x+CW,y+CH),radius=10,fill=(222,224,215))
 im=Image.open(ROOT/'previews'/(a['asset_id']+'.png')).convert('RGB').resize((CW,618),Image.Resampling.LANCZOS)
 out.paste(im,(x,y))
 title=a['label'];words=title.split();lines=['']
 for w in words:
  test=(lines[-1]+' '+w).strip()
  if d.textlength(test,font=fl)>CW-34:lines.append(w)
  else:lines[-1]=test
 for j,line in enumerate(lines):d.text((x+17,y+630+j*30),line,font=fl,fill=(39,47,43))
 dims=a['dimensions_m_xyz_blender'];s=' × '.join(f'{v:.2f}' for v in dims)+' m'
 d.text((x+17,y+704),s,font=fs,fill=(70,77,64))
 t=f"{a['triangles']:,} tris";d.text((x+CW-17-d.textlength(t,font=fs),y+704),t,font=fs,fill=(70,77,64))
d.text((M,2492),f"METRES · EDITABLE BLENDER + INDIVIDUAL GLB · {manifest['total_triangles']:,} TRIANGLES TOTAL · RENDER-ONLY BY DEFAULT",font=fs,fill=(179,190,172))
out.save(ROOT/'previews'/'00_contact_sheet.png',optimize=True)
print('Saved previews/00_contact_sheet.png')
