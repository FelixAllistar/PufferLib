"""Opening state sets for env.fen_curric_pct (2026-11-02): positions from games where OUR side plays one fixed
repertoire and the other side is Maia (1100/1500/1900, picked per game) with some randomness, so we learn our
opening against many replies. Our side follows the book below while in it, then Maia-1900 plays for us; every
position with our side to move up to MAX_PLY becomes a starting position.

Repertoire (simple and aggressive):
  White: Italian Game 1.e4 e5 2.Nf3 Nc6 3.Bc4 (Giuoco Piano 3...Bc5 4.c3; Fried Liver attack 3...Nf6 4.Ng5),
         with plain developing answers to the other first replies.
  Black: 1.e4 e5 (2.Nf3 Nc6, 3.Bc4 Bc5, 3.Bb5 a6, 3.d4 exd4); 1.d4 d5 2.c4 e6 (Queen's Gambit Declined),
         2.Nf3 / 2.Bf4 Nf6; 1.c4 e5; 1.Nf3 d5.
  italian: ONE opening only (2026-11-03): the Italian Game, 1.e4 e5 2.Nf3 Nc6 3.Bc4 forced for both sides, then the
  Giuoco Piano / Fried Liver book for White and Maia (1100/1500/1900, some random replies) for Black, and Maia-1900
  for White out of book. Positions with EITHER side to move are kept, so both sides of the Italian are learned.
Usage: python gen_openings.py white|black|italian N OUT.txt [--lc0 ./lc0bin] [--seed S] [--max-ply 60]
"""
import random
import sys

import chess
import chess.engine

# our move after a SAN move list (the game so far); missing keys = out of book
WHITE_BOOK = {
    '': 'e4',
    'e4 e5': 'Nf3', 'e4 e5 Nf3 Nc6': 'Bc4',
    'e4 e5 Nf3 Nc6 Bc4 Bc5': 'c3', 'e4 e5 Nf3 Nc6 Bc4 Bc5 c3 Nf6': 'd4',
    'e4 e5 Nf3 Nc6 Bc4 Nf6': 'Ng5', 'e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 d5': 'exd5',
    'e4 e5 Nf3 d6': 'd4', 'e4 e5 Nf3 Nf6': 'Nxe5', 'e4 e5 Nf3 Nc6 Bc4 Be7': 'd4',
    'e4 c5': 'Nf3', 'e4 c5 Nf3 d6': 'd4', 'e4 c5 Nf3 Nc6': 'd4', 'e4 c5 Nf3 e6': 'd4',
    'e4 e6': 'd4', 'e4 e6 d4 d5': 'Nc3', 'e4 c6': 'd4', 'e4 c6 d4 d5': 'Nc3',
    'e4 d5': 'exd5', 'e4 d5 exd5 Qxd5': 'Nc3', 'e4 d6': 'd4', 'e4 Nf6': 'e5', 'e4 g6': 'd4',
}
BLACK_BOOK = {
    'e4': 'e5', 'e4 e5 Nf3': 'Nc6', 'e4 e5 Nf3 Nc6 Bc4': 'Bc5', 'e4 e5 Nf3 Nc6 Bb5': 'a6',
    'e4 e5 Nf3 Nc6 d4': 'exd4', 'e4 e5 Nc3': 'Nf6', 'e4 e5 Bc4': 'Nf6',
    'd4': 'd5', 'd4 d5 c4': 'e6', 'd4 d5 c4 e6 Nc3': 'Nf6', 'd4 d5 Nf3': 'Nf6', 'd4 d5 Bf4': 'Nf6',
    'c4': 'e5', 'Nf3': 'd5', 'e3': 'e5', 'g3': 'd5', 'b3': 'e5', 'f4': 'd5',
}
# the opponent's first move is drawn from these (then Maia), so the common lines all show up
WHITE_FIRST = [('e4', 50), ('d4', 35), ('c4', 8), ('Nf3', 7)]
BLACK_REPLY = [('e5', 45), ('c5', 20), ('e6', 10), ('c6', 10), ('d5', 8), (None, 7)]


def pick(rng, table):
    r, acc = rng.uniform(0, sum(w for _, w in table)), 0
    for move, w in table:
        acc += w
        if r <= acc:
            return move
    return table[-1][0]


ITALIAN = ['e4', 'e5', 'Nf3', 'Nc6', 'Bc4']


def play_italian(rng, engines, max_ply, out):
    opp = engines[rng.choice(['1100', '1500', '1900'])]
    b, sans = chess.Board(), []
    in_book = True
    while not b.is_game_over() and len(sans) < max_ply:
        out.append(b.fen())
        key = ' '.join(sans)
        if len(sans) < len(ITALIAN):
            move = b.parse_san(ITALIAN[len(sans)])
        elif b.turn == chess.WHITE:
            mv = WHITE_BOOK.get(key) if in_book else None
            if mv is None:
                in_book = False
                move = engines['1900'].play(b, chess.engine.Limit(nodes=1)).move
            else:
                move = b.parse_san(mv)
        else:
            move = rng.choice(list(b.legal_moves)) if len(sans) < 12 and rng.random() < 0.08 else \
                opp.play(b, chess.engine.Limit(nodes=1)).move
        sans.append(b.san(move))
        b.push(move)


def play_game(rng, us, engines, max_ply, out):
    book = WHITE_BOOK if us == chess.WHITE else BLACK_BOOK
    opp = engines[rng.choice(['1100', '1500', '1900'])]
    b, sans = chess.Board(), []
    in_book = True
    while not b.is_game_over() and len(sans) < max_ply:
        key = ' '.join(sans)
        if b.turn == us:
            out.append(b.fen())
            mv = book.get(key) if in_book else None
            if mv is None:
                in_book = False
                move = engines['1900'].play(b, chess.engine.Limit(nodes=1)).move
            else:
                move = b.parse_san(mv)
        else:
            move = None
            if len(sans) == 0:
                first = pick(rng, WHITE_FIRST)
                move = b.parse_san(first)
            elif len(sans) == 1 and us == chess.WHITE:
                reply = pick(rng, BLACK_REPLY)
                move = b.parse_san(reply) if reply else None
            if move is None and len(sans) < 10 and rng.random() < 0.08:
                move = rng.choice(list(b.legal_moves))  # an off-book reply now and then
            if move is None:
                move = opp.play(b, chess.engine.Limit(nodes=1)).move
        sans.append(b.san(move))
        b.push(move)


def main():
    side, n, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    args = dict(zip(sys.argv[4::2], sys.argv[5::2]))
    lc0 = args.get('--lc0', './lc0bin')
    rng = random.Random(int(args.get('--seed', 20261102)))
    max_ply = int(args.get('--max-ply', 60))
    us = chess.WHITE if side == 'white' else chess.BLACK
    engines = {e: chess.engine.SimpleEngine.popen_uci([lc0, f'--weights=lc0/maia-{e}.pb.gz']) for e in
               ('1100', '1500', '1900')}
    out = []
    try:
        while len(out) < n:
            if side == 'italian':
                play_italian(rng, engines, max_ply, out)
            else:
                play_game(rng, us, engines, max_ply, out)
    finally:
        for e in engines.values():
            e.quit()
    open(path, 'w').write('\n'.join(out[:n]) + '\n')
    print(f'{min(n, len(out))} {side} positions -> {path}')


if __name__ == '__main__':
    main()
