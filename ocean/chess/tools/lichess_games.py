#!/usr/bin/env python3
"""Human games for behavior cloning (2026-11-10): streams one Lichess monthly database dump (CC0,
https://database.lichess.org, .pgn.zst) and keeps rated standard games that finished normally (no abandonments or
rule infractions; time forfeits are kept), at blitz 3+0 or slower, with both players rated at least --min-elo.
The download is decompressed on the fly and stops once --games games are kept, so only part of a month is fetched.

Output, one game per line: "WHITE_ELO BLACK_ELO RESULT<TAB>UCI moves" (RESULT 1-0, 0-1 or 1/2-1/2), the input of
bc_train.py.
Usage: lichess_games.py OUT --url URL [--games 1000000] [--min-elo 2000] [--min-base 180] [--workers 4]
       (or --pgn FILE.pgn.zst for a local file)
"""
import argparse
import io
import re
import sys
import urllib.request
from multiprocessing import Pool

import chess
import zstandard

HEADER = re.compile(r'^\[(\w+) "(.*)"\]$')
COMMENT = re.compile(r'\{[^}]*\}')
MOVE_NUMBER = re.compile(r'^\d+\.+$')
RESULTS = {'1-0', '0-1', '1/2-1/2'}


def games(stream):
    """Yields (headers, movetext) for each game in a PGN text stream."""
    headers, moves = {}, []
    for line in stream:
        line = line.rstrip('\n')
        if line.startswith('['):
            if moves:
                yield headers, ' '.join(moves)
                headers, moves = {}, []
            m = HEADER.match(line)
            if m:
                headers[m.group(1)] = m.group(2)
        elif line.strip():
            moves.append(line)
    if moves:
        yield headers, ' '.join(moves)


def wanted(h, args):
    if h.get('Result') not in RESULTS or h.get('Variant', 'Standard') != 'Standard':
        return False
    if h.get('Termination') not in ('Normal', 'Time forfeit'):
        return False
    if 'Rated' not in h.get('Event', ''):
        return False
    tc = h.get('TimeControl', '-')
    if '+' not in tc:
        return False
    base, inc = tc.split('+')
    if int(base) + 40 * int(inc) < args.min_base:
        return False
    try:
        return int(h['WhiteElo']) >= args.min_elo and int(h['BlackElo']) >= args.min_elo
    except (KeyError, ValueError):
        return False


def convert(item):
    """SAN movetext -> one output line, or None if it fails to replay."""
    h, text = item
    board, uci = chess.Board(), []
    for tok in COMMENT.sub(' ', text).split():
        if MOVE_NUMBER.match(tok) or tok in RESULTS or tok == '*':
            continue
        tok = re.sub(r'^\d+\.+', '', tok)
        try:
            move = board.parse_san(tok)
        except ValueError:
            return None
        uci.append(move.uci())
        board.push(move)
    if len(uci) < 10:
        return None
    return f"{h['WhiteElo']} {h['BlackElo']} {h['Result']}\t{' '.join(uci)}\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--url')
    ap.add_argument('--pgn')
    ap.add_argument('--games', type=int, default=1000000)
    ap.add_argument('--min-elo', type=int, default=2000)
    ap.add_argument('--min-base', type=int, default=180, help='base + 40 x increment, in seconds')
    ap.add_argument('--workers', type=int, default=4)
    args = ap.parse_args()
    raw = urllib.request.urlopen(args.url) if args.url else open(args.pgn, 'rb')
    text = io.TextIOWrapper(zstandard.ZstdDecompressor().stream_reader(raw), encoding='utf-8', errors='replace')

    def selected():
        for h, movetext in games(text):
            if wanted(h, args):
                yield h, movetext

    kept = seen = 0
    with open(args.out, 'w') as out, Pool(args.workers) as pool:
        for line in pool.imap(convert, selected(), chunksize=256):
            seen += 1
            if line:
                out.write(line)
                kept += 1
                if kept % 50000 == 0:
                    print(f'{kept} games kept', file=sys.stderr, flush=True)
                if kept >= args.games:
                    break
        pool.terminate()
    print(f'{kept} games kept of {seen} that passed the header filter')


if __name__ == '__main__':
    main()
