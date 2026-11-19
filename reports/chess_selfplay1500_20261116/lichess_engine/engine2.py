#!/usr/bin/env python3
"""UCI engine for native feat2 nets (2026-11-19, main2: pure self-play, 1024x2 MinGRU, -DCHESS_RULES -DCHESS_MOVE_FEATS
-DCHESS_MOVE_FEATS2) + the same small search as ../engine.py (policy top-K moves, each scored by a DEPTH-ply material
search; the most likely move within MARGIN cp of the best is played). The checkpoint is read in native order: enc
(H x 1307), dec ((A+1) x H), w1, w2 (critic, unused here), mf_w (12 stored, 9 used), mf_u (9 x H), gru[0..L-1],
each at its 8-element-aligned offset (see Net2). The
per-move features come from the observation itself (slots after CHESS_OBS_END, chess_decoder.cu chess_mf_bias).
Env: PUFFER_MODEL (.bin), PUFFER_SEARCH (plies, 0 = policy only), PUFFER_K, PUFFER_MARGIN, PUFFER_HIDDEN."""
import ctypes, os, sys
import numpy as np, torch
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.expanduser('~/tools/lichess-bot/venv/lib/python3.12/site-packages'))
import chess  # noqa: E402  repetition checks only

A, OBS_END, SLOTS, MF, MF_STORE, CH, CF = 4097, 1307, 128, 9, 12, 256, 794
MODEL = os.environ.get('PUFFER_MODEL', os.path.join(HERE, 'main2.bin'))
H = int(os.environ.get('PUFFER_HIDDEN', '1024'))
DEPTH, K, MARGIN = (int(os.environ.get(k, d)) for k, d in (('PUFFER_SEARCH', '2'), ('PUFFER_K', '5'), ('PUFFER_MARGIN', '50')))
START = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'


class Rules:
    def __init__(self):
        self.lib = ctypes.CDLL(os.path.join(HERE, 'libchesslib4.so'))
        self.lib.cl_search.restype = ctypes.c_long
        self.obs_n = self.lib.cl_obs_size()
        assert self.obs_n == OBS_END + 3 * SLOTS, self.obs_n
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

    def search(self, fen, moves, cands, depth):
        sc = (ctypes.c_int * len(cands))()
        self.lib.cl_search(fen.encode(), ' '.join(moves).encode(), ' '.join(cands).encode(), depth, sc)
        return list(sc)


class Net2:
    def __init__(self, path, hidden=1024, layers=2):
        torch.set_num_threads(1)
        flat = np.fromfile(path, dtype=np.float32)
        # Native registration order. The trainer's bf16 param buffer starts every tensor on a 16-byte boundary
        # (8 elements) while checkpoints hold the buffer's flat elements: a file index is a buffer index, so each
        # tensor is read from its aligned offset. feat2's 12-float mf_w leaves a 4-element gap: mf_u and the GRU
        # weights sit 4 floats later than a packed layout puts them (checked 2026-11-19: 87% first-move agreement
        # with the native eval's games vs 4% packed). The last 4 GRU floats are past the file end (zeros, as the
        # trainer's own load leaves them).
        shapes = [('enc', (hidden, OBS_END)), ('dec', (A + 1, hidden)), ('w1', (CH, CF)), ('w2', (CH,)),
                  ('mf_w', (MF_STORE,)), ('mf_u', (MF, hidden))] + [(f'gru{i}', (3 * hidden, hidden)) for i in range(layers)]
        need = sum(int(np.prod(s)) for _, s in shapes)
        assert len(flat) == need, f'{path}: {len(flat)} floats, native feat2 {hidden}x{layers} needs {need}'
        flat = np.concatenate([flat, np.zeros(64, np.float32)])
        self.p, off = {}, 0
        for k, s in shapes:
            off = (off + 7) // 8 * 8
            n = int(np.prod(s))
            self.p[k] = torch.from_numpy(flat[off:off + n].reshape(s).copy()); off += n
        self.H, self.layers = hidden, layers
        self.gru = [self.p[f'gru{i}'] for i in range(layers)]

    @torch.no_grad()
    def value(self, obs):
        """The critic (chess_decoder.cu): tanh(0.5 (f(own) - f(opp))) for the observation's player, f = a 794 -> 256 -> 1
        MLP on the board planes, side, castling, en passant, rule 50, repetition and check bits."""
        o = obs.astype(np.float32)
        own = np.concatenate([o[0:768], o[896:897], o[897:901], o[901:910], o[910:918], o[918:920], o[920:922]])
        planes = o[0:768].reshape(12, 64)
        opp_planes = np.concatenate([planes[6:], planes[:6]])[:, np.arange(64) ^ 56].reshape(-1)
        cas = o[897:901]
        opp = np.concatenate([opp_planes, 1 - o[896:897], cas[[2, 3, 0, 1]], o[901:910], o[910:918], o[918:920],
                              o[[921, 920]]])
        f = lambda x: float(torch.relu(torch.from_numpy(x) @ self.p['w1'].t()) @ self.p['w2'])
        return float(np.tanh(0.5 * (f(own) - f(opp))))

    def zero(self):
        return [torch.zeros(1, self.H) for _ in range(self.layers)]

    @torch.no_grad()
    def step(self, obs, state):
        """One ply: logits [A] (with the per-move feature bias), new recurrent state (MinGRU as in self-play)."""
        o = torch.from_numpy(obs[:OBS_END]).float().unsqueeze(0)
        x, new = o @ self.p['enc'].t(), []
        for w, h in zip(self.gru, state):
            hid, gate, proj = (x @ w.t()).split(self.H, dim=-1)
            z = torch.sigmoid(gate)
            ht = torch.where(hid >= 0, hid + 0.5, torch.sigmoid(hid))
            h = h + z * (ht - h)
            new.append(h)
            s = torch.sigmoid(proj)
            x = s * h + (1 - s) * x
        logits = (x @ self.p['dec'][:A].t())[0]
        sl = obs[OBS_END:].reshape(SLOTS, 3).astype(np.int64)
        ok = sl[:, 2] <= 127
        if ok.any():
            act = torch.from_numpy(sl[ok, 0] * 64 + sl[ok, 1])
            code = torch.from_numpy(sl[ok, 2])
            f = torch.arange(MF)
            bits = torch.where(f < 5, ((code[:, None] & 7) == f).float(), ((code[:, None] >> (f - 2).clamp(min=0)) & 1).float())
            c = (x[0] @ self.p['mf_u'].t())  # (MF,)
            logits = logits.index_add(0, act, (bits * (self.p['mf_w'][:MF] + c)).sum(-1))
        return logits, new


def action_uci(a, side, legal):
    f, t = a // 64, a % 64
    if side == 1:
        f, t = f ^ 56, t ^ 56
    u = 'abcdefgh'[f % 8] + str(f // 8 + 1) + 'abcdefgh'[t % 8] + str(t // 8 + 1)
    if u in legal:
        return u
    for p in 'qrbn':
        if u + p in legal:
            return u + p
    raise ValueError(f'action {a} ({u}) not legal: {legal}')


def main():
    rules = Rules()
    net = Net2(MODEL, H, 2)
    fen, moves, cache = START, [], None
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
                _, state = net.step(obs, state)
            _, obs, mask, legal, status = rules.state(fen, moves, side)
            logits, state = net.step(obs, state)
            cache = (fen, side, list(moves), state, len(moves) + 1)
            lg = logits.masked_fill(torch.from_numpy(mask == 0), -1e9)
            kt = torch.topk(lg, min(K if DEPTH else 1, int(mask.sum())))
            cands = [action_uci(int(i), side, legal) for i in kt.indices]
            if DEPTH and len(cands) > 1:
                sc = list(rules.search(fen, moves, cands, DEPTH))
                if max(sc) >= 100:  # ahead: never walk into a repeated position
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
