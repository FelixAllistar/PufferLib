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
VOICE = sys.argv[3] if len(sys.argv) > 3 else 'Ryan'
WORK = MEDIA / 'video' / 'work'
WORK.mkdir(parents=True, exist_ok=True)
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
IMPACT = '/mnt/c/Windows/Fonts/impact.ttf'
BG = '0x101828'
MUSIC = MEDIA / 'music'

# v6 (2026-11-01): story order per the user: our model (why we can answer) -> Gen 1 basics -> the popular answer and
# why it is wrong -> fixes -> still a numbers game -> Stadium 3v3 -> countdown -> Brock -> next time (Magikarp).
# Each `say` has a `cue` (delivery direction for the TTS model; Qwen3-TTS instruct).
HYPE = 'Confident, punchy YouTube narrator. Energetic but not shouting.'
CALM = 'Clear, friendly explainer voice, medium pace, relaxed.'
WRY = 'Dry, wry comedic delivery, a little amused.'
BUILD = 'Building suspense, slower, dramatic pauses, like a countdown host.'
REVEAL = 'Big triumphant reveal, excited and loud, like announcing a champion.'
SEGMENTS = [
    dict(kind='clip', src='000001_LORELEI_vs_BLUE_p1', from_end=40, dur=9, label='LORELEI vs CHAMPION BLUE', cue=HYPE,
         chapter='Our AI', say="We trained an A.I. that plays Gen One Pokemon better than anything we could find."),
    dict(kind='scene', src='credentials', cue=HYPE,
         say="It beats Metamon, the best published Pokemon A.I., about two games out of three. And on the public Gen "
             "One ladder, it went two hundred and one wins to ninety eight."),
    dict(kind='scene', src='title', cue=HYPE,
         say="So we pointed it at an old argument. Who is actually the strongest trainer in Generation One?"),
    dict(kind='scene', src='oak_team', cue=WRY, chapter='The popular answer',
         say="The popular answer is Professor Oak. A video that made every trainer in Pokemon Red fight every other "
             "trainer crowned him champion. A man you never even get to battle."),
    dict(kind='scene', src='stats', cue=CALM, chapter='Gen 1 in 60 seconds',
         say="Quick crash course. A Pokemon's stats come from three things. Its level, its hidden D.V.s, and stat "
             "experience it earns by battling. Your Pokemon can max all three. A trainer's Pokemon get average D.V.s "
             "and zero stat experience. So for trainers, level is almost everything."),
    dict(kind='scene', src='game_ai', cue=CALM,
         say="And the A.I. that runs every trainer is simple. A few rules for scoring moves, a bug in how it checks "
             "type matchups, and otherwise, it picks at random."),
    dict(kind='scene', src='rerun', cue=WRY, chapter='Why the old answer is wrong',
         say="So we re-ran that experiment. Every trainer against every trainer, ten thousand battles per pair. "
             "Seven hundred and sixty two million battles. And yeah. Professor Oak. Again."),
    dict(kind='image', src='levels.png', cue=CALM,
         say="But plot each trainer's rank against their average level, and it's almost a straight line. That "
             "ranking doesn't measure teams. It measures levels, played by an A.I. that can barely battle."),
    dict(kind='scene', src='fixes', cue=CALM, chapter='Fixing it',
         say="So, two fixes. Every trainer goes to level one hundred with perfect stats. And our A.I. plays both "
             "sides, so every team gets piloted as well as it can be."),
    dict(kind='clip', src='000022_BLUE_vs_LANCE_p1', start=50, dur=6, label='CHAMPION BLUE vs LANCE', cue=WRY),
    dict(kind='image', src='party_size.png', cue=WRY, chapter='Still a numbers game',
         say="And it was still a numbers game. Six Pokemon beat one, almost no matter what the six are. More "
             "Pokemon beats better Pokemon."),
    dict(kind='scene', src='stadium_rules', cue=CALM, chapter='Stadium rules',
         say="So we borrowed the rule from Pokemon Stadium. Bring six, battle with three. And so nobody loses to a "
             "bad pick, we played every possible three, with every lead, against every other trainer's. Fifty five "
             "million battles. Then we solved for each trainer's best picks."),
    dict(kind='scene', src='countdown_10', cue=BUILD, chapter='The real top 10',
         say="Here's the real top ten. Number ten. A channeler. Two Gastly and a Haunter."),
    dict(kind='scene', src='countdown_09', cue=BUILD, say="Nine. A cooltrainer with the whole Charmander line."),
    dict(kind='scene', src='countdown_08', cue=WRY, say="Eight. Lance. The dragon master, eighth."),
    dict(kind='scene', src='countdown_07', cue=BUILD, say="Seven. A random psychic. Two Kadabra and a Slowpoke."),
    dict(kind='scene', src='countdown_06', cue=WRY, say="Six. Professor Oak. Good. Not number one."),
    dict(kind='scene', src='countdown_05', cue=WRY, say="Five. A juggler. Hypno and two Kadabra. Psychic types run Gen "
                                                         "One."),
    dict(kind='scene', src='countdown_04', cue=BUILD, say="Four. Agatha. Two Gengar and a Haunter."),
    dict(kind='scene', src='countdown_03', cue=BUILD, say="Three. Sabrina."),
    dict(kind='scene', src='countdown_02', cue=BUILD, say="Two. Lorelei. Slowbro, Jynx and Lapras."),
    dict(kind='scene', src='countdown_01', cue=REVEAL,
         say="And number one. Champion Blue. Alakazam, Gyarados, Charizard."),
    dict(kind='clip', src='000001_LORELEI_vs_BLUE_p1', start=40, cue=CALM,
         say="It's close at the top, though. Lorelei's team beats Blue's in some of our games. This is one of them."),
    dict(kind='scene', src='brock', cue=WRY, chapter='And then there is Brock',
         say="And Brock. Brock is two hundred and fifty sixth. He's a gym leader. He loses to bug catchers. We have "
             "questions."),
    dict(kind='clip', src='000243_ERIKA_vs_LANCE_p1', from_end=24, dur=20, speed=1.25, game=1.0, chapter='Highlights',
         label='ERIKA vs LANCE'),
    dict(kind='clip', src='000022_BLUE_vs_LANCE_p1', from_end=26, dur=22, speed=1.25, game=1.0,
         label='CHAMPION BLUE vs LANCE'),
    dict(kind='scene', src='outro', cue=HYPE, chapter='Next time',
         say="Next time. How many level one hundred Magikarp does it take to beat the Elite Four? We're training "
             "the A.I. for that right now."),
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


NARR = MEDIA / 'video' / 'narration'


def line_id(text, cue):
    import hashlib
    return hashlib.sha1(f'{VOICE}|{cue}|{text}'.encode()).hexdigest()[:12]


def tts(text, cue):
    """Narration wav from narration/<id>.wav (voiced on the GPU box by narrate.sh, tts_qwen.py)."""
    wav = NARR / f'{line_id(text, cue)}.wav'
    with wave.open(str(wav)) as w:
        return wav, w.getnframes() / w.getframerate()


# lines not voiced yet -> narration/todo.json, then stop (run narrate.sh, then build again)
NARR.mkdir(parents=True, exist_ok=True)
todo = [dict(id=line_id(g['say'], g.get('cue', '')), text=g['say'], speaker=VOICE, instruct=g.get('cue', ''))
        for g in SEGMENTS if g.get('say')]
todo = [t for t in todo if not (NARR / f"{t['id']}.wav").exists()]
if todo:
    (NARR / 'todo.json').write_text(json.dumps(todo, indent=1))
    print(f'{len(todo)} lines to voice -> {NARR / "todo.json"}; run narrate.sh')
    sys.exit(2)


def esc(t):
    return t.replace(':', '\\:').replace("'", "’")


# 4:3 game picture under a 108 px title band (labels never cover the game's own text box)
# game picture 1280x960 with a white border, on the darkened arena backdrop (scenes/clip_bg.png)
FRAME = ("scale=1280:960:flags=neighbor,pad=1296:976:8:8:white,fps=30[g];[1:v]scale=1920:1080,fps=30[b];"
         "[b][g]overlay=312:96:shortest=1,format=yuv420p")
parts, clock, subs, chapters = [], 0.0, [], []
music_bed = str(MUSIC / 'Music_TrainerBattle.wav')
for i, seg in enumerate(SEGMENTS):
    out = WORK / f'seg_{i:02d}.mp4'
    say = seg.get('say')
    wav, talk = tts(say, seg.get('cue', '')) if say else (None, 0.0)
    dur = max(seg.get('dur', 0), talk + 1.2 if say else 0) or 4
    vf, inputs, amix = [], [], ''
    if seg['kind'] == 'clip':
        src = MEDIA / 'videos_v5' / f"{seg['src']}.mp4"
        speed = seg.get('speed', 1.0)
        start = seg['start'] if 'start' in seg else probe(src) - seg['from_end']
        inputs = ['-ss', f'{start:.2f}', '-i', str(src), '-loop', '1', '-i', str(MEDIA / 'video/scenes/clip_bg.png')]
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
    elif seg['kind'] in ('image', 'scene'):
        img = MEDIA / 'video' / ('charts' if seg['kind'] == 'image' else 'scenes') / seg['src']
        inputs = ['-loop', '1', '-i', str(img if seg['kind'] == 'image' else img.with_suffix('.png'))]
        # slow push-in
        vf.append(f"zoompan=z='min(1.0+0.0003*on,1.04)':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d={int(dur * 30)}:s=1920x1080:fps=30,format=yuv420p")
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
        vf.append(f"drawtext=fontfile={IMPACT}:expansion=none:text='{esc(seg['label'])}':fontcolor=white:fontsize=60:"
                  f"borderw=5:bordercolor=black:x=(w-text_w)/2:y=18")
    n_in = 2
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
