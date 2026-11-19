# Real-opponent failure states (2026-11-19): main2 vs Maia 1900 games (CHESS_GAMES_OUT lines: our color, result, start
# fen, moves). Stockfish depth 9 after every ply, main2's view. A collapse = main2 held >= -100 on two of its turns
# in a row, then its eval falls >= 250 below that level and stays there for the next 4 plies. Positions with main2
# to move 2/4/6/10/16 plies before the first fallen ply go to collapse_<N>.txt (games main2 did not win only).
# Usage: collapse_bank.py OUT_DIR shard nshards games...
import os, sys, chess, chess.engine
out, shard, nsh, files = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:]
OFFS = (2, 4, 6, 10, 16)
eng = chess.engine.SimpleEngine.popen_uci(os.path.expanduser('~/tools/stockfish16/stockfish/stockfish-ubuntu-x86-64-avx2'))
eng.configure({'Threads': 1, 'Hash': 64})
banks = {o: [] for o in OFFS}; games = coll = 0
lines = [l.rstrip('\n').split('\t') for f in files for l in open(f) if l.strip()]
for i, (color, res, fen, *mv) in enumerate(lines):
    if i % nsh != shard: continue
    me = chess.WHITE if color == 'w' else chess.BLACK
    won = (res.startswith('White') and me == chess.WHITE) or (res.startswith('Black') and me == chess.BLACK)
    if won: continue
    games += 1
    b = chess.Board(fen); fens, ev = [b.fen()], [eng.analyse(b, chess.engine.Limit(depth=9))['score'].pov(me).score(mate_score=3000)]
    for m in (mv[0].split() if mv else []):
        b.push_uci(m); fens.append(b.fen())
        ev.append(eng.analyse(b, chess.engine.Limit(depth=9))['score'].pov(me).score(mate_score=3000))
    stm_me = lambda k: chess.Board(fens[k]).turn == me
    mine = [k for k in range(len(ev)) if stm_me(k)]
    for j in range(1, len(mine)):
        a, c = mine[j - 1], mine[j]
        level = min(ev[a], ev[c])
        if level < -100: continue
        bad = next((k for k in range(c + 1, len(ev) - 4) if all(ev[x] <= level - 250 for x in range(k, k + 5))), None)
        if bad is None: break
        p0 = max(k for k in mine if k < bad)  # our last turn before the fall
        for o in OFFS:
            if p0 - o >= 0: banks[o].append(fens[p0 - o])
        coll += 1
        break
eng.quit()
for o in OFFS:
    with open(os.path.join(out, f'collapse_{o}.shard{shard}.txt'), 'w') as f:
        f.write(''.join(x + '\n' for x in banks[o]))
print(f'shard {shard}: {games} non-won games, {coll} collapses', flush=True)
