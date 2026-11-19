# Every main2 Lichess blunder (our move drops >= 250 cp, from not-already-lost): how deep is the punishment?
# Stockfish depth 16 line after our move; material (P1 N3 B3 R5 Q9) along it: when does the loss show up.
import os, sys, time
import chess, chess.pgn, chess.engine
SF = os.path.expanduser('~/tools/stockfish16/stockfish/stockfish-ubuntu-x86-64-avx2')
eng = chess.engine.SimpleEngine.popen_uci(SF); eng.configure({'Threads': 4})
V = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9, chess.KING: 0}
mat = lambda b, me: sum(V[p.piece_type] * (1 if p.color == me else -1) for p in b.piece_map().values())
d = os.path.expanduser('~/tools/lichess-bot/game_records')
t0 = time.mktime(time.strptime('2026-11-19 10:27', '%Y-%m-%d %H:%M')) - time.timezone
kinds = {}
for f in sorted(os.listdir(d)):
    p = os.path.join(d, f)
    if not f.endswith('.pgn') or os.path.getmtime(p) < t0: continue
    g = chess.pgn.read_game(open(p)); me = chess.WHITE if g.headers['White'] == 'felix_pufferlib' else chess.BLACK
    b = g.board(); prev = eng.analyse(b, chess.engine.Limit(depth=12))['score'].pov(me).score(mate_score=3000)
    for mv in g.mainline_moves():
        mover = b.turn; b.push(mv)
        info = eng.analyse(b, chess.engine.Limit(depth=12)); cur = info['score'].pov(me).score(mate_score=3000)
        if mover == me and prev - cur >= 250 and -300 < prev < 1500:
            deep = eng.analyse(b, chess.engine.Limit(depth=16)); pv = deep.get('pv', [])[:12]
            bb = b.copy(); m0 = mat(bb, me); ms = []
            opp_first = bb.san(pv[0]) if pv else '?'
            for x in pv:
                bb.push(x); ms.append(mat(bb, me) - m0)
            # settled: first ply after which we stay >= 2 down to the end of the line (trades that recapture do not count)
            first = next((i + 1 for i in range(len(ms)) if all(m <= -2 for m in ms[i:])), None)
            if deep['score'].pov(me).is_mate(): kind = 'mate threat'
            elif first is not None: kind = 'material, settled by ply %d' % first
            else: kind = 'positional (no lasting material loss in 12 plies)'
            k = 'mate' if kind == 'mate threat' else ('1 reply (hangs)' if first == 1 else '2-3 plies' if first and first <= 3
                 else '4-6 plies' if first and first <= 6 else '7+ plies' if first else 'positional')
            kinds[k] = kinds.get(k, 0) + 1
            print(f'{g.headers["TimeControl"]:6s} move {(b.ply() + 1) // 2:3d} {mv.uci()} {prev:+5d} -> {cur:+5d}  reply {opp_first:6s} {kind}', flush=True)
        prev = cur
eng.quit()
print(kinds)
