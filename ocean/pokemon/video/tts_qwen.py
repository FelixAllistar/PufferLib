"""Narration with Qwen3-TTS (Apache 2.0) on a GPU box (2026-11-01). Each script line carries its own delivery cue.
Input JSON: [{"id": "...", "text": "...", "speaker": "Ryan", "instruct": "excited, building up"}, ...]
Writes OUT_DIR/<id>.wav. Usage: python tts_qwen.py LINES.json OUT_DIR [MODEL]
"""
import json
import pathlib
import sys

import soundfile as sf
import torch
from qwen_tts import Qwen3TTSModel

lines = json.load(open(sys.argv[1]))
out = pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
name = sys.argv[3] if len(sys.argv) > 3 else 'Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice'
model = Qwen3TTSModel.from_pretrained(name, device_map='cuda:0', dtype=torch.bfloat16)
print('speakers:', model.get_supported_speakers())
for ln in lines:
    wavs, sr = model.generate_custom_voice(text=ln['text'], language='English', speaker=ln.get('speaker', 'Ryan'),
                                           instruct=ln.get('instruct', ''))
    sf.write(out / f"{ln['id']}.wav", wavs[0], sr)
    print(ln['id'], f'{len(wavs[0]) / sr:.1f}s', flush=True)
