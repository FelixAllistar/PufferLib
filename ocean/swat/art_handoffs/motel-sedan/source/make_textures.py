"""Original deterministic sedan maps. No downloaded imagery, marks or photographs."""
from pathlib import Path
import os,json
import numpy as np
from PIL import Image
R=Path(os.environ.get('SEDAN_OUT',Path(__file__).resolve().parent.parent)); D=R/'source/maps';D.mkdir(parents=True,exist_ok=True)
rng=np.random.default_rng(27183);n=512
# Subtle near-realistic desaturated green-grey paint, no baked illumination.
noise=rng.normal(0,.75,(n,n,1));base=np.array([112,129,122])[None,None,:]+noise
Image.fromarray(np.uint8(np.clip(base,0,255))).save(D/'paint_baseColor.png')
orm=np.zeros((n,n,3),np.uint8);orm[:]=[255,93,64];orm[:,:,1]=np.clip(93+rng.normal(0,1.2,(n,n)),0,255);Image.fromarray(orm).save(D/'paint_ORM.png')
# Rubber uses the same original material; tire UVs locate tread while trim uses a plain region.
y,x=np.mgrid[0:n,0:n];noise=rng.normal(0,.8,(n,n));rub=np.repeat((29+noise)[:,:,None],3,axis=2)
v=1-y/(n-1);u=x/(n-1)
tread=(v>.40)&(v<.61)
groove=((np.mod(u*42+np.abs(v-.505)*15,1)<.13)| (np.abs(v-.47)<.008)|(np.abs(v-.54)<.008))&tread
rub[groove]*=.53
Image.fromarray(np.uint8(np.clip(rub,0,255))).save(D/'rubber_baseColor.png')
# Detail atlas, UV tiles: silver, warm headlamp, amber, red, dark metal, plate blank, mirror, tail pale.
colors=[(158,165,165),(223,216,186),(184,97,30),(144,35,28),(37,42,42),(151,153,143),(98,119,124),(205,201,177), (80,87,87),(52,55,51),(125,133,128),(178,184,181),(110,116,111),(70,80,77),(65,70,67),(145,153,149)]
rough=[.31,.21,.27,.27,.52,.62,.14,.24]+[.45]*8
metal=[.8,0,0,0,.6,0,.9,0]+[.5]*8
im=np.zeros((n,n,3),np.uint8);om=np.zeros((n,n,3),np.uint8)
for k,c in enumerate(colors):
 col=k%4;row=k//4; ys=slice(n-(row+1)*128,n-row*128);xs=slice(col*128,(col+1)*128)
 tile=np.full((128,128,3),c,dtype=float)+rng.normal(0,.55,(128,128,1))
 if k in [1,2,3,7]:
  tx,ty=np.meshgrid(np.arange(128),np.arange(128));tile+=(((tx%9)<2)*-8+((ty%16)<2)*-4)[:,:,None]
 im[ys,xs]=np.uint8(np.clip(tile,0,255));om[ys,xs]=[255,int(rough[k]*255),int(metal[k]*255)]
Image.fromarray(im).save(D/'detail_baseColor.png');Image.fromarray(om).save(D/'detail_ORM.png')
(D/'texture_recipe.json').write_text(json.dumps({'seed':27183,'resolution':512,'all_original':True,'baked_lighting':False,'description':'Subtle pigment, rubber/tread pattern and neutral material/lens atlas. No logos, plate identity or downloaded imagery.'},indent=2))
print(D)
