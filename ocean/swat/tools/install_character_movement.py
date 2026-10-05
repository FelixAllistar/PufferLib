#!/usr/bin/env python3
"""Install four privately supplied movement fixtures without publishing their art.
Arguments: extracted left, right, crouch-ready, crouch-forward package directories.
All four original GLB hashes are checked before changing the ignored install.
"""
from pathlib import Path
import hashlib, json, shutil, sys

EXPECTED = [
    ('walk_left', '4fe97ec1997bb4799440216ddc3a74d858154c0d8e51bf863e2d86082b1a1996'),
    ('walk_right', 'edcb227870a0483d06c99e89df491e24f6eb22c879e35833e597f3d6a196b899'),
    ('crouch_ready', '54957d0769b6ecaaa164969f0c0315316f6834714044a2b376364f2a32fd6d66'),
    ('crouch_walk', 'ea828e53f305de77b35ba55abdf67470ae98e871911c43fae5505c5022d47dd1'),
]

def main(paths):
    if len(paths)!=4: raise SystemExit(__doc__)
    inputs=[]
    for directory,(name,expected) in zip(paths,EXPECTED):
        root=Path(directory).resolve(); manifest=json.loads((root/'manifest.json').read_text())
        source=(root/manifest['glb']).resolve()
        if not source.is_relative_to(root) or hashlib.sha256(source.read_bytes()).hexdigest()!=expected:
            raise SystemExit(f'{name}: fixture does not match the calibrated original handoff')
        inputs.append((source,name))
    destination=Path(__file__).resolve().parents[3]/'build/swat/assets/characters'
    destination.mkdir(parents=True,exist_ok=True)
    for source,name in inputs:
        shutil.copyfile(source,destination/(name+'.glb'));print('Installed',name)
if __name__=='__main__': main(sys.argv[1:])
