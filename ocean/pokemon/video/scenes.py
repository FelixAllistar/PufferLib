"""Designed 1920x1080 scenes for the Gen 1 video (2026-11-01), Stadium style (stadium.py), filled from our results.
Usage: uv run --with pillow python scenes.py OUT_DIR
"""
import json
import pathlib
import sys

from PIL import Image

from stadium import (GRAY, GREEN, RED, WHITE, YELLOW, BLUE_OUT, arena, cross, mon, panel, paste_c, text, trainer)

OUT = pathlib.Path(sys.argv[1])
OUT.mkdir(parents=True, exist_ok=True)
W, H = 1920, 1080
ROOT = pathlib.Path(__file__).resolve().parents[3]
R = ROOT / 'reports/pokemon_experiments/20261030_gen1_trainers'
TRAINERS = {f"{t['name']} #{t['index']}": t for t in json.load(open(ROOT / 'ocean/pokemon/cartridge/trainers.json'))}
RANK = json.load(open(R / 'rank_sets.json'))
PICKS = {p['name']: p for p in json.load(open(R / 'sets_picks.json'))}
DISPLAY = {'RIVAL3': 'CHAMPION BLUE', 'PROF_OAK': 'PROF. OAK', 'COOLTRAINER_M': 'COOLTRAINER', 'LT_SURGE': 'LT. SURGE',
           'PSYCHIC_TR': 'PSYCHIC'}


def label(cls):
    return DISPLAY.get(cls, cls)


def save(im, name):
    im.convert('RGB').save(OUT / f'{name}.png')


def best3(key):
    s = PICKS[key]['sets'][0]
    return [s['lead']] + s['with']


# hook: our credentials
im = arena(dim=0.35)
text(im, (W / 2, 120), 'OUR AI', 120, YELLOW, BLUE_OUT)
for i, (big, small) in enumerate((('68 - 32', 'vs METAMON, THE BEST PUBLISHED POKEMON AI'),
                                  ('201 - 98', 'ON THE PUBLIC GEN 1 LADDER'))):
    x0 = 160 + i * 820
    panel(im, (x0, 290, x0 + 780, 800))
    text(im, (x0 + 390, 470), big, 190, WHITE)
    text(im, (x0 + 390, 680), small, 40 if i == 0 else 46, GRAY, shadow=False, width=2)
text(im, (W / 2, 930), 'trained only by playing itself - no human games', 48, WHITE, shadow=False, width=3)
save(im, 'credentials')

# title card
im = arena()
paste_c(im, trainer('PROF_OAK', 8), 330, 980)
cross(im, (170, 560, 500, 960), 30)
paste_c(im, mon('CHARIZARD', 7), 1420, 960)
paste_c(im, trainer('RIVAL3', 8), 1660, 990)
text(im, (W / 2, 170), 'WHO IS ACTUALLY THE', 120, WHITE)
text(im, (W / 2, 330), 'STRONGEST TRAINER?', 170, YELLOW, BLUE_OUT)
save(im, 'title')


def team_card(key, name, sub, levels=True, scale=5, file=None, highlight=None):
    im = arena(dim=0.45, floor=False)
    t = TRAINERS[key]
    paste_c(im, trainer(t['class'], 7), 280, 960)
    text(im, (W / 2, 95), name, 110, YELLOW, BLUE_OUT)
    text(im, (W / 2, 190), sub, 46, WHITE, shadow=False, width=3)
    party = t['party']
    for i, m in enumerate(party):
        cx, cy = 800 + (i % 3) * 390, 470 + (i // 3) * 390
        panel(im, (cx - 180, cy - 190, cx + 180, cy + 180), outline=YELLOW if highlight and m['species'] in highlight
              else WHITE)
        paste_c(im, mon(m['species'], scale), cx, cy + 90)
        lvl = f"Lv{m['level']}" if levels else ''
        text(im, (cx, cy + 135), f"{m['species'].replace('_', '. ')}  {lvl}".strip(), 36, WHITE, shadow=False, width=2)
    return im


# the popular answer: Oak (his unused team, ranked #1 in the old style tournament)
save(team_card('PROF.OAK #1', 'PROF. OAK', 'the popular #1 - you never battle him'), 'oak_team')

# Gen 1 crash course: where stats come from
im = arena(dim=0.5, floor=False)
text(im, (W / 2, 110), 'WHERE STATS COME FROM', 100, YELLOW, BLUE_OUT)
paste_c(im, mon('ALAKAZAM', 8), 330, 820)
rows = (('LEVEL', 'up to 100', 'fixed', 'the big one'), ('DVs', 'up to 15', 'about 8', 'hidden genes'),
        ('STAT EXP', 'maxed', 'ZERO', 'from battling'))
panel(im, (640, 230, 1830, 950))
text(im, (1180, 300), 'YOUR POKEMON', 44, GREEN, shadow=False, width=2)
text(im, (1580, 300), "A TRAINER'S", 44, RED, shadow=False, width=2)
for i, (k, mine, theirs, note) in enumerate(rows):
    y = 450 + i * 190
    text(im, (700, y), k, 70, WHITE, anchor='lm')
    text(im, (700, y + 60), note, 32, GRAY, anchor='lm', shadow=False, width=1)
    text(im, (1180, y), mine, 60, GREEN, shadow=False, width=3)
    text(im, (1580, y), theirs, 60, RED, shadow=False, width=3)
save(im, 'stats')

# the trainer AI
im = arena(dim=0.5, floor=False)
text(im, (W / 2, 110), "THE GAME'S AI", 100, YELLOW, BLUE_OUT)
paste_c(im, trainer('BUG_CATCHER', 7), 330, 880)
panel(im, (640, 230, 1830, 900))
for i, s in enumerate(('a few rules for scoring moves', 'a bug in its type matchup check',
                       'otherwise: picks at random', 'some use potions and X items')):
    text(im, (720, 340 + i * 150), f'{i + 1}.  {s}', 58, WHITE, anchor='lm', shadow=False, width=3)
save(im, 'game_ai')

# the old experiment re-run: one huge number
im = arena(dim=0.4, floor=False)
text(im, (W / 2, 260), '762,450,000', 230, WHITE)
text(im, (W / 2, 420), 'BATTLES - EVERY TRAINER vs EVERY TRAINER - GAME AI', 50, GRAY, shadow=False, width=2)
paste_c(im, trainer('PROF_OAK', 8), W / 2 - 300, 1010)
text(im, (W / 2 + 230, 760), '#1', 260, YELLOW, BLUE_OUT)
text(im, (W / 2 + 230, 920), 'PROF. OAK. AGAIN.', 70, WHITE)
save(im, 'rerun')

# two fixes
im = arena(dim=0.45, floor=False)
text(im, (W / 2, 110), 'TWO FIXES', 110, YELLOW, BLUE_OUT)
for i, (head, body, art) in enumerate((('LEVEL 100', 'every trainer, perfect stats', 'RHYDON'),
                                        ('OUR AI', 'plays both sides', None))):
    x0 = 160 + i * 820
    panel(im, (x0, 240, x0 + 780, 960))
    text(im, (x0 + 390, 330), head, 100, WHITE)
    text(im, (x0 + 390, 430), body, 44, GRAY, shadow=False, width=2)
    if art:
        paste_c(im, mon(art, 7), x0 + 390, 900)
    else:
        text(im, (x0 + 390, 660), '68 - 32', 150, GREEN)
        text(im, (x0 + 390, 790), 'vs Metamon', 50, GRAY, shadow=False, width=2)
save(im, 'fixes')

# Stadium rules: bring six, battle with three (Blue's party, his best three highlighted)
blue3 = best3('RIVAL3 #3')
im = team_card('RIVAL3 #3', 'BRING 6', 'battle with 3', levels=False, highlight=set(blue3))
text(im, (280, 300), 'STADIUM RULES', 50, WHITE, shadow=False, width=3)
save(im, 'stadium_rules')

# countdown: best version of each trainer class, #10 -> #1
top, seen = [], set()
for r in RANK:
    c = 'RIVAL' if r['name'].startswith('RIVAL') else r['name'].split(' #')[0]
    if c not in seen:
        seen.add(c)
        top.append(r)
top = top[:10]
for place, r in reversed(list(enumerate(top, 1))):
    t = TRAINERS[r['name']]
    im = arena(dim=0.45, floor=False)
    big = place == 1
    paste_c(im, trainer(t['class'], 8 if big else 7), 300, 1000)
    text(im, (300, 170), f'#{place}', 230 if big else 190, YELLOW, BLUE_OUT)
    name = label(t['class']).replace('CHANNELER', 'A CHANNELER').replace('PSYCHIC', 'A PSYCHIC') \
        .replace('JUGGLER', 'A JUGGLER').replace('COOLTRAINER', 'A COOLTRAINER')
    text(im, (1190, 140), name, 120 if len(name) < 14 else 96, WHITE)
    for i, sp in enumerate(best3(r['name'])):
        cx = 820 + i * 370
        panel(im, (cx - 170, 330, cx + 170, 860), outline=YELLOW if big else WHITE)
        paste_c(im, mon(sp, 6 if big else 5), cx, 760)
        text(im, (cx, 815), sp.replace('_', '. '), 44, WHITE, shadow=False, width=3)
    text(im, (1190, 950), f"their best three  -  overall #{r['rank']} of 391 teams", 44, GRAY, shadow=False, width=2)
    save(im, f'countdown_{place:02d}')

# Brock
im = arena(dim=0.3)
paste_c(im, trainer('BROCK', 9), 520, 1000)
text(im, (1300, 300), '#256', 300, RED)
text(im, (1300, 520), 'OUT OF 391', 90, WHITE)
for i, sp in enumerate(best3('BROCK #1')):
    paste_c(im, mon(sp, 5), 1150 + i * 330, 960)
save(im, 'brock')

# outro: next time, Magikarp
im = arena()
paste_c(im, mon('MAGIKARP', 9), W / 2, 960)
text(im, (W / 2, 150), 'NEXT TIME', 90, WHITE)
text(im, (W / 2, 300), 'HOW MANY MAGIKARP?', 150, YELLOW, BLUE_OUT)
save(im, 'outro')

# backdrop for battle footage (4:3 picture goes on top)
save(arena(dim=0.55, floor=False), 'clip_bg')
print('scenes ->', OUT)
