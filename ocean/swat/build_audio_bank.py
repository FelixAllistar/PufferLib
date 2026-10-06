#!/usr/bin/env python3
"""Prepare the checked-in CC0 bank from archived original recordings.

Sources/provenance are in assets/audio/SOURCES.json. The converter uses Raylib
without opening a window or audio device; no gameplay randomness is involved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import wave
import numpy as np

ROOT = Path(__file__).resolve().parents[2]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source-dir', type=Path, default=ROOT/'build/swat/audio-sources')
    ap.add_argument('--converter', type=Path, default=ROOT/'build/swat/audio-sources/convert')
    ap.add_argument('--output', type=Path, default=ROOT/'ocean/swat/assets/audio')
    args = ap.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    manifest = ['# kind material(-1=any) weapon-slot(-1=any) gain file']
    entries = []
    def clip(source, name, kind, material=-1, profile=-1, gain=.6, start=0, duration=None):
        source = args.source_dir/source
        with tempfile.TemporaryDirectory() as tmp:
            converted = Path(tmp)/'mono.wav'
            subprocess.run([str(args.converter), str(source), str(converted)], check=True, stdout=subprocess.DEVNULL)
            with wave.open(str(converted)) as w:
                rate = w.getframerate(); x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(np.float64)/32768
        first=int(start*rate); last=min(len(x), first+int((duration or 3)*rate)); x=x[first:last]
        if not len(x): raise ValueError(f'Empty recording: {source}')
        x-=np.mean(x); peak=np.max(np.abs(x))
        if peak<.005: raise ValueError(f'Silent recording: {source}')
        x*=.80/peak
        # Preserve transients; taper only the final 30 ms of the selected tail.
        fade=min(int(.03*rate),len(x)); x[-fade:]*=np.linspace(1,0,fade)
        output=args.output/name
        with wave.open(str(output),'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(np.round(x*32767).astype('<i2').tobytes())
        manifest.append(f'{kind} {material} {profile} {gain:.3f} {name}')
        entries.append({'file':name,'source':str(source.relative_to(args.source_dir)),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'start_seconds':start,'duration_seconds':len(x)/rate,'sha256':hashlib.sha256(output.read_bytes()).hexdigest()})
    for i,(name,start) in enumerate([('D_24P.wav',.53),('D_32P.wav',.69),('D_32P.wav',5.62)]):
        clip(Path('firearm/Prepared SFX Library/AR-15')/name,f'carbine_{i}.wav',0,profile=0,gain=.85,start=start,duration=.70)
    # Inspect waveform transients for the supplied pistol takes; select separate
    # shots, not an entire multi-shot recording played by every trigger press.
    for i,name in enumerate(('A_34P.wav','A_42P.wav')):
        source=Path('firearm/Prepared SFX Library/1911')/name
        with tempfile.TemporaryDirectory() as tmp:
            converted=Path(tmp)/'pistol.wav'; subprocess.run([str(args.converter),str(args.source_dir/source),str(converted)],check=True,stdout=subprocess.DEVNULL)
            with wave.open(str(converted)) as w: rate=w.getframerate(); x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2')
            peak=np.max(np.abs(x.astype(np.float64))); onset=np.flatnonzero(np.abs(x.astype(np.float64))>peak*.22)[0]/rate
        clip(source,f'sidearm_{i}.wav',0,profile=1,gain=.8,start=max(0,onset-.015),duration=.65)
    for material,floor in [(0,'concrete'),(2,'wood'),(9,'carpet'),(10,'grass')]:
        for i in range(5): clip(Path('kenney/Audio')/f'footstep_{floor}_{i:03d}.ogg',f'step_{floor}_{i}.wav',1,material=material,gain=.45)
    for material,prefix in [(0,'impactGeneric_light'),(1,'impactPlank_medium'),(2,'impactWood_light'),(3,'impactGlass_medium'),(4,'impactMetal_medium')]:
        for i in range(3): clip(Path('kenney/Audio')/f'{prefix}_{i:03d}.ogg',f'impact_{material}_{i}.wav',5,material=material,gain=.55)
    for i,start in enumerate((.23,1.02)):
        clip(Path('reload.wav'),f'mechanism_{i}.wav',2,gain=.55,start=start,duration=.26)
        manifest.append(f'3 -1 -1 0.45 mechanism_{i}.wav')
    for i in range(3):
        clip(Path('kenney/Audio')/f'impactWood_heavy_{i:03d}.ogg',f'break_wood_{i}.wav',6,material=2,gain=.60)
        manifest.append(f'4 -1 -1 0.50 break_wood_{i}.wav')
    # Reuse the concrete surface variants on the physical tile/brick/plaster
    # families until those receive individually recorded production selections.
    for kind,from_material,to_material in [(1,0,8),(5,0,5),(5,1,6),(6,2,1),(6,2,6)]:
        for line in list(manifest[1:]):
            parts=line.split()
            if int(parts[0])==kind and int(parts[1])==from_material:
                parts[1]=str(to_material); manifest.append(' '.join(parts))
    if len(manifest)-1>80: raise ValueError('Bank exceeds the bounded runtime clip capacity')
    (args.output/'bank.txt').write_text('\n'.join(manifest)+'\n')
    (args.output/'SOURCES.json').write_text(json.dumps({'license':'CC0-1.0','sources':[{'url':'https://opengameart.org/content/the-free-firearm-sound-library','authors':['Ben Jaszczak','Brian Nelson','Kevin Heras','Matthew Nanney']},{'url':'https://kenney.nl/assets/impact-sounds','author':'Kenney'},{'url':'https://opengameart.org/content/gun-reload-sounds','author':'SpringySpringo','description':'Airsoft mechanical handling'}],'clips':entries},indent=2)+'\n')
    print(f'{len(entries)} unique 48 kHz mono recordings, {len(manifest)-1} bindings')

if __name__=='__main__': main()
