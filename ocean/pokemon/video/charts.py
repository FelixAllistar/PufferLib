"""Charts for the Gen 1 trainer video (2026-11-01), 1920x1080 PNGs drawn from our real results.
Usage: uv run --with matplotlib --with numpy python charts.py OUT_DIR
"""
import json
import pathlib
import sys

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt  # noqa: E402

R = pathlib.Path(__file__).resolve().parents[3] / 'reports/pokemon_experiments/20261030_gen1_trainers'
OUT = pathlib.Path(sys.argv[1])
OUT.mkdir(parents=True, exist_ok=True)
BG, FG, ACCENT, DIM, GOOD, BAD = '#101828', '#F2F4F7', '#F8D030', '#667085', '#4ADE80', '#F87171'
plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.weight': 'bold', 'text.color': FG, 'axes.labelcolor': FG,
                     'xtick.color': FG, 'ytick.color': FG, 'axes.edgecolor': DIM})


def figure():
    fig = plt.figure(figsize=(19.2, 10.8), dpi=100, facecolor=BG)
    ax = fig.add_axes([0.08, 0.1, 0.84, 0.75], facecolor=BG)
    for s in ax.spines.values():
        s.set_visible(False)
    return fig, ax


def save(fig, name):
    fig.savefig(OUT / name, facecolor=BG)
    plt.close(fig)


ranks = {k: {r['name']: r['rank'] for r in json.load(open(R / f))}
         for k, f in (('asis', 'rank_10k.json'), ('l100', 'rank_l100.json'), ('ours', 'rank_sets.json'))}
LABEL = {'RIVAL3 #3': 'CHAMPION BLUE', 'RIVAL3 #1': 'CHAMPION BLUE', 'RIVAL2 #9': 'RIVAL BLUE (mid-game)',
         'COOLTRAINER♀ #3': 'COOLTRAINER (F)', 'COOLTRAINER♂ #5': 'COOLTRAINER (M)', 'CHANNELER #24': 'A CHANNELER', 'PROF.OAK #1': 'PROF. OAK', 'GIOVANNI #3': 'GIOVANNI', 'JUGGLER #2': 'A JUGGLER',
         'FISHERMAN #7': 'A FISHERMAN', 'BIKER #7': 'A BIKER'}


def name(k):
    return LABEL.get(k, k.split(' #')[0].replace('LT.SURGE', 'LT. SURGE').replace('PROF.OAK', 'PROF. OAK'))


# 1) rank tables: original levels with the game's AI, level 100 with the game's AI (whole team), level 100 with our
# AI picking its best 3; sorted by the last, with the change from the game-AI level 100 ranking
def journey(keys, fname, title, show=None):
    if show is None:  # the full table plus one frame per row revealed (fname_1.png ... fname_N.png)
        for k in range(1, len(keys) + 1):
            journey(keys, fname.replace('.png', f'_{k}.png'), title, k)
        show = len(keys)
    fig, ax = figure()
    ax.axis('off')
    keys = sorted(keys, key=lambda k: ranks['ours'][k])
    cols = [0.02, 0.42, 0.6, 0.78, 0.96]
    heads = ['TRAINER', 'ORIGINAL\ngame AI', 'LEVEL 100\ngame AI', 'LEVEL 100\nOUR AI', 'CHANGE']
    for x, h in zip(cols, heads):
        ax.text(x, 1.04, h, ha='left' if x == cols[0] else 'center', va='center', fontsize=24, color=ACCENT)
    step = 0.9 / len(keys)
    for i, k in enumerate(keys[:show]):
        y = 0.9 - i * step
        a, b, c = ranks['asis'][k], ranks['l100'][k], ranks['ours'][k]
        d = b - c
        col = GOOD if d > 0 else BAD if d < 0 else FG
        ax.text(cols[0], y, name(k), ha='left', va='center', fontsize=34)
        ax.text(cols[1], y, f'#{a}', ha='center', va='center', fontsize=32, color=DIM)
        ax.text(cols[2], y, f'#{b}', ha='center', va='center', fontsize=32, color=DIM)
        ax.text(cols[3], y, f'#{c}', ha='center', va='center', fontsize=38)
        ax.text(cols[4], y, ('+' if d > 0 else '') + str(d) if d else '=', ha='center', va='center', fontsize=34,
                color=col)
    fig.text(0.5, 0.95, title, ha='center', va='center', fontsize=42)
    save(fig, fname)


journey(['RIVAL3 #3', 'LORELEI #1', 'SABRINA #1', 'AGATHA #1', 'LANCE #1', 'PROF.OAK #1', 'BRUNO #1', 'BLAINE #1'],
        'journey_elite.png', 'THE TOP TRAINERS')
journey(['MISTY #1', 'LT.SURGE #1', 'ERIKA #1', 'KOGA #1', 'GIOVANNI #3', 'BROCK #1', 'FISHERMAN #7', 'BIKER #7'],
        'journey_gyms.png', 'GYM LEADERS (AND SOME RANDOS)')

# 2) at level 100 with the game's AI, party size decides everything
fig, ax = figure()
sizes, med = [1, 2, 3, 4, 5, 6], [324, 252, 156, 96, 36, 4]
bars = ax.bar([str(s) for s in sizes], [392 - m for m in med], color=[DIM] * 5 + [ACCENT], width=0.65)
for b, m in zip(bars, med):
    ax.text(b.get_x() + b.get_width() / 2, b.get_height() + 6, f'#{m}', ha='center', fontsize=34)
ax.set_yticks([])
ax.set_xlabel('POKEMON IN THE PARTY', fontsize=26, labelpad=16)
ax.tick_params(axis='x', labelsize=30, length=0)
fig.text(0.5, 0.94, 'LEVEL 100, GAME AI: TYPICAL RANK BY PARTY SIZE', ha='center', fontsize=38)
save(fig, 'party_size.png')

# 3) Weedle: our AI vs Poison Sting spam vs Gym Leader Matt (1 in N; higher is worse)
fig, ax = figure()
ax.set_position([0.17, 0.12, 0.76, 0.72])
rows = [('BRUNO', 6, 5.5, 3), ('LANCE', 29, 31, 31), ('LORELEI', 1818, 2633, 4000), ('CHAMPION', 3400, 4770, 25000)]
import numpy as np  # noqa: E402
y = np.arange(len(rows))[::-1]
for i, (lbl, spam, matt, ours) in enumerate(rows):
    for j, (v, c, tag) in enumerate(((matt, DIM, 'MATT'), (spam, '#94A3B8', 'SPAM (OUR ENGINE)'), (ours, ACCENT, 'OUR AI'))):
        ax.barh(y[i] + (1 - j) * 0.26, np.log10(v) + 0.3, height=0.24, color=c, label=tag if i == 0 else None)
        ax.text(np.log10(v) + 0.36, y[i] + (1 - j) * 0.26, f'1 in {v:,.1f}'.replace('.0', ''), va='center',
                fontsize=20)
ax.set_yticks(y, [r[0] for r in rows], fontsize=30)
ax.tick_params(axis='y', length=0)
ax.set_xticks([])
ax.legend(loc='upper right', fontsize=24, frameon=False)
fig.text(0.5, 0.94, 'LEVEL 100 WEEDLE: CHANCE TO WIN  (shorter = better)', ha='center', fontsize=38)
fig.text(0.5, 0.06, 'AGATHA: 0 wins in 20,000 for everyone', ha='center', fontsize=30, color=BAD)
save(fig, 'weedle_odds.png')

# 4) the final top 10 (our AI, every possible team tested)
# best version of each trainer class (the rival alone has a dozen teams)
top, seen = [], set()
for k, r in sorted(ranks['ours'].items(), key=lambda kv: kv[1]):
    cls = 'RIVAL' if k.startswith('RIVAL') else k.split(' #')[0]
    if cls not in seen:
        seen.add(cls)
        top.append((k, r))
top = top[:10]
fig, ax = figure()
ax.axis('off')
for i, (k, r) in enumerate(top):
    ax.text(0.25, 0.9 - i * 0.095, f'#{r}', fontsize=40, color=ACCENT, va='center', ha='right')
    ax.text(0.3, 0.9 - i * 0.095, name(k), fontsize=40, va='center')
fig.text(0.5, 0.95, 'THE REAL TOP 10', ha='center', fontsize=48)
fig.text(0.5, 0.9, 'best version of each trainer - rank out of 391 teams - 55 million battles', ha='center', fontsize=26,
         color=DIM)
save(fig, 'top10.png')
print('charts ->', OUT)
