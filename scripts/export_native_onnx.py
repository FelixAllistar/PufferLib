#!/usr/bin/env python3
"""Export a native PufferLib 4.0 checkpoint to ONNX.

This handles the common native policy layout:
  encoder linear (no bias) -> MinGRU layers -> fused decoder logits/value.

It intentionally verifies the raw checkpoint layout before exporting. Native
checkpoints are flat fp32 master weights, including allocator alignment padding.
"""

import argparse
import json
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn


def align8(idx):
    return (idx + 7) & ~7


class NativeMinGRUPolicy(nn.Module):
    def __init__(self, obs_size, hidden_size, num_layers, action_size, continuous=False):
        super().__init__()
        self.obs_size = obs_size
        self.hidden_size = hidden_size
        self.num_layers = num_layers
        self.action_size = action_size
        self.continuous = continuous

        self.encoder = nn.Linear(obs_size, hidden_size, bias=False)
        self.mingru = nn.ModuleList(
            [nn.Linear(hidden_size, 3 * hidden_size, bias=False) for _ in range(num_layers)]
        )
        decoder_out = action_size + 1
        self.decoder = nn.Linear(hidden_size, decoder_out, bias=False)
        if continuous:
            self.logstd = nn.Parameter(torch.zeros(1, action_size))

    @staticmethod
    def _g(x):
        return torch.where(x >= 0, x + 0.5, torch.sigmoid(x))

    def forward(self, obs, state=None):
        h = self.encoder(obs.reshape(obs.shape[0], -1).float())
        if state is None:
            state = torch.zeros(
                self.num_layers,
                h.shape[0],
                self.hidden_size,
                dtype=h.dtype,
                device=h.device,
            )

        next_states = []
        for i, layer in enumerate(self.mingru):
            hidden, gate, proj = layer(h).chunk(3, dim=-1)
            out = torch.lerp(state[i], self._g(hidden), torch.sigmoid(gate))
            h = torch.sigmoid(proj) * out + (1.0 - torch.sigmoid(proj)) * h
            next_states.append(out)
        next_state = torch.stack(next_states, dim=0)

        decoded = self.decoder(h)
        logits = decoded[:, : self.action_size]
        value = decoded[:, self.action_size : self.action_size + 1]
        return logits, value, next_state


def take(weights, idx, shape, aligned=True):
    n = int(np.prod(shape))
    end = idx + n
    if end > len(weights):
        raise ValueError(f"checkpoint ended early while reading shape {shape} at float {idx}")
    tensor = torch.from_numpy(weights[idx:end].reshape(shape).copy())
    return tensor, align8(end) if aligned else end


def load_native_weights(path, model):
    weights = np.fromfile(path, dtype=np.float32)
    idx = 0
    tensors = {}

    tensor, idx = take(weights, idx, model.encoder.weight.shape)
    model.encoder.weight.data.copy_(tensor)
    tensors["encoder.weight"] = list(tensor.shape)

    tensor, idx = take(weights, idx, model.decoder.weight.shape)
    model.decoder.weight.data.copy_(tensor)
    tensors["decoder.weight"] = list(tensor.shape)

    if model.continuous:
        tensor, idx = take(weights, idx, model.logstd.shape)
        model.logstd.data.copy_(tensor)
        tensors["logstd"] = list(tensor.shape)

    for layer_idx, layer in enumerate(model.mingru):
        tensor, idx = take(weights, idx, layer.weight.shape)
        layer.weight.data.copy_(tensor)
        tensors[f"mingru.{layer_idx}.weight"] = list(tensor.shape)

    if idx != len(weights):
        raise ValueError(
            f"checkpoint layout mismatch: consumed {idx} floats but file has {len(weights)}"
        )
    return tensors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--obs-size", type=int, default=8)
    parser.add_argument("--hidden-size", type=int, default=128)
    parser.add_argument("--num-layers", type=int, default=2)
    parser.add_argument("--action-size", type=int, default=3)
    parser.add_argument("--continuous", action="store_true")
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--metadata", type=Path)
    args = parser.parse_args()

    model = NativeMinGRUPolicy(
        obs_size=args.obs_size,
        hidden_size=args.hidden_size,
        num_layers=args.num_layers,
        action_size=args.action_size,
        continuous=args.continuous,
    )
    tensors = load_native_weights(args.checkpoint, model)
    model.eval()

    dummy_obs = torch.zeros(args.batch_size, args.obs_size, dtype=torch.float32)
    dummy_state = torch.zeros(args.num_layers, args.batch_size, args.hidden_size, dtype=torch.float32)
    with torch.no_grad():
        logits, value, next_state = model(dummy_obs, dummy_state)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        model,
        (dummy_obs, dummy_state),
        args.output,
        input_names=["obs", "state"],
        output_names=["logits", "value", "next_state"],
        dynamic_axes={
            "obs": {0: "batch"},
            "state": {1: "batch"},
            "logits": {0: "batch"},
            "value": {0: "batch"},
            "next_state": {1: "batch"},
        },
        opset_version=18,
    )

    metadata = {
        "checkpoint": str(args.checkpoint),
        "output": str(args.output),
        "obs_size": args.obs_size,
        "hidden_size": args.hidden_size,
        "num_layers": args.num_layers,
        "action_size": args.action_size,
        "continuous": args.continuous,
        "tensors": tensors,
        "smoke": {
            "logits_shape": list(logits.shape),
            "value_shape": list(value.shape),
            "next_state_shape": list(next_state.shape),
            "logits_zero_obs": logits.tolist(),
            "value_zero_obs": value.tolist(),
        },
    }
    if args.metadata:
        args.metadata.parent.mkdir(parents=True, exist_ok=True)
        args.metadata.write_text(json.dumps(metadata, indent=2))
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
