"""Does a long game history hurt the policy? (2026-11-14) For our-move positions of the Lichess bot games, compare the
greedy policy move with the recurrent state built from the full game vs from only the last W plies (zero state before),
graded by Stockfish depth 12 cp loss. BC training windows are 128 plies; endgames often sit past ply 100.
Usage: state_test.py MODEL FEATS(0/1) OUT.json"""
import os, sys, glob, json
sys.path.insert(0, os.path.expanduser('~/tools/puffer_chess_engine'))
os.environ.setdefault('CHESS_TOOLS', '/home/felix/puffertank/pufferlib/ocean/chess/tools')
sys.path.append(os.path.expanduser('~/tools/lichess-bot/venv/lib/python3.12/site-packages'))
import torch, chess, chess.pgn, chess.engine
from play import Rules, Net, action_uci
torch.set_num_threads(2)
model, feats, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
rules = Rules(); net = Net(model, rules.obs_n, 1024, 2, 7 if feats else 0)
sf = chess.engine.SimpleEngine.popen_uci('/home/felix/tools/stockfish16/stockfish/stockfish-ubuntu-x86-64-avx2')
L = chess.engine.Limit(depth=12)
START = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'
WINDOWS = ['full', 64, 32]
FULL = [None]

def policy_move(moves, side, w):
    lo = len(moves) if w == 'full' else max(0, len(moves) - w)
    st = FULL[0] if w == 'full' else net.zero()
    for n in range(lo, len(moves)):
        _, obs, *_ = rules.state(START, moves[:n], side); _, _, st = net.step(obs, st)
    _, obs, mask, legal, _ = rules.state(START, moves, side)
    mf = rules.move_feats(START, moves) if feats else None
    logits, _, _ = net.step(obs, st, mf)
    a = int(torch.argmax(logits.masked_fill(torch.from_numpy(mask == 0), -1e9)))
    return action_uci(a, side, legal)

rows = []
for p in sorted(glob.glob(os.path.expanduser('~/tools/lichess-bot/game_records/*.pgn'))):
    g = chess.pgn.read_game(open(p)); us = chess.WHITE if g.headers['White'] == 'felix_pufferlib' else chess.BLACK
    ms = [m.uci() for m in g.mainline_moves()]; b = chess.Board()
    side = 0 if us == chess.WHITE else 1; full = net.zero()  # full-history state, carried along the game
    for n, m in enumerate(ms):
        if b.turn == us and n >= 20 and n % 8 in (0, 1):  # every 4th own move after the opening
            FULL[0] = full
            best = sf.analyse(b, L)['score'].pov(us).score(mate_score=2000)
            r = {'ply': n}
            for w in WINDOWS:
                mv = policy_move(ms[:n], side, w); b.push_uci(mv)
                r[str(w)] = best - sf.analyse(b, L)['score'].pov(us).score(mate_score=2000); r['m' + str(w)] = mv; b.pop()
            rows.append(r)
        _, obs, *_ = rules.state(START, ms[:n], side); _, _, full = net.step(obs, full)
        b.push_uci(m)
    json.dump(rows, open(out, 'w'))
    print(p.split('/')[-1][:30], len(rows), flush=True)
sf.quit()
