#!/usr/bin/env python3
"""Would a cheap fork detector have flagged our decisive mistakes? (2026-11-18). For a move m: after m, does the
opponent have a quiet move r whose moved piece lands on a square we can't profitably take and attacks >= 2 of our
non-pawn pieces (or a piece + our king) that are each undefended or worth more than it? Scored on the played move vs
Stockfish's move, like failure_drill.py. usage: fork_probe.py SCAN.jsonl [...]"""
import collections, json, os, sys
sys.path.append(os.path.expanduser('~/tools/lichess-bot/venv/lib/python3.12/site-packages'))
import chess
V = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9, chess.KING: 100}

def fork_after(b):
    """b: opponent to move. Any quiet fork?"""
    them = b.turn; us = not them
    for r in b.legal_moves:
        if b.is_capture(r) or r.promotion: continue
        p = b.piece_at(r.from_square); pv = V[p.piece_type]
        b.push(r)
        to = r.to_square
        ours = b.attackers(us, to)
        safe = not ours or (b.attackers(them, to) and min(V[b.piece_at(s).piece_type] for s in ours) >= pv)
        n = 0
        if safe:
            for sq in b.attacks(to):
                q = b.piece_at(sq)
                if not q or q.color != us or q.piece_type == chess.PAWN: continue
                if q.piece_type == chess.KING or V[q.piece_type] > pv or not b.attackers(us, sq):
                    n += 1
        b.pop()
        if n >= 2: return True
    return False

def flag(b, m):
    b.push(m); f = fork_after(b); b.pop(); return f

tot = collections.Counter(); hit = collections.Counter(); disc = collections.Counter(); bestfire = collections.Counter()
for path in sys.argv[1:]:
    for l in open(path):
        g = json.loads(l); w = g['worst']
        if g['tag'] == 'fair_draw' or not w or w['drop'] < 15 or not w['best'] or w['best'] == w['move']: continue
        cat = g['tag'].split(':')[-1]; cat = 'allowed_mate' if cat.startswith('allowed_mate') else cat
        b = chess.Board(g['start'])
        for u in g['moves'].split()[:w['ply']]: b.push_uci(u)
        lost = 0.5 if g['draw'] else 1.0
        fp = flag(b, chess.Move.from_uci(w['uci'])); fb = flag(b, b.parse_san(w['best']))
        tot[cat] += lost; hit[cat] += lost * fp; disc[cat] += lost * (fp and not fb); bestfire[cat] += lost * fb
print(f"{'category':38s} {'pts':>6s} {'played':>7s} {'disc':>6s} {'SFbest':>7s}")
for c in sorted(tot, key=lambda c: -tot[c]):
    print(f"{c:38s} {tot[c]:6.1f} {hit[c]:7.1f} {disc[c]:6.1f} {bestfire[c]:7.1f}")
