"""Behavior cloning of a TAS into the native Mario policy (2026-11-01), VPT-style but with the TAS's exact buttons
instead of an inverse-dynamics model. A PyTorch copy of the CPU policy in retro_policy_cpu.h (same flat float32
weight file, same order: 3 convs NHWC [oc, ky, kx, ic] + bias, RAM linear + bias, fusion linear + bias, decoder
(64 logits + value), MinGRU projections), trained with cross-entropy on tas_bc_data records in windows of WINDOW
frames with zero recurrent state (as practice starts are). The value row is not trained.
Usage: python bc_train.py INIT.bin DATA.bin OUT.bin --hidden 256 --layers 2 [--parity LOGITS.txt] [--epochs 30]
"""
import argparse
import numpy as np
import torch
import torch.nn.functional as F

RAM, IMG_H, IMG_W = 112, 60, 64
CONVS = [(1, 8, 8, 4, 0), (8, 16, 4, 2, 1), (16, 32, 3, 2, 1)]  # ic, oc, k, stride, pad
CNN_FEATURES, RAM_HIDDEN = 4 * 4 * 32, 32


def shapes(hidden, layers):
    out = []
    for ic, oc, k, _, _ in CONVS:
        out += [('conv_w', (oc, k, k, ic)), ('conv_b', (oc,))]
    out += [('ram_w', (RAM_HIDDEN, RAM)), ('ram_b', (RAM_HIDDEN,)),
            ('fusion_w', (hidden, CNN_FEATURES + RAM_HIDDEN)), ('fusion_b', (hidden,)), ('decoder_w', (65, hidden))]
    out += [('gru', (3 * hidden, hidden))] * layers
    return out


def load(path, hidden, layers):
    flat = np.fromfile(path, dtype=np.float32)
    params, i = [], 0
    for _, shp in shapes(hidden, layers):
        n = int(np.prod(shp))
        params.append(torch.nn.Parameter(torch.tensor(flat[i:i + n].reshape(shp))))
        i += n
        i = (i + 7) & ~7  # get_weights_aligned
    assert i == len(flat), (i, len(flat))
    return params


def save(params, path):
    parts = []
    for p in params:
        a = p.detach().cpu().numpy().astype(np.float32).ravel()
        pad = (-len(a)) % 8
        parts += [a, np.zeros(pad, np.float32)]
    np.concatenate(parts).tofile(path)


def forward(params, obs, layers):
    """obs (B, T, OBS) -> outputs (B, T, 65); recurrent state starts at zero."""
    B, T, _ = obs.shape
    x = obs.reshape(B * T, -1)
    img = x[:, RAM:RAM + IMG_H * IMG_W].reshape(B * T, 1, IMG_H, IMG_W)
    for l, (ic, oc, k, s, pad) in enumerate(CONVS):
        w = params[2 * l].permute(0, 3, 1, 2)  # [oc, ic, ky, kx]
        img = F.relu(F.conv2d(img, w, params[2 * l + 1], stride=s, padding=pad))
    cnn = img.permute(0, 2, 3, 1).reshape(B * T, -1)  # NHWC flatten
    ram = F.relu(x[:, :RAM] @ params[6].T + params[7])
    h = F.relu(torch.cat([cnn, ram], 1) @ params[8].T + params[9]).reshape(B, T, -1)
    dec = params[10]
    H = h.shape[-1]
    for l in range(layers):
        comb = h @ params[11 + l].T  # (B, T, 3H)
        hid, gate, hw = comb[..., :H], comb[..., H:2 * H], comb[..., 2 * H:]
        g, hws = torch.sigmoid(gate), torch.sigmoid(hw)
        ht = torch.where(hid >= 0, hid + 0.5, torch.sigmoid(hid))
        s = torch.zeros(B, H, device=obs.device)
        outs = []
        for t in range(T):
            s = s + g[:, t] * (ht[:, t] - s)
            outs.append(hws[:, t] * s + (1 - hws[:, t]) * h[:, t])
        h = torch.stack(outs, 1)
    return h @ dec.T


def read_data(path):
    with open(path, 'rb') as f:
        n, obs_size = np.fromfile(f, np.int32, 2)
        rec = np.fromfile(f, np.dtype([('meta', np.int32, 3), ('obs', np.float32, obs_size)]), n)
    return rec['meta'], rec['obs']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('init'); ap.add_argument('data'); ap.add_argument('out')
    ap.add_argument('--hidden', type=int, default=256); ap.add_argument('--layers', type=int, default=2)
    ap.add_argument('--parity'); ap.add_argument('--epochs', type=int, default=30)
    ap.add_argument('--window', type=int, default=128); ap.add_argument('--lr', type=float, default=3e-4)
    ap.add_argument('--focus', default='', help='level ids to oversample 4x, e.g. 15,31 (4-4, 8-4)')
    a = ap.parse_args()
    dev = 'cuda' if torch.cuda.is_available() else 'cpu'
    params = load(a.init, a.hidden, a.layers)
    meta, obs = read_data(a.data)
    print(f'{len(meta)} records, obs {obs.shape[1]}, device {dev}')
    if a.parity:  # outputs of the C policy over the first records, in order
        ref = np.loadtxt(a.parity)
        with torch.no_grad():
            out = forward([p.to(dev) for p in params], torch.tensor(obs[None, :len(ref)], device=dev), a.layers)
        diff = np.abs(out[0].cpu().numpy() - ref).max()
        print(f'parity: max |torch - C| over {len(ref)} steps = {diff:.2e}')
        assert diff < 1e-3, 'PyTorch copy does not match the C policy'
    # windows of consecutive records within one level, half-overlapping
    starts = []
    for lid in np.unique(meta[:, 0]):
        rows = np.nonzero(meta[:, 0] == lid)[0]
        lo, hi = rows[0], rows[-1] + 1
        starts += list(range(lo, hi - a.window + 1, a.window // 2))
    starts = np.array(starts)
    focus = {int(x) for x in a.focus.split(',') if x}
    weights = np.array([4.0 if meta[s, 0] in focus else 1.0 for s in starts])
    rng = np.random.default_rng(0)
    val = rng.random(len(starts)) < 0.1
    tr, va = starts[~val], starts[val]
    print(f'{len(tr)} train windows, {len(va)} validation windows of {a.window} frames')
    obs_t = torch.tensor(obs, device=dev)
    act_t = torch.tensor(meta[:, 2], device=dev, dtype=torch.long)
    params = [torch.nn.Parameter(p.to(dev)) for p in params]
    opt = torch.optim.Adam(params, lr=a.lr)
    idx = torch.arange(a.window, device=dev)

    def batch(ss):
        rows = torch.tensor(ss, device=dev)[:, None] + idx[None]
        return obs_t[rows], act_t[rows]

    def accuracy(ss):
        hits = tot = 0
        with torch.no_grad():
            for b in range(0, len(ss), 64):
                o, y = batch(ss[b:b + 64])
                pred = forward(params, o, a.layers)[..., :64].argmax(-1)
                hits += (pred == y).sum().item(); tot += y.numel()
        return hits / max(1, tot)

    print(f'before: train acc {accuracy(tr[:512]):.3f} val acc {accuracy(va):.3f}')
    p_tr = weights[~val] / weights[~val].sum()
    steps = max(1, len(tr) // 32)
    for ep in range(a.epochs):
        tot = 0.0
        for _ in range(steps):
            o, y = batch(rng.choice(tr, 32, p=p_tr))
            out = forward(params, o, a.layers)
            loss = F.cross_entropy(out[..., :64].reshape(-1, 64), y.reshape(-1))
            opt.zero_grad(); loss.backward()
            params[10].grad[64] = 0  # keep the value head
            opt.step(); tot += loss.item()
        print(f'epoch {ep + 1}: loss {tot / steps:.4f} train acc {accuracy(tr[:512]):.3f} val acc {accuracy(va):.3f}',
              flush=True)
    save(params, a.out)
    print('saved', a.out)


if __name__ == '__main__':
    main()
