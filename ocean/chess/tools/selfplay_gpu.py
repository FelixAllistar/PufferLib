#!/usr/bin/env python3
"""Batched self-play on the GPU for DAgger (2026-11-14). Same games as selfplay_games.py --lines --no-search (each side
keeps its own MinGRU state and both views observe every ply; the mover samples at --temp; games start from the start
position after a random 6-16 ply prefix of a lichess_games.py line), but --batch games run at once and one net step on
the GPU serves all of them. Why: 56 single-game CPU jobs with the 2048 net were memory-bandwidth bound (~3 min/game).
Writes 'FEN\\tUCI moves' lines for sf_label.py.
Usage: selfplay_gpu.py CKPT LINES OUT --games N [--batch 512] [--hidden 2048] [--move-feats] [--temp 1.0] [--seed 1]"""
import argparse, os, random, sys
import numpy as np
import torch
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from play import Rules, action_uci, A  # noqa: E402
from bc_train import ChessNet  # noqa: E402

START = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ckpt'); ap.add_argument('lines'); ap.add_argument('out')
    ap.add_argument('--games', type=int, default=1000); ap.add_argument('--batch', type=int, default=512)
    ap.add_argument('--hidden', type=int, default=2048); ap.add_argument('--layers', type=int, default=2)
    ap.add_argument('--move-feats', action='store_true'); ap.add_argument('--temp', type=float, default=1.0)
    ap.add_argument('--seed', type=int, default=1); ap.add_argument('--max-plies', type=int, default=300)
    args = ap.parse_args()
    rng = random.Random(args.seed); torch.manual_seed(args.seed)
    dev = 'cuda'
    lines = [l.rstrip('\n').split('\t')[1].split()[:16] for i, l in enumerate(open(args.lines))
             if i % 50 == args.seed % 50 and '\t' in l]
    rules = Rules()
    net = ChessNet(rules.obs_n, args.hidden, args.layers, 7 if args.move_feats else 0)
    net.load_bin(args.ckpt); net.to(dev).eval()
    H, Lr, B = args.hidden, args.layers, args.batch
    out = open(args.out, 'a')
    # per slot: moves, forced prefix; states [layers, view, B, H]
    st = torch.zeros(Lr, 2, B, H, device=dev)
    slots = [None] * B
    started = done = 0

    def new_game(i):
        nonlocal started
        line = rng.choice(lines)
        slots[i] = {'moves': [], 'forced': line[:rng.randint(6, 16)]}
        st[:, :, i] = 0
        started += 1

    for i in range(min(B, args.games)):
        new_game(i)
    with torch.no_grad():
        while done < args.games:
            act_idx = [i for i in range(B) if slots[i] is not None]
            if not act_idx:
                break
            obs = np.zeros((2, len(act_idx), rules.obs_n), np.float32)
            info, ended = [], []
            for k, i in enumerate(act_idx):
                g = slots[i]
                s0, o0, m0, l0, stat = rules.state(START, g['moves'], 0)
                if stat or len(g['moves']) >= args.max_plies:
                    ended.append(i); info.append(None); continue
                s1, o1, m1, l1, _ = rules.state(START, g['moves'], 1)
                obs[0, k], obs[1, k] = o0, o1
                info.append((s0, m0 if s0 == 0 else m1, l0 if s0 == 0 else l1))
            idx = torch.tensor(act_idx, device=dev)
            x = torch.from_numpy(obs).to(dev) @ net.enc.t()                       # [2, n, H]
            for li, w in enumerate(net.gru):
                hid, gate, proj = (x @ w.t()).split(H, dim=-1)
                z = torch.sigmoid(gate)
                ht = torch.where(hid >= 0, hid + 0.5, torch.sigmoid(hid))
                h = st[li][:, idx]
                h = h + z * (ht - h)
                st[li][:, idx] = h
                s = torch.sigmoid(proj)
                x = s * h + (1 - s) * x
            live = [k for k in range(len(act_idx)) if info[k] is not None]
            if live:
                side = torch.tensor([info[k][0] for k in live], device=dev)
                xm = x[side, torch.tensor(live, device=dev)]                           # mover's view [m, H]
                logits = xm @ net.dec[:A].t()
                if args.move_feats:
                    act = torch.full((len(live), 128), A - 1, dtype=torch.long); feat = torch.zeros(len(live), 128, 7)
                    for j, k in enumerate(live):
                        a, f = rules.move_feats(START, slots[act_idx[k]]['moves'])
                        act[j, :len(a)], feat[j, :len(a)] = a, f
                    act, feat = act.to(dev), feat.to(dev)
                    coef = net.mf_w + xm @ net.mf_u.t()
                    logits = logits.scatter_add(1, act, (feat * coef.unsqueeze(1)).sum(-1))
                mask = torch.from_numpy(np.stack([info[k][1] for k in live])).to(dev)
                logits = logits.masked_fill(mask == 0, -1e9)
                a = torch.multinomial(torch.softmax(logits / args.temp, -1), 1)[:, 0].tolist()
                for j, k in enumerate(live):
                    g = slots[act_idx[k]]
                    if g['forced']:
                        g['moves'].append(g['forced'].pop(0))   # human prefix: both views observed it above
                    else:
                        g['moves'].append(action_uci(a[j], info[k][0], info[k][2]))
            for i in ended:
                g = slots[i]
                if not g['forced']:
                    out.write(f'{START}\t{" ".join(g["moves"])}\n')
                done += 1
                slots[i] = None
                if started < args.games:
                    new_game(i)
            out.flush()


if __name__ == '__main__':
    main()
