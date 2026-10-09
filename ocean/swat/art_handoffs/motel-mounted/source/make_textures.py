"""Original seeded zinc/oxide PBR atlas. No external images or generative imports."""
from pathlib import Path
import numpy as np
from PIL import Image,ImageFilter,ImageDraw
from scipy.spatial import cKDTree
R=Path(__file__).resolve().parent/'textures'; R.mkdir(exist_ok=True)
rng=np.random.default_rng(4817); N=1024
base=np.zeros((N,N,3),np.uint8); orm=np.zeros_like(base); heights=np.zeros((N,N),float)
for k,(col,rough,metal) in enumerate([((122,130,127),.60,.85),((151,155,149),.46,.92),((39,43,40),.88,.0),((167,164,146),.40,.88)]):
 y=(k//2)*512;x=(k%2)*512
 small=Image.fromarray(rng.integers(0,256,(48,48),dtype=np.uint8)).resize((512,512),Image.Resampling.BILINEAR).filter(ImageFilter.GaussianBlur(2))
 mott=(np.asarray(small).astype(float)-128)/128
 grain=rng.normal(0,1,(512,512)); yy,xx=np.mgrid[:512,:512]; edge=np.exp(-np.minimum.reduce([xx,yy,511-xx,511-yy])/10)
 stains=np.asarray(Image.fromarray(rng.integers(0,256,(14,14),dtype=np.uint8)).resize((512,512),Image.Resampling.BICUBIC),float)/255
 oxide=np.maximum(0,stains-.6)*edge*2.5 if k!=2 else np.zeros((512,512))
 seeds=rng.uniform(0,512,(1800,2)); idx=cKDTree(seeds).query(np.column_stack((xx.ravel(),yy.ravel())))[1].reshape(512,512)
 spangle=rng.normal(0,1,1800)[idx]
 c=np.array(col)[None,None,:]+mott[:,:,None]*5+spangle[:,:,None]*3.5+grain[:,:,None]*.7-edge[:,:,None]*8
 c+=oxide[:,:,None]*np.array([10,-32,-39]);base[y:y+512,x:x+512]=np.clip(c,0,255)
 orm[y:y+512,x:x+512,0]=255
 orm[y:y+512,x:x+512,1]=np.clip(255*(rough+mott*.025+oxide*.14),0,255)
 orm[y:y+512,x:x+512,2]=np.clip(255*(metal-oxide*.2),0,255)
 heights[y:y+512,x:x+512]=spangle*.00005+grain*.000012
# Original fine handling scratches, concentrated around cover edges; no text or symbols.
img=Image.fromarray(base); draw=ImageDraw.Draw(img)
for k in [0,1,3]:
 ox=(k%2)*512;oy=(k//2)*512
 for i in range(95):
  x=int(rng.integers(10,495));y=int(rng.integers(10,495));length=int(rng.integers(2,18));slope=int(rng.integers(-3,4))
  sample=base[oy+y,ox+x].astype(int);col=tuple(np.clip(sample+rng.choice([-11,9]),0,255))
  draw.line((ox+x,oy+y,ox+min(502,x+length),oy+y+slope),fill=col,width=1)
base=np.asarray(img)
# Very restrained micro-normal, no cartoon bumps.
dy,dx=np.gradient(heights); normals=np.dstack((-dx*65,dy*65,np.ones((N,N)))); normals/=np.linalg.norm(normals,axis=2)[:,:,None]
for name,a in [('basecolor',base),('orm',orm),('normal',np.clip((normals*.5+.5)*255,0,255).astype('uint8'))]:Image.fromarray(a).save(R/f'junction_{name}.png')
