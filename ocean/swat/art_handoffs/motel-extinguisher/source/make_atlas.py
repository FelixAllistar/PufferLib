"""Original procedural PBR textures and fictional non-certified decorative label; CC0."""
from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
import random,math
r=random.Random(20261009);im=Image.new('RGB',(512,512));orm=Image.new('RGB',(512,512));p=im.load();q=orm.load()
colors=[(158,34,25),(31,32,29),(113,113,101),(211,207,186)]
for y in range(512):
 for x in range(512):
  k=y//256*2+x//256;n=r.gauss(0,1.1)
  p[x,y]=tuple(max(0,min(255,int(v+n)))for v in colors[k]);q[x,y]=(255,[151,211,124,210][k]+r.randrange(-2,3),[20,0,220,0][k])
d=ImageDraw.Draw(im,'RGBA')
for k in range(3):
 ox=k%2*256;oy=k//2*256
 for i in range(65):
  x=ox+r.randrange(12,244);y=oy+r.randrange(12,244)
  d.line((x,y,x+r.randrange(2,9),y+1),fill=(187,173,150,r.randrange(8,30)),width=1)
# Decorative original label and gauge; no safety certification or usable operating instructions.
d=ImageDraw.Draw(im)
d.ellipse((328,72,440,184),fill=(224,220,197),outline=(30,34,30),width=4)
for a in range(30,331,20):
 t=math.radians(a);cx,cy=384,128;d.line((cx+44*math.cos(t),cy+44*math.sin(t),cx+49*math.cos(t),cy+49*math.sin(t)),fill=(35,38,32),width=2)
d.arc((344,88,424,168),210,280,fill=(45,112,61),width=7);d.line((384,128,372,94),fill=(32,35,28),width=3);d.ellipse((380,124,388,132),fill=(38,38,32))
font='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
f=ImageFont.truetype(font,32);f2=ImageFont.truetype(font,18);small=ImageFont.truetype(font,11)
d.rectangle((264,264,504,504),fill=(212,207,186));d.rectangle((268,268,501,309),fill=(143,27,22));d.text((280,269),'FIRE',font=f,fill=(240,233,211));d.text((276,316),'EXTINGUISHER',font=f2,fill=(28,32,27))
for i in range(3):
 y=350+i*46;d.rectangle((277,y,323,y+34),outline=(44,48,39),width=2);d.text((281,y+3),str(i+1),font=f2,fill=(44,48,39));d.line((295,y+27,316,y+9),fill=(44,48,39),width=2)
 for j in range(3):d.line((336,y+6+j*9,480-j*9,y+6+j*9),fill=(92,94,80),width=2)
d.text((277,490),'SERVICE EQUIPMENT',font=small,fill=(51,55,44))
root=Path(__file__).resolve().parent;im.save(root/'extinguisher_atlas_512.png');orm.save(root/'extinguisher_orm_512.png')
