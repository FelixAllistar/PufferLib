"""Losslessly export an explicitly bound, already fitted v2 full TRAIN forest.

This source performs no fitting, prediction or record lookup. The caller must
supply the exact new TRAIN NPZ digest from its sealed authorization. Learned
header data and export receipts remain private and must not enter Git.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np

NAMES = ("model_version", "size_domain", "roots", "feature", "threshold", "left", "right",
         "counts", "quantiles", "support")
DTYPES = {"model_version": "uint16", "size_domain": "uint16", "roots": "int16",
          "feature": "int16", "left": "int16", "right": "int16", "threshold": "float64",
          "counts": "float64", "quantiles": "float64", "support": "uint8"}
DOMAIN = "behavior-clone-full-model-v2"
SIZE_ENCODING = "legal_interior_fraction_f64_v2"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical(payload):
    data = bytearray((DOMAIN + "\0").encode("ascii"))
    for value in (2, len(payload["feature"]), 32, 64, 480):
        data.extend(struct.pack(">Q", value))
    for name in NAMES:
        array = payload[name]
        for value in array.flat:
            data.extend(struct.pack(">d", float(value)) if array.dtype.kind == "f" else
                        struct.pack(">Q", int(value) & ((1 << 64) - 1)))
    return data


def load_model(path, expected_sha256):
    require(type(expected_sha256) is str and len(expected_sha256) == 64
            and all(c in "0123456789abcdef" for c in expected_sha256), "exact lowercase NPZ SHA256 required")
    require(hashlib.sha256(path.read_bytes()).hexdigest() == expected_sha256,
            "NPZ does not match the explicitly sealed TRAIN identity")
    with np.load(path, allow_pickle=False) as saved:
        require(set(saved.files) == set(NAMES), "exact v2 full-forest NPZ members required; legacy model rejected")
        payload = {name: saved[name].copy() for name in NAMES}
    n = len(payload["feature"])
    shapes = {"model_version": (1,), "size_domain": (1,), "roots": (32,), "feature": (n,),
              "threshold": (n,), "left": (n,), "right": (n,), "counts": (n, 5),
              "quantiles": (n, 8), "support": (480,)}
    for name in NAMES:
        array = payload[name]
        require(array.dtype == np.dtype(DTYPES[name]) and array.shape == shapes[name],
                "v2 model dtype or shape differs: " + name)
        if array.dtype.kind == "f":
            require(np.isfinite(array).all(), "nonfinite model array: " + name)
    require(int(payload["model_version"][0]) == 2 and int(payload["size_domain"][0]) == 2,
            "v2 model version and sizing domain are both required")
    require(1 <= n <= 4064, "fixed full-forest node limit exceeded")
    require(np.all(payload["counts"] >= 0), "negative class count")
    quantiles = payload["quantiles"]
    require(np.all(quantiles >= 0) and np.all(quantiles <= 1)
            and np.all(quantiles[:, 1:] >= quantiles[:, :-1]),
            "quantiles must be nondecreasing finite binary64 fractions in [0,1]")
    require(np.all((payload["support"] == 0) | (payload["support"] == 1)), "invalid support bit")
    seen = set()
    for root in payload["roots"]:
        todo = [(int(root), 0)]
        while todo:
            node, depth = todo.pop()
            require(node not in seen and 0 <= node < n and depth <= 6, "invalid/disjoint tree graph or depth")
            seen.add(node)
            feature = int(payload["feature"][node])
            if feature == -1:
                require(payload["left"][node] == payload["right"][node] == -1, "leaf has children")
            else:
                require(0 <= feature < 64, "split feature out of range")
                todo.extend(((int(payload["left"][node]), depth + 1),
                             (int(payload["right"][node]), depth + 1)))
    require(len(seen) == n, "unreachable node")
    require(sum(array.nbytes for array in payload.values()) <= 524288, "numeric model limit exceeded")
    for array in payload.values():
        array.setflags(write=False)
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--expected-npz-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists() and not args.receipt.exists(), "export paths must be new")
    payload = load_model(args.model, args.expected_npz_sha256)
    identity = hashlib.sha256(canonical(payload)).digest()
    lines = ["/* PRIVATE generated v2 TRAIN data: do not submit to Git. */", "#pragma once",
        "#define BC_MODEL_VERSION 2", "#define BC_MODEL_SIZE_DOMAIN 2", "#define BC_MODEL_V2_TAGS 1",
        f'#define BC_MODEL_DOMAIN_TAG "{DOMAIN}"', f'#define BC_MODEL_SIZE_ENCODING "{SIZE_ENCODING}"',
        f"#define BC_MODEL_NODE_COUNT {len(payload['feature'])}", "#define BC_MODEL_TREE_COUNT 32",
        "#define BC_MODEL_FEATURE_COUNT 64", "#define BC_MODEL_CELL_COUNT 480",
        f'#define BC_MODEL_NPZ_SHA256 "{args.expected_npz_sha256}"',
        f'#define BC_MODEL_NUMERIC_SHA256 "{identity.hex()}"',
        "static const unsigned char bc_model_digest[32] = {" + ",".join(str(x) for x in identity) + "};"]
    for name in NAMES:
        array = payload[name]
        ctype = "double" if array.dtype.kind == "f" else "uint16_t" if name in ("model_version", "size_domain") else "uint8_t" if name == "support" else "int16_t"
        symbol = name + "_data" if name in ("model_version", "size_domain") else name
        values = [float(value).hex() if array.dtype.kind == "f" else str(int(value)) for value in array.flat]
        lines.append(f"static const {ctype} bc_{symbol}[{array.size}] = {{")
        lines.extend("    " + ",".join(values[i:i + 8]) + "," for i in range(0, len(values), 8))
        lines.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as output:
        output.write("\n".join(lines) + "\n")
    receipt = {"schema": "frozen-clone-v2-lossless-model-export-v1",
        "source_npz_sha256": args.expected_npz_sha256, "model_version": 2, "size_domain": 2,
        "size_encoding": SIZE_ENCODING, "canonical_domain": DOMAIN,
        "canonical_numeric_sha256": identity.hex(),
        "header_sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
        "nodes": len(payload["feature"]), "numeric_bytes": sum(array.nbytes for array in payload.values()),
        "array_shapes": {name: list(array.shape) for name, array in payload.items()},
        "scope": "lossless already-fitted v2 TRAIN export only; no fitting, prediction, record access or model change"}
    with args.receipt.open("x") as output:
        json.dump(receipt, output, indent=2, sort_keys=True)
        output.write("\n")
    print(json.dumps({key: receipt[key] for key in ("model_version", "size_domain", "nodes",
        "numeric_bytes", "canonical_numeric_sha256", "header_sha256")}), flush=True)


if __name__ == "__main__":
    main()
