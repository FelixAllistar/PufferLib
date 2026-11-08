"""Reference for the structured Cretaceous encoder (cretaceous_encoder.cu).

Reads the dump written by ocean/cretaceous/test_encoder.cu (weights, obs, upstream gradient, CUDA output and CUDA
weight gradients), recomputes the forward pass from the architecture spec in float64 NumPy and compares it with the
CUDA output. When PyTorch is importable it runs the same forward with autograd and compares every weight gradient
(the hand-written backward) element by element.

    python ocean/cretaceous/tools/encoder_reference.py build/cretaceous/encoder_test_fp32.bin

The forward is written from docs/encoder.md, not from the kernels: obs slices, 3x3 valid convs by window
reshapes, masked softmax and max over neighbours by array ops.
"""
import sys

import numpy as np

SELF, SPECIES_AT, SPECIES, PATCH, NB, NB_OBS = 39, 16, 6, 7, 6, 10
SELF4, NB_OBS4 = 22, 4  # contract 4, appended after the contract 3 layout
PATCH_AT = SELF
NB_AT = SELF + PATCH * PATCH * 2
SELF4_AT = NB_AT + NB * NB_OBS
NB4_AT = SELF4_AT + SELF4
D, HEADS = 64, 4
NAMES = ["species", "self1", "self2", "query", "conv1", "conv2", "patch_fc", "neighbour1", "neighbour2",
         "trunk1", "trunk2"]


def load(path):
    raw = open(path, "rb").read()
    head = np.frombuffer(raw[:28], dtype=np.int32)
    magic, version, B, H, obs_size, nw, bf16 = (int(x) for x in head)
    assert magic == 0x43524554 and version == 1, "not an encoder test dump"
    off = 28

    def take(n, dtype=np.float32):
        nonlocal off
        a = np.frombuffer(raw, dtype=dtype, count=n, offset=off)
        off += a.nbytes
        return a

    obs = take(B * obs_size).reshape(B, obs_size)
    grad = take(B * H).reshape(B, H)
    out = take(B * H).reshape(B, H)
    weights, grads = {}, {}
    for name in NAMES[:nw]:
        r, c = (int(x) for x in take(2, np.int32))
        weights[name] = take(r * c).reshape(r, c)
        grads[name] = take(r * c).reshape(r, c)
    assert off == len(raw), "trailing bytes in dump"
    return dict(B=B, H=H, bf16=bool(bf16), obs=obs, grad=grad, out=out, w=weights, dw=grads)


class Numpy:
    cat = staticmethod(lambda xs, axis=-1: np.concatenate(xs, axis=axis))
    relu = staticmethod(lambda x: np.maximum(x, 0))
    exp = staticmethod(np.exp)
    where = staticmethod(np.where)
    amax = staticmethod(lambda x, axis: x.max(axis=axis))
    ones = staticmethod(lambda like, n: np.ones(like.shape[:-1] + (n,), like.dtype))
    zeros = staticmethod(lambda like, n: np.zeros(like.shape[:-1] + (n,), like.dtype))
    einsum = staticmethod(np.einsum)


class Torch:
    def __init__(self, torch):
        self.t = torch

    def cat(self, xs, axis=-1): return self.t.cat(xs, dim=axis)
    def relu(self, x): return self.t.relu(x)
    def exp(self, x): return self.t.exp(x)
    def where(self, c, a, b): return self.t.where(c, a, b)
    def amax(self, x, axis): return x.amax(dim=axis)
    def ones(self, like, n): return self.t.ones(like.shape[:-1] + (n,), dtype=like.dtype)
    def zeros(self, like, n): return self.t.zeros(like.shape[:-1] + (n,), dtype=like.dtype)
    def einsum(self, s, *a): return self.t.einsum(s, *a)


def forward(obs, w, xp):
    """obs (B, 243), w: name -> (out, in incl. bias column padded to 8). Returns (B, H)."""
    B = obs.shape[0]

    def lin(x, name):  # x (..., n) -> [x, 1, 0 pad] @ W^T
        W = w[name]
        pad = W.shape[1] - x.shape[-1] - 1
        assert pad >= 0 and (x.shape[-1] + 1 + pad) % 8 == 0
        xa = xp.cat([x, xp.ones(x, 1)] + ([xp.zeros(x, pad)] if pad else []))
        return xa @ W.T

    def conv3x3(img, name):  # img (B, S, S, C), valid conv, window features ordered (ky, kx, c)
        S = img.shape[1]
        rows = []
        for oy in range(S - 2):
            cols = []
            for ox in range(S - 2):
                win = img[:, oy:oy + 3, ox:ox + 3, :].reshape(B, -1)
                cols.append(xp.relu(lin(win, name))[:, None, :])
            rows.append(xp.cat(cols, axis=1)[:, None])
        return xp.cat(rows, axis=1)  # (B, S-2, S-2, Cout)

    self_obs = xp.cat([obs[:, :SELF], obs[:, SELF4_AT:SELF4_AT + SELF4]])
    s1 = xp.relu(lin(self_obs, "self1"))
    self_e = xp.relu(lin(s1, "self2"))
    q = lin(self_e, "query")
    sp = lin(obs[:, SPECIES_AT:SPECIES_AT + SPECIES], "species")

    patch = obs[:, PATCH_AT:PATCH_AT + PATCH * PATCH * 2].reshape(B, PATCH, PATCH, 2)
    c2 = conv3x3(conv3x3(patch, "conv1"), "conv2")
    p = xp.relu(lin(c2.reshape(B, -1), "patch_fc"))

    nb = xp.cat([obs[:, NB_AT:NB_AT + NB * NB_OBS].reshape(B, NB, NB_OBS),
                 obs[:, NB4_AT:NB4_AT + NB * NB_OBS4].reshape(B, NB, NB_OBS4)])
    mask = nb[:, :, 0] > 0.5  # (B, N)
    sp_n = xp.cat([sp[:, None, :]] * NB, axis=1)
    h = xp.relu(lin(xp.relu(lin(xp.cat([nb, sp_n]), "neighbour1")), "neighbour2"))  # (B, N, D)

    dh = D // HEADS
    hh = h.reshape(B, NB, HEADS, dh)
    s = xp.einsum("bgd,bngd->bgn", q.reshape(B, HEADS, dh), hh) / np.sqrt(dh)
    m = mask[:, None, :]
    s = xp.where(m, s, s * 0 - 1e30)
    e = xp.where(m, xp.exp(s - xp.amax(s, 2)[..., None]), s * 0)
    tot = e.sum(2)[..., None]
    alpha = e / xp.where(tot > 0, tot, tot * 0 + 1)
    attn = xp.einsum("bgn,bngd->bgd", alpha, hh).reshape(B, D)
    any_nb = mask.any(1)[:, None]
    mx = xp.amax(xp.where(mask[..., None], h, h * 0 - 1e30), 1)
    mx = xp.where(any_nb, mx, mx * 0)

    cat = xp.cat([self_e, p, attn, mx, sp])
    return xp.relu(lin(xp.relu(lin(cat, "trunk1")), "trunk2"))


def check(name, ref, got, atol, rtol):
    err = np.abs(ref - got)
    bad = err > atol + rtol * np.maximum(np.abs(ref), np.abs(got))
    worst = float((err / (atol + rtol * np.maximum(np.abs(ref), np.abs(got)))).max()) if ref.size else 0.0
    status = "PASS" if not bad.any() else "FAIL"
    print(f"{status} {name:<26} max abs err {err.max():.2e}  (worst / tolerance {worst:.2f})")
    if bad.any():
        i = np.unravel_index(np.argmax(err), err.shape)
        print(f"     at {i}: reference {ref[i]:.6g} cuda {got[i]:.6g}")
    return not bad.any()


def main(path):
    d = load(path)
    bf16 = d["bf16"]
    # bf16 rounds every activation, so its tolerance is loose; fp32 should agree to float rounding.
    fatol, frtol = (0.05, 0.05) if bf16 else (2e-5, 1e-4)
    w64 = {k: v.astype(np.float64) for k, v in d["w"].items()}
    ref = forward(d["obs"].astype(np.float64), w64, Numpy)
    ok = check("forward (numpy)", ref, d["out"], fatol, frtol)
    try:
        import torch
    except ImportError:
        print("skip gradient reference: no torch")
        return ok
    wt = {k: torch.tensor(v, requires_grad=True) for k, v in w64.items()}
    obs = torch.tensor(d["obs"].astype(np.float64))
    out = forward(obs, wt, Torch(torch))
    ok &= check("forward (torch)", ref, out.detach().numpy(), 1e-9, 1e-9)
    (out * torch.tensor(d["grad"].astype(np.float64))).sum().backward()
    for name in NAMES:
        g, got = wt[name].grad.numpy(), d["dw"][name].astype(np.float64)
        if not bf16:
            ok &= check(f"grad {name}", g, got, 2e-5, 1e-3)
            continue
        # bf16 rounds every activation (8 mantissa bits), so compare whole tensors: relative L2 error and cosine
        rel = np.linalg.norm(got - g) / max(np.linalg.norm(g), 1e-12)
        cos = float((got * g).sum() / max(np.linalg.norm(got) * np.linalg.norm(g), 1e-12))
        good = rel < 0.12 and cos > 0.99   # measured worst: neighbour1 0.084 / 0.9965 (softmax backward)
        print(f"{'PASS' if good else 'FAIL'} grad {name:<21} relative L2 err {rel:.3f}  cosine {cos:.5f}")
        ok &= good
    print("ALL PASS" if ok else "SOME CHECKS FAILED")
    return ok


if __name__ == "__main__":
    sys.exit(0 if main(sys.argv[1] if len(sys.argv) > 1 else "build/cretaceous/encoder_test_fp32.bin") else 1)
