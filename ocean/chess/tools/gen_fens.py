"""Starting-position banks for env.fen_curric_pct (2026-11-02), written to resources/chess/fens.txt style files.
  endgame N OUT: random legal K+Q vs K and K+R vs K positions, the strong side to move (either color), not over.
  mate1 N OUT: the same endgames, kept only when the side to move has a checkmate in one.
  maia N OUT --weights lc0/maia-1500.pb.gz --lc0 ./lc0bin: positions from Maia self-play games, each opened with
    2-10 random plies, one FEN per ply (game-over positions skipped).
Usage: python gen_fens.py endgame 20000 out.txt | python gen_fens.py maia 50000 out.txt [--weights W] [--lc0 BIN]
"""
import random
import sys

import chess
import chess.engine


def endgame(n, rng):
    out = []
    while len(out) < n:
        strong = rng.choice([chess.WHITE, chess.BLACK])
        piece = rng.choice([chess.QUEEN, chess.ROOK])
        b = chess.Board(None)
        sq = rng.sample(range(64), 3)
        b.set_piece_at(sq[0], chess.Piece(chess.KING, strong))
        b.set_piece_at(sq[1], chess.Piece(piece, strong))
        b.set_piece_at(sq[2], chess.Piece(chess.KING, not strong))
        b.turn = strong
        if b.is_valid() and not b.is_game_over() and not b.is_check():
            out.append(b.fen())
    return out


def mate_in_one(b):
    for m in b.legal_moves:
        b.push(m)
        mate = b.is_checkmate()
        b.pop()
        if mate:
            return True
    return False


def mate1(n, rng):
    out = []
    while len(out) < n:
        for fen in endgame(256, rng):
            b = chess.Board(fen)
            if mate_in_one(b):
                out.append(fen)
    return out[:n]


def maia(n, rng, weights, lc0):
    eng = chess.engine.SimpleEngine.popen_uci([lc0, f'--weights={weights}'])
    out = []
    try:
        while len(out) < n:
            b = chess.Board()
            for _ in range(rng.randint(2, 10)):
                moves = list(b.legal_moves)
                if not moves:
                    break
                b.push(rng.choice(moves))
            for _ in range(160):
                if b.is_game_over():
                    break
                out.append(b.fen())
                b.push(eng.play(b, chess.engine.Limit(nodes=1)).move)
    finally:
        eng.quit()
    return out[:n]


if __name__ == '__main__':
    kind, n, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    args = dict(zip(sys.argv[4::2], sys.argv[5::2]))
    rng = random.Random(int(args.get('--seed', 20261102)))
    fens = endgame(n, rng) if kind == 'endgame' else mate1(n, rng) if kind == 'mate1' else maia(n, rng, args.get('--weights', 'lc0/maia-1500.pb.gz'),
                                                        args.get('--lc0', './lc0bin'))
    open(path, 'w').write('\n'.join(fens) + '\n')
    print(f'{len(fens)} positions -> {path}')
