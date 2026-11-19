#!/usr/bin/env python3
"""Bank groups for the chess state-group teacher (chess_teacher.h, 2026-11-16): random legal positions of fixed
material where one side is ahead by 3+ points. No engine and no games are used: pieces are placed at random, the
position must be legal and not over, and when the weaker side is to move it must have no capture (so the lead is
real at the start). Either color is the strong side; the strong side is to move in 2 of 3 positions.
Writes OUTDIR/<name>.txt (FENs) and OUTDIR/teacher_groups.txt ("name path" lines; copy or point
resources/chess/teacher_groups.txt at it). Usage: gen_teacher_banks.py OUTDIR [--n 20000] [--seed 777]"""
import argparse, os, random, sys
import chess

P = {'Q': chess.QUEEN, 'R': chess.ROOK, 'B': chess.BISHOP, 'N': chess.KNIGHT, 'P': chess.PAWN}
SETS = {  # name: (strong side's pieces beside the king, weak side's)
    'kqk': ('Q', ''), 'krk': ('R', ''), 'krrk': ('RR', ''), 'kbbk': ('BB', ''), 'kqkr': ('Q', 'R'),
    'kqrkr': ('QR', 'R'), 'kqppkpp': ('QPP', 'PP'), 'krppkpp': ('RPP', 'PP'), 'knppkpp': ('NPPP', 'PP'),
    'kqnppkrpp': ('QNPP', 'RPP'), 'krrppkrpp': ('RRPP', 'RPP'), 'krnppknpp': ('RNPP', 'NPP'),
    'kqbpppkrnppp': ('QBPPP', 'RNPPP'),
}


def rand_pos(rng, strong_pcs, weak_pcs):
    while True:
        strong = rng.random() < 0.5
        b = chess.Board(None)
        sqs = rng.sample(range(64), 2 + len(strong_pcs) + len(weak_pcs))
        b.set_piece_at(sqs[0], chess.Piece(chess.KING, strong)); b.set_piece_at(sqs[1], chess.Piece(chess.KING, not strong))
        ok = True
        for i, c in enumerate(strong_pcs + weak_pcs):
            if c == 'P' and sqs[2 + i] // 8 in (0, 7):
                ok = False
            b.set_piece_at(sqs[2 + i], chess.Piece(P[c], strong if i < len(strong_pcs) else (not strong)))
        if 'BB' in strong_pcs and len({(s % 8 + s // 8) % 2 for s in b.pieces(chess.BISHOP, strong)}) < 2:
            ok = False  # two bishops on one color cannot mate
        b.turn = strong if rng.random() < 2 / 3 else (not strong)
        if not ok or not b.is_valid() or b.is_game_over() or b.is_insufficient_material():
            continue
        if b.turn != strong and any(b.is_capture(m) for m in b.legal_moves):
            continue
        return b.fen()


ap = argparse.ArgumentParser()
ap.add_argument('outdir'); ap.add_argument('--n', type=int, default=20000); ap.add_argument('--seed', type=int, default=777)
a = ap.parse_args()
os.makedirs(a.outdir, exist_ok=True)
rng = random.Random(a.seed)
with open(os.path.join(a.outdir, 'teacher_groups.txt'), 'w') as g:
    g.write('# chess teacher bank groups (gen_teacher_banks.py): name path\n')
    for name, (s, w) in SETS.items():
        fens = set()
        while len(fens) < a.n:
            fens.add(rand_pos(rng, s, w))
        path = os.path.join(a.outdir, name + '.txt')
        open(path, 'w').write('\n'.join(sorted(fens)) + '\n')
        g.write(f'{name} {path}\n')
        print(name, len(fens), flush=True)
