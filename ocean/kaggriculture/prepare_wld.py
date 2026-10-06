"""Copy an H256/L2 checkpoint, reinitialize only its critic, and verify actor identity."""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import random


def critic_slice(hidden=256, layers=2):
    augment = lambda features: (features + 8) & ~7
    specs = [(184, 64, 64), (56, 32, 32), (24, 32, 32), (32, 16, 16),
        (880, hidden, hidden), (hidden, hidden // 2, 748), (hidden, hidden // 2, 1230)]
    start = sum(mid * augment(inputs) + out * augment(mid) for inputs, mid, out in specs)
    count = (hidden // 2) * augment(hidden) + augment(hidden // 2)
    total = start + count + layers * 3 * hidden * hidden
    return start, count, total


def prepare(source, output, seed=73):
    assert not output.exists(), f"Refusing to overwrite: {output}"
    assert not output.with_suffix(".json").exists(), "Refusing to overwrite provenance"
    data = source.read_bytes()
    start, count, total = critic_slice()
    assert total == 1082200 and len(data) == total * 4
    original = array("f", data)
    assert all(math.isfinite(value) for value in original)
    weights = array("f", original)
    rng = random.Random(seed)
    # Nonzero hidden features + neutral output: the value head can learn on step 1.
    width = (256 + 8) & ~7
    for row in range(128):
        for feature in range(width):
            weights[start + row * width + feature] = (
                rng.gauss(0, math.sqrt(2 / 256)) if feature < 256 else 0)
    for index in range(start + 128 * width, start + count):
        weights[index] = 0
    changed = weights.tobytes()
    assert changed[:start * 4] == data[:start * 4]
    assert changed[(start + count) * 4:] == data[(start + count) * 4:]
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(changed)
    metadata = {"source": str(source.resolve()), "output": str(output.resolve()),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "sha256": hashlib.sha256(changed).hexdigest(), "critic_start": start,
        "critic_parameters": count, "parameters": total, "seed": seed,
        "actor_and_recurrent_weights_byte_identical": True, "initial_value": 0}
    with output.with_suffix(".json").open("x") as stream:
        stream.write(json.dumps(metadata, indent=2) + "\n")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seed", type=int, default=73)
    args = parser.parse_args()
    print(json.dumps(prepare(args.source, args.output, args.seed), indent=2))


if __name__ == "__main__":
    main()
