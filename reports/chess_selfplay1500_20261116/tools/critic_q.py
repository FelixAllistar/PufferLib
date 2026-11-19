# Critic quality: main2's value for the side to move vs Stockfish (depth 10) on positions from the Lichess games.
import os, sys, numpy as np
sys.path.insert(0, os.path.expanduser('~/tools/puffer_chess_engine/main2'))
import engine2 as E
import chess, chess.pgn, chess.engine
eng = chess.engine.SimpleEngine.popen_uci(os.path.expanduser('~/tools/stockfish16/stockfish/stockfish-ubuntu-x86-64-avx2'))
r = E.Rules(); n = E.Net2(E.MODEL)
d = os.path.expanduser('~/tools/lichess-bot/game_records'); fs = sorted(f for f in os.listdir(d) if f.endswith('.pgn'))[-60:]
V, S = [], []
for f in fs:
    g = chess.pgn.read_game(open(os.path.join(d, f)))
    if g is None: continue
    b = g.board(); moves = []
    for i, mv in enumerate(g.mainline_moves()):
        b.push(mv); moves.append(mv.uci())
        if i % 3 or b.is_game_over(): continue
        side, obs, *_ = r.state(E.START, moves, 0 if b.turn else 1)
        V.append(n.value(obs))
        S.append(eng.analyse(b, chess.engine.Limit(depth=10))['score'].relative.score(mate_score=2000))
eng.quit()
V, S = np.array(V), np.clip(np.array(S), -2000, 2000)
rank = lambda x: np.argsort(np.argsort(x))
print('positions', len(V), 'spearman %.3f' % np.corrcoef(rank(V), rank(S))[0, 1])
for lo in (100, 300, 600):
    m = np.abs(S) >= lo
    print(f'|SF| >= {lo}cp: {m.sum()} positions, critic sign agrees {np.mean(np.sign(V[m]) == np.sign(S[m])):.3f}')
for a, b_ in ((-3000, -300), (-300, -100), (-100, 100), (100, 300), (300, 3000)):
    m = (S >= a) & (S < b_)
    print(f'SF in [{a},{b_}): mean critic {V[m].mean():+.3f}  n={m.sum()}')
