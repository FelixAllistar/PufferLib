"""Which openings does a chess policy play (2026-11-02)? Reads the PGNs an eval wrote (CHESS_PGN_OUT=prefix) and
reports, for the Learner, its first moves as White and its replies as Black, and how often it played the repertoire
of tools/gen_openings.py (White: 1.e4, 2.Nf3 after 1...e5, 3.Bc4 after 2...Nc6; Black: 1...e5 vs 1.e4,
2...Nc6 vs 2.Nf3, 1...d5 vs 1.d4, 2...e6 vs 2.c4). Also the score by color.
Usage: python opening_report.py 'PREFIX*.pgn'
"""
import collections
import glob
import sys

import chess.pgn

BOOK_W = {'': 'e4', 'e4 e5': 'Nf3', 'e4 e5 Nf3 Nc6': 'Bc4'}
BOOK_B = {'e4': 'e5', 'e4 e5 Nf3': 'Nc6', 'd4': 'd5', 'd4 d5 c4': 'e6'}

games = []
for path in glob.glob(sys.argv[1]):
    with open(path) as f:
        while (g := chess.pgn.read_game(f)) is not None:
            games.append(g)
first = {'w': collections.Counter(), 'b': collections.Counter()}
book = collections.defaultdict(lambda: [0, 0])
score = {'w': [0.0, 0], 'b': [0.0, 0]}
for g in games:
    side = 'w' if g.headers.get('White') == 'Learner' else 'b' if g.headers.get('Black') == 'Learner' else None
    if side is None:
        continue
    res = g.headers.get('Result', '*')
    pts = {'1-0': 1.0, '0-1': 0.0, '1/2-1/2': 0.5, 'White Wins': 1.0, 'Black Wins': 0.0, 'Draw': 0.5}.get(res)
    if pts is not None:
        score[side][0] += pts if side == 'w' else 1 - pts
        score[side][1] += 1
    b, sans = g.board(), []
    for mv in g.mainline_moves():
        san = b.san(mv)
        mover = 'w' if b.turn == chess.WHITE else 'b'
        key = ' '.join(sans)
        if mover == side and len(sans) < 6:
            if len(sans) <= 1:
                first[side][(key + ' ' + san).strip() if side == 'b' else san] += 1
            table = BOOK_W if side == 'w' else BOOK_B
            if key in table:
                book[(side, key)][0] += san == table[key]
                book[(side, key)][1] += 1
        sans.append(san)
        b.push(mv)
print(f'{len(games)} games')
for side, name in (('w', 'White'), ('b', 'Black')):
    s, n = score[side]
    print(f'\nLearner as {name}: {n} games, score {s / max(1, n):.3f}')
    print('  first moves:', ', '.join(f'{m} {c}' for m, c in first[side].most_common(8)))
    for (sd, key), (hit, tot) in sorted(book.items()):
        if sd == side:
            want = (BOOK_W if side == 'w' else BOOK_B)[key]
            print(f'  after "{key or "start"}": book move {want} {hit}/{tot} = {hit / tot:.0%}')
