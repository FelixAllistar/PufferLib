#!/usr/bin/env python3
"""UCI engine for the Lichess bot (2026-11-13): a PufferLib chess net (1024x2 MinGRU) + the small search (policy
top-k moves, each checked by a 2-ply material search; the most likely move within MARGIN cp of the best is played).
The recurrent state is rebuilt by stepping the net through every position of the game from our side's view, as in
self-play, and cached between moves. Env: PUFFER_MODEL (.bin), PUFFER_FEATS=1 for nets with per-move lookahead
features, PUFFER_SEARCH (plies, 0 = policy only), PUFFER_K, PUFFER_MARGIN."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
os.environ.setdefault('CHESS_TOOLS', '/home/felix/puffertank/pufferlib/ocean/chess/tools')
sys.path.insert(0, HERE)
import numpy as np, torch  # noqa: E402
sys.path.append(os.path.expanduser('~/tools/lichess-bot/venv/lib/python3.12/site-packages'))
import chess  # noqa: E402  pure-python, only for repetition checks (the net and the search do not see game history)
from play import Rules, Net, action_uci  # noqa: E402

MODEL = os.environ.get('PUFFER_MODEL', os.path.join(HERE, 'rl_best.bin'))
FEATS = int(os.environ.get('PUFFER_FEATS', '0'))
DEPTH, K, MARGIN = (int(os.environ.get(k, d)) for k, d in (('PUFFER_SEARCH', '2'), ('PUFFER_K', '5'), ('PUFFER_MARGIN', '50')))
START = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'


def main():
    rules = Rules()
    net = Net(MODEL, rules.obs_n, 1024, 2, 7 if FEATS else 0)
    fen, moves, cache = START, [], None  # cache: (fen, color, processed moves, state)
    out = lambda s: (sys.stdout.write(s + '\n'), sys.stdout.flush())
    for line in sys.stdin:
        t = line.split()
        if not t:
            continue
        if t[0] == 'uci':
            out('id name PufferChess'); out('id author felix_pufferlib'); out('uciok')
        elif t[0] == 'isready':
            out('readyok')
        elif t[0] == 'ucinewgame':
            cache = None
        elif t[0] == 'position':
            i = t.index('moves') if 'moves' in t else len(t)
            fen = START if t[1] == 'startpos' else ' '.join(t[2:i])
            moves = t[i + 1:]
        elif t[0] == 'go':
            side, *_ = rules.state(fen, moves, 0)
            if not (cache and cache[0] == fen and cache[1] == side and moves[:len(cache[2])] == cache[2]
                    and len(moves) >= len(cache[2])):
                cache = (fen, side, [], net.zero(), 0)
            _, _, done, state, p = cache
            for n in range(p, len(moves)):  # earlier positions: advance the recurrent state only
                _, obs, _, _, _ = rules.state(fen, moves[:n], side)
                _, _, state = net.step(obs, state)
            _, obs, mask, legal, status = rules.state(fen, moves, side)
            mf = rules.move_feats(fen, moves) if FEATS else None
            logits, _, state = net.step(obs, state, mf)
            cache = (fen, side, list(moves), state, len(moves) + 1)
            pr = torch.softmax(logits.masked_fill(torch.from_numpy(mask == 0), -1e9), 0)
            kt = torch.topk(pr, min(K if DEPTH else 1, int(mask.sum())))
            cands = [action_uci(int(i), side, legal) for i in kt.indices]
            if DEPTH and len(cands) > 1:
                sc = list(rules.search(fen, moves, cands, DEPTH))
                if max(sc) >= 100:  # ahead: never walk into a repeated position (threefold draws threw away +7 games)
                    b = chess.Board(fen)
                    for m in moves:
                        b.push_uci(m)
                    for i, c in enumerate(cands):
                        b.push_uci(c)
                        if b.is_repetition(2):
                            sc[i] -= 10 ** 6
                        b.pop()
                best = cands[min(i for i in range(len(cands)) if sc[i] >= max(sc) - MARGIN)]
            else:
                best = cands[0]
            out('bestmove ' + best)
        elif t[0] == 'quit':
            break


if __name__ == '__main__':
    main()
