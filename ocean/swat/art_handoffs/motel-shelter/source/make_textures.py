"""Seeded original pigment/weathering artwork, with no photographic inputs or lighting.
Python 3 + Pillow + NumPy. SHELTER_OUT redirects newly generated maps.
The inherited steel.png is a verified immutable input, not regenerated here.
"""
from pathlib import Path
import os, random
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent
OUT = Path(os.environ.get('SHELTER_OUT', ROOT.parent)) / 'source' / 'maps'
OUT.mkdir(parents=True, exist_ok=True)
N = 512
paint = Image.new('RGB', (1024, 1024))
orm = Image.new('RGB', (1024, 1024), (255, 214, 5))
for q in range(4):
    r = random.Random(17102640 + q)
    rng = np.random.default_rng(17102640 + q)
    low = np.array(Image.fromarray(rng.integers(95, 162, (24, 24), dtype=np.uint8)).resize((N,N), Image.Resampling.BICUBIC)).astype(float) - 128
    c = np.array([64, 77, 68]) + np.array([q*.7, q*.8, q*.6])
    arr = np.clip(c + low[:,:,None]*.13 + rng.normal(0, 1.2, (N,N,1)), 0, 255).astype(np.uint8)
    im = Image.fromarray(arr); d = ImageDraw.Draw(im)
    oc = Image.new('RGB', (N,N), (255, 214, 5)); od = ImageDraw.Draw(oc)
    # Rear-screen cells 0/1: weathered seams and lower hem; frame cell 2, bases cell 3.
    for i in range(250 if q < 2 else 130):
        if i % 4 == 0:
            x = r.choice([r.randrange(3,12),r.randrange(N-12,N-3)]); y = r.randrange(10,N-5)
        else:
            x = r.randrange(4,N-5); y = r.choice([r.randrange(2,11),r.randrange(N-35,N-4)])
        w=r.randrange(2,13); h=r.randrange(1,4)
        poly=[(x,y),(x+w,y-1),(x+w-2,y+h),(x+1,y+h+1)]
        col=r.choice([(95,69,47),(88,77,61),(47,52,46),(116,108,88)])
        d.polygon(poly, fill=col); od.polygon(poly, fill=(255, 226 if col[0]<100 else 165, 25 if col[0]<100 else 135))
    # Short, sparse seam runoff. No directional light, shadows, or ambient occlusion.
    for i in range(13):
        x=r.choice([r.randrange(7,40),r.randrange(N-40,N-7),r.randrange(30,N-30)])
        y=r.randrange(2,40); length=r.randrange(18,110)
        for k in range(length):
            a=.15*(1-k/length); p=im.getpixel((x,y+k))
            im.putpixel((x,y+k), tuple(int(v*(1-a)+t*a) for v,t in zip(p,(78,64,43))))
    paint.paste(im, ((q%2)*N,(q//2)*N)); orm.paste(oc, ((q%2)*N,(q//2)*N))
paint.save(OUT/'shelter_paint_atlas.png', compress_level=9)
orm.save(OUT/'shelter_paint_orm.png', compress_level=9)

# Eight wide plank cells. Grain runs across the board length (U), never across its width.
W,H=1024,1024
wood=np.zeros((H,W,3),dtype=np.uint8)
rough=np.zeros((H,W),dtype=np.uint8)
for b in range(8):
    rng=np.random.default_rng(171030+b); r=random.Random(171030+b)
    yy,xx=np.mgrid[0:128,0:1024]; u=xx/1024;v=yy/128
    bend=.018*np.sin(u*9+b)+.010*np.sin(u*23+b*.7)
    grain=np.sin((v+bend)*180+1.7*np.sin(u*13))*.8 + np.sin((v+bend)*425 + np.sin(u*19))*.6
    fine=rng.normal(0,.85,(128,1024)); fibers=np.array(Image.fromarray(rng.integers(80,178,(128,48),dtype=np.uint8)).resize((1024,128),Image.Resampling.BICUBIC)).astype(float)-128
    tone=np.array([139,128,104])+np.array([b%3*2,b%3*1.6,b%3])
    data=tone + grain[:,:,None]*2 + fibers[:,:,None]*.19 + fine[:,:,None]
    # Narrow weathered lines and two irregular knots per plank, without baked highlights.
    for k in range(2):
        cx=r.uniform(.15,.85);cy=r.uniform(.2,.8)
        rr=np.sqrt(((u-cx)*5.5)**2+((v-cy)*.72)**2)
        mask=np.exp(-rr*rr*110)
        ring=np.sin(rr*180)*mask*5
        data += ring[:,:,None]-mask[:,:,None]*5
    data=np.clip(data,0,255).astype(np.uint8)
    im=Image.fromarray(data);d=ImageDraw.Draw(im)
    for k in range(32):
        x=r.randrange(8,990); y=r.randrange(8,120);length=r.randrange(8,80)
        if k<5:x=r.choice([r.randrange(1,25),r.randrange(950,1020)]);length=r.randrange(10,35)
        d.line((x,y,x+length,y+r.choice([-1,0,1])), fill=(112+r.randrange(14),105+r.randrange(10),86+r.randrange(10)),width=1)
    wood[b*128:(b+1)*128]=np.array(im)
    rough[b*128:(b+1)*128]=np.clip(215+fibers*.13+fine*2,192,238).astype(np.uint8)
Image.fromarray(wood).save(OUT/'shelter_timber_atlas.png',compress_level=9)
Image.fromarray(rough).convert('RGB').save(OUT/'shelter_timber_roughness.png',compress_level=9)
print('Generated four original seeded maps; inherited steel.png unchanged.')
