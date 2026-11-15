#!/usr/bin/env python3
"""Stockfish labels for every ply of a game (2026-11-12): reads 'FEN\\tUCI moves' lines (selfplay_games.py) or
lichess_games.py lines ('W B RESULT\\tmoves', start position), and writes 'FEN\\tmoves\\tbest\\tcp' lines: for ply i,
best[i] is Stockfish's move and cp[i] its score in centipawns from the side to move's view (mates as +-(30000-plies)).
The BC trainer (bc_train.py --teacher) trains the policy on best[] and the critic on cp[] along the played line.
Usage: sf_label.py IN OUT [--depth 10 | --nodes N] [--skip K --every M] [--sf PATH]"""
import argparse, os, subprocess, sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp'); ap.add_argument('out')
    ap.add_argument('--depth', type=int, default=10); ap.add_argument('--nodes', type=int, default=0)
    ap.add_argument('--skip', type=int, default=0, help='this worker labels lines i with i %% every == skip')
    ap.add_argument('--every', type=int, default=1)
    ap.add_argument('--max-lines', type=int, default=0)
    ap.add_argument('--sf', default=os.environ.get('SF_BIN', os.path.expanduser('~/tools/stockfish16/stockfish/stockfish-ubuntu-x86-64-avx2')))
    args = ap.parse_args()
    sf = subprocess.Popen([args.sf], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def cmd(s):
        sf.stdin.write(s + '\n'); sf.stdin.flush()

    def wait(tok):
        out = []
        while True:
            l = sf.stdout.readline()
            if not l:
                raise RuntimeError('stockfish died')
            out.append(l)
            if l.startswith(tok):
                return out
    cmd('uci'); wait('uciok')
    cmd('setoption name Threads value 1'); cmd('setoption name Hash value 32'); cmd('isready'); wait('readyok')
    go = f'go nodes {args.nodes}' if args.nodes else f'go depth {args.depth}'
    out = open(args.out, 'a')
    n = 0
    for i, line in enumerate(open(args.inp)):
        if i % args.every != args.skip:
            continue
        parts = line.rstrip('\n').split('\t')
        a, moves = parts[0], parts[1] if len(parts) > 1 else ''
        srch = parts[2].split() if len(parts) > 2 else []  # selfplay_games.py search moves, passed through
        fen = 'startpos' if ' ' in a and a.split()[0].isdigit() else a  # lichess_games.py lines start with Elos
        ms = moves.split()
        cmd('ucinewgame'); cmd('isready'); wait('readyok')
        best, cps = [], []
        for k in range(len(ms) + 1):
            pos = ('position startpos' if fen == 'startpos' else f'position fen {fen}') + (' moves ' + ' '.join(ms[:k]) if k else '')
            cmd(pos); cmd(go)
            ls = wait('bestmove')
            bm = ls[-1].split()[1]
            if bm == '(none)':
                break  # mate or stalemate: no label
            cp = 0
            for l in ls:
                t = l.split()
                if t and t[0] == 'info' and 'score' in t and 'pv' in t:
                    j = t.index('score')
                    cp = int(t[j + 2]) if t[j + 1] == 'cp' else (30000 - abs(int(t[j + 2]))) * (1 if int(t[j + 2]) > 0 else -1)
            best.append(bm); cps.append(str(cp))
        n_lab = min(len(best), len(ms))
        if n_lab:
            out.write(f'{"" if fen == "startpos" else fen}\t{" ".join(ms[:n_lab])}\t{" ".join(best[:n_lab])}\t{" ".join(cps[:n_lab])}'
                      + (f'\t{" ".join(srch[:n_lab])}' if srch else '') + '\n')
            out.flush()
        n += 1
        if args.max_lines and n >= args.max_lines:
            break
    cmd('quit')


if __name__ == '__main__':
    main()
