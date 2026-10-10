"""Seeded original cork, worn wood and fictional notices. Raster lettering, no text mesh."""
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFont
R=Path(__file__).resolve().parent;rng=np.random.default_rng(271828);S=1024
ar=np.zeros((S,S,3),dtype=np.uint8);ar[:]=[96,72,47]
# Cork has irregular small agglomerated flakes, not a regular checker pattern.
a=np.clip(rng.normal(0,7,(512,512,1))+np.array([151,112,69]),0,255).astype('uint8');im=Image.fromarray(a);d=ImageDraw.Draw(im)
for i in range(15500):
 x,y=rng.integers(0,512,2);r=int(rng.integers(1,4));v=int(rng.integers(-29,25));col=tuple(int(np.clip(c+v,0,255)) for c in [151,112,69]);d.polygon([(x,y),(x+r,y-1),(x+r+1,y+r),(x-1,y+r+1)],fill=col)
ar[:512,:512]=np.array(im)
# Faint ghosts from older notices; feathered edges, original cork grain retained.
Y,X=np.mgrid[0:512,0:512]
for x0,y0,x1,y1,amount in [(31,42,283,436,8.5),(317,149,479,435,-6.0),(89,441,250,492,6.0)]:
 edge=np.minimum.reduce([X-x0,x1-X,Y-y0,y1-Y]);mask=np.clip((edge+2)/5,0,1)
 ar[:512,:512]=np.clip(ar[:512,:512].astype(float)+mask[:,:,None]*amount,0,255).astype('uint8')
# Sparse old punctures only in exposed cork, with tiny pale compressed rims.
im=Image.fromarray(ar[:512,:512]);d=ImageDraw.Draw(im)
for x,y in [(43,45),(277,46),(322,150),(476,151),(476,320),(90,445),(248,447),(250,486),(60,465),(293,245),(478,438)]:
 d.ellipse((x-2,y-2,x+2,y+2),outline=(164,126,79));d.ellipse((x-1,y-1,x+1,y+1),fill=(88,65,40));d.point((x,y+1),fill=(107,80,48))
ar[:512,:512]=np.array(im)
for y in range(256):
 grain=7*np.sin(y*.9)+4*np.sin(y*.13)
 ar[y,512:]=np.clip(np.array([85,61,39])+grain+rng.normal(0,2,(512,1)),0,255)
ar[256:384,512:]=np.clip(np.array([86,72,55])+rng.normal(0,4,(128,512,1)),0,255)
ar[384:512,512:]=[111,58,44]
im=Image.fromarray(ar);d=ImageDraw.Draw(im)
for i in range(200):
 x=int(rng.integers(514,1000));y=int(rng.integers(3,253));d.line((x,y,min(1020,x+int(rng.integers(2,35))),y),fill=(116,88,57),width=1)
# Longitudinal end wear: lower atlas strip serves the bottom rail only.
a=np.array(im).astype(float);Y,X=np.mgrid[0:256,0:512]
ends=np.exp(-((X-12)/24)**2)+np.exp(-((X-502)/27)**2)
lower=np.clip((Y-128)/26,0,1);wear=ends*(.15+.85*lower)
# Dark hand oils near ends, with thin rubbed high points on the lower rail.
a[:256,512:]*=(1-.23*wear[:,:,None]);rub=ends*np.exp(-((Y-237)/7)**2)
a[:256,512:]+=rub[:,:,None]*np.array([23,20,15]);im=Image.fromarray(np.clip(a,0,255).astype('uint8'))
def paper(rect,color):
 x0,y0,x1,y1=rect
 tex=np.clip(np.array(color)+rng.normal(0,1.7,(y1-y0,x1-x0,1)),0,255).astype('uint8');im.paste(Image.fromarray(tex),(x0,y0));dd=ImageDraw.Draw(im)
 for inset in range(5):dd.rectangle((x0+inset,y0+inset,x1-inset-1,y1-inset-1),outline=tuple(c-18+inset*3 for c in color))
 return dd
font=lambda n,b=False:ImageFont.truetype(str(R/'fonts'/('DejaVuSerif-Bold.ttf' if b else 'DejaVuSans.ttf')),n)
def center(t,y,n,rect,b=False,col=(55,59,51)):
 dd=ImageDraw.Draw(im);x0,_,x1,_=rect;w=dd.textbbox((0,0),t,font=font(n,b))[2];dd.text(((x0+x1-w)/2,y),t,font=font(n,b),fill=col)
r=(0,512,640,1024);d=paper(r,(224,218,193))
center('BRIAR COURT',548,49,r,True);center('GUEST INFORMATION',621,27,r);d.line((48,673,592,673),fill=(95,107,85),width=2)
for t,y,n in [('Welcome. Make yourself at home.',703,23),('CHECK-OUT  •  11 AM',766,29),('Please return your room key',819,24),('to the reception desk.',854,24),('For fresh towels or local directions,',927,21),('ask at the desk. We are happy to help.',961,21)]:center(t,y,n,r)
r=(640,512,1024,832);d=paper(r,(214,224,209));center('QUIET HOURS',552,29,r,True);center('10 PM – 7 AM',603,31,r);d.line((676,658,988,658),fill=(103,116,97),width=2)
for t,y in [('Please keep voices low',684),('along the rooms.',713),('Thank you for being',756),('a thoughtful neighbor.',782)]:center(t,y,20,r)
r=(640,832,1024,1024);d=paper(r,(231,213,173));center('MORNING COFFEE',859,25,r,True);center('At reception • 7–10 AM',906,23,r);center('Take a moment. Stay awhile.',959,20,r)
# Restrained age: one shallow fold per larger sheet and corner foxing, never over print.
d=ImageDraw.Draw(im)
d.line((17,744,36,750,602,751,626,747),fill=(211,206,183),width=1)
d.line((20,746,601,753),fill=(231,226,202),width=1)
d.line((658,652,1009,650),fill=(200,210,195),width=1)
for box,col in [((3,515,637,1021),(190,180,152)),((643,515,1021,829),(188,201,180)),((643,835,1021,1021),(202,181,140))]:
 x0,y0,x1,y1=box
 for i in range(65):
  x=int(rng.integers(x0+2,x1-2));y=int(rng.integers(y0+3,y0+15)) if i%2 else int(rng.integers(y1-15,y1-2));d.point((x,y),fill=col)
# Uneven narrow edge oxidation and a few tiny rub marks; all outside lettering.
a=np.array(im).astype(float)
for box in [(0,512,640,1024),(640,512,1024,832),(640,832,1024,1024)]:
 x0,y0,x1,y1=box;yy,xx=np.mgrid[0:y1-y0,0:x1-x0];dist=np.minimum.reduce([xx,yy,x1-x0-1-xx,y1-y0-1-yy]);edge=np.exp(-dist/4.2);uneven=.6+.25*np.sin(xx*.039+yy*.027)+.15*np.sin(xx*.103-yy*.073)
 a[y0:y1,x0:x1]-=(edge*uneven)[:,:,None]*np.array([13,17,23])
im=Image.fromarray(np.clip(a,0,255).astype('uint8'));d=ImageDraw.Draw(im)
for x,y in [(4,570),(11,1015),(612,1018),(637,786),(644,724),(1018,820),(991,1020),(646,976)]:
 d.line((x,y,x+int(rng.integers(2,7)),y+1),fill=(175,163,133),width=1)
im.save(R/'noticeboard_basecolor_1024.png')
# Rough dielectric surfaces only: AO white, roughness varies, metallic black.
orm=np.zeros((1024,1024,3),dtype=np.uint8);orm[:,:,0]=255;orm[:,:,1]=220;orm[:512,:512,1]=239;orm[:256,512:,1]=196;orm[384:512,512:,1]=158
Image.fromarray(orm).save(R/'noticeboard_orm_1024.png')
