#!/usr/bin/env python3
"""Endgame probe (2026-11-16): can a chess checkpoint finish won games, policy only (greedy, no search)?
Test 1 (mate1): random positions with a mate in 1 for the side to move; does the top policy move mate? Split by where
  the losing king stands (corner / edge / inner). Also the critic's value there.
Test 2 (convert): random won endgames (KQK, KRK, ...), the model plays the strong side against Stockfish (node-limited,
  full strength) under the env's rules (threefold and 50-move draws). Outcome counts and plies to mate.
Usage: endgame_probe.py CKPT OUT.json [--hidden 1024] [--feats] [--n1 200] [--n2 60] [--sets KQK,KRK,...]
Runs on CPU; needs ~/tools/puffer_chess_engine (play.py + libchesslib3.so), python-chess, Stockfish (SF_BIN)."""
import argparse, collections, json, os, random, sys
ENG = os.path.expanduser('~/tools/puffer_chess_engine')
sys.path.insert(0, ENG)
sys.path.append(os.path.expanduser('~/tools/lichess-bot/venv/lib/python3.12/site-packages'))
import chess, chess.engine  # noqa: E402
import numpy as np, torch  # noqa: E402
from play import Rules, Net, action_uci, SF_BIN  # noqa: E402

P = {'Q': chess.QUEEN, 'R': chess.ROOK, 'B': chess.BISHOP, 'N': chess.KNIGHT, 'P': chess.PAWN}
SETS = {  # strong side's extra pieces, weak side's extra pieces
    'KQK': ('Q', ''), 'KRK': ('R', ''), 'KRRK': ('RR', ''), 'KQPPKPP': ('QPP', 'PP'), 'KRPPKPP': ('RPP', 'PP'),
    'KQRKR': ('QR', 'R'), 'KQNPPKRPP': ('QNPP', 'RPP'),
}
CORNERS = {0, 7, 56, 63}


def king_zone(sq):
    f, r = sq % 8, sq // 8
    return 'corner' if sq in CORNERS else 'edge' if f in (0, 7) or r in (0, 7) else 'inner'


def rand_pos(rng, name, strong, zone=None):
    s, w = SETS[name]
    while True:
        b = chess.Board(None)
        sqs = rng.sample(range(64), 2 + len(s) + len(w))
        if zone and king_zone(sqs[1]) != zone:
            continue
        b.set_piece_at(sqs[0], chess.Piece(chess.KING, strong)); b.set_piece_at(sqs[1], chess.Piece(chess.KING, not strong))
        ok = True
        for i, c in enumerate(s + w):
            sq = sqs[2 + i]
            if c == 'P' and sq // 8 in (0, 7):
                ok = False
            b.set_piece_at(sq, chess.Piece(P[c], strong if i < len(s) else (not strong)))
        b.turn = strong
        if ok and b.is_valid() and not b.is_game_over() and not b.is_insufficient_material():
            return b


def mates(b):
    out = []
    for m in b.legal_moves:
        b.push(m)
        if b.is_checkmate():
            out.append(m.uci())
        b.pop()
    return out


class Model:
    def __init__(self, a):
        self.rules = Rules()
        self.net = Net(a.ckpt, self.rules.obs_n, a.hidden, 2, 7 if a.feats else 0)
        self.feats = a.feats

    def start(self, fen, side):
        self.fen, self.side, self.state, self.done = fen, side, self.net.zero(), 0

    def move(self, moves):
        """Our move after `moves` from self.fen (the net is stepped through every earlier position, as in self-play)."""
        for n in range(self.done, len(moves)):
            _, obs, _, _, _ = self.rules.state(self.fen, moves[:n], self.side)
            _, _, self.state = self.net.step(obs, self.state)
        _, obs, mask, legal, _ = self.rules.state(self.fen, moves, self.side)
        mf = self.rules.move_feats(self.fen, moves) if self.feats else None
        logits, value, self.state = self.net.step(obs, self.state, mf)
        self.done = len(moves) + 1
        pr = torch.softmax(logits.masked_fill(torch.from_numpy(mask == 0), -1e9), 0)
        return pr, value, legal


def test_mate1(model, rng, name, n):
    rows = []
    for zone in ('corner', 'edge', 'inner'):  # n per zone; a zone with no mate in 1 (KQK inner) is given up on
        got = fails = 0
        while got < n and fails < 30000:
            strong = rng.random() < 0.5
            b = rand_pos(rng, name, strong, zone)
            mm = mates(b)
            if not mm:
                fails += 1
                continue
            got += 1
            rows.append(probe_mate1(model, b, strong, mm))
    return rows


def probe_mate1(model, b, strong, mm):
    side, stems = 0 if strong else 1, {m[:4] for m in mm}
    model.start(b.fen(), side)
    pr, value, legal = model.move([])
    top = action_uci(int(pr.argmax()), side, legal)
    pm = sum(float(pr[i]) for i in torch.nonzero(pr > 1e-4).flatten().tolist() if action_uci(i, side, legal)[:4] in stems)
    return {'zone': king_zone(b.king(not strong)), 'hit': top[:4] in stems, 'p_mate': pm, 'value': value,
            'n_legal': len(legal), 'fen': b.fen(), 'top': top}


def test_convert(model, sf, rng, name, n, nodes, cap):
    rows = []
    for g in range(n):
        strong = g % 2 == 0
        b = rand_pos(rng, name, strong)
        fen, side, moves = b.fen(), 0 if strong else 1, []
        model.start(fen, side)
        seen = collections.Counter([b._transposition_key()])
        out, v0 = None, None
        while out is None:
            if b.turn == strong:
                pr, value, legal = model.move(moves)
                v0 = value if v0 is None else v0
                mv = action_uci(int(pr.argmax()), side, legal)
            else:
                mv = sf.play(b, chess.engine.Limit(nodes=nodes)).move.uci()
            b.push_uci(mv); moves.append(mv)
            seen[b._transposition_key()] += 1
            if b.is_checkmate():
                out = 'mate' if b.turn != strong else 'got_mated'
            elif b.is_stalemate():
                out = 'stalemate'
            elif b.is_insufficient_material():
                out = 'lost_material'
            elif seen[b._transposition_key()] >= 3:
                out = 'threefold'
            elif b.halfmove_clock >= 100:
                out = 'fifty'
            elif len(moves) >= cap:
                out = 'cap'
        rows.append({'out': out, 'plies': len(moves), 'value0': v0, 'fen': fen, 'moves': ' '.join(moves[:400])})
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ckpt'); ap.add_argument('out')
    ap.add_argument('--hidden', type=int, default=1024); ap.add_argument('--feats', action='store_true')
    ap.add_argument('--n1', type=int, default=200); ap.add_argument('--n2', type=int, default=60)
    ap.add_argument('--sets', default='KQK,KRK,KRRK,KQPPKPP,KRPPKPP,KQRKR,KQNPPKRPP')
    ap.add_argument('--mate1-sets', default='KQK,KRK,KRRK')
    ap.add_argument('--nodes', type=int, default=4000); ap.add_argument('--cap', type=int, default=400)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    model = Model(a)
    sf = chess.engine.SimpleEngine.popen_uci(SF_BIN)
    sf.configure({'Threads': 1, 'Hash': 16})
    res = {'ckpt': a.ckpt, 'mate1': {}, 'convert': {}}
    for name in a.mate1_sets.split(','):
        rows = test_mate1(model, random.Random(a.seed), name, a.n1)
        res['mate1'][name] = rows
        for z in ('corner', 'edge', 'inner'):
            r = [x for x in rows if x['zone'] == z]
            if r:
                print(f"mate1 {name} {z}: n={len(r)} top-move mates {np.mean([x['hit'] for x in r]):.2f} "
                      f"p(mate) {np.mean([x['p_mate'] for x in r]):.2f} value {np.mean([x['value'] for x in r]):+.2f}", flush=True)
    for name in a.sets.split(','):
        rows = test_convert(model, sf, random.Random(a.seed + 1), name, a.n2, a.nodes, a.cap)
        res['convert'][name] = rows
        c = collections.Counter(x['out'] for x in rows)
        pl = [x['plies'] for x in rows if x['out'] == 'mate']
        print(f"convert {name}: n={len(rows)} " + ' '.join(f'{k}={v}' for k, v in c.most_common()) +
              (f" | plies to mate median {int(np.median(pl))}" if pl else '') +
              f" | start value {np.mean([x['value0'] for x in rows]):+.2f}", flush=True)
    sf.quit()
    json.dump(res, open(a.out, 'w'))


if __name__ == '__main__':
    main()
