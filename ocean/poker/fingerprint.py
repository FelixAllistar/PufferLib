"""Behavior fingerprints: distances, a map and per-situation summaries (2026-11-05).

Reads the text output of `fingerprint probe` (ocean/poker/fingerprint.c): per player, its distribution over the 8
engine actions at every probe. Actions are collapsed to six kinds (fold, check/call, small raise: min or +1/3 pot,
medium: +2/3 or +pot, big: +2 pots, all in). The distance between two players is the Jensen-Shannon divergence of
their kind distributions, averaged over probes (each stratum holds at most CAP probes, so situations weigh about
equally). The map is classical MDS of that matrix. Summaries are what a HUD would call style, measured in fixed spots.

Usage: python3 fingerprint.py PROBES.txt [MAP.svg]
"""
import sys
import numpy as np

KINDS = ['fold', 'call', 'small', 'medium', 'big', 'all-in']
GROUP = [0, 1, 2, 2, 3, 3, 4, 5]  # engine action -> kind
STREETS = ['preflop', 'flop', 'turn', 'river']
FACING = ['unopened', 'facing a bet', 'raised back']


def load(path):
    names, dists = [], []
    with open(path) as f:
        n = int(f.readline().split()[1])
        sit = np.array(f.readline().split()[1:], dtype=int).reshape(n, 5)  # street facing position strength count
        for line in f:
            parts = line.split()
            names.append(parts[0])
            raw = np.array(parts[1:], dtype=float).reshape(n, 8)
            kinds = np.zeros((n, len(KINDS)))
            for a, k in enumerate(GROUP):
                kinds[:, k] += raw[:, a]
            dists.append(kinds)
    return names, np.array(dists), sit


def js(p, q):
    m = (p + q) / 2
    def kl(a, b):
        with np.errstate(divide='ignore', invalid='ignore'):
            return np.where(a > 0, a * np.log2(a / np.maximum(b, 1e-12)), 0).sum(-1)
    return (kl(p, m) + kl(q, m)) / 2


def main():
    names, d, sit = load(sys.argv[1])
    k = len(names)
    dist = np.array([[js(d[i], d[j]).mean() for j in range(k)] for i in range(k)])
    print(f'{d.shape[1]} probes, {k} players. Mean Jensen-Shannon distance (0 = same play, 1 = disjoint):')
    w = max(len(n) for n in names)
    print(' ' * (w + 1) + ' '.join(f'{n[:7]:>7}' for n in names))
    for i, n in enumerate(names):
        print(f'{n:<{w}} ' + ' '.join(f'{dist[i, j]:7.3f}' for j in range(k)))
    print('\nNearest other player:')
    for i, n in enumerate(names):
        j = min((x for x in range(k) if x != i), key=lambda x: dist[i, x])
        print(f'  {n:<{w}} -> {names[j]} ({dist[i, j]:.3f})')

    # Style in fixed spots: average kind probabilities in a few situation groups.
    def spot(mask):
        return d[:, mask].mean(1) if mask.any() else np.full((k, len(KINDS)), np.nan)
    pre, post = sit[:, 0] == 0, sit[:, 0] > 0
    rows = [
        ('preflop unopened, weak hands', pre & (sit[:, 1] == 0) & (sit[:, 3] <= 1)),
        ('preflop unopened, strong hands', pre & (sit[:, 1] == 0) & (sit[:, 3] >= 4)),
        ('preflop facing a raise, weak', pre & (sit[:, 1] == 1) & (sit[:, 3] <= 1)),
        ('preflop facing a raise, strong', pre & (sit[:, 1] == 1) & (sit[:, 3] >= 4)),
        ('postflop unopened, nothing', post & (sit[:, 1] == 0) & (sit[:, 3] == 0)),
        ('postflop unopened, two pair+', post & (sit[:, 1] == 0) & (sit[:, 3] >= 2)),
        ('postflop facing a bet, nothing', post & (sit[:, 1] == 1) & (sit[:, 3] == 0)),
        ('postflop facing a bet, two pair+', post & (sit[:, 1] == 1) & (sit[:, 3] >= 2)),
        ('raised back after betting', sit[:, 1] == 2),
    ]
    print('\nStyle in fixed spots: fold / raise (any size) / all-in, % of the time')
    print(f'{"":<34}' + ''.join(f'{n[:12]:>14}' for n in names))
    for label, mask in rows:
        s = spot(mask)
        cells = ''.join(f'{100*s[i,0]:4.0f}/{100*s[i,2:5].sum():3.0f}/{100*s[i,5]:3.0f}  ' for i in range(k))
        print(f'{label:<28}{mask.sum():5d} ' + cells)

    if len(sys.argv) > 2:
        # Classical MDS of the distance matrix.
        j = np.eye(k) - np.ones((k, k)) / k
        b = -0.5 * j @ (dist ** 2) @ j
        vals, vecs = np.linalg.eigh(b)
        order = np.argsort(vals)[::-1][:2]
        xy = vecs[:, order] * np.sqrt(np.maximum(vals[order], 0))
        # A small SVG map (no plotting dependency): bots red, models teal.
        bots = {'rules', 'fullhouse', 'range', 'tight', 'trapper'}
        lo, hi = xy.min(0), xy.max(0)
        span = np.maximum(hi - lo, 1e-9)
        W, H, M = 760, 560, 70
        pts = [(M + (x - lo[0]) / span[0] * (W - 2 * M), H - M - (y - lo[1]) / span[1] * (H - 2 * M)) for x, y in xy]
        out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" font-family="Inter,Arial,sans-serif">',
               f'<rect width="{W}" height="{H}" fill="#fbfaf7"/>',
               '<text x="20" y="30" font-size="16" fill="#222">Poker behavior map (MDS of action fingerprints)</text>',
               '<text x="20" y="50" font-size="12" fill="#666">red: bots, teal: models; distance = mean Jensen-Shannon divergence of actions in fixed spots</text>']
        for (x, y), n in zip(pts, names):
            c = '#c44b36' if n in bots else '#2a7f8f'
            out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="7" fill="{c}"/><text x="{x + 10:.1f}" y="{y + 4:.1f}" font-size="13" fill="#222">{n}</text>')
        out.append('</svg>')
        open(sys.argv[2], 'w').write('\n'.join(out))
        print(f'\nmap: {sys.argv[2]}')


if __name__ == '__main__':
    main()
