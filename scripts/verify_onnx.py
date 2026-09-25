#!/usr/bin/env python3
"""Numerically verify an exported model and its recurrent-state contract."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort

from export_onnx import NativeMinGRU, load_weights


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("onnx", type=Path)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--seed", type=int, default=123)
    parser.add_argument("--steps", type=int, default=32)
    parser.add_argument("--tolerance", type=float, default=1e-5)
    args = parser.parse_args()
    if args.steps < 1 or args.batch_size < 1:
        parser.error("steps and batch-size must be positive")

    metadata_path = args.metadata or Path(str(args.onnx) + ".json")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    model = NativeMinGRU(
        metadata["obs_size"], metadata["hidden_size"], metadata["num_layers"],
        sum(metadata["action_sizes"]),
    )
    load_weights(Path(metadata["checkpoint"]), model)
    model.eval()

    rng = np.random.default_rng(args.seed)
    state = rng.normal(size=(
        metadata["num_layers"], args.batch_size, metadata["hidden_size"]
    )).astype(np.float32)
    import torch
    session = ort.InferenceSession(str(args.onnx), providers=["CPUExecutionProvider"])
    actual_state = state.copy()
    maxima = np.zeros(3)
    for step in range(args.steps):
        if step % 7 == 0:
            state[:, ::2] = 0
            actual_state[:, ::2] = 0
        obs = rng.normal(size=(args.batch_size, metadata["obs_size"])).astype(np.float32)
        with torch.no_grad():
            expected = [value.numpy() for value in model(
                torch.from_numpy(obs), torch.from_numpy(state))]
        actual = session.run(None, {"obs": obs, "state": actual_state})
        maxima = np.maximum(maxima, [np.max(np.abs(a - b))
            for a, b in zip(expected, actual)])
        state = expected[2].copy()
        actual_state = actual[2].copy()
    names = ["logits", "value", "next_state"]
    passed = True
    for name, maximum in zip(names, maxima):
        print(f"{name}: max_abs={maximum:.9g}")
        passed &= maximum <= args.tolerance
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
