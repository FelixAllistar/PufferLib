#!/usr/bin/env python3
"""Train the bounded house policy; all geometry validation stays in the C engine.

Bootstrap labels are authored design scores. Only explicit player comparisons
train the preference model. Synthetic smoke data is marked and refused by the
normal finetune command. CPU training needs numpy and torch; play needs neither.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

CATEGORIES = (3, 3, 3, 3, 2, 3, 3, 3, 3, 3, 3, 2)
OFFSETS = np.cumsum((0,) + CATEGORIES[:-1]).tolist()
INPUT, HIDDEN, OUTPUT = 49, 64, 3


class Policy(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(nn.Linear(INPUT, HIDDEN), nn.Tanh(), nn.Linear(HIDDEN, OUTPUT))

    def forward(self, x):
        return self.net(x)


class Reward(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(nn.Linear(37, 64), nn.Tanh(), nn.Linear(64, 1))

    def forward(self, x):
        return self.net(x).squeeze(-1)


def engine(tool, *args, lines=None):
    process = subprocess.run([str(tool), *map(str, args)], input=lines, text=True,
                             capture_output=True, check=True)
    return [json.loads(line) for line in process.stdout.splitlines() if line.strip()]


def corpus(tool, count, seed, model=None):
    extra = ["neural", "--model", model] if model else []
    return engine(tool, "corpus", count, seed, *extra)


def evaluate(tool, tokens, difficulty):
    lines = "".join(",".join(map(str, t)) + f" {int(d)}\n" for t, d in zip(tokens, difficulty))
    return engine(tool, "batch", lines=lines)


def features(tokens, difficulty):
    tokens = torch.as_tensor(tokens, dtype=torch.long)
    difficulty = torch.as_tensor(difficulty, dtype=torch.long)
    rows = torch.arange(len(tokens))
    x = torch.zeros(len(tokens), 37)
    for step, offset in enumerate(OFFSETS):
        x[rows, offset + tokens[:, step]] = 1
    x[rows, 34 + difficulty] = 1
    return x


def inputs(tokens, difficulty):
    full = features(tokens, difficulty)
    x = torch.zeros(len(tokens), 12, INPUT)
    for step, offset in enumerate(OFFSETS):
        x[:, step, :offset] = full[:, :offset]
        x[:, step, 34 + step] = 1
        x[:, step, 46:] = full[:, 34:]
    return x


def teacher_data(plans):
    tokens = torch.tensor([p["tokens"] for p in plans])
    x = inputs(tokens, [p["difficulty"] for p in plans]).reshape(-1, INPUT)
    allowed = torch.tensor(CATEGORIES).repeat(len(plans))
    mask = torch.arange(3)[None, :] >= allowed[:, None]
    return x, tokens.reshape(-1), mask


def sample(policy, difficulty, base=None):
    count = len(difficulty)
    tokens = torch.zeros(count, 12, dtype=torch.long)
    prefix = torch.zeros(count, INPUT)
    prefix[torch.arange(count), 46 + difficulty] = 1
    logp, entropy, kl = torch.zeros(count), torch.zeros(count), torch.zeros(count)
    for step, offset in enumerate(OFFSETS):
        prefix[:, 34:46] = 0
        prefix[:, 34 + step] = 1
        x = prefix.clone()
        logits = policy(x)[:, :CATEGORIES[step]] / .9
        dist = torch.distributions.Categorical(logits=logits)
        choice = dist.sample()
        tokens[:, step] = choice
        logp = logp + dist.log_prob(choice)
        entropy = entropy + dist.entropy()
        if base is not None:
            with torch.no_grad():
                reference = torch.distributions.Categorical(logits=base(x)[:, :CATEGORIES[step]] / .9)
            kl = kl + torch.distributions.kl_divergence(dist, reference)
        prefix[torch.arange(count), offset + choice] = 1
    return tokens, logp, entropy, kl


def parameters(model):
    return np.concatenate([p.detach().numpy().reshape(-1) for p in model.parameters()]).astype(np.float32)


def load_policy(path):
    if path.suffix == ".npz":
        with np.load(path, allow_pickle=False) as saved:
            weights = saved["weights"]
    else:
        words = path.read_text().split()
        if words[:5] != ["SWAT_LAYOUT_NET", "1", "49", "64", "3"]:
            raise ValueError("Invalid policy text header")
        weights = np.array([float(v) for v in words[6:]], dtype=np.float32)
    if weights.shape != (3395,) or not np.isfinite(weights).all() or np.abs(weights).max() >= 100:
        raise ValueError("Invalid policy archive")
    model, cursor = Policy(), 0
    with torch.no_grad():
        for p in model.parameters():
            n = p.numel()
            p.copy_(torch.from_numpy(weights[cursor:cursor + n]).reshape(p.shape))
            cursor += n
    return model


def export(policy, out, manifest, header=None):
    out.mkdir(parents=True, exist_ok=True)
    weights = parameters(policy)
    digest = hashlib.sha256(weights.astype("<f4").tobytes()).hexdigest()
    model_id = int(digest[:8], 16) or 1
    np.savez(out / "policy.npz", weights=weights)
    values = [format(float(w), ".9g") for w in weights]
    (out / "policy.txt").write_text(f"SWAT_LAYOUT_NET 1 49 64 3 {model_id}\n" + "\n".join(values) + "\n")
    if header:
        header.parent.mkdir(parents=True, exist_ok=True)
        floats = [(s if "." in s or "e" in s else s + ".0") + "f" for s in values]
        text = "// Trained bootstrap policy. Reproduce with train_layout.py; see layout_training.json.\n"
        text += f"static const uint32_t swat_layout_builtin_id={model_id}u;\n"
        text += "static const float swat_layout_builtin[SWAT_LAYOUT_PARAMETERS]={\n"
        text += "\n".join("    " + ",".join(floats[i:i + 6]) + "," for i in range(0, len(floats), 6))
        header.write_text(text + "\n};\n")
    manifest.update(model_id=model_id, weights_sha256=digest, layout_version=1,
                    architecture=[49, 64, 3], temperature=.9, torch_version=torch.__version__)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return out / "policy.txt"


def metrics(plans):
    return {"count": len(plans), "unique_layouts": len({p["fingerprint"] for p in plans}),
            "mean_bootstrap_quality": float(np.mean([p["quality"] for p in plans])),
            "room_counts": [sum(p["rooms"] == rooms for p in plans) for rooms in (3, 4, 5)],
            "dimensions": len({(p["width"], p["depth"]) for p in plans})}


def parity(tool, policy, path):
    rng = np.random.default_rng(9281)
    error = 0
    for _ in range(36):
        tokens = [int(rng.integers(n)) for n in CATEGORIES]
        difficulty, step = int(rng.integers(3)), int(rng.integers(12))
        actual = engine(tool, "logits", difficulty, step, ",".join(map(str, tokens[:step])), "--model", path)[0]
        with torch.no_grad():
            expected = policy(inputs([tokens], [difficulty])[0, step]).numpy()
        error = max(error, float(np.max(np.abs(expected - actual))))
    if error > 2e-5:
        raise RuntimeError(f"C/PyTorch logit mismatch: {error}")
    return error


def bootstrap(args):
    raw = corpus(args.tool, args.count, args.seed)
    # Split by fingerprint, so a duplicate generated house cannot cross splits.
    unique = list({p["fingerprint"]: p for p in raw}.values())
    selected = []
    for difficulty in range(3):
        group = sorted((p for p in unique if p["difficulty"] == difficulty), key=lambda p: -p["quality"])
        selected.extend(group[:max(1, int(len(group) * .4))])
    train = [p for p in selected if p["fingerprint"] % 5 != 0]
    held = [p for p in selected if p["fingerprint"] % 5 == 0]
    if not train or not held:
        raise ValueError("Corpus too small for a disjoint validation split")
    x, y, mask = teacher_data(train)
    hx, hy, hm = teacher_data(held)
    policy = Policy()
    optimizer = torch.optim.AdamW(policy.parameters(), lr=.002, weight_decay=.001)
    for _ in range(args.steps):
        ids = torch.randint(len(x), (512,))
        loss = F.cross_entropy(policy(x[ids]).masked_fill(mask[ids], -1e9), y[ids])
        optimizer.zero_grad(); loss.backward(); optimizer.step()
    with torch.no_grad():
        held_nll = F.cross_entropy(policy(hx).masked_fill(hm, -1e9), hy).item()
    manifest = {"training": "bootstrap: imitation of top 40% authored layout scores per difficulty",
                "human_feedback_pairs": 0, "seed": args.seed, "steps": args.steps,
                "corpus_count": len(raw), "corpus_unique": len(unique), "train_layouts": len(train),
                "heldout_layouts": len(held), "heldout_token_nll": held_nll,
                "corpus_sha256": hashlib.sha256(json.dumps(raw, sort_keys=True).encode()).hexdigest()}
    path = export(policy, args.out, manifest, args.header)
    manifest["c_python_max_logit_error"] = parity(args.tool, policy, path)
    uniform = corpus(args.tool, 1500, 90000000)
    neural = corpus(args.tool, 1500, 90000000, path)
    manifest["unseen_seeds_uniform"] = metrics(uniform)
    manifest["unseen_seeds_neural"] = metrics(neural)
    with torch.no_grad():
        difficulty = torch.arange(1200) % 3
        tokens, *_ = sample(policy, difficulty)
    accepted = evaluate(args.tool, tokens.tolist(), difficulty.tolist())
    manifest["raw_sample_validity"] = sum(p["valid"] for p in accepted) / len(accepted)
    export(policy, args.out, manifest, args.header)
    if args.manifest:
        args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2), flush=True)


def read_pairs(path, allow_synthetic):
    pairs = []
    seen = set()
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("version") != 1 or row.get("source") not in ("player", "synthetic-test"):
            raise ValueError(f"Line {line_number}: unsupported feedback source/version")
        if row["source"] != "player" and not allow_synthetic:
            raise ValueError("Synthetic labels require --allow-synthetic-test and cannot be reported as player feedback")
        for side in ("a", "b"):
            p = row[side]
            if (type(p.get("difficulty")) is not int or not 0 <= p["difficulty"] < 3 or
                    len(p.get("tokens", [])) != 12 or any(type(v) is not int or not 0 <= v < n
                    for v, n in zip(p["tokens"], CATEGORIES))):
                raise ValueError(f"Line {line_number}: invalid layout")
        if row["choice"] not in ("a", "b", "tie"):
            raise ValueError(f"Line {line_number}: invalid preference")
        # Deduplicate a comparison regardless of ordering; do not amplify repeat clicks.
        keys = [tuple(row[s]["tokens"]) + (row[s]["difficulty"],) for s in ("a", "b")]
        pair_key = tuple(sorted(keys))
        if keys[0] == keys[1] or pair_key in seen:
            continue
        seen.add(pair_key)
        pairs.append(row)
    if len(pairs) < 64:
        raise ValueError("Collect at least 64 distinct comparisons before preference training")
    return pairs


def heldout_plan(plan):
    key = bytes(plan["tokens"] + [plan["difficulty"]])
    return hashlib.sha256(key).digest()[0] < 64


def pair_tensors(rows):
    a = features([r["a"]["tokens"] for r in rows], [r["a"]["difficulty"] for r in rows])
    b = features([r["b"]["tokens"] for r in rows], [r["b"]["difficulty"] for r in rows])
    y = torch.tensor([{"a": 1., "b": 0., "tie": .5}[r["choice"]] for r in rows])
    return a, b, y


def finetune(args):
    pairs = read_pairs(args.feedback, args.allow_synthetic_test)
    plans = {tuple(p[s]["tokens"]) + (p[s]["difficulty"],): p[s] for p in pairs for s in ("a", "b")}
    checks = evaluate(args.tool, [p["tokens"] for p in plans.values()], [p["difficulty"] for p in plans.values()])
    if not all(p["valid"] for p in checks):
        raise ValueError("Feedback contains layouts rejected by this engine version")
    # Exclude cross-split comparisons; every validation house is unseen in training.
    train = [p for p in pairs if not heldout_plan(p["a"]) and not heldout_plan(p["b"])]
    held = [p for p in pairs if heldout_plan(p["a"]) and heldout_plan(p["b"])]
    if len(train) < 32 or len(held) < 8:
        raise ValueError("Need 32 training and 8 validation pairs with disjoint houses; collect more comparisons")
    reward = Reward()
    a, b, y = pair_tensors(train)
    ha, hb, hy = pair_tensors(held)
    optimizer = torch.optim.AdamW(reward.parameters(), lr=.002, weight_decay=.02)
    best_loss, best_state = float("inf"), None
    for step in range(800):
        ids = torch.randint(len(y), (128,))
        sa, sb = reward(a[ids]), reward(b[ids])
        loss = F.binary_cross_entropy_with_logits(sa - sb, y[ids]) + .002 * (sa.square().mean() + sb.square().mean())
        optimizer.zero_grad(); loss.backward(); optimizer.step()
        if step % 20 == 19:
            with torch.no_grad():
                validation = F.binary_cross_entropy_with_logits(reward(ha) - reward(hb), hy).item()
            if validation < best_loss:
                best_loss = validation
                best_state = {k: v.detach().clone() for k, v in reward.state_dict().items()}
    reward.load_state_dict(best_state)
    reward.requires_grad_(False)
    with torch.no_grad():
        margins = reward(ha) - reward(hb)
        decisive = hy != .5
        accuracy = ((margins[decisive] > 0) == (hy[decisive] > .5)).float().mean().item() if decisive.any() else None
        scores = reward(torch.cat([a, b]))
        center, scale = scores.mean(), scores.std().clamp_min(.25)
    base = load_policy(args.base).requires_grad_(False)
    policy = load_policy(args.base)
    optimizer = torch.optim.AdamW(policy.parameters(), lr=.0003)

    def policy_score(model, sample_seed):
        with torch.random.fork_rng():
            torch.manual_seed(sample_seed)
            with torch.no_grad():
                difficulty = torch.arange(600) % 3
                tokens, _, _, kl = sample(model, difficulty, base)
                plans = evaluate(args.tool, tokens.tolist(), difficulty.tolist())
                valid = torch.tensor([p["valid"] for p in plans])
                score = ((reward(features(tokens, difficulty)) - center) / scale).clamp(-3, 3)
                return {"raw_validity": valid.float().mean().item(),
                        "mean_predicted_reward_valid": score[valid].mean().item(),
                        "mean_kl_to_base": kl.mean().item()}

    before = policy_score(base, 104729)
    for _ in range(args.steps):
        difficulty = torch.randint(3, (96,))
        tokens, logp, entropy, kl = sample(policy, difficulty, base)
        plans = evaluate(args.tool, tokens.tolist(), difficulty.tolist())
        with torch.no_grad():
            value = ((reward(features(tokens, difficulty)) - center) / scale).clamp(-3, 3)
            value[torch.tensor([not p["valid"] for p in plans])] = -4
            advantage = value - value.mean()
        loss = -(advantage * logp).mean() + .06 * kl.mean() - .003 * entropy.mean()
        optimizer.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(policy.parameters(), 2); optimizer.step()
    manifest = {"training": "Bradley-Terry preference reward + REINFORCE with KL to bootstrap policy",
                "feedback_source": "synthetic-test" if any(p["source"] != "player" for p in pairs) else "player",
                "human_feedback_pairs": sum(p["source"] == "player" for p in pairs),
                "feedback_sha256": hashlib.sha256(args.feedback.read_bytes()).hexdigest(),
                "train_pairs": len(train), "heldout_pairs": len(held), "excluded_cross_split_pairs": len(pairs)-len(train)-len(held),
                "heldout_preference_bce": best_loss, "heldout_decisive_accuracy": accuracy,
                "steps": args.steps, "seed": args.seed, "before": before, "after": policy_score(policy, 104729),
                "base_weights_sha256": hashlib.sha256(parameters(base).astype("<f4").tobytes()).hexdigest()}
    path = export(policy, args.out, manifest)
    np.savez(args.out / "reward.npz", weights=parameters(reward))
    manifest["c_python_max_logit_error"] = parity(args.tool, policy, path)
    export(policy, args.out, manifest)
    print(json.dumps(manifest, indent=2), flush=True)


def smoke(args):
    args.out.mkdir(parents=True, exist_ok=True)
    plans = corpus(args.tool, 2048, 100000000)
    feedback = args.out / "synthetic-comparisons.jsonl"
    with feedback.open("w") as file:
        for a, b in zip(plans[::2], plans[1::2]):
            choice = "tie" if abs(a["quality"]-b["quality"]) < .03 else ("a" if a["quality"] > b["quality"] else "b")
            file.write(json.dumps({"version": 1, "source": "synthetic-test", "a": a, "b": b, "choice": choice}) + "\n")
    args.feedback, args.allow_synthetic_test = feedback, True
    finetune(args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("bootstrap", "finetune", "smoke"))
    parser.add_argument("--tool", type=Path, default=Path("build/swat/layout_tool"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--base", type=Path)
    parser.add_argument("--feedback", type=Path)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--steps", type=int)
    parser.add_argument("--count", type=int, default=12000)
    parser.add_argument("--seed", type=int, default=20261003)
    parser.add_argument("--allow-synthetic-test", action="store_true")
    args = parser.parse_args()
    if args.command != "bootstrap" and not args.base:
        parser.error("--base is required for preference training")
    if args.command == "finetune" and not args.feedback:
        parser.error("--feedback is required")
    if args.steps is None:
        args.steps = 1600 if args.command == "bootstrap" else 180
    if not 1 <= args.steps <= 100000 or not 100 <= args.count <= 1000000 or not 0 <= args.seed <= 0xffffffff:
        parser.error("Invalid steps, corpus count or seed")
    torch.set_num_threads(2)
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    {"bootstrap": bootstrap, "finetune": finetune, "smoke": smoke}[args.command](args)


if __name__ == "__main__":
    main()
