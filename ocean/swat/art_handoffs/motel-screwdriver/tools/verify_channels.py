"""Python 3 + Pillow: verify runtime packed map bytes against original channels."""
from pathlib import Path
from PIL import Image,ImageChops
import json,struct,io,sys
p=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
raw=(p/'runtime/wood_handle_flathead_screwdriver_250mm_MAIN.glb').read_bytes();n=struct.unpack_from('<I',raw,12)[0];g=json.loads(raw[20:20+n]);blob=raw[28+n:];im=g['images'][1];v=g['bufferViews'][im['bufferView']];packed=Image.open(io.BytesIO(blob[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']]));r={}
for role,ch in [('roughness',1),('metallic',2)]:
 original=Image.open(p/'textures'/('screwdriver_'+role+'.png')).convert('RGB').getchannel(0);actual=packed.getchannel(ch);assert actual.size==original.size;delta=ImageChops.difference(actual,original);err=delta.getextrema()[1];r[role]={'packed_channel':'G' if ch==1 else 'B','source_resolution':list(original.size),'packed_resolution':list(actual.size),'max_byte_error':err,'exact_pixel_match':err==0};assert err==0
r['R_channel']='Exporter-generated 255, not connected as occlusion';(p/'qa/runtime_channel_verification.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
