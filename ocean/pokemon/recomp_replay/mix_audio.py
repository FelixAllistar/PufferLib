"""Mix a recording's captured sound (record_driver.lua, REPLAY_AUDIO_DIR) into one WAV on the video's timeline
(2026-11-01). Game frame f is at (f - video_start) / 60 s. Each music stream plays its queued buffers back to back
from its first queue frame until it is stopped; each one-shot sound plays from its frame with its pitch (as a speed
change) and volume until its stop event.
Usage: uv run --with numpy python mix_audio.py AUDIO_DIR OUT.wav [SECONDS]
"""
import pathlib
import sys
import wave

import numpy as np

RATE = 48000
d = pathlib.Path(sys.argv[1])
out_path = sys.argv[2]
events = [line.split() for line in (d / 'events.txt').read_text().splitlines() if line.strip()]
start = next(int(e[0]) for e in events if e[1] == 'video_start')
last = max(int(e[0]) for e in events)
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else (last - start) / 60 + 5
mix = np.zeros((int(seconds * RATE) + RATE, 2), np.float32)
raw = (d / 'music.raw').read_bytes()


def place(samples, at_frame, stop_frame=None):
    """Add float stereo samples at a game frame, cut at stop_frame."""
    t = int((at_frame - start) / 60 * RATE)
    if stop_frame is not None:
        samples = samples[:max(0, int((stop_frame - at_frame) / 60 * RATE))]
    if t < 0:
        samples, t = samples[-t:], 0
    n = min(len(samples), len(mix) - t)
    if n > 0:
        mix[t:t + n] += samples[:n]


def to_stereo(pcm, channels, rate):
    x = pcm.reshape(-1, channels).astype(np.float32) / 32768.0
    if channels == 1:
        x = np.repeat(x, 2, axis=1)
    if rate != RATE:  # linear resample
        n = int(len(x) * RATE / rate)
        src = np.linspace(0, len(x) - 1, n)
        x = np.stack([np.interp(src, np.arange(len(x)), x[:, c]) for c in range(2)], axis=1).astype(np.float32)
    return x


# music: per stream, buffers back to back from the first queue frame
streams, stops = {}, {}
for e in events:
    if e[1] == 'queue':
        frame, sid, off, size, rate, ch = map(int, [e[0]] + e[2:])
        streams.setdefault(sid, {'frame': frame, 'chunks': [], 'rate': rate, 'ch': ch})['chunks'].append((off, size))
    elif e[1] == 'stopstream':
        stops.setdefault(int(e[2]), int(e[0]))
for sid, s in streams.items():
    pcm = np.concatenate([np.frombuffer(raw[o:o + n], '<i2') for o, n in s['chunks']])
    place(to_stereo(pcm, s['ch'], s['rate']), s['frame'], stops.get(sid))

# one-shot sounds
cache = {}
plays = [e for e in events if e[1] in ('play', 'stop')]
for i, e in enumerate(plays):
    if e[1] != 'play':
        continue
    frame, sid, pitch, volume = int(e[0]), int(e[2]), float(e[3]), float(e[4])
    if sid not in cache:
        with wave.open(str(d / f'sfx_{sid}.wav')) as w:
            cache[sid] = to_stereo(np.frombuffer(w.readframes(w.getnframes()), '<i2'), w.getnchannels(),
                                   w.getframerate())
    x = cache[sid]
    if abs(pitch - 1) > 1e-3:  # pitch as a speed change
        n = max(1, int(len(x) / pitch))
        src = np.linspace(0, len(x) - 1, n)
        x = np.stack([np.interp(src, np.arange(len(x)), x[:, c]) for c in range(2)], axis=1).astype(np.float32)
    stop = next((int(p[0]) for p in plays[i + 1:] if p[1] == 'stop' and int(p[2]) == sid and int(p[0]) >= frame), None)
    place(x * volume, frame, stop)

peak = float(np.abs(mix).max()) or 1.0
mix = mix[:int(seconds * RATE)] * (0.9 / max(1.0, peak))
with wave.open(out_path, 'wb') as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes((mix * 32767).astype('<i2').tobytes())
print(f'{len(streams)} music streams, {sum(1 for e in plays if e[1] == "play")} sounds, {seconds:.1f} s -> {out_path}')
