from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
O=Path(__file__).resolve().parent;R=O.parent
f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',20);s=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',16)
w=1600;size=400;top=80;lh=32;footer=66
im=Image.new('RGB',(w,top+2*(size+lh)+footer),(32,37,45));d=ImageDraw.Draw(im)
d.text((15,10),'FROZEN GRIP DIAGNOSTIC C1 | index-base crease restored; thumb wrap unresolved',font=f,fill=(240,220,185));d.text((15,41),'Same rifle and matched cameras. Final thumb/finger tuning moves to the in-game ADS view; no automatic pose replacement.',font=s,fill='white')
for row,(sub,tag) in enumerate([('source_inspection/F','Original N / gear F'),('candidate_c1/views','Candidate C1')]):
 for col,(name,title) in enumerate([('left_inner','finger wrap'),('left_below','underside'),('left_outer','palm and wrist'),('near_visor_inspection','uncalibrated near-visor')]):
  a=Image.open(R/sub/(name+'.png')).convert('RGB').resize((size,size));x=col*size;y=top+row*(size+lh);im.paste(a,(x,y));d.text((x+9,y+size+6),tag+' / '+title,font=s,fill='white')
y=top+2*(size+lh);d.text((15,y+9),'Central palm gap: 10.56 to 5.55 mm. Sampled overlap: 0.538 to 0.825 mm; none above 1 mm.',font=s,fill='white');d.text((15,y+34),'Index pad sits about 2 mm clear. Target crease returns near original; native folds remain. No motion or ADS-fit approval.',font=s,fill=(240,220,185));im.save(O/'support-grip-c1-matched-comparison.jpg',quality=94)
