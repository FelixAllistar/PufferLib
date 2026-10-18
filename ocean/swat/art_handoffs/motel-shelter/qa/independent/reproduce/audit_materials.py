import json,struct,hashlib,io,sys
from pathlib import Path
from PIL import Image
import numpy as np
D=Path(__file__).parent
paths=[D/'snapshot/runtime/BC_SHELTER01_static.glb',D/'packed_only/reexport.glb',D/'relocated/runtime/BC_SHELTER01_static.glb']
def inspect(p):
 raw=p.read_bytes();n=struct.unpack_from('<I',raw,12)[0];j=json.loads(raw[20:20+n]);bn=struct.unpack_from('<I',raw,20+n)[0];b=raw[28+n:28+n+bn];imgs=[]
 for im in j['images']:
  v=j['bufferViews'][im['bufferView']];data=b[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']];img=Image.open(io.BytesIO(data));pix=np.array(img.convert('RGBA'));imgr={'name':im.get('name'),'mimeType':im['mimeType'],'size':list(img.size),'mode':img.mode,'encoded_sha256':hashlib.sha256(data).hexdigest(),'rgba_pixel_sha256':hashlib.sha256(pix.tobytes()).hexdigest(),'rgba_channel_min':pix.min(axis=(0,1)).tolist(),'rgba_channel_max':pix.max(axis=(0,1)).tolist()};imgs.append(imgr)
 def texture(t):
  tex=j['textures'][t['index']];sam=j.get('samplers',[{}])[tex.get('sampler',0)];return {'image_rgba_pixel_sha256':imgs[tex['source']]['rgba_pixel_sha256'],'texCoord':t.get('texCoord',0),'sampler':{'magFilter':sam.get('magFilter'),'minFilter':sam.get('minFilter'),'wrapS':sam.get('wrapS',10497),'wrapT':sam.get('wrapT',10497)}}
 mats={}
 for m in j['materials']:
  pbr=m.get('pbrMetallicRoughness',{});mats[m['name']]={'alphaMode':m.get('alphaMode','OPAQUE'),'doubleSided':m.get('doubleSided',False),'baseColorFactor':pbr.get('baseColorFactor',[1,1,1,1]),'metallicFactor':pbr.get('metallicFactor',1),'roughnessFactor':pbr.get('roughnessFactor',1),'baseColorTexture':texture(pbr['baseColorTexture']),'metallicRoughnessTexture':texture(pbr['metallicRoughnessTexture']) if 'metallicRoughnessTexture' in pbr else None,'normalTexture':m.get('normalTexture'),'occlusionTexture':m.get('occlusionTexture'),'emissiveFactor':m.get('emissiveFactor',[0,0,0]),'extensions':m.get('extensions')}
 nodes={n['name']:{k:v for k,v in n.items() if k not in ['mesh','children']} for n in j['nodes']}
 for node in nodes.values():
  if 'source_parts' in node.get('extras',{}):node['extras']['source_parts'].sort()
 return {'path':str(p),'sha256':hashlib.sha256(raw).hexdigest(),'images':imgs,'materials_semantic':mats,'nodes_semantic':nodes,'external_uris':[i.get('uri') for k in ['buffers','images'] for i in j[k] if 'uri' in i],'extensionsUsed':j.get('extensionsUsed',[]),'animations':len(j.get('animations',[])),'skins':len(j.get('skins',[])),'cameras':len(j.get('cameras',[]))}
r=[inspect(p) for p in paths];out={'files':r,'packed_helper_material_pixel_sampler_factor_match':r[0]['materials_semantic']==r[1]['materials_semantic'],'relocated_material_pixel_sampler_factor_match':r[0]['materials_semantic']==r[2]['materials_semantic'],'packed_helper_node_transform_extras_match':r[0]['nodes_semantic']==r[1]['nodes_semantic'],'relocated_node_transform_extras_match':r[0]['nodes_semantic']==r[2]['nodes_semantic'],'scope':'Core glTF PBR factors, effective opaque/sided defaults, decoded RGBA pixels, effective sampler defaults and texture coordinates, plus named node transforms/extras; source_parts is compared as a sorted inventory since its order is nonsemantic. Not full Khronos validator.'}
(D/'material_comparison.json').write_text(json.dumps(out,indent=2));print(json.dumps({k:v for k,v in out.items() if k!='files'},indent=2))
