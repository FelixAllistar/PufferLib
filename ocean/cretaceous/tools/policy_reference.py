"""NumPy reference for the CPU policy path (policy_cpu.c) and writer of its test fixtures.

Builds a whole policy (encoder, MinGRU layers, decoder) on weights drawn from the sim's xorshift generator, runs it in
float64 over a few steps of random observations with recurrent state and resets, and writes a fixture: the seed and
tensor shapes (tests/test_policy.c regenerates the same float32 weights bit for bit and writes them as a checkpoint),
the observations, the resets, and the expected encoder output, logits + value, greedy and sampled actions per step.
tests/test_policy.c runs the C path on it and prints the largest error, so `make test` checks it without Python.

    python3 ocean/cretaceous/tools/policy_reference.py --net 1 --out ocean/cretaceous/tests/policy_fixture_net1.bin
    python3 ocean/cretaceous/tools/policy_reference.py --net 1 --hidden 1024 --batch 64 --out /tmp/fx.bin   # full size

The structured encoder is encoder_reference.forward (written from docs/encoder.md); the default encoder is one
bias-free linear layer; the MinGRU and the sampling follow src/algo.cu (mingru_gate) and src/pufferl.cu (sample_logits).
"""
import argparse
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import encoder_reference as er  # noqa: E402

OBS = 246
HEADS = [5, 5, 3, 2, 8, 5]
ATN = sum(HEADS)
NAMES = er.NAMES
MAGIC, VERSION = 0x43504F4C, 1


def aug(n):
    return (n + 8) & ~7


def encoder_shapes(net, hidden):
    if net == 0:
        return [(hidden, OBS)]
    return [(16, aug(6)), (64, aug(64)), (64, aug(64)), (64, aug(64)), (16, aug(18)), (32, aug(144)), (64, aug(288)),
            (64, aug(30)), (64, aug(64)), (256, aug(272)), (hidden, aug(256))]


def shapes(net, hidden, layers):
    return encoder_shapes(net, hidden) + [(ATN + 1, hidden)] + [(3 * hidden, hidden)] * layers


def xs32(s):
    s ^= (s << 13) & 0xFFFFFFFF
    s ^= s >> 17
    s ^= (s << 5) & 0xFFFFFFFF
    return s if s else 0x9E3779B9


def gen_weights(seed, shp):
    """Float32 weights uniform in +-sqrt(6 / cols), the same draws and roundings as tests/test_policy.c."""
    s, out = seed, []
    for r, c in shp:
        n = r * c
        u = np.empty(n, np.uint32)
        for i in range(n):
            s = xs32(s)
            u[i] = s >> 8
        scale = np.sqrt(np.float32(6.0) / np.float32(c), dtype=np.float32)
        w = (u.astype(np.float32) * np.float32(1.0 / 16777216.0) * np.float32(2.0) - np.float32(1.0)) * scale
        out.append(w.astype(np.float32).reshape(r, c))
    return out


def gen_obs(rng, B):
    obs = rng.uniform(-1, 1, (B, OBS))
    obs[:, er.PATCH_AT:er.NB_AT] = rng.uniform(-1, 2, (B, er.NB_AT - er.PATCH_AT))
    obs[:, er.SPECIES_AT:er.SPECIES_AT + er.SPECIES] = 0
    obs[np.arange(B), er.SPECIES_AT + rng.integers(0, er.SPECIES, B)] = 1
    present = (rng.uniform(0, 1, (B, er.NB)) < 0.6).astype(np.float64)
    present[0] = 0          # no neighbour: attention and max pools are 0
    if B > 1:
        present[1] = 1      # every slot present
    for n in range(er.NB):
        obs[:, er.NB_AT + n * er.NB_OBS] = present[:, n]
    return obs.astype(np.float32)


def sigmoid(x):
    return 1 / (1 + np.exp(-x))


class Policy:
    def __init__(self, net, hidden, layers, weights):
        self.net, self.H, self.L = net, hidden, layers
        w = [x.astype(np.float64) for x in weights]
        ne = len(encoder_shapes(net, hidden))
        self.enc, self.dec, self.gru = w[:ne], w[ne], w[ne + 1:]

    def encode(self, obs):
        if self.net == 0:
            return obs @ self.enc[0].T
        return er.forward(obs, dict(zip(NAMES, self.enc)), er.Numpy)

    def step(self, obs, state):
        """obs (B, 246) float64, state (L, B, H) updated in place. Returns encoder output and logits + value."""
        e = self.encode(obs)
        x = e
        for l in range(self.L):
            c = x @ self.gru[l].T
            hid, gate, hw = c[:, :self.H], c[:, self.H:2 * self.H], c[:, 2 * self.H:]
            ht = np.where(hid >= 0, hid + 0.5, sigmoid(hid))
            z = sigmoid(gate)
            h = state[l] + z * (ht - state[l])
            state[l] = h
            s = sigmoid(hw)
            x = s * h + (1 - s) * x
        return e, x @ self.dec.T


def pick(logits, greedy, rng_state, margin=1e-4):
    """Actions per head and the generator state; -1 where float32 rounding in C could pick differently."""
    out, off = [], 0
    for A in HEADS:
        l = logits[off:off + A]
        off += A
        if greedy:
            order = np.sort(l)
            out.append(int(np.argmax(l)) if A < 2 or order[-1] - order[-2] > margin else -1)
            continue
        rng_state = xs32(rng_state)
        u = (rng_state >> 8) / 16777216.0
        p = np.exp(l - l.max())
        cum = np.cumsum(p / p.sum())
        a = int(np.searchsorted(cum, u, side="right"))
        a = min(a, A - 1)
        out.append(a if np.abs(cum[:-1] - u).min(initial=1.0) > margin else -1)
    return out, rng_state


def build(net, hidden, layers, B, T, wseed, sseed, oseed):
    shp = shapes(net, hidden, layers)
    pol = Policy(net, hidden, layers, gen_weights(wseed, shp))
    rng = np.random.default_rng(oseed)
    state = np.zeros((layers, B, hidden))
    steps, srng = [], sseed
    for t in range(T):
        reset = np.zeros(B, np.int32)
        if t == 0:
            reset[:] = 1
        elif t == T // 2:
            reset[3 % B] = reset[5 % B] = 1   # two agents start a new life mid-sequence
        state[:, reset == 1, :] = 0
        obs = gen_obs(rng, B)
        enc, out = pol.step(obs.astype(np.float64), state)
        greedy = [pick(out[b], True, 0)[0] for b in range(B)]
        sampled = []
        for b in range(B):
            a, srng = pick(out[b], False, srng)
            sampled.append(a)
        steps.append((reset, obs, enc, out, np.array(greedy, np.int32), np.array(sampled, np.int32)))
    return shp, steps


def write(path, net, hidden, layers, B, T, wseed, sseed, shp, steps):
    with open(path, "wb") as f:
        f.write(struct.pack("<8i2I", MAGIC, VERSION, net, hidden, layers, B, T, len(shp), wseed, sseed))
        for r, c in shp:
            f.write(struct.pack("<2i", r, c))
        for reset, obs, enc, out, greedy, sampled in steps:
            f.write(reset.astype("<i4").tobytes())
            f.write(obs.astype("<f4").tobytes())
            f.write(enc.astype("<f4").tobytes())
            f.write(out.astype("<f4").tobytes())
            f.write(greedy.astype("<i4").tobytes())
            f.write(sampled.astype("<i4").tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--net", type=int, default=1)
    ap.add_argument("--hidden", type=int, default=32)
    ap.add_argument("--layers", type=int, default=2)
    ap.add_argument("--batch", type=int, default=8)
    ap.add_argument("--steps", type=int, default=4)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    wseed, sseed = (0x2545F491 * a.seed) & 0xFFFFFFFF or 1, (0x9E3779B1 * a.seed + 7) & 0xFFFFFFFF or 1
    shp, steps = build(a.net, a.hidden, a.layers, a.batch, a.steps, wseed, sseed, a.seed)
    write(a.out, a.net, a.hidden, a.layers, a.batch, a.steps, wseed, sseed, shp, steps)
    n = sum(r * c for r, c in shp)
    unchecked = sum(int((s[4] < 0).sum() + (s[5] < 0).sum()) for s in steps)
    print(f"{a.out}: cretaceous_net {a.net}, hidden {a.hidden} x {a.layers}, {n} weights, {a.batch} agents x {a.steps} steps, "
          f"{os.path.getsize(a.out)} bytes, {unchecked} near-tie actions left unchecked")


if __name__ == "__main__":
    main()
