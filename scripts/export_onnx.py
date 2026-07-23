#!/usr/bin/env python3
"""Export a native 5c MinGRU checkpoint to ONNX.

The checkpoint is the exact flat fp32 master-weight stream written by 5c:
encoder, fused logits/value decoder, then one MinGRU matrix per layer.
"""

from __future__ import annotations

import argparse
import ast
import configparser
import json
import re
import subprocess
from pathlib import Path

import numpy as np
import torch
from torch import nn


ROOT = Path(__file__).resolve().parents[1]


class NativeMinGRU(nn.Module):
    def __init__(self, obs_size: int, hidden_size: int, num_layers: int, action_size: int):
        super().__init__()
        self.encoder = nn.Linear(obs_size, hidden_size, bias=False)
        self.decoder = nn.Linear(hidden_size, action_size + 1, bias=False)
        self.layers = nn.ModuleList(
            nn.Linear(hidden_size, 3 * hidden_size, bias=False)
            for _ in range(num_layers)
        )
        self.hidden_size = hidden_size
        self.num_layers = num_layers
        self.action_size = action_size

    @staticmethod
    def candidate(x: torch.Tensor) -> torch.Tensor:
        return torch.where(x >= 0, x + 0.5, torch.sigmoid(x))

    def forward(self, obs: torch.Tensor, state: torch.Tensor):
        x = self.encoder(obs.float())
        next_states = []
        for layer_index, layer in enumerate(self.layers):
            candidate, gate, projection = layer(x).chunk(3, dim=-1)
            next_state = torch.lerp(
                state[layer_index], self.candidate(candidate), torch.sigmoid(gate)
            )
            x = torch.sigmoid(projection) * next_state + (
                1.0 - torch.sigmoid(projection)
            ) * x
            next_states.append(next_state)
        fused = self.decoder(x)
        return fused[:, : self.action_size], fused[:, -1:], torch.stack(next_states)


def load_config(env_name: str) -> configparser.ConfigParser:
    config = configparser.ConfigParser(interpolation=None)
    paths = [ROOT / "config" / "default.ini", ROOT / "config" / f"{env_name}.ini"]
    if not paths[1].is_file():
        raise FileNotFoundError(paths[1])
    config.read(paths)
    return config


def _eval_int(expression: str) -> int:
    expression = re.sub(r"(?<=\d)[uUlL]+", "", expression)
    tree = ast.parse(expression, mode="eval")
    allowed = (ast.Expression, ast.Constant, ast.UnaryOp, ast.BinOp,
               ast.UAdd, ast.USub, ast.Add, ast.Sub, ast.Mult,
               ast.Div, ast.FloorDiv, ast.Mod, ast.Pow)
    if any(not isinstance(node, allowed) for node in ast.walk(tree)):
        raise ValueError(f"unsupported C integer expression: {expression}")
    return int(eval(compile(tree, "<schema>", "eval"), {"__builtins__": {}}, {}))


def read_env_schema(env_name: str) -> tuple[int, list[int]]:
    env_dir = ROOT / "ocean" / env_name
    header = env_dir / f"{env_name}.h"
    if not header.is_file():
        raise FileNotFoundError(header)
    source = (
        f'#include "{header.as_posix()}"\n'
        "PUF_SCHEMA_OBS OBS_SIZE\n"
        "PUF_SCHEMA_HEADS NUM_ATNS\n"
        "PUF_SCHEMA_ACTIONS ACT_SIZES\n"
    )
    command = [
        "cc", "-E", "-P", "-DPS_HEADLESS_BINDING",
        f"-I{ROOT / 'src'}", f"-I{env_dir}",
        f"-I{ROOT / 'raylib-5.5_linux_amd64' / 'include'}", "-xc", "-",
    ]
    expanded = subprocess.run(
        command, input=source, text=True, capture_output=True, check=True
    ).stdout
    obs_match = re.search(r"^PUF_SCHEMA_OBS\s+(.+)$", expanded, re.MULTILINE)
    heads_match = re.search(r"^PUF_SCHEMA_HEADS\s+(.+)$", expanded, re.MULTILINE)
    actions_match = re.search(r"^PUF_SCHEMA_ACTIONS\s+\{([^}]+)\}", expanded, re.MULTILINE)
    if not (obs_match and heads_match and actions_match):
        raise RuntimeError(f"could not extract schema from {header}")
    obs_size = _eval_int(obs_match.group(1))
    num_heads = _eval_int(heads_match.group(1))
    action_sizes = [_eval_int(value.strip()) for value in actions_match.group(1).split(",")]
    if len(action_sizes) != num_heads:
        raise RuntimeError(f"NUM_ATNS={num_heads}, ACT_SIZES has {len(action_sizes)} entries")
    return obs_size, action_sizes


def resolve_checkpoint(env_name: str, value: str, config: configparser.ConfigParser) -> Path:
    if value != "latest":
        path = Path(value).expanduser()
        return path if path.is_absolute() else ROOT / path
    root = ROOT / config["base"].get("checkpoint_dir", "checkpoints") / env_name
    candidates = list(root.rglob("*.bin"))
    if not candidates:
        raise FileNotFoundError(f"no checkpoints under {root}")
    return max(candidates, key=lambda path: (path.stat().st_mtime_ns, path.name))


def load_weights(checkpoint: Path, model: NativeMinGRU) -> dict[str, list[int]]:
    weights = np.fromfile(checkpoint, dtype=np.float32)
    index = 0
    loaded: dict[str, list[int]] = {}

    def take(name: str, parameter: torch.Tensor) -> None:
        nonlocal index
        count = parameter.numel()
        end = index + count
        if end > weights.size:
            raise ValueError(f"{checkpoint} ended while reading {name}")
        value = torch.from_numpy(weights[index:end].reshape(parameter.shape).copy())
        parameter.data.copy_(value)
        loaded[name] = list(parameter.shape)
        index = end

    take("encoder.weight", model.encoder.weight)
    take("decoder.weight", model.decoder.weight)
    for layer_index, layer in enumerate(model.layers):
        take(f"layers.{layer_index}.weight", layer.weight)
    if index != weights.size:
        raise ValueError(
            f"checkpoint layout mismatch: expected {index} fp32 values, found {weights.size}; "
            "check hidden_size and num_layers"
        )
    return loaded


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("env")
    parser.add_argument("--checkpoint", default="latest")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--hidden-size", type=int)
    parser.add_argument("--num-layers", type=int)
    parser.add_argument("--batch-size", type=int, default=4)
    args = parser.parse_args()

    config = load_config(args.env)
    obs_size, action_sizes = read_env_schema(args.env)
    hidden_size = args.hidden_size or config.getint("policy", "hidden_size")
    num_layers = args.num_layers or config.getint("policy", "num_layers")
    checkpoint = resolve_checkpoint(args.env, args.checkpoint, config).resolve()
    output = (args.output or Path("exports") / f"{args.env}.onnx")
    if not output.is_absolute():
        output = ROOT / output
    metadata_path = args.metadata or Path(str(output) + ".json")
    if not metadata_path.is_absolute():
        metadata_path = ROOT / metadata_path

    model = NativeMinGRU(obs_size, hidden_size, num_layers, sum(action_sizes))
    tensors = load_weights(checkpoint, model)
    model.eval()
    obs = torch.zeros(args.batch_size, obs_size, dtype=torch.float32)
    state = torch.zeros(num_layers, args.batch_size, hidden_size, dtype=torch.float32)
    output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        model, (obs, state), output,
        input_names=["obs", "state"],
        output_names=["logits", "value", "next_state"],
        dynamic_axes={
            "obs": {0: "batch"}, "state": {1: "batch"},
            "logits": {0: "batch"}, "value": {0: "batch"},
            "next_state": {1: "batch"},
        },
        opset_version=18,
    )
    metadata = {
        "format": "pufferlib-5c-mingru-v1",
        "environment": args.env,
        "checkpoint": str(checkpoint),
        "onnx": str(output.resolve()),
        "obs_size": obs_size,
        "action_sizes": action_sizes,
        "action_offsets": np.cumsum([0, *action_sizes]).tolist(),
        "hidden_size": hidden_size,
        "num_layers": num_layers,
        "state_layout": ["layers", "batch", "hidden"],
        "tensors": tensors,
    }
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(metadata, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
