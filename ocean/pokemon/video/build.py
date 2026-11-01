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
    dict(kind='clip', src='000001_LORELEI_vs_BLUE_p1', from_end=30, dur=24, chapter='Who has the best team?',
         label='LORELEI vs CHAMPION BLUE',
         say="Who actually has the best team in Generation One? A popular video tried to answer this by making every "
             "trainer in Pokemon Red fight every other trainer. Its winner was Professor Oak. A guy you never even "
             "fight. That answer is wrong, and the reason why is more interesting than the answer."),
    dict(kind='card', title='WHO ACTUALLY HAS THE BEST TEAM IN GEN 1?', sub='391 trainers - 55 million battles', dur=4),
    dict(kind='clip', src='000243_ERIKA_vs_LANCE_p1', start=40, chapter='Gen 1 in 60 seconds',
         say="Quick refresher. In Gen 1, a Pokemon's stats come from its level, its hidden D.V.s, and stat experience "
             "from battling. Trainers' Pokemon get mediocre fixed D.V.s and no stat experience at all, so for them, "
             "level is almost everything. And the A.I. that runs every trainer is simple. It scores moves with a "
             "handful of rules, it has a bug in how it checks type matchups, and otherwise it picks almost at random."),
    dict(kind='clip', src='000130_SABRINA_vs_AGATHA_p1', start=60, chapter='Why the old ranking is wrong',
         say="So we rebuilt the old experiment. All three hundred and ninety one trainers against each other, ten "
             "thousand times per pair, with the game's own A.I. on both sides. Seven hundred and sixty two million "
             "battles. And we got the same answer. Professor Oak, number one."),
    dict(kind='image', src='levels.png',
         say="But look at what that ranking actually measures. Put each trainer's average level next to their rank, "
             "and it's almost a straight line. The old ranking isn't a ranking of teams. It's a list of levels, "
             "played by an A.I. that doesn't really know how to battle."),
    dict(kind='clip', src='000022_BLUE_vs_LANCE_p1', start=50, chapter='Fixing it',
         say="So we fixed both problems. First the A.I.: we trained our own, by having it play Gen 1 against itself "
             "billions of times. Then the levels: every trainer's Pokemon at level one hundred, with perfect stats."),
    dict(kind='image', src='party_size.png', chapter='Still a numbers game',
         say="And it was still a numbers game. At level one hundred, trainers with six Pokemon land near the top, and "
             "trainers with one or two land near the bottom, almost no matter what's in the team. More Pokemon beats "
             "better Pokemon."),
    dict(kind='clip', src='000328_BLAINE_vs_MISTY_p2', start=40, chapter='Stadium rules',
         say="So we borrowed the rules Pokemon Stadium used for its cups: everyone brings their best three. Our A.I. "
             "plays both sides. And to make sure every trainer really brings their best three, we tried every "
             "possible team of three, with every lead, against every other trainer's. Fifty five million battles, "
             "then we solved each matchup for the best picks."),
    dict(kind='image', src='top10_cd.png', reveal=10, chapter='The real top 10',
         say="Here's the real top ten. Number ten, a channeler with nothing but Gastly and Haunter. A cooltrainer. "
             "Lance only makes eighth. A psychic. Professor Oak, sixth. A random juggler, fifth. Agatha, fourth. "
             "Sabrina, third. Lorelei, second. And number one: Champion Blue."),
    dict(kind='clip', src='000001_LORELEI_vs_BLUE_p1', start=40,
         say="Blue's best three are Alakazam, Gyarados and Charizard. But it's close. Lorelei's Slowbro, Jynx and "
             "Lapras beat him in some of our games, and that's one of them."),
    dict(kind='image', src='journey_gyms.png', reveal=8, chapter='And then there is Brock',
         say="Further down, Erika and Lieutenant Surge climb, a fisherman with three Seaking jumps a hundred and "
             "ninety two places, and Brock. Brock is two hundred and fifty sixth. Trainers in this game are supposed "
             "to be professionals. Brock is out here squinting at everybody, traveling the world, and losing to bug "
             "catchers. We have questions."),
    dict(kind='clip', src='000243_ERIKA_vs_LANCE_p1', from_end=24, dur=20, speed=1.25, game=1.0, chapter='Highlights',
         label='ERIKA vs LANCE'),
    dict(kind='clip', src='000328_BLAINE_vs_MISTY_p2', from_end=24, dur=20, speed=1.25, game=1.0,
         label='BLAINE vs MISTY'),
    dict(kind='clip', src='000022_BLUE_vs_LANCE_p1', from_end=26, dur=22, speed=1.25, game=1.0,
         label='CHAMPION BLUE vs LANCE'),
    dict(kind='card', title='THE BEST TEAM IN GEN 1', sub='Champion Blue - Alakazam, Gyarados, Charizard', dur=6),
]


DESCRIPTION = '''Who actually has the best team in Gen 1? The old answer (Professor Oak) came from making every trainer in
Pokemon Red fight with the game's own AI at their original levels, and that ranking turns out to be a list of levels.
We rebuilt it, then fixed it: an AI we trained, everyone at level 100, Pokemon Stadium rules (best three), and every
possible team tested (55 million battles).

Engine: pkmn/engine with our cartridge trainer-AI port. Training: PufferLib. Footage: our battles replayed in the
Gen 1 Recompilation Project with the Battle Art Voxel Fork mod. Original ranking: PimanRules.'''


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
parts, clock, subs, chapters = [], 0.0, [], []
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
    elif seg['kind'] == 'image' and seg.get('reveal'):
        # rows appear one by one over the first 85% of the narration, then the full table holds
        n = seg['reveal']
        stem = seg['src'].replace('.png', '')
        step = dur * 0.85 / n
        listing = WORK / f'reveal_{i:02d}.txt'
        listing.write_text(''.join(f"file '{MEDIA / 'video' / 'charts' / f'{stem}_{k}.png'}'\nduration {step:.3f}\n"
                                   for k in range(1, n + 1))
                           + f"file '{MEDIA / 'video' / 'charts' / f'{stem}_{n}.png'}'\nduration {dur:.3f}\n")
        inputs = ['-f', 'concat', '-safe', '0', '-i', str(listing)]
        vf.append('fps=30,format=yuv420p')
        inputs += ['-ss', f'{(i * 17) % 90}', '-i', music_bed]
        a0 = '[1:a]volume=0.12[ga]'
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
    if seg.get('chapter'):
        chapters.append((clock, seg['chapter']))
    if say:  # one subtitle per sentence, timed by length across the narration (it starts 0.5 s in)
        import re
        sentences = [x.strip() for x in re.split(r'(?<=[.?!])\s+', say) if x.strip()]
        total = sum(len(x) for x in sentences)
        t = clock + 0.5
        for x in sentences:
            d = talk * len(x) / total
            subs.append((t, t + d, x))
            t += d
    clock += dur
    parts.append(out)
    print(f'segment {i:2d} {seg["kind"]:<5} {dur:5.1f}s', flush=True)

lst = WORK / 'parts.txt'
lst.write_text(''.join(f"file '{p}'\n" for p in parts))
run(['ffmpeg', '-nostdin', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', str(lst), '-c', 'copy', str(OUT)])
print(f'{OUT}: {probe(OUT):.1f}s')


def stamp(t, srt=True):
    h, m, sec = int(t // 3600), int(t % 3600 // 60), t % 60
    return f'{h:02d}:{m:02d}:{sec:06.3f}'.replace('.', ',') if srt else f'{m}:{int(sec):02d}'


OUT.with_suffix('.srt').write_text(''.join(f'{n}\n{stamp(a)} --> {stamp(b)}\n{x}\n\n'
                                           for n, (a, b, x) in enumerate(subs, 1)))
desc = [DESCRIPTION, '', 'Chapters:'] + [f'{stamp(t, False)} {name}' for t, name in chapters]
OUT.with_suffix('.description.txt').write_text('\n'.join(desc) + '\n')
print('subtitles, description ->', OUT.with_suffix('.srt'), OUT.with_suffix('.description.txt'))

# thumbnail 1280x720: a cold-open frame large on the left, the hook on the right (the title gives the context)
run(['ffmpeg', '-nostdin', '-loglevel', 'error', '-y', '-ss', '20.4', '-i', str(parts[0]), '-frames:v', '1', '-vf',
     f"crop=1296:972:312:108,scale=800:600:flags=neighbor,pad=1280:720:0:60:color={BG},"
     f"drawtext=fontfile={FONT}:expansion=none:text='#1':fontcolor=0xF8D030:fontsize=160:x=800+(480-text_w)/2:y=110,"
     f"drawtext=fontfile={FONT}:expansion=none:text='IT IS NOT':fontcolor=white:fontsize=54:x=800+(480-text_w)/2:y=320,"
     f"drawtext=fontfile={FONT}:expansion=none:text=' ':fontcolor=white:fontsize=44:x=800+(480-text_w)/2:y=395,"
     f"drawtext=fontfile={FONT}:expansion=none:text='PROF. OAK':fontcolor=white:fontsize=60:x=800+(480-text_w)/2:y=460",
     str(OUT.with_suffix('.thumbnail.png'))])
print('thumbnail ->', OUT.with_suffix('.thumbnail.png'))
