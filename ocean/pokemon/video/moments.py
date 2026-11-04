"""Key moments in recorded battles (2026-11-04): reads the game's text box (RapidOCR) and writes, per video, every
message with its time, so the video can cut to knockouts, critical hits and super-effective hits instead of only the
last seconds. Frames come from ffmpeg at 6 fps (box only, half size); OCR runs only once a changed box has stopped
typing (two equal frames), about once per message. Results are saved after every video.
Usage: uv run --with rapidocr-onnxruntime --with numpy python moments.py OUT.json VIDEO.mp4...
"""
import json
import pathlib
import subprocess
import sys

import numpy as np
from rapidocr_onnxruntime import RapidOCR

FPS, W, H = 6, 400, 122
ocr = RapidOCR()
path = pathlib.Path(sys.argv[1])
out = json.loads(path.read_text()) if path.exists() else {}


def tag(s):
    s = s.lower()
    if 'aint' in s:
        return 'faint'
    if 'ritic' in s:
        return 'crit'
    if 'ffect' in s and 'not' not in s and 'doesn' not in s:
        return 'super'
    return ''


for video in sys.argv[2:]:
    name = pathlib.Path(video).stem
    if name in out:
        continue
    proc = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', video, '-vf', f'fps={FPS},crop=800:245:112:500,scale={W}:{H}',
                             '-f', 'rawvideo', '-pix_fmt', 'gray', '-'], stdout=subprocess.PIPE)
    events, prev, done = [], None, None
    k = 0
    while True:
        buf = proc.stdout.read(W * H)
        if len(buf) < W * H:
            break
        ink = np.frombuffer(buf, np.uint8).reshape(H, W) < 100  # dark glyph pixels on the light box
        stable = prev is not None and (ink != prev).mean() < 0.002
        if stable and (done is None or (ink != done).mean() > 0.01) and ink.mean() > 0.005:
            img = np.where(ink, 0, 255).astype(np.uint8)
            r, _ = ocr(np.stack([img] * 3, -1))
            text = ' '.join(x[1] for x in r) if r else ''
            if text:
                events.append(dict(t=round(k / FPS, 2), text=text, tag=tag(text)))
            done = ink
        prev = ink
        k += 1
    proc.wait()
    out[name] = events
    path.write_text(json.dumps(out, indent=1))
    print(name, len(events), 'messages;', sum(1 for e in events if e['tag']), 'tagged', flush=True)
