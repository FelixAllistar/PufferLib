#!/usr/bin/env python3
"""Elo-limited Stockfish (2026-11-06): a UCI proxy for the chess env's Maia eval mode (chess.h, MAIA_LC0_PATH). Ignores
lc0's arguments, runs Stockfish with UCI_LimitStrength at SF_ELO (1320-3190; Stockfish calibrates it at 60s+0.6s) and
turns the env's 'go nodes 1' into 'go movetime SF_MS' (default 1000 ms)."""
import os, subprocess, sys
proc = subprocess.Popen([os.environ.get('SF_BIN', '/usr/games/stockfish')], stdin=subprocess.PIPE,
                        stdout=sys.stdout, text=True, bufsize=1)
configured = False
for line in sys.stdin:
    cmd = line.strip()
    if cmd == 'isready' and not configured:
        for opt in ('Threads value 1', 'Hash value 16', 'UCI_LimitStrength value true',
                    f'UCI_Elo value {os.environ.get("SF_ELO", "1320")}'):
            proc.stdin.write(f'setoption name {opt}\n')
        configured = True
    if cmd.startswith('go'):
        cmd = f'go movetime {os.environ.get("SF_MS", "1000")}'
    proc.stdin.write(cmd + '\n')
    proc.stdin.flush()
    if cmd == 'quit':
        break
proc.wait()
