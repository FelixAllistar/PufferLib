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
    parser.add_argument("--tolerance", type=float, default=1e-5)
    args = parser.parse_args()

    metadata_path = args.metadata or Path(str(args.onnx) + ".json")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    model = NativeMinGRU(
        metadata["obs_size"], metadata["hidden_size"], metadata["num_layers"],
        sum(metadata["action_sizes"]),
    )
    load_weights(Path(metadata["checkpoint"]), model)
    model.eval()

    rng = np.random.default_rng(args.seed)
    obs = rng.normal(size=(args.batch_size, metadata["obs_size"])).astype(np.float32)
    state = rng.normal(size=(
        metadata["num_layers"], args.batch_size, metadata["hidden_size"]
    )).astype(np.float32)
    import torch
    with torch.no_grad():
        expected = [value.numpy() for value in model(torch.from_numpy(obs), torch.from_numpy(state))]
    session = ort.InferenceSession(str(args.onnx), providers=["CPUExecutionProvider"])
    actual = session.run(None, {"obs": obs, "state": state})
    names = ["logits", "value", "next_state"]
    passed = True
    for name, left, right in zip(names, expected, actual):
        maximum = float(np.max(np.abs(left - right)))
        print(f"{name}: max_abs={maximum:.9g}")
        passed &= maximum <= args.tolerance
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
