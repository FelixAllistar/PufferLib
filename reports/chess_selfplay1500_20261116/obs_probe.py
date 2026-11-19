#!/usr/bin/env python3
"""Observation probe (2026-11-16): is a failure an observation/architecture limit or a training-data gap?
Supervised, CPU only: train a fresh PufferNet-shaped chess net (linear encoder -> MinGRU x2 -> linear head, one step
from a zero recurrent state, as at the start of a state-set game) to put its probability on the mating moves of
mate-in-1 positions (KQK / KRK bank), then score top-move-mates on held-out positions, split by the losing king's
zone. Run once with the env observation and once with extra king features appended (--extra):
  flight squares of each king (64 + 64: adjacent, not own-occupied, not attacked by the other side), their counts as
  thermometers (8 + 8), and the kings' Chebyshev distance one-hot (7). All 0/1, mover's view.
If the plain observation reaches ~100% quickly, the net can see the mate and the fix is states; if only --extra does,
it is an observation gap.
Usage: obs_probe.py BANK.txt OUT.json [--extra] [--hidden 256] [--steps 1500] [--n 20000]"""
import argparse, json, os, random, sys, time
ENG = os.path.expanduser('~/tools/puffer_chess_engine')
sys.path.insert(0, ENG)
sys.path.append(os.path.expanduser('~/tools/lichess-bot/venv/lib/python3.12/site-packages'))
import chess  # noqa: E402
import numpy as np, torch  # noqa: E402
from play import Rules  # noqa: E402
from bc_train import ChessNet  # noqa: E402

A = 4096
CORNERS = {0, 7, 56, 63}


def zone(sq):
    f, r = sq % 8, sq // 8
    return 0 if sq in CORNERS else 1 if f in (0, 7) or r in (0, 7) else 2


def flights(b, color):
    """Squares the `color` king could step to: adjacent, not own-occupied, not attacked by the other side."""
    k = b.king(color)
    c = b.copy(stack=False); c.remove_piece_at(k)  # sliders see through the king
    return [s for s in chess.SQUARES if chess.square_distance(s, k) == 1 and b.color_at(s) != color
            and not c.is_attacked_by(not color, s)]


def extra_feats(b):
    me, flip = b.turn, 56 if b.turn == chess.BLACK else 0
    x = np.zeros(64 + 64 + 8 + 8 + 7, np.float32)
    for j, col in enumerate((me, not me)):
        fl = flights(b, col)
        for s in fl:
            x[64 * j + (s ^ flip)] = 1
        x[128 + 8 * j:128 + 8 * j + len(fl)] = 1
    x[144 + chess.square_distance(b.king(me), b.king(not me)) - 1] = 1
    return x


def forward(net, o):
    """One step from a zero recurrent state (play.py Net.step, batched)."""
    x = o @ net.enc.t()
    for w in net.gru:
        hid, gate, proj = (x @ w.t()).split(net.H, dim=-1)
        h = torch.sigmoid(gate) * torch.where(hid >= 0, hid + 0.5, torch.sigmoid(hid))
        s = torch.sigmoid(proj)
        x = s * h + (1 - s) * x
    return x @ net.dec[:A].t()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bank'); ap.add_argument('out'); ap.add_argument('--extra', action='store_true')
    ap.add_argument('--hidden', type=int, default=256); ap.add_argument('--steps', type=int, default=1500)
    ap.add_argument('--n', type=int, default=20000); ap.add_argument('--lr', type=float, default=1e-3)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    torch.manual_seed(a.seed); torch.set_num_threads(1)
    rules = Rules()
    fens = [l.strip() for l in open(a.bank) if l.strip()][:a.n]
    O, M, Y, Z, K = [], [], [], [], []
    for fen in fens:
        b = chess.Board(fen)
        side, obs, mask, legal, _ = rules.state(fen, [], 0 if b.turn else 1)
        flip = 56 if b.turn == chess.BLACK else 0
        y = np.zeros(A, np.float32)
        for m in b.legal_moves:
            b.push(m); mate = b.is_checkmate(); b.pop()
            if mate:
                y[(m.from_square ^ flip) * 64 + (m.to_square ^ flip)] = 1
        if not y.any():
            continue
        O.append(np.concatenate([obs.astype(np.float32), extra_feats(b)]) if a.extra else obs.astype(np.float32))
        M.append(mask[:A]); Y.append(y); Z.append(zone(b.king(not b.turn)))
        K.append('Q' if b.pieces(chess.QUEEN, b.turn) else 'R')
    O, M, Y = torch.from_numpy(np.stack(O)), torch.from_numpy(np.stack(M)).bool(), torch.from_numpy(np.stack(Y))
    Z, K = np.array(Z), np.array(K)
    n = len(O); nt = n // 10
    perm = torch.randperm(n, generator=torch.Generator().manual_seed(0))
    te, tr = perm[:nt], perm[nt:]
    net = ChessNet(rules.obs_n, a.hidden, 2)  # critic index needs the env's obs size
    if a.extra:  # widen the encoder for the extra inputs (the critic keeps reading the base planes)
        net.enc = torch.nn.Parameter(torch.randn(a.hidden, O.shape[1]) * (2 ** 0.5) / O.shape[1] ** 0.5)
    opt = torch.optim.Adam([net.enc, net.dec] + list(net.gru), lr=a.lr)
    log, t0 = [], time.time()

    def score():
        with torch.no_grad():
            lg = forward(net, O[te]).masked_fill(~M[te], -1e9)
            hit = Y[te].gather(1, lg.argmax(1, keepdim=True))[:, 0].numpy()
        r = {'all': float(hit.mean())}
        for zi, zn in enumerate(('corner', 'edge', 'inner')):
            for k in 'QR':
                sel = (Z[te.numpy()] == zi) & (K[te.numpy()] == k)
                if sel.sum() >= 20:
                    r[f'{k}_{zn}'] = float(hit[sel].mean())
        return r

    for step in range(a.steps + 1):
        if step % 250 == 0:
            r = score(); r['step'] = step; r['secs'] = round(time.time() - t0); log.append(r)
            print(' '.join(f'{k}={v:.2f}' if isinstance(v, float) else f'{k}={v}' for k, v in r.items()), flush=True)
        idx = tr[torch.randint(len(tr), (512,))]
        lp = torch.log_softmax(forward(net, O[idx]).masked_fill(~M[idx], -1e9), 1)
        loss = -torch.logsumexp(lp.masked_fill(Y[idx] == 0, -1e9), 1).mean()
        opt.zero_grad(); loss.backward(); opt.step()
    json.dump({'extra': a.extra, 'hidden': a.hidden, 'n_train': len(tr), 'n_test': nt, 'log': log}, open(a.out, 'w'))


if __name__ == '__main__':
    main()
