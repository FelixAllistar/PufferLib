#!/usr/bin/env python3
"""Fingerprint how our net loses (2026-11-17). Input: game lines logged by the eval with CHESS_GAMES_OUT
("learner_color\tresult\tstart_fen\tuci moves"). For every game the learner did not win, Stockfish scores each
position (from the learner's side, as win %), and the script finds the learner move that cost the most. That move is
tagged by what the net's own per-move lookahead features (captures-only quiescence, as in chess_tactics.h) said about
it and about Stockfish's move, so we can tell "the obs showed it and the net ignored it" from "the obs could not see
it". Draws are split into fair draws and thrown wins (with how the game ended).

usage: failure_scan.py GAMES.txt [GAMES.txt ...] --out DIR [--nodes 60000] [--procs 4]"""
import argparse, collections, json, math, multiprocessing as mp, os, sys
sys.path.append(os.path.expanduser('~/tools/lichess-bot/venv/lib/python3.12/site-packages'))
import chess, chess.engine  # noqa: E402

SF_BIN = os.environ.get('SF_BIN', os.path.expanduser('~/tools/stockfish16/stockfish/stockfish-ubuntu-x86-64-avx2'))
VAL = {chess.PAWN: 100, chess.KNIGHT: 300, chess.BISHOP: 300, chess.ROOK: 500, chess.QUEEN: 900, chess.KING: 0}
BLUNDER = 15.0  # win-% points a single move must lose to count as the decisive mistake
MATE_CP = 10000


def winpct(cp):
    return 50 + 50 * (2 / (1 + math.exp(-0.00368208 * max(-MATE_CP, min(MATE_CP, cp)))) - 1)


def material(b, c):
    return sum(VAL[p.piece_type] * (1 if p.color == c else -1) for p in b.piece_map().values())


def q(b, alpha, beta, ply):
    """Captures-only quiescence with stand-pat, side to move's view (approximation of ct_q: captures, e.p., queen
    promotions; 16-ply cap)."""
    if b.is_checkmate():
        return -MATE_CP
    stand = material(b, b.turn)
    if not b.is_check():
        if stand >= beta:
            return stand
        alpha = max(alpha, stand)
    if ply >= 16:
        return stand
    moves = [m for m in b.legal_moves if b.is_capture(m) or m.promotion == chess.QUEEN or b.is_check()]
    if b.is_check() and not moves:
        return -MATE_CP
    moves.sort(key=lambda m: -(VAL[b.piece_type_at(m.to_square)] if b.piece_type_at(m.to_square) else 100))
    best = stand if not b.is_check() else -MATE_CP
    for m in moves:
        b.push(m)
        v = -q(b, -beta, -alpha, ply + 1)
        b.pop()
        if v > best:
            best = v
        if v > alpha:
            alpha = v
        if alpha >= beta:
            break
    return best


def feat(b, m):
    """The net's per-move feature for move m: material swing bucket (like CHESS_MOVE_FEATS), check, mate."""
    us = b.turn
    now = material(b, us)
    b.push(m)
    mate = b.is_checkmate()
    chk = b.is_check()
    after = -q(b, -MATE_CP, MATE_CP, 1) if not mate else MATE_CP
    b.pop()
    sw = after - now
    bucket = 'mate' if mate else ('--' if sw <= -250 else '-' if sw <= -50 else '0' if sw < 50 else '+' if sw < 250 else '++')
    return bucket, chk


def score(info, pov):
    s = info['score'].pov(pov)
    if s.is_mate():
        mt = s.mate()
        return (MATE_CP - abs(mt)) * (1 if mt > 0 else -1), mt
    return s.score(), None


def end_reason(b):
    if b.is_checkmate():
        return 'mate'
    if b.is_stalemate():
        return 'stalemate'
    if b.is_insufficient_material():
        return 'insufficient'
    if b.can_claim_threefold_repetition():
        return 'repetition'
    if b.halfmove_clock >= 100:
        return '50move'
    return 'other(len %d)' % len(b.move_stack)


def scan(args):
    line, nodes = args
    col, result, fen, mv = line.rstrip('\n').split('\t')
    me = chess.WHITE if col == 'w' else chess.BLACK
    won = (result == 'White Wins') == (me == chess.WHITE) and result != 'Draw'
    if won:
        return None
    sf = scan.sf
    b = chess.Board(fen)
    moves = [chess.Move.from_uci(u) for u in mv.split()] if mv else []
    lim = chess.engine.Limit(nodes=nodes)
    info = sf.analyse(b, lim)
    rows, wp_max = [], 0
    for i, m in enumerate(moves):
        if m not in b.legal_moves:
            return {'error': 'illegal %s at %d' % (m, i)}
        mover = b.turn
        cp_b, mate_b = score(info, me)
        best = info['pv'][0] if info.get('pv') else None
        fm = feat(b, m) if mover == me else None
        fb = feat(b, best) if mover == me and best else None
        san = b.san(m)
        bsan = b.san(best) if best and mover == me else None
        b.push(m)
        info = sf.analyse(b, lim) if not b.is_game_over(claim_draw=False) else None
        if info is None:
            cp_a = MATE_CP if b.is_checkmate() and mover == me else -MATE_CP if b.is_checkmate() else 0
            mate_a = (1 if mover == me else -1) if b.is_checkmate() else None
        else:
            cp_a, mate_a = score(info, me)
        if mover == me:
            wb, wa = winpct(cp_b), winpct(cp_a)
            wp_max = max(wp_max, wb)
            reply = info['pv'][0] if info and info.get('pv') else None
            rows.append(dict(ply=i, move=san, uci=m.uci(), best=bsan, wp_before=round(wb, 1), wp_after=round(wa, 1),
                             drop=round(wb - wa, 1), mate_before=mate_b, mate_after=mate_a, feat=fm[0], check=fm[1],
                             best_feat=fb[0] if fb else None, reply=b.san(reply) if reply else None,
                             reply_check=b.gives_check(reply) if reply else None,
                             reply_capture=b.is_capture(reply) if reply else None,
                             pieces=len(b.piece_map())))
        if info is None:
            break
    draw = result == 'Draw'
    worst = max(rows, key=lambda r: r['drop']) if rows else None
    g = dict(color=col, result=result, start=fen, plies=len(moves), draw=draw, wp_max=round(wp_max, 1),
             end=end_reason(b), worst=worst, moves=mv)
    g['tag'] = classify(g, rows)
    return g


def classify(g, rows):
    w = g['worst']
    if g['draw'] and g['wp_max'] < 85:
        return 'fair_draw'
    pre = 'thrown_draw:' if g['draw'] else ('thrown_loss:' if g['wp_max'] >= 85 else 'loss:')
    if w is None or w['drop'] < BLUNDER:
        return pre + ('drift(' + g['end'] + ')' if g['draw'] else 'drift')
    if w['mate_after'] is not None and w['mate_after'] < 0 and (w['mate_before'] is None or w['mate_before'] > 0):
        return pre + 'allowed_mate_in_%d' % min(-w['mate_after'], 4)
    if w['mate_before'] is not None and w['mate_before'] > 0 and (w['mate_after'] is None or w['mate_after'] < 0):
        return pre + 'missed_mate'
    if w['feat'] in ('--', '-'):
        return pre + 'obs_flagged_loss_ignored'
    if w['best_feat'] in ('+', '++', 'mate') and w['feat'] in ('0',):
        return pre + 'obs_flagged_gain_ignored'
    kind = 'check' if w['reply_check'] else 'capture' if w['reply_capture'] else 'quiet'
    return pre + 'invisible_tactic(reply ' + kind + ')'


def init(nodes):
    scan.sf = chess.engine.SimpleEngine.popen_uci(SF_BIN)
    scan.sf.configure({'Threads': 1, 'Hash': 32})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('games', nargs='+'); ap.add_argument('--out', required=True)
    ap.add_argument('--nodes', type=int, default=60000); ap.add_argument('--procs', type=int, default=4)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for path in a.games:
        lines = open(path).read().splitlines()
        with mp.Pool(a.procs, initializer=init, initargs=(a.nodes,)) as pool:
            res = [r for r in pool.imap(scan, [(l, a.nodes) for l in lines], chunksize=4) if r]
        name = os.path.splitext(os.path.basename(path))[0]
        with open(os.path.join(a.out, name + '.jsonl'), 'w') as f:
            for r in res:
                f.write(json.dumps(r) + '\n')
        n = len(lines)
        c = collections.Counter(r.get('tag', 'error') for r in res)
        print(f'== {name}: {n} games, {len(res)} not won')
        for k, v in c.most_common():
            print(f'  {v:4d} {100 * v / n:5.1f}%  {k}')
        sys.stdout.flush()


if __name__ == '__main__':
    main()
