#!/usr/bin/env python3
"""Drill into failure_scan.py output (2026-11-17): for each decisive mistake m with Stockfish's refutation r, what
pattern r uses, and whether a cheap candidate feature computed for m (before r is known) would have flagged it.
Candidate features for a move m (all from the mover's view, computed by playing m):
  mate1   the opponent then has a mate in 1
  chkq    quiescence where the opponent's first ply may also be a check (checks extend the capture search) loses
          >= 200 more than the plain capture quiescence
  fork    some opponent reply leaves >= 2 of our pieces en prise (attacked and undefended, or attacked by a cheaper
          piece), or gives check while another piece is en prise
usage: failure_drill.py SCAN.jsonl [...]"""
import collections, json, os, sys
sys.path.append(os.path.expanduser('~/tools/lichess-bot/venv/lib/python3.12/site-packages'))
import chess  # noqa: E402
from failure_scan import q, material, VAL, MATE_CP  # noqa: E402


def en_prise(b, color):
    out = []
    for sq, p in b.piece_map().items():
        if p.color != color or p.piece_type == chess.KING:
            continue
        att = b.attackers(not color, sq)
        if not att:
            continue
        defended = bool(b.attackers(color, sq))
        cheapest = min(VAL[b.piece_type_at(a)] or 10000 for a in att)
        if not defended or cheapest < VAL[p.piece_type]:
            out.append(sq)
    return out


def mate_in_1(b):
    for r in b.legal_moves:
        if b.gives_check(r):
            b.push(r)
            mt = b.is_checkmate()
            b.pop()
            if mt:
                return True
    return False


def q_checks(b, ply=0):
    """Opponent to move: best material (their view) when their first ply may be any capture, promotion or check,
    then plain quiescence."""
    stand = material(b, b.turn)
    best = stand
    for r in b.legal_moves:
        if not (b.is_capture(r) or r.promotion or b.gives_check(r)):
            continue
        b.push(r)
        v = -q(b, -MATE_CP, MATE_CP, 1)
        b.pop()
        best = max(best, v)
    return best


def cands(b, m):
    """b: position with us to move. Candidate features of move m."""
    us = b.turn
    b.push(m)
    f = {}
    f['mate1'] = mate_in_1(b)
    plain = q(b, -MATE_CP, MATE_CP, 1)  # opponent's view
    f['chkq'] = q_checks(b) - plain >= 200
    fork = False
    for r in list(b.legal_moves):
        b.push(r)
        ep = en_prise(b, us)
        if len(ep) >= 2 or (b.is_check() and len(ep) >= 1):
            fork = True
        b.pop()
        if fork:
            break
    f['fork'] = fork
    b.pop()
    return f


def main():
    rows = collections.Counter(); totals = collections.Counter(); best_rows = collections.Counter(); n = 0
    pat = collections.Counter()
    for path in sys.argv[1:]:
        for l in open(path):
            g = json.loads(l)
            t = g['tag']
            if t == 'fair_draw' or g['worst'] is None or g['worst']['drop'] < 15:
                continue
            w = g['worst']
            b = chess.Board(g['start'])
            for u in g['moves'].split()[:w['ply']]:
                b.push_uci(u)
            m = chess.Move.from_uci(w['uci'])
            cat = t.split(':')[-1]
            cat = 'allowed_mate' if cat.startswith('allowed_mate') else cat
            lost = 0.5 if g['draw'] else 1.0
            fm = cands(b, m)
            fb = cands(b, b.parse_san(w['best'])) if w['best'] and w['best'] != w['move'] else None
            n += 1
            totals[cat] += lost
            for k, v in fm.items():
                if v:
                    rows[(cat, k)] += lost
                    if fb is not None and not fb[k]:
                        best_rows[(cat, k)] += lost
            if any(fm.values()):
                rows[(cat, 'any')] += lost
                if fb is not None and not any(fb.values()):
                    best_rows[(cat, 'any')] += lost
            if cat.startswith('invisible') and w.get('reply'):
                bb = b.copy(); bb.push(m)
                try:
                    r = bb.parse_san(w['reply'])
                except ValueError:
                    continue
                bb.push(r)
                ep = en_prise(bb, bb.turn)  # our pieces (we are to move)
                kind = 'check+en_prise' if bb.is_check() and ep else 'check' if bb.is_check() else \
                       'double_attack' if len(ep) >= 2 else 'single_en_prise' if len(ep) == 1 else 'none_en_prise'
                pat[(cat, kind)] += lost
    print('decisive mistakes (points lost); candidate flags on the played move [and only on it, not on SF best]')
    for cat in sorted(totals, key=lambda c: -totals[c]):
        s = ' '.join(f"{k}={rows[(cat, k)]:.1f}[{best_rows[(cat, k)]:.1f}]" for k in ('mate1', 'chkq', 'fork', 'any'))
        print(f'  {cat:40s} {totals[cat]:6.1f}  {s}')
    print('refutation pattern after the invisible tactics (points lost)')
    for (cat, kind), v in sorted(pat.items(), key=lambda x: -x[1]):
        print(f'  {cat:40s} {kind:16s} {v:6.1f}')


if __name__ == '__main__':
    main()
