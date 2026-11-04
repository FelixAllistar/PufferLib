#!/usr/bin/env python3
"""Sampled Maia (2026-11-04): a UCI proxy for the chess env's Maia eval mode (chess.h, MAIA_LC0_PATH). It runs lc0
with the env's arguments (--weights=...), turns on VerboseMoveStats, and on each 'go' returns a move sampled from
Maia's policy, P(move)^(1/MAIA_TEMP) normalized (1 = Maia's own distribution, 0 = its top move as before), instead of
lc0's top move. Still one network evaluation per move (the env sends 'go nodes 1'). MAIA_SAMPLE_SEED seeds it."""
import os
import random
import re
import subprocess
import sys

lc0 = os.environ.get('MAIA_LC0_BIN', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lc0bin'))
proc = subprocess.Popen([lc0] + sys.argv[1:], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
temp = float(os.environ.get('MAIA_TEMP', '1.0'))
rng = random.Random(int(os.environ.get('MAIA_SAMPLE_SEED', '0')) * 1000003 + os.getpid())
stat = re.compile(r'^info string ([a-h][1-8][a-h][1-8][qrbn]?) .*\(P:\s*([0-9.]+)%\)')
configured = False


def send(line):
    proc.stdin.write(line + '\n')
    proc.stdin.flush()


for line in sys.stdin:
    cmd = line.strip()
    if cmd == 'isready' and not configured:
        send('setoption name VerboseMoveStats value true')
        configured = True
    send(cmd)
    if cmd == 'quit':
        break
    if cmd.startswith('go'):
        moves, weights = [], []
        for out in proc.stdout:
            m = stat.match(out)
            if m:
                moves.append(m.group(1))
                weights.append(float(m.group(2)))
                continue
            if out.startswith('bestmove'):
                best = out.split()[1]
                if moves and temp > 0:
                    w = [p ** (1.0 / temp) for p in weights]
                    best = rng.choices(moves, weights=w)[0] if sum(w) > 0 else best
                sys.stdout.write(f'bestmove {best}\n')
                sys.stdout.flush()
                break
            if not out.startswith('info'):
                sys.stdout.write(out)
                sys.stdout.flush()
        continue
    if cmd in ('uci', 'isready'):  # pass the handshake through
        want = 'uciok' if cmd == 'uci' else 'readyok'
        for out in proc.stdout:
            sys.stdout.write(out)
            sys.stdout.flush()
            if out.strip() == want:
                break
proc.wait()
