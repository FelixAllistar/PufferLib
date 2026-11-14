#!/usr/bin/env python3
"""Diverse start states for env.fen_curric_pct (2026-11-10). The user: "generating good states of all the openings and
opponent diversity, mix n match openings against each other". The env starts that share of training games from a
random line of resources/chess/fens.txt, so this file decides which positions the policy practices.

Each sample is built in three steps:
1. An opening, one of three kinds:
   named: every move of one named line from the Lichess opening list (lichess-org/chess-openings, CC0; ~3,700 lines
     A00-E99), drawn uniformly over lines, so rare openings are as common as main lines.
   mix: White plays the moves of one named line and Black the moves of another (a London against a King's Indian,
     an Italian against a Caro-Kann). On its turn a side plays the next move of its own line that is legal here
     (skipping up to two that are not); a side whose line is used up or blocked leaves book and its player takes
     over. This makes the hybrid structures real games reach and named lines never list.
   free: no book; the players below start from move 1 (as the old 38K opening set did, with Maia only).
2. A continuation of 0-30 plies where each side gets its own player, drawn independently: top-move-free Maia
   1100/1500/1900 (sampled from Maia's policy, temperature 1) or Stockfish at Skill Level 0/5/10/15/20. Mismatched
   pairs (Maia-1100 vs Stockfish 20) give the lopsided middlegames real opponents create.
3. A Stockfish check (--nodes per position) from the side to move: positions within --balanced centipawns are kept;
   positions up to --edge centipawns are kept with probability --edge-keep (practice converting and defending);
   others, mates and finished games are dropped.
Positions are deduplicated on board, side to move, castling and en passant, and every position in --held-out (the
eval's 1000 openings) is dropped so the eval lines stay unseen.

Output: OUT (one FEN per line, shuffled) and OUT.tsv (fen, kind, white line, black line, white player, black player,
plies, cp) for analysis. Workers append to OUT.part<worker> as they go, so a run can be stopped at any time and
--merge-only builds OUT from the parts written so far.
Usage: gen_states.py OUT --samples N --workers W --openings DIR --lc0 LC0 --maia-dir DIR --stockfish SF
       [--maia-proxy uci_maia_sample.py] [--held-out fens.txt] [--seed S] [--mix-kinds named:0.35,mix:0.45,free:0.2]
"""
import argparse
import csv
import os
import random
import sys
from multiprocessing import Pool

import chess
import chess.engine

PLAYERS = ['maia1100', 'maia1500', 'maia1900', 'sf0', 'sf5', 'sf10', 'sf15', 'sf20']


def key(fen):
    return ' '.join(fen.split()[:4])


def load_lines(directory):
    """(name, [SAN moves]) for every line of the Lichess opening TSVs."""
    lines = []
    for f in sorted(os.listdir(directory)):
        if not f.endswith('.tsv'):
            continue
        with open(os.path.join(directory, f)) as fh:
            for row in csv.DictReader(fh, delimiter='\t'):
                sans = [t for t in row['pgn'].split() if not t.endswith('.')]
                b = chess.Board()
                try:
                    for s in sans:
                        b.push_san(s)
                except ValueError:
                    continue
                lines.append((f"{row['eco']} {row['name']}", sans))
    return lines


def side_moves(sans, color):
    """The SAN moves one side plays in a line (White: even plies, Black: odd)."""
    return sans[0 if color == chess.WHITE else 1::2]


def play_mix(board, white, black, max_ply=24):
    """Each side plays its own line's next legal move; returns (plies played from book, which sides left book)."""
    queue = {chess.WHITE: list(white), chess.BLACK: list(black)}
    out = {chess.WHITE: False, chess.BLACK: False}
    while board.ply() < max_ply and not board.is_game_over() and not (out[chess.WHITE] and out[chess.BLACK]):
        side = board.turn
        if out[side]:
            return out  # the other side may still have book, but the side to move needs its player now
        move = None
        for skip in range(3):
            if skip >= len(queue[side]):
                break
            try:
                move = board.parse_san(queue[side][skip])
            except ValueError:
                continue
            queue[side] = queue[side][skip + 1:]
            break
        if move is None:
            out[side] = True
            return out
        board.push(move)
    return out


class Players:
    """Lazily started engines, one process per player kind per worker."""

    def __init__(self, args, seed):
        self.args, self.seed, self.engines = args, seed, {}

    def get(self, name):
        if name not in self.engines:
            a = self.args
            if name.startswith('maia'):
                env = dict(os.environ, MAIA_LC0_BIN=a.lc0, MAIA_TEMP='1', MAIA_SAMPLE_SEED=str(self.seed))
                weights = os.path.join(a.maia_dir, f'maia-{name[4:]}.pb.gz')
                self.engines[name] = chess.engine.SimpleEngine.popen_uci(
                    [sys.executable, a.maia_proxy, f'--weights={weights}', '--threads=1'], env=env)
            else:
                eng = chess.engine.SimpleEngine.popen_uci(a.stockfish)
                eng.configure({'Threads': 1, 'Hash': 16, 'Skill Level': int(name[2:])})
                self.engines[name] = eng
        return self.engines[name]

    def move(self, name, board):
        limit = chess.engine.Limit(nodes=1) if name.startswith('maia') else chess.engine.Limit(nodes=20000)
        return self.get(name).play(board, limit).move

    def judge(self):
        if 'judge' not in self.engines:
            eng = chess.engine.SimpleEngine.popen_uci(self.args.stockfish)
            eng.configure({'Threads': 1, 'Hash': 32})
            self.engines['judge'] = eng
        return self.engines['judge']

    def close(self):
        for e in self.engines.values():
            e.quit()


def work(job):
    args, wid, n = job
    rng = random.Random(args.seed * 1000003 + wid)
    part = open(f'{args.out}.part{wid}', 'a')
    lines = load_lines(args.openings)
    kinds = [(k, float(w)) for k, w in (kv.split(':') for kv in args.mix_kinds.split(','))]
    players = Players(args, args.seed * 100 + wid)
    rows, tries = [], 0
    while len(rows) < n and tries < n * 20:
        tries += 1
        kind = rng.choices([k for k, _ in kinds], [w for _, w in kinds])[0]
        board, wname, bname = chess.Board(), '', ''
        if kind == 'named':
            wname, sans = rng.choice(lines)
            bname = wname
            for s in sans:
                board.push_san(s)
        elif kind == 'mix':
            (wname, wl), (bname, bl) = rng.choice(lines), rng.choice(lines)
            play_mix(board, side_moves(wl, chess.WHITE), side_moves(bl, chess.BLACK))
        pw, pb = rng.choice(PLAYERS), rng.choice(PLAYERS)
        r = rng.random()
        extra = 0 if r < 0.35 else rng.randint(1, 12) if r < 0.8 else rng.randint(13, 30)
        if kind == 'free':
            extra = rng.randint(2, 16) + (0 if r < 0.5 else rng.randint(1, 20))
        for _ in range(extra):
            if board.is_game_over():
                break
            board.push(players.move(pw if board.turn == chess.WHITE else pb, board))
        if board.is_game_over() or board.ply() < 2:
            continue
        info = players.judge().analyse(board, chess.engine.Limit(nodes=args.nodes))
        score = info['score'].pov(board.turn)
        if score.is_mate():
            continue
        cp = score.score()
        if abs(cp) > args.edge or (abs(cp) > args.balanced and rng.random() >= args.edge_keep):
            continue
        rows.append((board.fen(), kind, wname, bname, pw, pb, board.ply(), cp))
        part.write('\t'.join(map(str, rows[-1])) + '\n')
        part.flush()
        if len(rows) % 200 == 0:
            print(f'worker {wid}: {len(rows)}/{n}', file=sys.stderr, flush=True)
    players.close()
    part.close()
    return len(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--samples', type=int, default=100000)
    ap.add_argument('--workers', type=int, default=4)
    ap.add_argument('--openings')
    ap.add_argument('--lc0')
    ap.add_argument('--maia-dir')
    ap.add_argument('--maia-proxy', default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                          '..', '..', '..', 'uci_maia_sample.py'))
    ap.add_argument('--stockfish')
    ap.add_argument('--held-out', default=None)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--nodes', type=int, default=100000)
    ap.add_argument('--balanced', type=int, default=150)
    ap.add_argument('--edge', type=int, default=500)
    ap.add_argument('--edge-keep', type=float, default=0.25)
    ap.add_argument('--mix-kinds', default='named:0.35,mix:0.45,free:0.2')
    ap.add_argument('--merge-only', action='store_true', help='build OUT from the OUT.part* files written so far')
    args = ap.parse_args()
    if not args.merge_only:
        assert args.openings and args.lc0 and args.maia_dir and args.stockfish, \
            '--openings, --lc0, --maia-dir and --stockfish are needed to generate'
        per = -(-int(args.samples * 1.15) // args.workers)  # ~15% spare for duplicates
        with Pool(args.workers) as p:
            p.map(work, [(args, w, per) for w in range(args.workers)])
    rows = []
    folder = os.path.dirname(os.path.abspath(args.out))
    for f in sorted(os.listdir(folder)):
        if f.startswith(os.path.basename(args.out) + '.part'):
            with open(os.path.join(folder, f)) as fh:
                rows += [tuple(l.rstrip('\n').split('\t')) for l in fh if l.count('\t') == 7]
    seen = set()
    if args.held_out:
        seen |= {key(l) for l in open(args.held_out) if l.strip()}
    held = len(seen)
    keep = []
    for r in rows:
        if key(r[0]) not in seen:
            seen.add(key(r[0]))
            keep.append(r)
    random.Random(args.seed).shuffle(keep)
    keep = keep[:args.samples]
    with open(args.out, 'w') as f:
        f.write(''.join(r[0] + '\n' for r in keep))
    with open(args.out + '.tsv', 'w') as f:
        f.write('fen\tkind\twhite_line\tblack_line\twhite_player\tblack_player\tplies\tcp\n')
        f.write(''.join('\t'.join(map(str, r)) + '\n' for r in keep))
    kinds = {}
    for r in keep:
        kinds[r[1]] = kinds.get(r[1], 0) + 1
    print(f'{len(rows)} samples -> {len(keep)} distinct states kept ({held} held-out positions excluded); by kind {kinds}')


if __name__ == '__main__':
    main()
