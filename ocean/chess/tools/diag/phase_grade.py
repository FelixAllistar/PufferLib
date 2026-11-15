"""Grade our moves by game phase (2026-11-14): Stockfish depth 12 cp loss per move; opening (first 12 of our moves
in a game from the start position), endgame (non-pawn material of both sides <= 26 pawns' worth), else middlegame.
Also: the phase of the decisive mistake in each lost game, and of the move that threw away a won game.
Usage: phase_grade.py OUT.json lichess|jsonl PATHS... (python-chess from the lichess-bot venv)."""
import sys, json, glob, chess, chess.pgn, chess.engine
from multiprocessing import Pool
SF = '/home/felix/tools/stockfish16/stockfish/stockfish-ubuntu-x86-64-avx2'
V = {chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9}

def phase(b, our_idx, from_start):
    npm = sum(V.get(p.piece_type, 0) for p in b.piece_map().values())
    if npm <= 26: return 'endgame'
    return 'opening' if from_start and our_idx < 12 else 'middlegame'

def grade(job):
    fen, moves, us, result = job
    sf = chess.engine.SimpleEngine.popen_uci(SF); L = chess.engine.Limit(depth=12)
    b = chess.Board(fen) if fen else chess.Board()
    ev = lambda: sf.analyse(b, L)['score'].pov(us).score(mate_score=2000)
    out, e, k = [], ev(), 0
    for m in moves:
        mover = b.turn
        ph = phase(b, k, not fen) if mover == us else None
        b.push(chess.Move.from_uci(m)); e2 = ev()
        if mover == us:
            out.append((ph, e, e2)); k += 1
        e = e2
    sf.quit()
    return out, result

def jobs(kind, paths):
    J = []
    for p in paths:
        if kind == 'lichess':
            g = chess.pgn.read_game(open(p)); h = g.headers
            us = chess.WHITE if h['White'] == 'felix_pufferlib' else chess.BLACK
            r = {'1-0': 1.0, '0-1': 0.0}.get(h['Result'], 0.5); r = r if us == chess.WHITE else 1 - r
            J.append((None, [m.uci() for m in g.mainline_moves()], us, r))
        else:
            for i, l in enumerate(open(p)):
                if i >= 120: break
                d = json.loads(l)
                J.append((d['fen'], d['moves'], chess.WHITE if d['color'] == 0 else chess.BLACK, d['result']))
    return J

if __name__ == '__main__':
    out, kind, paths = sys.argv[1], sys.argv[2], sys.argv[3:]
    with Pool(4) as pool: res = pool.map(grade, jobs(kind, paths))
    json.dump(res, open(out, 'w'))
