#!/usr/bin/env python3
"""Compare a native CUDA policy checkpoint against an exported ONNX model."""

import argparse
import json
import sys

import numpy as np
import onnxruntime as ort

import pufferlib.pufferl as pufferl
import pufferlib._C as backend


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("env")
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--onnx", required=True)
    parser.add_argument("--metadata", default=None)
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--seed", type=int, default=123)
    parser.add_argument("--total-agents", type=int, default=16)
    parser.add_argument("--horizon", type=int, default=8)
    parser.add_argument("--logit-tol", type=float, default=0.01)
    parser.add_argument("--value-tol", type=float, default=0.01)
    parser.add_argument("--state-tol", type=float, default=0.02)
    parser.add_argument("--argmax-min", type=float, default=0.99)
    return parser.parse_args()


def load_env_config(env_name, total_agents, horizon):
    old_argv = sys.argv
    sys.argv = [old_argv[0]]
    try:
        args = pufferl.load_config(env_name)
    finally:
        sys.argv = old_argv

    args["vec"]["total_agents"] = total_agents
    args["vec"]["num_buffers"] = 1
    args["vec"]["num_threads"] = 1
    args["train"]["horizon"] = horizon
    args["train"]["minibatch_size"] = total_agents
    return args


def compare(name, native_arr, onnx_arr, tol):
    native_arr = np.asarray(native_arr)
    diff = np.abs(native_arr - onnx_arr)
    result = {
        "name": name,
        "native_shape": list(native_arr.shape),
        "onnx_shape": list(onnx_arr.shape),
        "max_abs": float(diff.max()),
        "mean_abs": float(diff.mean()),
        "passed": bool(diff.max() <= tol),
    }
    print(
        f"{name}: shape={native_arr.shape} max_abs={result['max_abs']:.9g} "
        f"mean_abs={result['mean_abs']:.9g} tol={tol:g} "
        f"{'PASS' if result['passed'] else 'FAIL'}"
    )
    return result


def run_case(label, native_policy, session, metadata, rng, batch_size, zero_state):
    obs = rng.normal(size=(batch_size, metadata["obs_size"])).astype(np.float32)
    if zero_state:
        state = np.zeros(
            (metadata["num_layers"], batch_size, metadata["hidden_size"]),
            dtype=np.float32,
        )
    else:
        state = rng.normal(
            size=(metadata["num_layers"], batch_size, metadata["hidden_size"]),
        ).astype(np.float32)

    native = backend.debug_policy_forward(native_policy, obs, state)
    output_names = [output.name for output in session.get_outputs()]
    onnx = dict(zip(output_names, session.run(None, {"obs": obs, "state": state})))

    print(f"\ncase: {label}")
    results = [
        compare("logits", native["logits"], onnx["logits"], metadata["logit_tol"]),
        compare("value", native["value"], onnx["value"], metadata["value_tol"]),
        compare(
            "next_state",
            native["next_state"],
            onnx["next_state"],
            metadata["state_tol"],
        ),
    ]

    argmax_agreement = None
    if not metadata.get("continuous", False):
        argmax_agreement = float(
            (
                np.asarray(native["logits"]).argmax(axis=1)
                == onnx["logits"].argmax(axis=1)
            ).mean()
        )
        passed = argmax_agreement >= metadata["argmax_min"]
        print(
            f"argmax: agreement={argmax_agreement:.6g} "
            f"min={metadata['argmax_min']:g} {'PASS' if passed else 'FAIL'}"
        )
        results.append({"name": "argmax", "agreement": argmax_agreement, "passed": passed})

    return results


def main():
    args = parse_args()
    metadata_path = args.metadata or args.onnx + ".json"
    with open(metadata_path, "r", encoding="utf-8") as f:
        metadata = json.load(f)

    metadata["logit_tol"] = args.logit_tol
    metadata["value_tol"] = args.value_tol
    metadata["state_tol"] = args.state_tol
    metadata["argmax_min"] = args.argmax_min

    env_args = load_env_config(args.env, args.total_agents, args.horizon)
    native_policy = backend.create_pufferl(env_args)
    try:
        backend.load_weights(native_policy, args.checkpoint)
        session = ort.InferenceSession(args.onnx, providers=["CPUExecutionProvider"])
        rng = np.random.default_rng(args.seed)

        all_results = []
        all_results.extend(
            run_case(
                "zero recurrent state",
                native_policy,
                session,
                metadata,
                rng,
                args.batch_size,
                zero_state=True,
            )
        )
        all_results.extend(
            run_case(
                "random recurrent state",
                native_policy,
                session,
                metadata,
                rng,
                args.batch_size,
                zero_state=False,
            )
        )
    finally:
        backend.close(native_policy)

    if not all(result["passed"] for result in all_results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
