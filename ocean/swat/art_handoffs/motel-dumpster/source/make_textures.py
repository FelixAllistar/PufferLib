"""Original seeded procedural paint and original fictional pixel-letter sign.
Run under Python with numpy/Pillow. No photographic inputs or baked illumination.
Exact inherited steel/rubber maps are carried unmodified beside these outputs.
"""
from PIL import Image, ImageDraw, ImageFilter
from pathlib import Path
import numpy as np, random, os
ROOT=Path(__file__).resolve().parent
OUT=Path(os.environ.get('DUMP_OUT', ROOT.parent))/'source'/'maps';OUT.mkdir(parents=True,exist_ok=True)
rng=np.random.default_rng(171026);N=256
atlas=Image.new('RGB',(512,512))
for q in range(4):
 rr=random.Random(171026+q); fine=rng.normal(0,1.6,(N,N,1));
 low=np.array(Image.fromarray(rng.integers(100,155,(16,16),dtype=np.uint8)).resize((N,N),Image.Resampling.BICUBIC)).astype(float)-128
 c=np.array([66,85,70])+np.array([q*1.5,q*1.0,q*.5])
 data=np.clip(c+fine+low[:,:,None]*.16,0,255).astype(np.uint8)
 im=Image.fromarray(data);d=ImageDraw.Draw(im)
 # Steel-rub chips concentrated on lower hem and exposed side seams, not uniform dirt.
 for i in range(190):
  x=rr.randrange(N); y=rr.choice([rr.randrange(235,254),rr.randrange(2,8),rr.randrange(210,254)])
  if i%5==0:x=rr.choice([rr.randrange(2,9),rr.randrange(247,254)]);y=rr.randrange(N)
  w=rr.randrange(1,12);h=rr.randrange(1,4);col=rr.choice([(89,61,39),(102,70,43),(45,48,41),(116,104,81)])
  d.polygon([(x,y),(x+w,y-1),(x+w-2,y+h),(x+1,y+h+1)],fill=col)
 # Thin runoff traces and shallow broad scuffs, asymmetric across panels.
 for i in range(10):
  x=rr.randrange(8,248);y=rr.randrange(10,65);l=rr.randrange(10,50)
  for k in range(l):
   a=(1-k/l)*.24;pix=im.getpixel((x,y+k));im.putpixel((x,y+k),tuple(int(v*(1-a)+t*a) for v,t in zip(pix,(75,55,39))))
 for i in range(32):
  x=rr.randrange(15,230);y=rr.randrange(180,242);d.line((x,y,x+rr.randrange(3,20),y+rr.randrange(-2,3)),fill=(85,92,76),width=1)
 atlas.paste(im,((q%2)*N,(q//2)*N))
atlas.save(OUT/'dumpster_paint_atlas.png',compress_level=9)
# Four original sign cells; no real company names or manufacturer's artwork.
F={
'A':['01110','10001','10001','11111','10001','10001','10001'],
'B':['11110','10001','10001','11110','10001','10001','11110'],
'C':['01111','10000','10000','10000','10000','10000','01111'],
'D':['11110','10001','10001','10001','10001','10001','11110'],
'E':['11111','10000','10000','11110','10000','10000','11111'],
'F':['11111','10000','10000','11110','10000','10000','10000'],
'I':['11111','00100','00100','00100','00100','00100','11111'],
'K':['10001','10010','10100','11000','10100','10010','10001'],
'L':['10000','10000','10000','10000','10000','10000','11111'],
'N':['10001','11001','11001','10101','10011','10011','10001'],
'O':['01110','10001','10001','10001','10001','10001','01110'],
'P':['11110','10001','10001','11110','10000','10000','10000'],
'R':['11110','10001','10001','11110','10100','10010','10001'],
'S':['01111','10000','10000','01110','00001','00001','11110'],
'T':['11111','00100','00100','00100','00100','00100','00100'],
'U':['10001','10001','10001','10001','10001','10001','01110'],
'Y':['10001','10001','01010','00100','00100','00100','00100'],
'0':['01110','10001','10011','10101','11001','10001','01110'],
'4':['00010','00110','01010','10010','11111','00010','00010'],
' ':['00000']*7}
im=Image.new('RGB',(256,256),(188,182,149));dr=ImageDraw.Draw(im)
def txt(t,y,s=3):
 w=len(t)*6*s-s;x=(256-w)//2
 for ch in t:
  for j,row in enumerate(F[ch]):
   for i,b in enumerate(row):
    if b=='1':dr.rectangle((x+i*s,y+j*s,x+(i+1)*s-1,y+(j+1)*s-1),fill=(40,51,43))
  x+=6*s
# Upper half main plaque, lower left asset stencil, lower right caution chevron.
dr.rectangle((3,3,252,125),outline=(56,67,51),width=4)
txt('BRIAR COURT',17,3);txt('REFUSE',58,4);txt('BC 04',100,2)
dr.rectangle((0,128,127,255),fill=(58,77,63));dr.rectangle((128,128,255,255),fill=(189,154,70))
for k in range(-100,180,32):dr.polygon([(128+k,128),(144+k,128),(255,239-k),(255,255-k)],fill=(50,50,39))
# Original label remains readable but has a few chipped areas.
r=random.Random(172)
for i in range(110):
 x=r.randrange(256);y=r.randrange(126)
 if y<8 or y>118 or x<8 or x>247:dr.rectangle((x,y,x+r.randrange(1,4),y+1),fill=(75,83,65))
im.save(OUT/'dumpster_label_atlas.png',compress_level=9)
print('Wrote original paint 512px and label 256px atlases')
