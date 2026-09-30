#!/usr/bin/env python3
"""Upgrade a legacy 1224-input Abyss checkpoint to shared 1234-input ABI.

New EWAR input weights are zero; the old policy's output is preserved exactly
before further training. Old files are never overwritten.
"""
import argparse,array,hashlib,json,sys
from pathlib import Path
OLD,NEW,OUTPUTS=1224,1234,395

def upgrade(source:Path,destination:Path):
 raw=source.read_bytes()
 if len(raw)%4:raise ValueError('checkpoint is not FP32')
 count=len(raw)//4
 matches=[(h,l) for h in [8,16,32,64,128,256,512,1024,2048] for l in range(1,17) if (OLD+OUTPUTS)*h+3*h*h*l==count]
 if len(matches)!=1:raise ValueError('expected an unambiguous legacy 1224-input checkpoint')
 h,l=matches[0];weights=array.array('f');weights.frombytes(raw)
 if sys.byteorder!='little':weights.byteswap()
 output=array.array('f')
 for row in range(h):
  output.extend(weights[row*OLD:(row+1)*OLD]);output.extend([0.]*(NEW-OLD))
 output.extend(weights[h*OLD:])
 if sys.byteorder!='little':output.byteswap()
 with destination.open('xb') as f:f.write(output.tobytes())
 metadata=dict(source=str(source.resolve()),source_sha256=hashlib.sha256(raw).hexdigest(),observation_version=2,hidden_size=h,num_layers=l,environment='abyss',added_input_weights='zero')
 destination.with_suffix(destination.suffix+'.migration.json').write_text(json.dumps(metadata,indent=2)+'\n')
 return metadata

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkpoint',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
 destination=a.output or a.checkpoint.with_name(a.checkpoint.stem+'.abi2.bin')
 print(json.dumps(dict(output=str(destination),**upgrade(a.checkpoint,destination)),indent=2))
if __name__=='__main__':main()
