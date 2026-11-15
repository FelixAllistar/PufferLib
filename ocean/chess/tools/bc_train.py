#!/usr/bin/env python3
"""Behavior cloning of human games into the chess policy (2026-11-10), as a start (and a fixed magnet anchor) for RL.
A PyTorch copy of the trainer's chess network with the same flat float32 checkpoint (src/algo.cu, src/ocean.cu,
ocean/chess/chess_decoder.cu; parameters in registration order, no padding at these sizes):
  encoder  W_e [H, OBS]          x = obs @ W_e^T (no bias, no activation)
  decoder  W_d [4098, H]         logits = h @ W_d[:4097]^T (row 4097 is the plain value row, unused with the paired
                                 critic, kept as loaded)
  critic   W_1 [256, 794], W_2 [1, 256]   f = relu(feats @ W_1^T) @ W_2^T, V = tanh(0.5 (f(own) - f(opponent view)))
  MinGRU   W_l [3H, H] per layer c = x @ W_l^T = (hidden, gate, proj); z = sigmoid(gate);
                                 h~ = hidden + 0.5 if hidden >= 0 else sigmoid(hidden); h = h_prev + z (h~ - h_prev);
                                 out = s h + (1 - s) x with s = sigmoid(proj)
Data: lichess_games.py lines, encoded by bc_encode.c into the env's exact self-play observations: both players get
a row every ply (the side not to move can only pass) and the recurrent state advances every ply, so each game gives
a White-view and a Black-view sequence. Windows of --window plies start at the first ply half the time and anywhere
else otherwise, with zero recurrent state (as curriculum games starting from a FEN do). Loss: cross-entropy of the
human move over the legal-move mask on the mover's rows, plus --vf times the squared error of the paired critic
against the game result (+1 win, 0 draw, -1 loss from that row's side) on every row.
Usage: bc_train.py GAMES OUT.bin [--init CKPT.bin] [--hidden 1024] [--layers 2] [--steps N] [--batch 256]
       [--window 128] [--lr 1e-3] [--workers 8] [--device cuda] [--val 2000]
       bc_train.py GAMES --eval CKPT.bin [--val 2000]   (top-1 accuracy and loss of a checkpoint on held-out games)
Move features (--move-feats, 2026-11-13): chess_tactics.h gives every legal move CT_FEATS 0/1 features (material
swing after the opponent's best captures, check, mate); the move's logit gets feat . (w + U h), U [CT_FEATS, H]. The
two extra tensors follow the GRU weights in the checkpoint (--init from a plain checkpoint starts them at zero).
Teacher mode (--teacher, 2026-11-12): GAMES are sf_label.py lines 'FEN\tmoves\tbest\tcp'. The observations follow the
played moves from FEN (as the env would show them), but each mover row's target is Stockfish's move best[i], and
every row's value target is Stockfish's score cp[i] as 2 wp - 1 (wp = 1 / (1 + 10^(-cp/400))) from that row's side.
"""
import argparse
import ctypes
import math
import os
import random
import sys
import time

import numpy as np
import torch
import queue as pyqueue
import threading

import torch.multiprocessing as mp
import torch.nn.functional as F

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.environ.get('BC_ENCODE_LIB', os.path.join(HERE, '..', '..', '..', 'build', 'chess', 'libbc_encode.so'))
A = 4097
CF, CH = 794, 256
O_SIDE, O_CASTLE, O_EP, O_RULE50, O_REP, O_SELF_CHECK, O_OPP_CHECK = 896, 897, 901, 910, 918, 920, 921


# ---------------------------------------------------------------- data

class Encoder:
    def __init__(self):
        self.lib = ctypes.CDLL(LIB)
        self.obs = self.lib.bc_obs_size()
        self.ob, self.mb = self.lib.bc_obs_bytes(), self.lib.bc_mask_bytes()
        self.cf = self.lib.bc_ct_feats() if hasattr(self.lib, 'bc_ct_feats') else 0
        self.cm = self.lib.bc_ct_max_moves() if self.cf else 0

    def move_feats(self, moves, max_plies, fen=None):
        act = np.full((max_plies, self.cm), A - 1, np.int16)
        feat = np.zeros((max_plies, self.cm, self.cf), np.uint8)
        cnt = np.zeros(max_plies, np.int16)
        self.lib.bc_move_feats(fen.encode() if fen else None, moves.encode(), max_plies,
                               act.ctypes.data_as(ctypes.c_void_p), feat.ctypes.data_as(ctypes.c_void_p),
                               cnt.ctypes.data_as(ctypes.c_void_p))
        return act, feat

    def encode(self, moves, max_plies, fen=None):
        obs = np.zeros((max_plies, 2, self.ob), np.uint8)
        mask = np.zeros((max_plies, 2, self.mb), np.uint8)
        act = np.zeros((max_plies, 2), np.int16)
        n = self.lib.bc_encode(fen.encode() if fen else None, moves.encode(), max_plies, obs.ctypes.data_as(ctypes.c_void_p),
                               mask.ctypes.data_as(ctypes.c_void_p), act.ctypes.data_as(ctypes.c_void_p))
        return (n if n >= 0 else -n - 1), obs, mask, act


def uci_action(uci, mover):
    flip = 56 if mover == 1 else 0
    f = (ord(uci[0]) - 97 + 8 * (int(uci[1]) - 1)) ^ flip
    t = (ord(uci[2]) - 97 + 8 * (int(uci[3]) - 1)) ^ flip
    return f * 64 + t


def read_teacher(path, target='sf', start=0, stop=None, shard=(0, 1)):
    """target 'sf': Stockfish's move; 'search': the net + search move (5th column, selfplay_games.py).
    Reads lines start..stop, keeping every shard[1]-th one from offset shard[0] (one data worker's slice)."""
    games = []
    with open(path) as f:
        for i, line in enumerate(f):
            if i < start or (i - start) % shard[1] != shard[0]:
                continue
            if stop is not None and i >= stop:
                break
            parts = line.rstrip('\n').split('\t')
            fen, moves, best, cp = parts[:4]
            if target == 'search':
                best = parts[4]
            games.append((fen, moves, best.split(), np.array([int(c) for c in cp.split()], np.float32)))
    return games


def make_teacher_batch(enc, games, rng, batch, window, from_start=0.5, move_feats=False):
    """As make_batch, with Stockfish's move as the mover's target and its score as every row's value target.
    With move_feats, also the mover rows' per-move features: mf_act [B, T, M] i16 (A - 1 = padding), mf [B, T, M, F]."""
    B, T = batch, window
    obs = np.zeros((B, T, enc.ob), np.uint8)
    mask = np.zeros((B, T, enc.mb), np.uint8)
    act = np.full((B, T), A - 1, np.int64)
    valid = np.zeros((B, T), np.float32)
    mover = np.zeros((B, T), np.float32)
    value = np.zeros((B, T), np.float32)
    if move_feats:
        mf_act = np.full((B, T, enc.cm), A - 1, np.int16)
        mf = np.zeros((B, T, enc.cm, enc.cf), np.uint8)
    for b in range(B):
        fen, moves, best, cp = games[rng.randrange(len(games))]
        plies = len(best)
        start = 0 if rng.random() < from_start or plies <= T else rng.randrange(0, plies - T // 2)
        n, o, m, a = enc.encode(moves, min(plies, start + T), fen)
        if move_feats:
            fa, ff = enc.move_feats(moves, min(plies, start + T), fen)
        n = max(0, n - start)
        color = rng.randrange(2)
        obs[b, :n] = o[start:start + n, color]
        mask[b, :n] = m[start:start + n, color]
        valid[b, :n] = 1
        v = 2.0 / (1.0 + 10.0 ** (-np.clip(cp[start:start + n], -4000, 4000) / 400.0)) - 1.0  # mover's view
        for i in range(n):
            mv = 0 if a[start + i, 0] != A - 1 else 1  # who moved at this ply
            if mv == color:
                act[b, i] = uci_action(best[start + i], color)
                mover[b, i] = 1
                if move_feats:
                    mf_act[b, i] = fa[start + i]; mf[b, i] = ff[start + i]
            value[b, i] = v[i] if mv == color else -v[i]
    if move_feats:
        return obs, mask, act, valid, mover, value, mf_act, mf
    return obs, mask, act, valid, mover, value


def read_games(path):
    games = []
    with open(path) as f:
        for line in f:
            head, _, moves = line.rstrip('\n').partition('\t')
            result = head.split()[2]
            white = 1.0 if result == '1-0' else -1.0 if result == '0-1' else 0.0
            games.append((white, moves))
    return games


def make_batch(enc, games, rng, batch, window, from_start=0.5, move_feats=False):
    """Packed batch: obs [B, T, OB] u8, mask [B, T, MB] u8, act [B, T] i64, valid [B, T], mover [B, T], value [B, T]."""
    B, T = batch, window
    obs = np.zeros((B, T, enc.ob), np.uint8)
    mask = np.zeros((B, T, enc.mb), np.uint8)
    act = np.full((B, T), A - 1, np.int64)
    valid = np.zeros((B, T), np.float32)
    mover = np.zeros((B, T), np.float32)
    value = np.zeros((B, T), np.float32)
    if move_feats:
        mf_act = np.full((B, T, enc.cm), A - 1, np.int16)
        mf = np.zeros((B, T, enc.cm, enc.cf), np.uint8)
    for b in range(B):
        white, moves = games[rng.randrange(len(games))]
        plies = moves.count(' ') + 1
        start = 0 if rng.random() < from_start or plies <= T else rng.randrange(0, plies - T // 2)
        n, o, m, a = enc.encode(moves, min(plies, start + T))
        if move_feats:
            fa, ff = enc.move_feats(moves, min(plies, start + T))
        n = max(0, n - start)
        color = rng.randrange(2)
        obs[b, :n] = o[start:start + n, color]
        mask[b, :n] = m[start:start + n, color]
        act[b, :n] = a[start:start + n, color]
        valid[b, :n] = 1
        mover[b, :n] = act[b, :n] != A - 1
        value[b, :n] = white if color == 0 else -white
        if move_feats:
            rows = np.nonzero(mover[b, :n])[0]
            mf_act[b, rows] = fa[start + rows]; mf[b, rows] = ff[start + rows]
    if move_feats:
        return obs, mask, act, valid, mover, value, mf_act, mf
    return obs, mask, act, valid, mover, value


def producer(path_games, seed, batch, window, queue, teacher=False, move_feats=False):
    torch.set_num_threads(1)
    enc, rng = Encoder(), random.Random(seed)
    if isinstance(path_games, tuple):  # teacher shard spec (path, target, start, worker, workers): load only this slice
        path, target, start, w, n = path_games
        path_games = read_teacher(path, target, start, shard=(w, n))
    # Batches go to the trainer as shared-memory tensors (file_system strategy: no fd per tensor) with the per-move
    # features packed to one byte per move (2026-11-14: pickling ~60 MB batches through the pipe cost ~0.55 s/step).
    mp.set_sharing_strategy('file_system')
    while True:
        b = (make_teacher_batch if teacher else make_batch)(enc, path_games, rng, batch, window, move_feats=move_feats)
        if len(b) == 8:
            b = b[:7] + (pack_mf(b[6], b[7]),)
        queue.put(tuple(torch.from_numpy(x).share_memory_() for x in b))


def pack_mf(mf_act, mf):
    """[.., M, 7] one-hot features -> [.., M] uint8 code: swing bucket | check << 3 | mate << 4; 255 = no move."""
    code = mf[..., :5].argmax(-1).astype(np.uint8) | (mf[..., 5] << 3) | (mf[..., 6] << 4)
    code[mf_act == A - 1] = 255
    return code


MF_TABLE = torch.zeros(256, 7)  # unpacks pack_mf codes (row 255 = padding, all zero)
for _c in range(32):
    if (_c & 7) < 5:
        MF_TABLE[_c, _c & 7] = 1
    MF_TABLE[_c, 5], MF_TABLE[_c, 6] = (_c >> 3) & 1, (_c >> 4) & 1


# ---------------------------------------------------------------- model

def critic_index(obs_size):
    """Gather indices for the critic features (chess_decoder.cu chess_critic_features), own and opponent view."""
    own, opp = [], []
    for k in range(768):
        plane, sq = divmod(k, 64)
        own.append(k)
        opp.append(((plane + 6) % 12) * 64 + (sq ^ 56))
    for j in range(26):
        if j == 0:
            own.append(O_SIDE); opp.append(O_SIDE)  # opponent side flag is 1 - side, fixed below
        elif j <= 4:
            c = j - 1
            own.append(O_CASTLE + c); opp.append(O_CASTLE + (c + 2) % 4)
        elif j <= 13:
            own.append(O_EP + j - 5); opp.append(O_EP + j - 5)
        elif j <= 21:
            own.append(O_RULE50 + j - 14); opp.append(O_RULE50 + j - 14)
        elif j <= 23:
            own.append(O_REP + j - 22); opp.append(O_REP + j - 22)
        else:
            own.append(O_SELF_CHECK if j == 24 else O_OPP_CHECK)
            opp.append(O_OPP_CHECK if j == 24 else O_SELF_CHECK)
    return torch.tensor(own), torch.tensor(opp)


class ChessNet(torch.nn.Module):
    def __init__(self, obs_size, hidden, layers, move_feats=0):
        super().__init__()
        self.mfn = move_feats
        H = hidden
        self.H, self.layers = H, layers
        self.enc = torch.nn.Parameter(torch.empty(H, obs_size))
        self.dec = torch.nn.Parameter(torch.empty(A + 1, H))
        self.w1 = torch.nn.Parameter(torch.empty(CH, CF))
        self.w2 = torch.nn.Parameter(torch.empty(1, CH))
        self.gru = torch.nn.ParameterList([torch.nn.Parameter(torch.empty(3 * H, H)) for _ in range(layers)])
        if move_feats:
            self.mf_w = torch.nn.Parameter(torch.zeros(move_feats))
            self.mf_u = torch.nn.Parameter(torch.zeros(move_feats, H))
        own, opp = critic_index(obs_size)
        self.register_buffer('own_idx', own, persistent=False)
        self.register_buffer('opp_idx', opp, persistent=False)
        # trainer init: kaiming normal, gain sqrt(2) for the encoder, 1 elsewhere (puf_kaiming_init)
        for p, gain in [(self.enc, math.sqrt(2)), (self.dec, 1.0), (self.w1, 1.0), (self.w2, 1.0)] + [(g, 1.0) for g in self.gru]:
            torch.nn.init.normal_(p, 0.0, gain / math.sqrt(p.shape[1]))

    def params_in_order(self, base_only=False):
        ps = [self.enc, self.dec, self.w1, self.w2] + list(self.gru)
        return ps if base_only or not self.mfn else ps + [self.mf_w, self.mf_u]

    def load_bin(self, path):
        flat = np.fromfile(path, dtype=np.float32)
        need = sum(p.numel() for p in self.params_in_order())
        base = sum(p.numel() for p in self.params_in_order(True))
        if self.mfn and len(flat) == base:  # plain checkpoint: move-feature weights stay zero
            need, order = base, self.params_in_order(True)
        else:
            order = self.params_in_order()
        assert len(flat) == need, f'{path}: {len(flat)} floats, this net needs {need}'
        i = 0
        with torch.no_grad():
            for p in order:
                p.copy_(torch.from_numpy(flat[i:i + p.numel()].reshape(p.shape)))
                i += p.numel()

    def save_bin(self, path):
        flat = np.concatenate([p.detach().float().cpu().numpy().ravel() for p in self.params_in_order()])
        tmp = path + '.tmp'
        flat.astype(np.float32).tofile(tmp)
        os.replace(tmp, path)

    def forward(self, obs, mf_act=None, mf=None):
        """obs [B, T, OBS] float -> logits [B, T, 4097], value [B, T]; recurrent state starts at zero.
        mf_act [B, T, M] long, mf [B, T, M, F] float: per-move features added to those moves' logits."""
        B, T, _ = obs.shape
        x = obs @ self.enc.t()
        for w in self.gru:
            c = x @ w.t()
            hid, gate, proj = c.split(self.H, dim=-1)
            z = torch.sigmoid(gate)
            ht = torch.where(hid >= 0, hid + 0.5, torch.sigmoid(hid))
            # h_t = (1 - z_t) h_{t-1} + z_t ht_t from h_0 = 0, as a parallel log-space scan (2026-11-14; the per-step
            # Python loop made training launch-bound at ~16K moves/s): with a_t = 1 - z_t and b_t = z_t ht_t > 0,
            # h_t = exp(A_t) * sum_{k<=t} b_k exp(-A_k), A_t = sum_{j<=t} log a_j. Float32 for the cumulative sums.
            h = mingru_scan(gate.float(), hid.float()).to(x.dtype)
            s = torch.sigmoid(proj)
            x = s * h + (1 - s) * x
        logits = x @ self.dec[:A].t()
        if self.mfn and mf is not None:
            coef = self.mf_w + x @ self.mf_u.t()                       # [B, T, F]
            bias = (mf * coef.unsqueeze(-2)).sum(-1)                   # [B, T, M]
            logits = logits.scatter_add(-1, mf_act, bias.to(logits.dtype))
        f_own = torch.relu(self.features(obs, False) @ self.w1.t()) @ self.w2.t()
        f_opp = torch.relu(self.features(obs, True) @ self.w1.t()) @ self.w2.t()
        value = torch.tanh(0.5 * (f_own - f_opp)).squeeze(-1)
        return logits, value

    def features(self, obs, opponent):
        f = obs.index_select(-1, self.opp_idx if opponent else self.own_idx)
        if opponent:
            f = torch.cat([f[..., :768], 1 - f[..., 768:769], f[..., 769:]], -1)
        return f


def unpack(packed, n):
    bits = (packed.unsqueeze(-1) >> torch.arange(8, device=packed.device, dtype=torch.uint8)) & 1
    return bits.flatten(-2)[..., :n]


def mingru_scan(gate, hid):
    """MinGRU over the time axis (dim 1) from a zero state, parallel: equals h_t = h_{t-1} + z_t (g(hid_t) - h_{t-1})."""
    log_a = -F.softplus(gate)                                          # log(1 - sigmoid(gate))
    log_b = -F.softplus(-gate) + torch.where(hid >= 0, (hid + 0.5).clamp(min=0.5).log(), -F.softplus(-hid))
    a_star = torch.cumsum(log_a, dim=1)
    return torch.exp(a_star + torch.logcumsumexp(log_b - a_star, dim=1))


def losses(net, batch, device, obs_size, vf):
    batch = [x.to(device, non_blocking=True) for x in batch]
    obs, mask, act, valid, mover, value = batch[:6]
    mfa = batch[6].long() if len(batch) > 6 else None
    mff = None
    if len(batch) > 6:  # packed codes [B, T, M] from the workers, or one-hot [B, T, M, F] (evaluate)
        mff = MF_TABLE.to(device)[batch[7].long()] if batch[7].dim() == 3 else batch[7].float()
    o = unpack(obs, obs_size).float()
    legal = unpack(mask, A).bool()
    with torch.autocast(device_type=device.split(':')[0], dtype=torch.bfloat16, enabled=device.startswith('cuda')):
        logits, v = net(o, mfa, mff)
    logits = logits.float().masked_fill(~legal, -1e9)
    ce = F.cross_entropy(logits.flatten(0, 1), act.flatten(), reduction='none').view_as(mover)
    n_move = mover.sum().clamp(min=1)
    pol = (ce * mover).sum() / n_move
    acc = ((logits.argmax(-1) == act).float() * mover).sum() / n_move
    vl = (((v.float() - value) ** 2) * valid).sum() / valid.sum().clamp(min=1)
    return pol + vf * vl, pol, vl, acc, n_move


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('games')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--init')
    ap.add_argument('--eval')
    ap.add_argument('--hidden', type=int, default=1024)
    ap.add_argument('--layers', type=int, default=2)
    ap.add_argument('--steps', type=int, default=20000)
    ap.add_argument('--batch', type=int, default=256)
    ap.add_argument('--window', type=int, default=128)
    ap.add_argument('--lr', type=float, default=1e-3)
    ap.add_argument('--vf', type=float, default=0.5)
    ap.add_argument('--workers', type=int, default=8)
    ap.add_argument('--val', type=int, default=2000, help='held-out games (the first ones in GAMES)')
    ap.add_argument('--device', default='cuda' if torch.cuda.is_available() else 'cpu')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--save-every', type=int, default=2000)
    ap.add_argument('--log-every', type=int, default=100)
    ap.add_argument('--teacher', action='store_true', help="GAMES are sf_label.py lines (Stockfish targets)")
    ap.add_argument('--move-feats', action='store_true', help='per-move tactical features (chess_tactics.h)')
    ap.add_argument('--target', default='sf', choices=['sf', 'search'], help='teacher move: Stockfish or net + search')
    args = ap.parse_args()
    torch.manual_seed(args.seed)
    torch.set_num_threads(4)  # the main process only feeds the GPU; default threads took ~12 cores from the data workers
    # Teacher data: the main process reads only the held-out games and each worker reads its own 1/workers slice
    # (2026-11-14: pickling all 1M games into each of 52 spawned workers stalled for 12+ min at 10 GB).
    games = read_teacher(args.games, args.target, stop=args.val) if args.teacher else read_games(args.games)
    if args.teacher:
        mk = lambda *a, **k: make_teacher_batch(*a, **k, move_feats=args.move_feats)
    else:
        mk = lambda *a, **k: make_batch(*a, **k, move_feats=args.move_feats)
    val, train = games[:args.val], games[args.val:]
    enc = Encoder()
    net = ChessNet(enc.obs, args.hidden, args.layers, enc.cf if args.move_feats else 0).to(args.device)
    if args.init or args.eval:
        net.load_bin(args.init or args.eval)

    def evaluate(n_batches=8):
        net.eval()
        rng, tot = random.Random(12345), np.zeros(4)
        with torch.no_grad():
            for _ in range(n_batches):
                b = tuple(torch.from_numpy(x) for x in mk(enc, val, rng, args.batch, args.window, 1.0))
                _, pol, vl, acc, n = losses(net, b, args.device, enc.obs, args.vf)
                tot += np.array([pol.item() * n.item(), vl.item() * n.item(), acc.item() * n.item(), n.item()])
        net.train()
        return tot[0] / tot[3], tot[1] / tot[3], tot[2] / tot[3]

    if args.eval:
        pol, vl, acc = evaluate(16)
        print(f'{args.eval}: held-out move CE {pol:.4f}, top-1 {acc:.4f}, value MSE {vl:.4f}')
        return
    assert args.out, 'OUT.bin required for training'
    mp.set_sharing_strategy('file_system')
    ctx = mp.get_context('spawn')
    queue = ctx.Queue(maxsize=4 * args.workers)
    shard = lambda w: (args.games, args.target, args.val, w, args.workers) if args.teacher else train
    procs = [ctx.Process(target=producer, args=(shard(w), args.seed * 1000 + w, args.batch, args.window, queue, args.teacher, args.move_feats), daemon=True)
             for w in range(args.workers)]
    for p in procs:
        p.start()
    opt = torch.optim.AdamW(net.parameters(), lr=args.lr, betas=(0.9, 0.95), weight_decay=0.0)
    sched = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda s: min(1.0, (s + 1) / 500) * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * min(1.0, s / args.steps)))))
    # Prefetch thread: take shared batches off the queue and pin them so the copy to the GPU is fast and overlaps.
    ready = pyqueue.Queue(maxsize=4)

    def prefetch():
        while True:
            ready.put(tuple(x.pin_memory() for x in queue.get()))
    threading.Thread(target=prefetch, daemon=True).start()
    t0, rows = time.time(), 0
    for step in range(1, args.steps + 1):
        batch = ready.get()
        loss, pol, vl, acc, n = losses(net, batch, args.device, enc.obs, args.vf)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
        opt.step()
        sched.step()
        rows += n.item()
        if step % args.log_every == 0:
            print(f'step {step} move CE {pol.item():.4f} top-1 {acc.item():.3f} value MSE {vl.item():.4f} '
                  f'lr {sched.get_last_lr()[0]:.2e} {rows / (time.time() - t0):.0f} moves/s', flush=True)
        if step % args.save_every == 0 or step == args.steps:
            vp, vv, va = evaluate()
            print(f'step {step} held-out move CE {vp:.4f} top-1 {va:.4f} value MSE {vv:.4f}', flush=True)
            net.save_bin(args.out)
    for p in procs:
        p.terminate()


if __name__ == '__main__':
    main()
