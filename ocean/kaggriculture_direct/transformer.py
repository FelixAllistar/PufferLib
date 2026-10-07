"""Experimental structured-token actor on the existing compact primitive ABI.

Bootstrap backbone: pre-LN, width 256, 6 blocks, 8 heads, FFN 1024,
selective within-farm 2D RoPE (16 dimensions, base 100), no dropout.
This is NOT the winner's checkpoint or feature schema: it adapts our existing
public actor observation to 263 tokens and keeps our 60 command/quantity heads.
Private paired-critic columns never enter the actor. No neural recurrent state,
inferred-opponent-inventory token, macro executor or strategic post-processing.

Checkpoints are explicitly tagged .pt files, never native MinGRU .bin weights.
"""
from __future__ import annotations

import argparse
import configparser
import ctypes as C
from dataclasses import asdict, dataclass
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import struct
import time

import numpy as np
import torch
from torch import nn
import torch.nn.functional as F

from compact_contract import OBS, HEADS, LOGITS, PACKED, SIZES, STEPS

ROOT = Path(__file__).resolve().parents[2]
FORMAT = "kag_compact_v7_transformer_v2"
COMPILE_OPTIONS = {"emulate_precision_casts": True}


@dataclass(frozen=True)
class ModelConfig:
    width: int = 256
    layers: int = 6
    heads: int = 8
    ffn: int = 1024
    rope_dim: int = 16
    rope_base: float = 100.0
    adapter_version: int = 2

    def validate(self):
        assert self.width > 0 and self.layers > 0 and self.heads > 0
        assert self.width % self.heads == 0
        assert self.rope_dim % 4 == 0 and 0 < self.rope_dim <= self.width // self.heads
        assert self.rope_base > 1 and self.ffn > 0
        assert self.adapter_version in (1, 2)


def rotate_spatial(x, coords, rotary, base):
    """Rotate x/y axis pairs; nonspatial coordinates are zero (identity)."""
    axis = rotary // 2
    frequency = base ** (-torch.arange(0, axis, 2, device=x.device, dtype=torch.float32) / axis)
    angles = coords[:, None, :, :, None].float() * frequency
    cosine, sine = angles.cos().to(x.dtype), angles.sin().to(x.dtype)
    spatial = x[..., :rotary].reshape(*x.shape[:-1], 2, axis // 2, 2)
    even, odd = spatial.unbind(-1)
    rotated = torch.stack((even * cosine - odd * sine, even * sine + odd * cosine), -1)
    return torch.cat((rotated.flatten(-3), x[..., rotary:]), -1)


def selective_attention(q, k, v, coords, groups, valid, config):
    """Exact selective RoPE via augmented Q/K dot products, fused SDPA.

    Same-farm pairs get rotated scores; every other pair keeps its raw score.
    The extra two signed terms per farm cancel the raw rotary contribution
    only for a same-farm pair. Explicit scale uses the ORIGINAL head width.
    """
    qr = rotate_spatial(q, coords, config.rope_dim, config.rope_base)
    kr = rotate_spatial(k, coords, config.rope_dim, config.rope_base)
    qs, ks = [q], [k]
    for farm in (1, 2):
        mask = (groups == farm)[:, None, :, None]
        qs += [qr[..., :config.rope_dim] * mask, q[..., :config.rope_dim] * mask]
        ks += [kr[..., :config.rope_dim] * mask, -k[..., :config.rope_dim] * mask]
    qa, ka = torch.cat(qs, -1), torch.cat(ks, -1)
    result = F.scaled_dot_product_attention(qa, ka, v,
        attn_mask=valid[:, None, None, :], dropout_p=0.0, scale=1 / math.sqrt(q.shape[-1]))
    return result


class Block(nn.Module):
    def __init__(self, config):
        super().__init__()
        self.config = config
        self.norm1, self.norm2 = nn.LayerNorm(config.width), nn.LayerNorm(config.width)
        self.qkv, self.out = nn.Linear(config.width, 3 * config.width), nn.Linear(config.width, config.width)
        self.ffn = nn.Sequential(nn.Linear(config.width, config.ffn), nn.GELU(), nn.Linear(config.ffn, config.width))

    def forward(self, x, coords, groups, valid):
        b, tokens, width = x.shape
        q, k, v = self.qkv(self.norm1(x)).reshape(b, tokens, 3, self.config.heads, -1).permute(2, 0, 3, 1, 4)
        attended = selective_attention(q, k, v, coords, groups, valid, self.config)
        x = x + self.out(attended.transpose(1, 2).reshape(b, tokens, width))
        return (x + self.ffn(self.norm2(x))) * valid[..., None]


class CompactTransformer(nn.Module):
    def __init__(self, config=ModelConfig()):
        super().__init__()
        config.validate()
        self.config = config
        w = config.width
        self.cell, self.unit, self.global_state, self.commodity = (
            nn.Linear(18 if config.adapter_version == 2 else 16, w),
            nn.Linear(23 if config.adapter_version == 2 else 18, w),
            nn.Linear(128, w), nn.Linear(8, w))
        self.kind, self.species = nn.Embedding(9, w), nn.Embedding(9, w)
        self.commodity_id, self.market_slot = nn.Embedding(12, w), nn.Embedding(10, w)
        self.input_norm, self.final_norm = nn.LayerNorm(w), nn.LayerNorm(w)
        self.blocks = nn.ModuleList(Block(config) for _ in range(config.layers))
        self.unit_command, self.unit_quantity = nn.Linear(w, 44), nn.Linear(w, 20)
        self.market_command, self.market_quantity = nn.Linear(w, 22), nn.Linear(w, 100)
        self.register_buffer("cell_xy", torch.tensor([(i % 10, i // 10) for i in range(100)] * 2, dtype=torch.float32))
        self.register_buffer("cell_side", torch.tensor([[1., 0.]] * 100 + [[0., 1.]] * 100))
        self.register_buffer("unit_side", torch.tensor([[1., 0.]] * 20 + [[0., 1.]] * 20))
        self.register_buffer("rope_groups", torch.tensor([1] * 100 + [2] * 100 + [1] * 20 + [2] * 20 + [0] * 23))
        if config.adapter_version == 2:
            self.register_buffer("unit_index", torch.arange(20).repeat(2).float() / 19)
            self.register_buffer("unit_role", torch.tensor(([[1., 0.]] + [[0., 1.]] * 19) * 2))

    def tokenize(self, observation):
        # An explicit slice provides a hard information barrier for the actor.
        actor = observation[:, :3000]
        b = actor.shape[0]
        cells = actor[:, 200:2600].reshape(b, 200, 12)
        own = actor[:, 2600:2920].reshape(b, 20, 16)
        other = F.pad(actor[:, 2920:3000].reshape(b, 20, 4), (0, 12))
        units = torch.cat((own, other), 1)
        cell_features = torch.cat((cells, self.cell_side.expand(b, -1, -1),
                                   (self.cell_xy / 9).expand(b, -1, -1)), -1)
        unit_features = torch.cat((units, self.unit_side.expand(b, -1, -1)), -1)
        if self.config.adapter_version == 2:
            # Fixed slots already carry this public identity in the original
            # flat observation; shared heads must not erase it during tokenization.
            unit_features = torch.cat((unit_features, self.unit_role.expand(b, -1, -1),
                self.unit_index[None, :, None].expand(b, -1, -1),
                self.unit_side[None, :, :1].expand(b, -1, -1),
                units[..., 4:].sum(-1, keepdim=True)), -1)
            tile = (units[..., 1:3] * 9).round().long().clamp(0, 9)
            address = tile[..., 0] + 10 * tile[..., 1]
            address[:, 20:] += 100
            live = (units[..., 0] > .5).to(actor.dtype)
            farmer = live * self.unit_role[:, 0]
            hands = live * self.unit_role[:, 1]
            farmer_count, hand_count = actor.new_zeros(b, 200), actor.new_zeros(b, 200)
            farmer_count.scatter_add_(1, address, farmer)
            hand_count.scatter_add_(1, address, hands)
            cell_features = torch.cat((cell_features, farmer_count.clamp_max(1).unsqueeze(-1),
                                        (hand_count / 20).unsqueeze(-1)), -1)
        cell_tokens = self.cell(cell_features)
        cell_tokens = cell_tokens + self.kind((cells[..., 0] * 8).round().long().clamp(0, 8))
        cell_tokens = cell_tokens + self.species((cells[..., 1] * 8).round().long().clamp(0, 8))
        unit_tokens = self.unit(unit_features)
        products = actor[:, 128:200].reshape(b, 9, 8)
        animals = actor.new_zeros(b, 3, 8)
        animals[:, :, 2] = actor[:, 54:57]  # Public OWN shed only; no opponent inventory.
        commodity_tokens = self.commodity(torch.cat((products, animals), 1)) + self.commodity_id.weight
        global_token = self.global_state(actor[:, :128]).unsqueeze(1)
        markets = self.market_slot.weight.expand(b, -1, -1)
        tokens = torch.cat((cell_tokens, unit_tokens, global_token, commodity_tokens, markets), 1)
        valid = torch.cat((torch.ones(b, 200, device=actor.device, dtype=torch.bool),
                           units[..., 0] > .5, torch.ones(b, 23, device=actor.device, dtype=torch.bool)), 1)
        coords = torch.cat((self.cell_xy.expand(b, -1, -1), units[..., 1:3] * 9,
                            actor.new_zeros(b, 23, 2)), 1)
        return self.input_norm(tokens) * valid[..., None], coords, self.rope_groups.expand(b, -1), valid

    def forward(self, observation):
        x, coords, groups, valid = self.tokenize(observation)
        for block in self.blocks:
            x = block(x, coords, groups, valid)
        x = self.final_norm(x)
        own, markets = x[:, 200:220], x[:, 253:263]
        units = torch.cat((self.unit_command(own), self.unit_quantity(own)), -1).flatten(1)
        orders = torch.cat((self.market_command(markets), self.market_quantity(markets)), -1).flatten(1)
        return torch.cat((units, orders), -1).float()


class Dataset:
    def __init__(self, path):
        self.path = Path(path)
        with self.path.open("rb") as stream:
            header = struct.unpack("<16IQQd", stream.read(88))
        assert header[:2] == (0x4b414742, 3) and header[3:6] == (OBS, HEADS, PACKED)
        assert header[8:15] == (4, 5, 7, 0, 0, 1, 0) and header[7] == STEPS
        assert header[2] == header[6] * STEPS and 0 < header[15] < header[6]
        assert self.path.stat().st_size == 88 + header[2] * (OBS * 4 + HEADS * 4 + PACKED + 4)
        self.games, self.train_games = header[6], header[6] - header[15]
        n, off = header[2], 88
        self.obs = np.memmap(path, mode="r", dtype=np.float32, shape=(n, OBS), offset=off)
        off += self.obs.nbytes
        self.labels = np.memmap(path, mode="r", dtype=np.float32, shape=(n, HEADS), offset=off)
        off += self.labels.nbytes
        self.masks = np.memmap(path, mode="r", dtype=np.uint8, shape=(n, PACKED), offset=off)
        self.metadata = json.loads(self.path.with_suffix(".json").read_text())

    def game(self, game, device):
        a, z = game * STEPS, (game + 1) * STEPS
        obs = torch.tensor(np.array(self.obs[a:z, :3000]), device=device)
        labels = torch.tensor(np.array(self.labels[a:z]), device=device, dtype=torch.long)
        masks = torch.tensor(np.unpackbits(self.masks[a:z], axis=1, bitorder="little")[:, :LOGITS],
                             device=device, dtype=torch.bool)
        return obs, labels, masks


class CompactLoss(nn.Module):
    """Sum masked head CE with a device-resident, immutable head layout."""
    def __init__(self):
        super().__init__()
        sizes = torch.tensor(SIZES)
        offsets = sizes.cumsum(0) - sizes
        columns = torch.arange(max(SIZES))
        self.register_buffer("index", (offsets[:, None] + columns).clamp_max(LOGITS - 1), persistent=False)
        self.register_buffer("width_mask", columns[None, :] < sizes[:, None], persistent=False)

    def forward(self, logits, labels, mask):
        padded = logits[:, self.index].masked_fill(~(mask[:, self.index] & self.width_mask), -1e9)
        valid = labels >= 0
        safe = labels.clamp_min(0)
        # All-invalid terminal rows get finite sentinel logits and zero loss.
        loss = F.cross_entropy(padded.flatten(0, 1), safe.flatten(), reduction="none").reshape_as(labels)
        return (loss * valid).sum(), valid.sum(), ((padded.argmax(-1) == labels) & valid).sum()


@lru_cache(maxsize=8)
def _loss_for_device(device):
    return CompactLoss().to(device)


def loss_statistics(logits, labels, mask):
    """Sum CE over valid heads; forced/filtered/terminal labels stay excluded."""
    return _loss_for_device(logits.device)(logits, labels, mask)


class BCObjective(nn.Module):
    """Compile actor and masked loss together, including their backward graph."""
    def __init__(self, model):
        super().__init__()
        self.model = model
        self.loss = CompactLoss().to(next(model.parameters()).device)

    def forward(self, obs, labels, masks):
        with torch.autocast(device_type=obs.device.type, dtype=torch.bfloat16,
                            enabled=obs.device.type == "cuda"):
            logits = self.model(obs)
        return self.loss(logits, labels, masks)


def game_seeds(first, count, mode):
    assert first >= 0 and count > 0 and first + count <= 2**64
    return [((1664525 * i + 1013904223) & 0xffffffff) if mode == "native-index" else i
            for i in range(first, first + count)]


def configure_attention(backend):
    if backend != "auto":
        torch.backends.cuda.enable_flash_sdp(False)
        torch.backends.cuda.enable_math_sdp(False)
        torch.backends.cuda.enable_mem_efficient_sdp(backend == "efficient")
        torch.backends.cuda.enable_cudnn_sdp(backend == "cudnn")


def restore_training_state(saved, optimizer, rng, train_games):
    """Restore epoch-boundary state; explicitly identify legacy weights-only resumes."""
    if "optimizer" in saved:
        optimizer.load_state_dict(saved["optimizer"])
        rng.bit_generator.state = saved["numpy_rng_state"]
        torch.set_rng_state(saved["torch_rng_state"].cpu())
        if torch.cuda.is_available() and "cuda_rng_state" in saved:
            torch.cuda.set_rng_state_all([s.cpu() for s in saved["cuda_rng_state"]])
        return "full optimizer/RNG resume"
    # Original snapshots omitted Adam moments. Preserve the epoch schedule and
    # data permutation, but never claim this is an exact training continuation.
    for _ in range(saved["epoch"]):
        rng.permutation(train_games)
    return "legacy weights-only continuation; Adam moments reset"


def save_training_checkpoint(path, model, optimizer, epoch, rng, best, selected, dataset_sha256):
    payload = dict(format=f"kag_compact_v7_transformer_v{model.config.adapter_version}",
        config=asdict(model.config), model=model.state_dict(), optimizer=optimizer.state_dict(),
        epoch=epoch, numpy_rng_state=rng.bit_generator.state, torch_rng_state=torch.get_rng_state(),
        best=best, selected=selected, dataset_sha256=dataset_sha256)
    if next(model.parameters()).is_cuda:
        payload["cuda_rng_state"] = torch.cuda.get_rng_state_all()
    temporary = path.with_suffix(".pt.tmp")
    torch.save(payload, temporary)
    temporary.replace(path)


def evaluate(model, args, output, device):
    from build_bc_dataset import load_bridge
    import replay_native as native
    lib = load_bridge(ROOT / "ocean/kaggriculture_direct/build/replay.so")
    lib.kag_direct_sample_step.argtypes = [C.c_void_p, C.c_int, C.c_void_p, C.c_int,
                                         C.POINTER(C.c_uint), C.c_void_p, C.c_void_p]
    # Native first games use one LCG advance from each environment index. The
    # default 16-game cohort therefore exactly matches native eval with 16 rows.
    # Explicit seed mode is available for a separate fresh validation cohort.
    contexts, seats, states, random = [], [], [], []
    map_seeds = game_seeds(args.eval_seed, args.eval_games, args.map_seed_mode)
    for seed in map_seeds:
        for seat in (0, 1):
            config = native.CConfig()
            lib.kg_config_default(C.byref(config))
            config.seed = seed
            context = lib.kag_bc_create(C.byref(config), str(ROOT / "config/kaggriculture.ini").encode())
            assert context
            contexts.append(context); seats.append(seat)
            states.append(lib.kag_bc_state(context)); random.append(C.c_uint(seed ^ (seat + 73)))
    n = len(contexts)
    obs, masks, heads, rewards = (np.empty((n, OBS), np.float32), np.empty((n, LOGITS), np.uint8),
                                np.empty((n, HEADS), np.float32), np.empty((n, 2), np.float32))
    model.eval()
    try:
        with torch.inference_mode():
            for step in range(STEPS - 1):
                for i, (context, seat) in enumerate(zip(contexts, seats)):
                    assert lib.kag_bc_view(context, seat, obs[i].ctypes.data, masks[i].ctypes.data)
                outputs = []
                for first in range(0, n, args.microbatch):
                    with torch.autocast(device_type=device.type, dtype=torch.bfloat16, enabled=device.type == "cuda"):
                        outputs.append(model(torch.tensor(obs[first:first + args.microbatch, :3000], device=device)).cpu().numpy())
                logits = np.concatenate(outputs)
                assert np.isfinite(logits).all()
                for i, (context, seat) in enumerate(zip(contexts, seats)):
                    assert lib.kag_direct_sample_step(context, seat, logits[i].ctypes.data, 1,
                        C.byref(random[i]), heads[i].ctypes.data, rewards[i].ctypes.data)
        rows = []
        for i, seat in enumerate(seats):
            assert lib.kg_done(states[i])
            cash, other = [lib.kg_player_money(states[i], p) for p in (seat, 1 - seat)]
            rows.append(dict(seed=map_seeds[i // 2], seat=seat, cash=cash, opponent_cash=other,
                             win=float(cash > other) + .5 * (cash == other)))
        summary = dict(cash=float(np.mean([r["cash"] for r in rows])),
                       win=float(np.mean([r["win"] for r in rows])), games=len(rows),
                       cash_gain=float(np.mean([r["cash"] - 3000 for r in rows])), rows=rows)
        with output.open("x") as stream:
            json.dump(summary, stream, indent=2)
        print("TRANSFORMER_FRESH_EVAL " + json.dumps({k: v for k, v in summary.items() if k != "rows"}), flush=True)
        return summary
    finally:
        for context in contexts:
            lib.kag_bc_destroy(context)


def main():
    ini = configparser.ConfigParser()
    ini.read(ROOT / "config/kaggriculture.ini")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("check", "bc", "eval"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    checkpoints = parser.add_mutually_exclusive_group()
    checkpoints.add_argument("--checkpoint", type=Path)
    checkpoints.add_argument("--resume", type=Path, help="continue BC to --epochs TOTAL epochs")
    parser.add_argument("--compile", action=argparse.BooleanOptionalAction,
                        default=ini.getboolean("transformer", "compile", fallback=False), help="compile actor and masked BC loss")
    parser.add_argument("--sdpa", choices=("auto", "efficient", "cudnn"), default="auto")
    parser.add_argument("--fused-adam", action=argparse.BooleanOptionalAction,
                        default=ini.getboolean("transformer", "fused_adam", fallback=False))
    parser.add_argument("--epochs", type=int, default=ini.getint("bc", "epochs"))
    parser.add_argument("--microbatch", type=int, default=ini.getint("transformer", "microbatch", fallback=16))
    parser.add_argument("--max-train-games", type=int, default=0)
    parser.add_argument("--eval-games", type=int, help="games per seat")
    parser.add_argument("--eval-seed", type=int)
    parser.add_argument("--map-seed-mode", choices=("native-index", "explicit"))
    parser.add_argument("--validation-games", type=int, default=ini.getint("transformer", "validation_games", fallback=64), help="final separate-cohort games per seat")
    parser.add_argument("--validation-seed", type=int, default=ini.getint("transformer", "validation_seed", fallback=2026100700))
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--layers", type=int, default=6)
    parser.add_argument("--adapter-version", type=int, choices=(1, 2))
    args = parser.parse_args()
    if args.eval_games is None:
        args.eval_games = args.validation_games if args.mode == "eval" else ini.getint("transformer", "eval_games", fallback=16)
    if args.eval_seed is None:
        args.eval_seed = args.validation_seed if args.mode == "eval" else ini.getint("transformer", "eval_seed", fallback=0)
    if args.map_seed_mode is None:
        args.map_seed_mode = "explicit" if args.mode == "eval" else ini.get("transformer", "eval_map_seed_mode", fallback="native-index")
    if args.output is None:
        args.output = ROOT / ini.get("transformer", "output_root", fallback="saved/kaggriculture/transformer_v2") / f"{args.mode}_{time.time_ns()}"
    configured_checkpoint = ROOT / ini.get("transformer", "checkpoint", fallback="None")
    if args.mode == "eval" and args.checkpoint is None and not args.resume:
        args.checkpoint = configured_checkpoint
    assert args.epochs > 0 and args.microbatch > 0 and args.eval_games > 0 and args.validation_games > 0
    assert args.max_train_games >= 0
    if args.resume and args.mode != "bc":
        parser.error("--resume requires bc mode")
    screening_seeds = set(game_seeds(args.eval_seed, args.eval_games, args.map_seed_mode))
    validation_seeds = game_seeds(args.validation_seed, args.validation_games, "explicit")
    if args.mode == "bc":
        assert not screening_seeds.intersection(validation_seeds), "screening/validation seed overlap"
    if args.dry_run:
        print(json.dumps(vars(args), default=str, indent=2))
        return
    if args.checkpoint and args.checkpoint.resolve() == configured_checkpoint.resolve():
        with args.checkpoint.open("rb") as stream:
            assert hashlib.file_digest(stream, "sha256").hexdigest() == ini["transformer"]["checkpoint_sha256"], "configured checkpoint checksum mismatch"
    args.output.mkdir(parents=True, exist_ok=False)
    device = torch.device(args.device)
    torch.set_num_threads(4)
    torch.manual_seed(73)
    configure_attention(args.sdpa)
    saved = None
    checkpoint = args.resume or args.checkpoint
    if checkpoint:
        saved = torch.load(checkpoint, map_location=device, weights_only=True)
        assert saved["format"] in ("kag_compact_v7_transformer_v1", FORMAT, "kag_compact_v7_transformer_ppo_v1")
        if args.resume and saved["format"] == "kag_compact_v7_transformer_ppo_v1":
            parser.error("PPO checkpoints resume through transformer_ppo.py train --resume")
        saved_config = dict(saved["config"], adapter_version=saved["config"].get("adapter_version", 1))
        if args.adapter_version is None:
            args.adapter_version = saved_config["adapter_version"]
    config = ModelConfig(layers=args.layers, adapter_version=args.adapter_version or 2)
    model = CompactTransformer(config).to(device)
    if saved:
        assert saved_config == asdict(config), "checkpoint adapter/backbone mismatch; new adapters require fresh BC"
        model.load_state_dict(saved["model"])
    elif args.mode == "eval":
        parser.error("eval requires --checkpoint")
    if args.mode == "eval":
        evaluate(model, args, args.output / "fresh_eval.json", device)
        return
    data = Dataset(ROOT / ini["bc"]["data"])
    with data.path.open("rb") as stream:
        assert hashlib.file_digest(stream, "sha256").hexdigest() == data.metadata["sha256"], "dataset checksum mismatch"
    opt = torch.optim.Adam(model.parameters(), lr=ini.getfloat("bc", "learning_rate"),
                           eps=ini.getfloat("bc", "adam_epsilon"), fused=args.fused_adam)
    objective = BCObjective(model)
    if args.compile:
        # Preserve BF16 rounding at eager operator boundaries. Default fusion
        # removes those casts and changed the trained actor's gradients by ~4%.
        objective = torch.compile(objective, fullgraph=True, dynamic=False, options=COMPILE_OPTIONS)
    rng = np.random.default_rng(73)
    first_epoch, best, selected = 1, (-math.inf, -math.inf), None
    resume_kind = None
    if args.resume:
        source_receipt = json.loads(args.resume.parent.joinpath("config.json").read_text())
        assert source_receipt["dataset_sha256"] == data.metadata["sha256"], "resume dataset mismatch"
        for name in ("eval_games_per_seat", "eval_seed", "map_seed_mode"):
            current = args.eval_games if name == "eval_games_per_seat" else getattr(args, name)
            assert source_receipt[name] == current, "resume screening cohort mismatch"
        assert source_receipt["max_train_games"] == args.max_train_games, "resume training exposure mismatch"
        first_epoch = saved["epoch"] + 1
        assert first_epoch <= args.epochs, "--epochs is the total target, not additional epochs"
        resume_kind = restore_training_state(saved, opt, rng, data.train_games)
        # Preserve the requested runtime optimizer implementation after loading.
        for group in opt.param_groups:
            group["fused"] = args.fused_adam
        selected = saved.get("selected")
        if selected is None:
            selection_file = args.resume.parent / "selected.json"
            if selection_file.exists():
                selected = json.loads(selection_file.read_text())
                assert selected["epoch"] <= saved["epoch"], "selection is newer than the resumed checkpoint"
        if selected:
            selected["checkpoint"] = str((ROOT / selected["checkpoint"]).resolve())
            assert Path(selected["checkpoint"]).is_file(), "selected checkpoint is missing"
            best = (selected["cash"], -selected["heldout_loss"])
        print(f"TRANSFORMER_RESUME epoch={first_epoch} {resume_kind}", flush=True)
    # BC actor only: no expert-return fit or untrained critic promoted to PPO.
    checkpoint_format = f"kag_compact_v7_transformer_v{config.adapter_version}"
    receipt = dict(format=checkpoint_format, config=asdict(config), parameters=sum(p.numel() for p in model.parameters()),
                   dataset_sha256=data.metadata["sha256"], train_games=data.train_games,
                   heldout_games=data.games-data.train_games, microbatch=args.microbatch,
                   optimizer="Adam", lr=ini.getfloat("bc", "learning_rate"), epochs=args.epochs,
                   max_train_games=args.max_train_games, torch=torch.__version__, actor_observation=3000,
                   action_version=7, private_critic_input=False, compiled=args.compile,
                   compile_options=COMPILE_OPTIONS if args.compile else None,
                   sdpa=args.sdpa, fused_adam=args.fused_adam, first_epoch=first_epoch,
                   resume_checkpoint=str(args.resume.resolve()) if args.resume else None, resume_kind=resume_kind)
    receipt.update(source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                   shared_config=(ROOT / "config/kaggriculture.ini").read_text(),
                   eval_games_per_seat=args.eval_games, eval_seed=args.eval_seed,
                   map_seed_mode=args.map_seed_mode, validation_games_per_seat=args.validation_games,
                   validation_seed=args.validation_seed)
    with (args.output / "config.json").open("x") as stream:
        json.dump(receipt, stream, indent=2)
    print("TRANSFORMER_CONFIG " + json.dumps({k: v for k, v in receipt.items()
                                              if k != "shared_config"}), flush=True)
    started = time.monotonic()
    if selected:
        with (args.output / "selected.json").open("w") as stream:
            json.dump(selected, stream, indent=2)
    for epoch in range(first_epoch, args.epochs + 1):
        for split in ("train", "heldout"):
            model.train(split == "train")
            games = rng.permutation(data.train_games) if split == "train" else range(data.train_games, data.games)
            if args.max_train_games and split == "train":
                games = games[:args.max_train_games]
            total = np.zeros(3)
            for number, game in enumerate(games):
                obs, labels, masks = data.game(game, device)
                frames = max(1, int((labels >= 0).any(-1).sum()))
                opt.zero_grad(set_to_none=True)
                stats = torch.zeros(3, device=device)
                for first in range(0, STEPS, args.microbatch):
                    last = first + args.microbatch
                    with torch.set_grad_enabled(split == "train"):
                        loss, count, correct = objective(obs[first:last], labels[first:last], masks[first:last])
                        if split == "train":
                            (loss / frames).backward()
                    stats += torch.stack((loss.detach(), count, correct))
                if split == "train":
                    norm = torch.nn.utils.clip_grad_norm_(model.parameters(), ini.getfloat("bc", "max_grad_norm"), error_if_nonfinite=True)
                    opt.step()
                total += stats.cpu().numpy()
                if (number + 1) % 25 == 0 or args.mode == "check":
                    print(f"TRANSFORMER_PROGRESS epoch={epoch} split={split} games={number+1} ce={total[0]/max(1,total[1]):.6f} seconds={time.monotonic()-started:.1f}", flush=True)
                if args.mode == "check":
                    assert all(torch.isfinite(p).all() for p in model.parameters())
                    print(f"TRANSFORMER_CHECK loss={total[0]/max(1,total[1]):.6f} grad_norm={norm.item():.6f} peak_gpu_bytes={torch.cuda.max_memory_allocated() if device.type=='cuda' else 0}", flush=True)
                    return
            metrics = dict(epoch=epoch, split=split, loss=float(total[0]/max(1,total[1])),
                           accuracy=float(total[2]/max(1,total[1])), seconds=time.monotonic()-started)
            print("TRANSFORMER_METRICS " + json.dumps(metrics), flush=True)
            with (args.output / "metrics.jsonl").open("a") as stream:
                stream.write(json.dumps(metrics) + "\n")
        path = args.output / f"epoch_{epoch}.pt"
        fresh = evaluate(model, args, args.output / f"fresh_eval_epoch_{epoch}.json", device)
        ranking = (fresh["cash"], -metrics["loss"])
        if ranking > best:
            best = ranking
            selected = dict(epoch=epoch, checkpoint=str(path.resolve()), cash=fresh["cash"],
                            heldout_loss=metrics["loss"], criterion="fresh mean cash; held-out CE only breaks ties")
        save_training_checkpoint(path, model, opt, epoch, rng, best, selected, data.metadata["sha256"])
        with (args.output / "selected.json").open("w") as stream:
            json.dump(selected, stream, indent=2)
    # Final validation is not used to select an epoch. Never call the matched
    # screening win rate a strength estimate against an expert opponent.
    validation = argparse.Namespace(**vars(args))
    validation.eval_games, validation.eval_seed = args.validation_games, args.validation_seed
    validation.map_seed_mode = "explicit"
    saved = torch.load(selected["checkpoint"], map_location=device, weights_only=True)
    model.load_state_dict(saved["model"])
    evaluate(model, validation, args.output / "selected_fresh_validation.json", device)
    print(f"TRANSFORMER_COMPLETE seconds={time.monotonic()-started:.1f}", flush=True)


if __name__ == "__main__":
    main()
