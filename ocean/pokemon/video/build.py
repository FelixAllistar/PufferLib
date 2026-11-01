"""Build the Gen 1 trainer video (2026-11-01): the segment list below -> one 1920x1080 MP4.
Visuals are our recorded battles (recomp_replay/record.sh), charts.py PNGs and text cards; narration is Piper TTS
(local, rhasspy/piper-voices); the music bed is the game's own battle music (recomp_replay/music_driver.lua).
Each segment: kind clip | image | card, optional `say` (narration; the segment stretches to fit it), `dur`.
Clips: `src` (videos_sound/<src>.mp4), `start` or `from_end` (s), `speed`, `label` (lower-third text), `game` (game
audio volume without narration). Usage: uv run --with numpy python build.py MEDIA_DIR OUT.mp4 [VOICE]
"""
import json
import pathlib
import subprocess
import sys
import wave

MEDIA = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
VOICE = sys.argv[3] if len(sys.argv) > 3 else 'en_US-ryan-high'
WORK = MEDIA / 'video' / 'work'
WORK.mkdir(parents=True, exist_ok=True)
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
BG = '0x101828'
MUSIC = MEDIA / 'music'

SEGMENTS = [
    dict(kind='clip', src='000350_Player_vs_BRUNO_1_p1', from_end=38, dur=34, game=1.0,
         label='LEVEL 100 WEEDLE vs BRUNO'),
    dict(kind='card', title='WE MADE EVERY TRAINER FIGHT', sub='Pokemon Red - 391 trainers - one AI', dur=4),
    dict(kind='clip', src='000243_ERIKA_vs_LANCE_p1', start=40, speed=1.0,
         say="Pokemon Red has three hundred and ninety one trainers. Bug catchers, fishermen, rockets, gym leaders, "
             "the Elite Four. Every one of them has a real team stored on the cartridge. So we pulled every team out "
             "of the game, rebuilt the battle engine to match the cartridge exactly, and made every trainer fight "
             "every other trainer."),
    dict(kind='clip', src='000130_SABRINA_vs_AGATHA_p1', start=60,
         say="First, with the game's own A.I. on both sides, at their normal levels. Seven hundred and sixty two "
             "million battles. The winner? Professor Oak, who you never even get to fight. Then Champion Blue, Lance "
             "and Agatha. That matches what Piman Rules found, so the engine checks out."),
    dict(kind='image', src='party_size.png',
         say="But at normal levels that's mostly a level contest. So we put everyone at level one hundred with perfect "
             "stats. And now one thing decides almost everything: how many Pokemon you have. Six Pokemon, and you're "
             "near the top. One Pokemon, and you're near the bottom."),
    dict(kind='clip', src='000001_LORELEI_vs_BLUE_p1', start=30,
         say="So we switched to the Pokemon Stadium cup rules: everyone brings their best three. And instead of the "
             "game's A.I., both sides are played by an A.I. we trained ourselves. Then we tested every possible team "
             "of three against every other one. Fifty five million battles, and the best picks for every matchup."),
    dict(kind='image', src='top10.png', say="Here is the real top ten.", dur=6),
    dict(kind='image', src='journey_elite.png',
         say="Champion Blue stays number one. Lorelei, Sabrina and Agatha all jump into the top ten. Blaine climbs "
             "sixty one places once he gets to lead with Arcanine. Professor Oak and Lance slide, and Bruno drops to "
             "sixty eight."),
    dict(kind='image', src='journey_gyms.png',
         say="Further down, Erika and Lieutenant Surge climb, a fisherman with three Seaking jumps a hundred and "
             "ninety two places, and Brock. Brock is two hundred and fifty sixth. Trainers in this game are supposed "
             "to be professionals. Brock is out here squinting at everybody, traveling the world, and losing to bug "
             "catchers. We have questions."),
    dict(kind='card', title='DOWN TO THE LAST HIT POINT', sub='tournament highlights', dur=3),
    dict(kind='clip', src='000255_BROCK_vs_LANCE_p2', from_end=24, dur=20, speed=1.25, game=1.0,
         label='BROCK vs LANCE'),
    dict(kind='clip', src='000022_BLUE_vs_LANCE_p1', from_end=26, dur=20, speed=1.25, game=1.0,
         label='CHAMPION BLUE vs LANCE'),
    dict(kind='clip', src='000328_BLAINE_vs_MISTY_p2', from_end=26, dur=20, speed=1.25, game=1.0,
         label='BLAINE vs MISTY'),
    dict(kind='card', title='LEVEL 100 WEEDLE vs THE ELITE FOUR', sub='the Gym Leader Matt challenge', dur=4),
    dict(kind='clip', src='000350_Player_vs_BRUNO_1_p1', start=20,
         say="Gym Leader Matt famously tried to beat the Elite Four with a single level one hundred Weedle, using "
             "nothing but Poison Sting. We rebuilt the cartridge's own trainer A.I., items and all, and our numbers "
             "match his: one in six against Bruno, one in thirty one against Lance."),
    dict(kind='image', src='weedle_odds.png',
         say="Then we let our A.I. play the Weedle. It learned to slow everyone down with String Shot first. Against "
             "Bruno it wins one in three, twice as often as spamming. Against Lance, about the same as spam. And "
             "Agatha? Zero wins in twenty thousand. Her Gengars resist Poison Sting, and Night Shade does a hundred "
             "damage every single turn. Some things even an A.I. can't fix."),
    dict(kind='clip', src='000237_Player_vs_LANCE_1_p1', from_end=30, dur=26, game=1.0,
         label="WEEDLE vs LANCE - its only win in 400 tries"),
    dict(kind='card', title='EVERY BATTLE HERE WAS PLAYED BY OUR AI',
         sub='replayed in the Gen 1 Recomp project with the Battle Art Voxel mod', dur=5),
]


def run(cmd):
    subprocess.run(cmd, check=True)


def probe(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', str(path)],
                         capture_output=True, text=True, check=True).stdout
    return float(out.strip())


def tts(text, i):
    wav = WORK / f'say_{i:02d}.wav'
    voice = MEDIA / 'video' / 'tts' / f'{VOICE}.onnx'
    subprocess.run(['uv', 'run', '-q', '--with', 'piper-tts', 'python', '-m', 'piper', '-m', str(voice), '-f', str(wav),
                    '--sentence_silence', '0.25'], input=text, text=True, check=True, capture_output=True)
    with wave.open(str(wav)) as w:
        return wav, w.getnframes() / w.getframerate()


def esc(t):
    return t.replace(':', '\\:').replace("'", "’")


# 4:3 game picture under a 108 px title band (labels never cover the game's own text box)
FRAME = (f"scale=1296:972:flags=neighbor,pad=1920:1080:312:108:color={BG},fps=30,format=yuv420p")
parts = []
music_bed = str(MUSIC / 'Music_TrainerBattle.wav')
for i, seg in enumerate(SEGMENTS):
    out = WORK / f'seg_{i:02d}.mp4'
    say = seg.get('say')
    wav, talk = tts(say, i) if say else (None, 0.0)
    dur = max(seg.get('dur', 0), talk + 1.2 if say else 0) or 4
    vf, inputs, amix = [], [], ''
    if seg['kind'] == 'clip':
        src = MEDIA / 'videos_sound' / f"{seg['src']}.mp4"
        speed = seg.get('speed', 1.0)
        start = seg['start'] if 'start' in seg else probe(src) - seg['from_end']
        inputs = ['-ss', f'{start:.2f}', '-i', str(src)]
        vf.append(f'setpts=PTS/{speed},' + FRAME)
        game = 0.22 if say else seg.get('game', 1.0)
        a0 = f'[0:a]atempo={speed},volume={game}[ga]' if speed != 1 else f'[0:a]volume={game}[ga]'
    elif seg['kind'] == 'image':
        inputs = ['-loop', '1', '-i', str(MEDIA / 'video' / 'charts' / seg['src'])]
        # slow push-in
        vf.append(f"zoompan=z='min(1.0+0.0004*on,1.06)':d={int(dur * 30)}:s=1920x1080:fps=30,format=yuv420p")
        inputs += ['-ss', f'{(i * 17) % 90}', '-i', music_bed]
        a0 = '[1:a]volume=0.12[ga]'
    else:
        inputs = ['-f', 'lavfi', '-i', f'color=c={BG}:s=1920x1080:r=30']
        vf.append(f"drawtext=fontfile={FONT}:expansion=none:text='{esc(seg['title'])}':fontcolor=white:fontsize=78:"
                  f"x=(w-text_w)/2:y=h/2-90,drawtext=fontfile={FONT}:expansion=none:text='{esc(seg['sub'])}':"
                  f"fontcolor=0xF8D030:fontsize=40:x=(w-text_w)/2:y=h/2+30,format=yuv420p")
        inputs += ['-ss', f'{(i * 23) % 90}', '-i', music_bed]
        a0 = '[1:a]volume=0.12[ga]'
    if seg.get('label'):
        vf.append(f"drawtext=fontfile={FONT}:expansion=none:text='{esc(seg['label'])}':fontcolor=white:fontsize=52:"
                  f"x=(w-text_w)/2:y=28")
    n_in = 2 if seg['kind'] != 'clip' else 1
    if wav:
        inputs += ['-i', str(wav)]
        amix = (f'{a0};[{n_in}:a]adelay=500|500,volume=1.6[v];[ga][v]amix=inputs=2:duration=first:normalize=0,'
                f'aresample=48000[a]')
    else:
        amix = f'{a0};[ga]aresample=48000[a]'
    filt = f"[0:v]{','.join(vf)}[v0];{amix}"
    run(['ffmpeg', '-nostdin', '-loglevel', 'error', '-y', *inputs, '-filter_complex', filt, '-map', '[v0]', '-map',
         '[a]', '-t', f'{dur:.2f}', '-r', '30', '-c:v', 'libx264', '-crf', '20', '-pix_fmt', 'yuv420p', '-c:a', 'aac',
         '-b:a', '192k', '-ar', '48000', '-ac', '2', str(out)])
    parts.append(out)
    print(f'segment {i:2d} {seg["kind"]:<5} {dur:5.1f}s', flush=True)

lst = WORK / 'parts.txt'
lst.write_text(''.join(f"file '{p}'\n" for p in parts))
run(['ffmpeg', '-nostdin', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', str(lst), '-c', 'copy', str(OUT)])
print(f'{OUT}: {probe(OUT):.1f}s')
