"""Original deterministic 512px paint/rubber/steel/liner PBR atlas; no external inputs."""
from PIL import Image,ImageDraw
from pathlib import Path
import random
r=random.Random(410923);im=Image.new('RGB',(512,512));orm=Image.new('RGB',(512,512));p=im.load();q=orm.load()
colors=[(76,103,103),(37,39,36),(141,145,139),(72,77,66)]
for y in range(512):
 for x in range(512):
  k=y//256*2+x//256;n=r.gauss(0,1.4)
  if k==3:n+=(2 if (x+y)%5==0 else -1)
  p[x,y]=tuple(max(0,min(255,int(v+n)))for v in colors[k]);q[x,y]=(255,[178,212,120,218][k]+r.randrange(-3,4),[30,0,210,0][k])
d=ImageDraw.Draw(im,'RGBA')
for k in range(4):
 ox=k%2*256;oy=k//2*256
 for i in range(35):
  x=ox+r.randrange(15,240);y=oy+r.randrange(15,240)
  d.line((x,y,min(ox+240,x+r.randrange(2,12)),y+1),fill=(170,174,157,r.randrange(9,28)),width=1)
 if k==0:
  for i in range(30):
   x=ox+r.choice([r.randrange(12,20),r.randrange(236,244)]);y=oy+r.randrange(12,240)
   d.line((x,y,x+2,y+r.randrange(1,4)),fill=(54,58,51,90),width=1)
root=Path(__file__).resolve().parent;im.save(root/'trolley_atlas_512.png');orm.save(root/'trolley_orm_512.png')
