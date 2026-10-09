#!/usr/bin/env python3
"""Original CC0 zoning-mask generator. NumPy, Pillow, matplotlib. No geometry writes.
Run from any directory: python source/build_mask.py. Inputs ship in source/.
"""
import json, hashlib, pathlib
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle,Circle
R=pathlib.Path(__file__).resolve().parents[1]
P=json.loads((R/'source/composition_proposal.json').read_text()); M=json.loads((R/'source/frozen_placement_manifest.json').read_text())
N=512; DX=128/N; DZ=100/N
X,Z=np.meshgrid(-64+(np.arange(N)+.5)*DX,-48+(np.arange(N)+.5)*DZ)
def rect(x,z,b,pad=0):
 a,c,d,e=b;return (x>=a-pad)&(x<=c+pad)&(z>=d-pad)&(z<=e+pad)
def poly_distance(x,z,poly):
 inside=np.zeros(np.broadcast(x,z).shape,bool);dist=np.full(inside.shape,np.inf)
 for a,b in zip(poly,poly[1:]+poly[:1]):
  ax,az=a;bx,bz=b;vx=bx-ax;vz=bz-az
  t=np.clip(((x-ax)*vx+(z-az)*vz)/(vx*vx+vz*vz),0,1)
  dist=np.minimum(dist,np.hypot(x-ax-t*vx,z-az-t*vz))
  if bz!=az:inside^=((az>z)!=(bz>z))&(x<(bx-ax)*(z-az)/(bz-az)+ax)
 return np.where(inside,0,dist)
EL=[r for r in M['support_matrix'] if r['region']=='ground' and len(set(r['top_y_corners']))==1 and r['material']=='soil']
FORB=[dict(id='court',xz=[-24,24,-24,24]),dict(id='protected',xz=[-3,3,5,48])]
FORB += [dict(id=r['semantic_name'],xz=r['xz']) for r in M['support_matrix'] if r not in EL]
FORB += [dict(id='preserved_shoulder_bank_'+str(i),xz=b) for i,b in enumerate(M['preserved_shoulder_cuts_xz'])]
def eligible(x,z,pad=0):
 ok=np.zeros(np.broadcast(x,z).shape,bool)
 for r in EL:ok|=rect(x,z,r['xz'])
 for f in FORB:ok&=~rect(x,z,f['xz'],pad)
 for a,b in [[12.5,0],[12.5,6]]:ok&=np.hypot(x-a,z-b)>2+pad
 return ok
# One full diagonal texel guard guarantees LOD0 bilinear filter support cannot leak across exclusions.
GUARD=float(np.hypot(DX,DZ))
mask=np.zeros_like(X)
for zone in P['phase1']['zones']:
 d=poly_distance(X,Z,zone['polygon']);w=zone['dirt_core_weight']*np.clip(1-d/6,0,1)
 if zone['id']=='F':w=np.where(Z>=46+DZ,w,0) # conservative one-row filtering guard at explicit clipped far-verge boundary
 mask=np.maximum(mask,w)
mask=np.where(eligible(X,Z,GUARD),mask,0)
raw=np.floor(mask*255+.5).astype(np.uint8);Image.fromarray(raw,'L').save(R/'runtime/zoning_mask.png')
SUP=[dict(index=i,owner_inherited=1119+i,semantic_name=r['semantic_name'],xz=r['xz'],eligible_top=r in EL) for i,r in enumerate(M['support_matrix'])]
meta=dict(schema='briar_ground_zoning_phase_a_v1',status='SOURCE ART ONLY; engine blend implementation and native validation pending',bounds_xz=[-64,64,-48,52],up='+Y',size_px=[N,N],format='8-bit grayscale PNG; GPU R8_UNORM',channel='R = dirt weight byte/255; gravel weight = 1-R; no alpha or other channels',colorspace='linear data / Non-Color; never sRGB decode',png_orientation='column increases +X; row increases +Z; first stored PNG row is Z=-48 edge',uv_mask_top_left=['(worldX+64)/128','(worldZ+48)/100'],uv_mask_bottom_left=['(worldX+64)/128','1-(worldZ+48)/100'],pixel_center_world=['X=-64+(column+0.5)*0.25','Z=-48+(row+0.5)*0.1953125'],pixel_index_float=['column=512*u-0.5','row=512*v-0.5 (top-left convention)'],texel_size_m=[DX,DZ],filter='bilinear LOD 0 only; no mipmaps or anisotropic widening in this source contract',wrap='CLAMP_TO_EDGE; outside domain MUST return weight 0 before clamp',geometry_gate='Apply only top surfaces of eligible supports listed below. All bottom/side faces and excluded primitives keep their existing material. Use world XZ, never support-local UV normalization.',safety_guard_m=GUARD,guard_reason='Conservative zero collar one full texel diagonal about every forbidden rectangle/circle; bilinear LOD0 cannot reach nonzero from protected boundaries. Narrow added collar is sampling safety, not a new design zone.',transition=dict(width_m=6,rule='Linear falloff outside each polygon, max union, then eligibility/exclusions/guard; no noise',far_verge='F core Z48..52; original 6m taper clipped at Z46; nonzero stored texel centres require Z>=46+one Z texel (first centre 46.23828125); bilinear onset is just above Z46.04296875. Deliberate clipped onset, not a six-metre transition in this 4m strip.'),exclusions=FORB,fence_discs=dict(centers_xz=[[12.5,0],[12.5,6]],radius_m=2),supports=SUP,materials={},normal_blend='Decode each OpenGL tangent-space normal, multiply XY by normalScale, normalize; lerp the two vectors with R and renormalize in one common world-XZ tangent basis. Engine must verify handedness against baseline. No height/displacement.',blend='Decode sRGB base maps to linear, apply per-material baseColorFactor, then lerp. Roughness scalar lerp. Metallic 0 and opacity 1. Do not use this mask as alpha transparency.',native_tested=False)
for asset,key,repeat,factor in [('gravel_ground_01','gravel',8/3,.65),('dirt','dirt',2.,1.)]:
 meta['materials'][key]=dict(asset=asset,repeat_m=repeat,phase_origin_xz=[0,0],source_blender_uv=['worldX/repeat','worldZ/repeat'],gltf_exported_uv=['worldX/repeat','1-worldZ/repeat'],note='glTF exporter flips V versus Blender UV. Reuse existing gravel TEXCOORD_0; dirt coordinates are the same world phase scaled by 4/3 (account for the 1-V offset). PNG top-left sampler texture phase equivalent to v=-worldZ/repeat modulo 1.',wrap='REPEAT',basecolor=dict(path=f'../source/scans/{asset}/{asset}_diff_1k.png',colorspace='sRGB',factor_linear=[factor,factor,factor,1]),normal=dict(path=f'../source/scans/{asset}/{asset}_nor_gl_1k.png',colorspace='Non-Color',convention='OpenGL +Y',scale=.65),roughness=dict(path=f'../source/scans/{asset}/{asset}_rough_1k.png',colorspace='Non-Color',channel='R (grayscale)',factor=1),metallic=0,alphaMode='OPAQUE')
(R/'runtime/mapping.json').write_text(json.dumps(meta,indent=2,allow_nan=False)+'\n')
def sample(x,z):
 x,z=np.broadcast_arrays(x,z);px=np.clip((x+64)/DX-.5,0,511);pz=np.clip((z+48)/DZ-.5,0,511);i=np.floor(px).astype(int);j=np.floor(pz).astype(int);a=px-i;b=pz-j;i1=np.minimum(i+1,511);j1=np.minimum(j+1,511)
 v=((1-a)*(1-b)*raw[j,i]+a*(1-b)*raw[j,i1]+(1-a)*b*raw[j1,i]+a*b*raw[j1,i1])/255
 return np.where((x>=-64)&(x<=64)&(z>=-48)&(z<=52),v,0)
checks=[]
for f in FORB:
 a,b,c,d=f['xz'];xx,zz=np.meshgrid(np.linspace(a,b,129),np.linspace(c,d,129));v=sample(xx,zz);checks.append(dict(id=f['id'],samples=v.size,max_weight=float(v.max())));assert v.max()==0
for a,b in [[12.5,0],[12.5,6]]:
 t=np.linspace(0,2*np.pi,513);rr=np.linspace(0,2,33);v=sample(a+np.outer(rr,np.cos(t)),b+np.outer(rr,np.sin(t)));assert v.max()==0
points=[(-64,-48),(64,-48),(-64,52),(64,52),(-50,-40),(0,-42),(54,0),(0,50),(0,47),(0,46),(0,40),(0,24),(0,8),(12.5,6),(-30,0),(30,0),(-38,0),(-32,0)]
cps=[dict(world_xz=[x,z],uv_top_left=[(x+64)/128,(z+48)/100],weight=float(sample(x,z))) for x,z in points]
(R/'qa/control_points.json').write_text(json.dumps(cps,indent=2))
qa=dict(status='PASS',mask_min=int(raw.min()),mask_max=int(raw.max()),nonzero_pixels=int(np.count_nonzero(raw)),quantization_max_error=float(np.abs(raw/255-mask).max()),finite=True,channels=1,bits=8,support_count=len(SUP),eligible_support_count=len(EL),excluded_support_count=54-len(EL),prohibited_area_bilinear_checks=checks,all_prohibited_max_weight=0,fence_disc_max_weight=0,outside_bounds_weight=0,geometry_written=False,collider_written=False,native_tested=False,seams='One global mask and one world phase: shared-edge queries identical by construction; no per-support mask atlas. Support continuity samples in independent audit.',note='Mips disabled intentionally; unguarded auto-generated mips would invalidate protected-area claim.')
assert len(SUP)==54 and len(EL)==12
(R/'qa/mask_validation.json').write_text(json.dumps(qa,indent=2))
fig,ax=plt.subplots(figsize=(13,10));im=ax.imshow(raw/255,extent=(-64,64,52,-48),vmin=0,vmax=1,cmap='cividis',interpolation='nearest')
for r in SUP:
 a,b,c,d=r['xz'];ax.add_patch(Rectangle((a,c),b-a,d-c,fill=False,edgecolor='white' if r['eligible_top'] else '#fc6e65',lw=.4))
for x,z in points:ax.plot(x,z,'+',color='magenta',ms=6)
ax.set_xticks(range(-64,65,8));ax.set_yticks(range(-48,53,8));ax.grid(alpha=.25);ax.set_xlabel('World X, metres');ax.set_ylabel('World Z, metres; +Z frontage downward');ax.set_title('PHASE A / LINEAR DIRT WEIGHT\n512² mask, world grid, support boundaries, magenta control points');fig.colorbar(im,ax=ax,label='Dirt weight');fig.tight_layout();fig.savefig(R/'review/mask_diagnostic_grid.png',dpi=140);plt.close(fig)
print(json.dumps({k:qa[k] for k in ['status','mask_min','mask_max','eligible_support_count','all_prohibited_max_weight']}))
