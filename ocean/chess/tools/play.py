#!/usr/bin/env python3
"""Local chess games (2026-11-12): a PufferLib chess checkpoint (1024x2 MinGRU, bc_train.py's ChessNet) vs a UCI
engine, from the eval openings (chess_eval fens.txt, colors alternate), with the env's own observations (chesslib.c).
Writes one JSON line per game with every model decision (top-5 policy moves + probs, critic value) for analysis.
Usage: play.py CKPT OUT.jsonl [--games 50] [--first 0] [--elo 1320] [--ms 500] [--sample] [--search none|...]"""
import argparse, ctypes, json, os, subprocess, sys, time
import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.environ.get('CHESS_TOOLS', '/home/felix/puffertank/pufferlib/ocean/chess/tools'))
from bc_train import ChessNet  # noqa: E402

A = 4097
SF_BIN = os.environ.get('SF_BIN', os.path.expanduser('~/tools/stockfish16/stockfish/stockfish-ubuntu-x86-64-avx2'))


class Rules:
    def __init__(self):
        self.lib = ctypes.CDLL(os.path.join(HERE, 'libchesslib3.so'))
        self.lib.cl_search.restype = ctypes.c_long
        self.obs_n = self.lib.cl_obs_size()
        self.obs = np.zeros(self.obs_n, np.uint8)
        self.mask = np.zeros(A, np.uint8)
        self.legal = ctypes.create_string_buffer(4096)
        self.status = ctypes.c_int(0)

    def state(self, fen, moves, view):
        side = self.lib.cl_state(fen.encode(), ' '.join(moves).encode(), view,
                                 self.obs.ctypes.data_as(ctypes.c_void_p), self.mask.ctypes.data_as(ctypes.c_void_p),
                                 self.legal, ctypes.byref(self.status))
        assert side >= 0, f'illegal move {-side - 1} in {fen} {moves}'
        return side, self.obs.copy(), self.mask.copy(), self.legal.value.decode().split(), self.status.value

    def move_feats(self, fen, moves):
        act = np.zeros(128, np.int16); feat = np.zeros((128, 7), np.uint8)
        n = self.lib.cl_move_feats(fen.encode(), ' '.join(moves).encode(), act.ctypes.data_as(ctypes.c_void_p),
                                   feat.ctypes.data_as(ctypes.c_void_p))
        return torch.from_numpy(act[:n].astype(np.int64)), torch.from_numpy(feat[:n].astype(np.float32))

    def search(self, fen, moves, cands, depth):
        sc = (ctypes.c_int * len(cands))()
        self.lib.cl_search(fen.encode(), ' '.join(moves).encode(), ' '.join(cands).encode(), depth, sc)
        return list(sc)


class Net:
    def __init__(self, path, obs_n, hidden=1024, layers=2, move_feats=0):
        torch.set_num_threads(1)
        self.net = ChessNet(obs_n, hidden, layers, move_feats)
        self.net.load_bin(path)
        self.net.eval()
        self.H, self.layers = hidden, layers

    def zero(self):
        return [torch.zeros(1, self.H) for _ in range(self.layers)]

    @torch.no_grad()
    def step(self, obs, state, mf=None):
        """One ply: returns logits [A], critic value, new state (MinGRU advanced as in self-play)."""
        n = self.net
        o = torch.from_numpy(obs).float().unsqueeze(0)
        x, new = o @ n.enc.t(), []
        for w, h in zip(n.gru, state):
            hid, gate, proj = (x @ w.t()).split(self.H, dim=-1)
            z = torch.sigmoid(gate)
            ht = torch.where(hid >= 0, hid + 0.5, torch.sigmoid(hid))
            h = h + z * (ht - h)
            new.append(h)
            s = torch.sigmoid(proj)
            x = s * h + (1 - s) * x
        logits = (x @ n.dec[:A].t())[0]
        if mf is not None and n.mfn:
            act, feat = mf
            logits = logits.index_add(0, act, (feat * (n.mf_w + x[0] @ n.mf_u.t())).sum(-1))
        f_own = torch.relu(n.features(o, False) @ n.w1.t()) @ n.w2.t()
        f_opp = torch.relu(n.features(o, True) @ n.w1.t()) @ n.w2.t()
        return logits, float(torch.tanh(0.5 * (f_own - f_opp))), new


def action_uci(a, side, legal):
    flip = 56 if side == 1 else 0
    f, t = (a // 64) ^ flip, (a % 64) ^ flip
    stem = 'abcdefgh'[f % 8] + str(f // 8 + 1) + 'abcdefgh'[t % 8] + str(t // 8 + 1)
    c = [m for m in legal if m[:4] == stem]
    return (stem + 'q') if (stem + 'q') in c else c[0]


class Engine:
    def __init__(self, elo=None, threads=1, hash_mb=16, maia=None):
        argv = [os.path.join(HERE, 'PufferLib/lc0bin'), f'--weights={maia}'] if maia else [SF_BIN]
        self.p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  text=True, bufsize=1)
        self.cmd('uci'); self.wait('uciok')
        opts = [] if maia else [f'Threads value {threads}', f'Hash value {hash_mb}']
        if elo:
            opts += ['UCI_LimitStrength value true', f'UCI_Elo value {elo}']
        for o in opts:
            self.cmd('setoption name ' + o)
        self.cmd('isready'); self.wait('readyok')

    def cmd(self, s):
        self.p.stdin.write(s + '\n'); self.p.stdin.flush()

    def wait(self, tok):
        lines = []
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError('engine died')
            lines.append(line.strip())
            if line.startswith(tok):
                return lines

    def new_game(self):
        self.cmd('ucinewgame'); self.cmd('isready'); self.wait('readyok')

    def best(self, fen, moves, go):
        self.cmd(f'position fen {fen}' + (' moves ' + ' '.join(moves) if moves else ''))
        self.cmd('go ' + go)
        lines = self.wait('bestmove')
        return lines[-1].split()[1], lines

    def quit(self):
        try:
            self.cmd('quit'); self.p.wait(5)
        except Exception:
            self.p.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ckpt'); ap.add_argument('out')
    ap.add_argument('--games', type=int, default=50); ap.add_argument('--first', type=int, default=0)
    ap.add_argument('--elo', type=int, default=1320); ap.add_argument('--ms', type=int, default=500)
    ap.add_argument('--sample', action='store_true'); ap.add_argument('--max-plies', type=int, default=600)
    ap.add_argument('--fens', default=os.path.join(HERE, 'chess_eval/resources/chess/fens.txt'))
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--move-feats', action='store_true')
    ap.add_argument('--hidden', type=int, default=1024); ap.add_argument('--layers', type=int, default=2)
    ap.add_argument('--maia', type=int, default=0, help='play top-move Maia at this level (1 node) instead of Stockfish')
    ap.add_argument('--search', type=int, default=0, help='plies of full-width material search after each candidate (0 off)')
    ap.add_argument('--k', type=int, default=5, help='policy top-k candidates checked by the search')
    ap.add_argument('--margin', type=int, default=50, help='centipawns: candidates this close to the best are acceptable')
    ap.add_argument('--no-repeat', action='store_true', help='when ahead (search >= +100 cp, or critic > 0.2 without '
                    'search), never play a move that completes a threefold repetition (2026-11-14: ~80%% of draws)')
    args = ap.parse_args()
    fens = [l.strip() for l in open(args.fens) if l.strip()]
    rules = Rules()
    net = Net(args.ckpt, rules.obs_n, args.hidden, args.layers, 7 if args.move_feats else 0)
    eng = Engine(args.elo, maia=os.path.join(HERE, f'PufferLib/lc0/maia-{args.maia}.pb.gz') if args.maia else None)
    go = 'nodes 1' if args.maia else f'movetime {args.ms}'
    rng = np.random.default_rng(args.seed)
    out = open(args.out, 'a')
    for g in range(args.first, args.first + args.games):
        fen, me = fens[g % len(fens)], g % 2
        moves, state, dec = [], net.zero(), []
        eng.new_game()
        status, side, t0 = 0, 0, time.time()
        while len(moves) < args.max_plies:
            side, obs, mask, legal, status = rules.state(fen, moves, me)
            if status:
                break
            mf = rules.move_feats(fen, moves) if args.move_feats and side == me else None
            logits, value, state = net.step(obs, state, mf)
            if side == me:
                lg = logits.masked_fill(torch.from_numpy(mask == 0), -1e9)
                pr = torch.softmax(lg, 0)
                a = int(torch.multinomial(pr, 1, generator=None)) if args.sample else int(pr.argmax())
                na = int(mask.sum())
                top = torch.topk(pr, min(5, na))
                uci = action_uci(a, side, legal)
                srch = None
                if args.search:
                    kt = torch.topk(pr, min(args.k, na))
                    cands = [action_uci(int(i), side, legal) for i in kt.indices]
                    ps = np.array([float(p) for p in kt.values])
                    sc = rules.search(fen, moves, cands, args.search)
                    ok = [i for i in range(len(cands)) if sc[i] >= max(sc) - args.margin]
                    if args.sample:
                        w = ps[ok] / ps[ok].sum()
                        pick = ok[int(rng.choice(len(ok), p=w))]
                    else:
                        pick = max(ok, key=lambda i: ps[i])
                    if not args.sample and cands[pick] != uci or args.sample:
                        a = int(kt.indices[pick])
                    uci = cands[pick]
                    srch = [[c, int(v)] for c, v in zip(cands, sc)]
                ahead = (max(sc) >= 100) if args.search else value > 0.2
                if args.no_repeat and ahead:
                    # never return to a position seen before (Maia completes the threefold: 39-45 of 50 repetition
                    # draws left by a threefold-only guard were Maia's final move, most with us ahead)
                    import chess
                    bd = chess.Board(fen)
                    for m_ in moves:
                        bd.push_uci(m_)

                    def repeats(c):
                        bd.push_uci(c); r = bd.is_repetition(2); bd.pop(); return r
                    if repeats(uci):
                        order = [int(i) for i in torch.argsort(pr, descending=True)[:na]]
                        if args.search:  # acceptable search candidates first, then the rest of the policy order
                            order = [int(kt.indices[i]) for i in sorted(ok, key=lambda i: -ps[i])] + order
                        for b in order:
                            c = action_uci(b, side, legal)
                            if not repeats(c):
                                a, uci = b, c
                                break
                dec.append({'ply': len(moves), 'move': uci, 'p': round(float(pr[a]), 4), 'v': round(value, 3),
                            'n_legal': len(legal), 'search': srch,
                            'top': [[action_uci(int(i), side, legal), round(float(p), 4)]
                                    for p, i in zip(top.values, top.indices)]})
            else:
                uci, _ = eng.best(fen, moves, go)
            moves.append(uci)
        # result from the model's side: 1 win, 0.5 draw, 0 loss
        if status == 1:
            res = 0.0 if side == me else 1.0
        else:
            res = 0.5
        rec = {'game': g, 'fen': fen, 'color': me, 'moves': moves, 'status': status, 'result': res,
               'opp': f'maia{args.maia}' if args.maia else f'sf{args.elo}@{args.ms}ms', 'mode': ('sample' if args.sample else 'greedy') + (f'+search{args.search}k{args.k}m{args.margin}' if args.search else ''), 'ckpt': args.ckpt, 'decisions': dec, 'secs': round(time.time() - t0, 1)}
        out.write(json.dumps(rec) + '\n'); out.flush()
        print(f'game {g} color {"wb"[me]} result {res} status {status} plies {len(moves)} {rec["secs"]}s', flush=True)
    eng.quit()


if __name__ == '__main__':
    main()
